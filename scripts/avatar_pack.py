#!/usr/bin/env python3
"""Validate an avatar folder's manifest and generate what the firmware build needs.

    avatar_pack.py header --avatar-dir DIR --out FILE    # C header with colors and renderer
    avatar_pack.py cmake --avatar-dir DIR --out FILE     # CMake with the renderer and its art

Runs during the firmware build, so it uses only the standard library.
"""

import argparse
import json
import re
import sys
from pathlib import Path

MANIFEST_NAME = "avatar.json"
FALLBACK_SIZE = 320 * 240 * 2
GIF_FOLDER_NAME = "gifs"
BOOT_FILES = ("boot.png", "boot_image.c", "boot_name.c")
MAX_DISPLAY_NAME = 20

COLOR_FIELDS = ("background", "accent", "accent_text", "name_color", "icon_color")
COMMON_FIELDS = ("display_name", "renderer", *COLOR_FIELDS)
RENDERER_FIELDS = {
    "gif": ("emotions", "aliases", "fallback"),
    "image": ("emoji_collection",),
    "custom": (),
}
COLOR_PATTERN = re.compile(r"#[0-9A-Fa-f]{6}")
COLLECTION_PATTERN = re.compile(r"[a-z0-9_-]+")


class ManifestError(ValueError):
    """An avatar folder that must not be built. The message names the field or file."""


def _read_json(path: Path, label: str):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except OSError as error:
        raise ManifestError(f"{label}: cannot read {path}: {error.strerror}") from error
    except ValueError as error:
        raise ManifestError(f"{label}: {path} is not valid JSON: {error}") from error


def _check_fields(manifest: dict) -> str:
    if not isinstance(manifest, dict):
        raise ManifestError(f"{MANIFEST_NAME}: expected a JSON object")
    if "renderer" not in manifest:
        raise ManifestError("renderer: missing required field")
    renderer = manifest["renderer"]
    if not isinstance(renderer, str) or renderer not in RENDERER_FIELDS:
        raise ManifestError(
            f"renderer: expected one of {', '.join(RENDERER_FIELDS)}, got {renderer!r}")
    allowed = (*COMMON_FIELDS, *RENDERER_FIELDS[renderer])
    for field in allowed:
        if field not in manifest:
            raise ManifestError(f"{field}: missing required field for renderer {renderer}")
    for field in manifest:
        if field not in allowed:
            raise ManifestError(f"{field}: unknown field for renderer {renderer}")
    return renderer


def _check_display_name(name) -> None:
    if not isinstance(name, str) or not 0 < len(name) <= MAX_DISPLAY_NAME:
        raise ManifestError(f"display_name: expected 1 to {MAX_DISPLAY_NAME} characters")
    if any(character in '"\\' or ord(character) < 0x20 for character in name):
        raise ManifestError('display_name: must not contain ", \\ or control characters')


def _check_colors(manifest: dict) -> None:
    for field in COLOR_FIELDS:
        value = manifest[field]
        if not isinstance(value, str) or not COLOR_PATTERN.fullmatch(value):
            raise ManifestError(f"{field}: expected #RRGGBB, got {value!r}")


def _local_file(avatar_dir: Path, field: str, name) -> Path:
    """A file directly in the folder, so an avatar needs nothing outside it."""
    if not isinstance(name, str) or not name or Path(name).name != name or name == "..":
        raise ManifestError(f"{field}: expected a file name in the avatar folder, got {name!r}")
    return avatar_dir / name


def _check_gif_art(avatar_dir: Path, manifest: dict) -> None:
    if manifest["emotions"] != GIF_FOLDER_NAME:
        # elena_avatar_assets.py looks for the GIFs in a folder with exactly this name.
        raise ManifestError(
            f'emotions: must be "{GIF_FOLDER_NAME}", got {manifest["emotions"]!r}')
    gifs = avatar_dir / GIF_FOLDER_NAME
    if not gifs.is_dir():
        raise ManifestError(f"emotions: folder {gifs} does not exist")
    if not any(gifs.glob("*.gif")):
        raise ManifestError(f"emotions: folder {gifs} holds no .gif file")

    aliases = _local_file(avatar_dir, "aliases", manifest["aliases"])
    if not aliases.is_file():
        raise ManifestError(f"aliases: file {aliases} does not exist")
    _read_json(aliases, "aliases")

    fallback = _local_file(avatar_dir, "fallback", manifest["fallback"])
    if not fallback.is_file():
        raise ManifestError(f"fallback: file {fallback} does not exist")
    size = fallback.stat().st_size
    if size != FALLBACK_SIZE:
        raise ManifestError(f"fallback: {fallback} has {size} bytes, expected {FALLBACK_SIZE}")


def load_manifest(avatar_dir: Path) -> dict:
    """Return the validated manifest of an avatar folder, or raise ManifestError."""
    avatar_dir = Path(avatar_dir)
    path = avatar_dir / MANIFEST_NAME
    if not path.is_file():
        raise ManifestError(f"{MANIFEST_NAME}: {path} does not exist")
    manifest = _read_json(path, MANIFEST_NAME)
    renderer = _check_fields(manifest)
    _check_display_name(manifest["display_name"])
    _check_colors(manifest)
    for name in BOOT_FILES:
        if not (avatar_dir / name).is_file():
            raise ManifestError(f"{name}: every avatar needs {avatar_dir / name}")
    if renderer == "gif":
        _check_gif_art(avatar_dir, manifest)
    elif renderer == "image":
        collection = manifest["emoji_collection"]
        if not isinstance(collection, str) or not COLLECTION_PATTERN.fullmatch(collection):
            raise ManifestError(f"emoji_collection: invalid name {collection!r}")
    elif not (avatar_dir / "renderer.cc").is_file():
        raise ManifestError(f"renderer.cc: a custom avatar needs {avatar_dir / 'renderer.cc'}")
    return manifest


def render_header(manifest: dict) -> str:
    """The generated avatar_pack_config.h."""
    lines = [
        "/* Generated by scripts/avatar_pack.py - do not edit. */",
        "#ifndef AVATAR_PACK_CONFIG_H",
        "#define AVATAR_PACK_CONFIG_H",
        "",
        f'#define AVATAR_DISPLAY_NAME "{manifest["display_name"]}"',
    ]
    for field, macro in (
        ("background", "AVATAR_BACKGROUND_COLOR"),
        ("accent", "AVATAR_ACCENT_COLOR"),
        ("accent_text", "AVATAR_ACCENT_TEXT_COLOR"),
        ("name_color", "AVATAR_NAME_COLOR"),
        ("icon_color", "AVATAR_ICON_COLOR"),
    ):
        lines.append(f"#define {macro} 0x{manifest[field][1:].upper()}")
    lines += [f"#define AVATAR_RENDERER_{manifest['renderer'].upper()} 1", "", "#endif", ""]
    return "\n".join(lines)


def _cmake_set(name: str, value: str) -> str:
    return f'set({name} "{value}")'


def render_cmake(manifest: dict, avatar_dir: Path) -> str:
    """The generated CMake: the renderer and its art, with absolute paths."""
    folder = Path(avatar_dir).resolve()
    renderer = manifest["renderer"]
    lines = ["# Generated by scripts/avatar_pack.py - do not edit.",
             _cmake_set("AVATAR_RENDERER", renderer)]
    if renderer == "gif":
        lines += [
            _cmake_set("DEFAULT_EMOJI_DIRECTORY", (folder / manifest["emotions"]).as_posix()),
            _cmake_set("DEFAULT_EMOJI_ALIASES", (folder / manifest["aliases"]).as_posix()),
            _cmake_set("AVATAR_FALLBACK_RGB565", (folder / manifest["fallback"]).as_posix()),
        ]
    elif renderer == "image":
        lines.append(_cmake_set("DEFAULT_EMOJI_COLLECTION", manifest["emoji_collection"]))
    return "\n".join(lines) + "\n"


def _write_if_changed(path: Path, text: str) -> None:
    """Every CMake configure runs this; an untouched file does not recompile the firmware."""
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.is_file() or path.read_text(encoding="utf-8") != text:
        path.write_text(text, encoding="utf-8")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("header", "cmake"))
    parser.add_argument("--avatar-dir", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        manifest = load_manifest(args.avatar_dir)
    except ManifestError as error:
        print(error, file=sys.stderr)
        return 1
    if args.command == "header":
        text = render_header(manifest)
    else:
        text = render_cmake(manifest, args.avatar_dir)
    _write_if_changed(args.out, text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
