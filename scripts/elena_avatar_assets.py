#!/usr/bin/env python3
"""Elena avatar assets: manifest, validation and the static RGB565 fallback.

Standard library only. Pixel work uses the firmware's own GIF decoder
(main/display/lvgl_display/gif/gifdec.c) compiled for the host, so the shipped
fallback is produced by the same code that will render the animation.

Commands:
    validate [--assets DIR]                 verify hashes, sizes and aliases
    manifest [--assets DIR]                 print the asset manifest as JSON
    fallback  [--gif PATH --out PATH]       first frame of a GIF as RGB565 LE
    header    [--rgb565 PATH --out PATH]    C header with the static fallback
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / "scripts"
FIXTURES = SCRIPTS / "tests" / "fixtures"
GIF_FIXTURES = FIXTURES / "avatar_gif"
BOARD_DIR = ROOT / "main" / "boards" / "elena-stackchan"
DEFAULT_ASSET_DIR = BOARD_DIR / "assets"
GIFDEC_DIR = ROOT / "main" / "display" / "lvgl_display" / "gif"

CANVAS_WIDTH = 320
CANVAS_HEIGHT = 240
RGB565_SIZE = CANVAS_WIDTH * CANVAS_HEIGHT * 2
EMOTION_NAME_PATTERN = re.compile(r"[A-Za-z0-9_-]{1,40}")

GIFS_SUBDIR = "gifs"
ALIASES_FILE = "aliases.json"
SHA256SUMS_FILE = "SHA256SUMS"
FALLBACK_FILE = "neutral.rgb565"
CLI_SUMMARY = "Elena avatar assets: manifest, validation and the static RGB565 fallback"


class AvatarAssetError(ValueError):
    """An asset, hash or alias is missing, malformed or inconsistent."""


def is_valid_emotion_name(name: str) -> bool:
    """Same acceptance rule the controller uses: 1..40 ASCII [A-Za-z0-9_-]."""
    return isinstance(name, str) and EMOTION_NAME_PATTERN.fullmatch(name) is not None


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def gif_metadata(path: Path) -> dict:
    """Logical size and frame count of one GIF, parsed from that file only."""
    data = path.read_bytes()
    if len(data) < 13 or data[:6] not in (b"GIF87a", b"GIF89a"):
        raise AvatarAssetError(f"{path.name}: not a GIF87a/GIF89a file")

    width, height = _read_u16(data, 6), _read_u16(data, 8)
    flags = data[10]
    position = 13
    if flags & 0x80:
        position += 3 * (2 << (flags & 0x07))

    frames = 0
    while position < len(data):
        separator = data[position]
        if separator == 0x3B:  # trailer
            break
        if separator == 0x21:  # extension: label byte, then data sub-blocks
            position = _skip_sub_blocks(data, position + 2, path)
            continue
        if separator == 0x2C:  # image descriptor: 9 field bytes, then LZW data
            frames += 1
            position += 1 + 9
            local_flags = data[position - 1]
            if local_flags & 0x80:
                position += 3 * (2 << (local_flags & 0x07))
            position = _skip_sub_blocks(data, position + 1, path)
            continue
        raise AvatarAssetError(f"{path.name}: unexpected block 0x{separator:02x} at {position}")

    if frames == 0:
        raise AvatarAssetError(f"{path.name}: no image descriptors found")
    return {"width": width, "height": height, "frames": frames, "bytes": len(data)}


def _read_u16(data: bytes, offset: int) -> int:
    if offset + 2 > len(data):
        raise AvatarAssetError("truncated GIF header")
    return data[offset] | (data[offset + 1] << 8)


def _skip_sub_blocks(data: bytes, position: int, path: Path) -> int:
    """Walk size-prefixed data sub-blocks and return the position after the zero block."""
    while position < len(data):
        size = data[position]
        if size == 0:
            return position + 1
        position += 1 + size
    raise AvatarAssetError(f"{path.name}: truncated GIF data sub-blocks")


def read_sha256sums(path: Path) -> dict:
    checksums = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line:
            continue
        digest, separator, name = line.partition("  ")
        if not separator or len(digest) != 64:
            raise AvatarAssetError(f"{SHA256SUMS_FILE}: malformed line {line!r}")
        checksums[name.strip()] = digest.lower()
    return checksums


def validate_avatar_assets(asset_dir: Path) -> dict:
    """Check hashes, canvas size, aliases and fallback against what is on disk."""
    asset_dir = Path(asset_dir)
    gifs_dir = asset_dir / GIFS_SUBDIR
    sums_path = asset_dir / SHA256SUMS_FILE
    aliases_path = asset_dir / ALIASES_FILE
    fallback_path = asset_dir / FALLBACK_FILE

    for required in (gifs_dir, sums_path, aliases_path, fallback_path):
        if not required.exists():
            raise AvatarAssetError(f"missing avatar asset entry: {required}")

    checksums = read_sha256sums(sums_path)
    on_disk = sorted(
        f"{GIFS_SUBDIR}/{p.name}" for p in gifs_dir.iterdir() if p.suffix.lower() == ".gif"
    )
    if sorted(checksums) != on_disk:
        raise AvatarAssetError(
            f"{SHA256SUMS_FILE} lists {sorted(checksums)} but {gifs_dir} holds {on_disk}"
        )

    gifs = {}
    for entry, digest in sorted(checksums.items()):
        path = asset_dir / entry
        if path.name.lower() != path.name:
            raise AvatarAssetError(f"{entry}: file names must be lower case")
        actual = sha256_file(path)
        if actual != digest:
            raise AvatarAssetError(f"{entry}: sha256 {actual} != recorded {digest}")
        metadata = gif_metadata(path)
        if (metadata["width"], metadata["height"]) != (CANVAS_WIDTH, CANVAS_HEIGHT):
            raise AvatarAssetError(
                f"{entry}: {metadata['width']}x{metadata['height']} does not match the "
                f"{CANVAS_WIDTH}x{CANVAS_HEIGHT} canvas"
            )
        gifs[path.stem] = {"file": path.name, **metadata, "sha256": digest}

    aliases = json.loads(aliases_path.read_text(encoding="utf-8"))
    if not isinstance(aliases, dict):
        raise AvatarAssetError(f"{ALIASES_FILE}: must be a JSON object of alias -> gif")
    for alias, target in aliases.items():
        if not is_valid_emotion_name(alias):
            raise AvatarAssetError(f"{ALIASES_FILE}: invalid alias {alias!r}")
        if not isinstance(target, str) or not EMOTION_NAME_PATTERN.fullmatch(target):
            raise AvatarAssetError(f"{ALIASES_FILE}: invalid target {target!r} for {alias!r}")
        if target not in gifs:
            raise AvatarAssetError(f"{ALIASES_FILE}: alias {alias!r} targets unknown gif {target!r}")
        if alias in gifs:
            raise AvatarAssetError(f"{ALIASES_FILE}: alias {alias!r} collides with a gif name")

    fallback_bytes = fallback_path.read_bytes()
    if len(fallback_bytes) != RGB565_SIZE:
        raise AvatarAssetError(
            f"{FALLBACK_FILE}: {len(fallback_bytes)} bytes, expected {RGB565_SIZE} "
            f"({CANVAS_WIDTH}x{CANVAS_HEIGHT} RGB565)"
        )
    neutral = gifs.get("neutral")
    if neutral is None:
        raise AvatarAssetError(f"{FALLBACK_FILE}: neutral.gif is missing")

    return {
        "gifs": gifs,
        "aliases": aliases,
        "total_bytes": sum(entry["bytes"] for entry in gifs.values()),
        "fallback": {
            "file": FALLBACK_FILE,
            "bytes": len(fallback_bytes),
            "sha256": hashlib.sha256(fallback_bytes).hexdigest(),
            "width": CANVAS_WIDTH,
            "height": CANVAS_HEIGHT,
            "source_gif": neutral["file"],
        },
    }


def _compiler() -> list:
    return shlex.split(os.environ.get("CC", "cc"))


def build_gif_decoder(build_dir: Path, cache_decode_data: bool = False) -> Path:
    """Compile the firmware GIF decoder for the host with the test adapters."""
    build_dir = Path(build_dir)
    build_dir.mkdir(parents=True, exist_ok=True)
    output = build_dir / f"avatar_gif_host_cache_{int(bool(cache_decode_data))}"
    command = _compiler() + [
        "-std=c11",
        "-Wall",
        f"-DLV_GIF_CACHE_DECODE_DATA={1 if cache_decode_data else 0}",
        f"-I{GIF_FIXTURES}",
        f"-I{GIFDEC_DIR}",
        str(GIF_FIXTURES / "avatar_gif_host.c"),
        str(GIFDEC_DIR / "gifdec.c"),
        "-o",
        str(output),
    ]
    subprocess.run(command, check=True, cwd=build_dir)
    return output


def build_avatar_controller_harness(build_dir: Path) -> Path:
    """Compile the host avatar controller harness (same sources as its test)."""
    build_dir = Path(build_dir)
    build_dir.mkdir(parents=True, exist_ok=True)
    output = build_dir / "avatar_controller_test"
    command = shlex.split(os.environ.get("CXX", "c++")) + [
        "-std=c++20",
        "-Wall",
        f"-I{ROOT / 'main' / 'display'}",
        f"-I{BOARD_DIR}",
        str(FIXTURES / "avatar_controller_test.cc"),
        str(BOARD_DIR / "avatar_controller.cc"),
        "-o",
        str(output),
    ]
    subprocess.run(command, check=True, cwd=build_dir)
    return output


def prepare_avatar_fallback(gif_path: Path, output: Path, decoder: Path | None = None) -> None:
    """Write the first frame of a GIF as raw RGB565 little endian."""
    gif_path = Path(gif_path)
    output = Path(output)
    metadata = gif_metadata(gif_path)
    if (metadata["width"], metadata["height"]) != (CANVAS_WIDTH, CANVAS_HEIGHT):
        raise AvatarAssetError(
            f"{gif_path.name}: {metadata['width']}x{metadata['height']} does not match the "
            f"{CANVAS_WIDTH}x{CANVAS_HEIGHT} canvas"
        )

    if decoder is not None:
        binary = Path(decoder)
        temp_dir = None
    else:
        temp_dir = tempfile.TemporaryDirectory()
        binary = build_gif_decoder(Path(temp_dir.name), cache_decode_data=False)

    try:
        result = subprocess.run(
            [str(binary), "rgb565", str(gif_path), str(output)],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            raise AvatarAssetError(f"cannot build fallback from {gif_path.name}: {result.stderr}")
    finally:
        if temp_dir is not None:
            temp_dir.cleanup()

    produced = output.stat().st_size
    if produced != RGB565_SIZE:
        raise AvatarAssetError(f"{output.name}: produced {produced} bytes, expected {RGB565_SIZE}")


def write_avatar_fallback_header(rgb565_path: Path, output: Path) -> None:
    """Emit a C header holding the static fallback: no decoding at run time."""
    rgb565_path = Path(rgb565_path)
    output = Path(output)
    data = rgb565_path.read_bytes()
    if len(data) != RGB565_SIZE:
        raise AvatarAssetError(
            f"{rgb565_path.name}: {len(data)} bytes, expected {RGB565_SIZE} RGB565 bytes"
        )

    digest = hashlib.sha256(data).hexdigest()
    lines = [
        "/* Generated by scripts/elena_avatar_assets.py - do not edit. */",
        "#ifndef ELENA_AVATAR_FALLBACK_H",
        "#define ELENA_AVATAR_FALLBACK_H",
        "",
        "#include <stdint.h>",
        "",
        f"#define ELENA_AVATAR_FALLBACK_WIDTH {CANVAS_WIDTH}",
        f"#define ELENA_AVATAR_FALLBACK_HEIGHT {CANVAS_HEIGHT}",
        f"#define ELENA_AVATAR_FALLBACK_SIZE {RGB565_SIZE}",
        "",
        f"/* Source: {rgb565_path.name} sha256 {digest} (RGB565, little endian) */",
        "static const uint8_t elena_avatar_neutral_rgb565[ELENA_AVATAR_FALLBACK_SIZE] = {",
    ]
    for start in range(0, len(data), 12):
        chunk = data[start : start + 12]
        lines.append("    " + ", ".join(f"0x{byte:02x}" for byte in chunk) + ",")
    lines += ["};", "", "#endif  /* ELENA_AVATAR_FALLBACK_H */", ""]
    output.write_text("\n".join(lines), encoding="utf-8")


def alias_table_from_harness() -> dict:
    """The alias table exactly as the firmware code resolves it."""
    with tempfile.TemporaryDirectory() as build_dir:
        binary = build_avatar_controller_harness(Path(build_dir))
        count = int(subprocess.run([binary, "alias_count"], capture_output=True, text=True,
                                   check=True).stdout)
        table = {}
        for index in range(count):
            result = subprocess.run([binary, "alias", str(index)], capture_output=True, text=True,
                                    check=True)
            source, _, target = result.stdout.strip().partition(" ")
            table[source] = target
    return {name: target for name, target in sorted(table.items()) if name != target}


def _cmd_validate(args) -> int:
    manifest = validate_avatar_assets(Path(args.assets))
    print(
        f"OK: {len(manifest['gifs'])} gifs, {len(manifest['aliases'])} aliases, "
        f"{manifest['total_bytes']} gif bytes, fallback {manifest['fallback']['bytes']} bytes"
    )
    return 0


def _cmd_aliases(args) -> int:
    payload = json.dumps(alias_table_from_harness(), indent=2, ensure_ascii=False) + "\n"
    if args.out:
        Path(args.out).write_text(payload, encoding="utf-8")
        print(f"Wrote {args.out}")
    else:
        sys.stdout.write(payload)
    return 0


def _cmd_manifest(args) -> int:
    print(json.dumps(validate_avatar_assets(Path(args.assets)), indent=2, sort_keys=True))
    return 0


def _cmd_fallback(args) -> int:
    prepare_avatar_fallback(Path(args.gif), Path(args.out))
    print(f"Wrote {args.out}")
    return 0


def _cmd_header(args) -> int:
    write_avatar_fallback_header(Path(args.rgb565), Path(args.out))
    print(f"Wrote {args.out}")
    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=CLI_SUMMARY)
    sub = parser.add_subparsers(dest="command", required=True)

    validate = sub.add_parser("validate", help="verify hashes, sizes and aliases")
    validate.add_argument("--assets", default=str(DEFAULT_ASSET_DIR))
    validate.set_defaults(func=_cmd_validate)

    manifest = sub.add_parser("manifest", help="print the asset manifest")
    manifest.add_argument("--assets", default=str(DEFAULT_ASSET_DIR))
    manifest.set_defaults(func=_cmd_manifest)

    aliases = sub.add_parser("aliases", help="print the alias table the firmware code uses")
    aliases.add_argument("--out", help="write the JSON here instead of stdout")
    aliases.set_defaults(func=_cmd_aliases)

    fallback = sub.add_parser("fallback", help="first GIF frame as RGB565 little endian")
    fallback.add_argument("--gif", default=str(DEFAULT_ASSET_DIR / GIFS_SUBDIR / "neutral.gif"))
    fallback.add_argument("--out", default=str(DEFAULT_ASSET_DIR / FALLBACK_FILE))
    fallback.set_defaults(func=_cmd_fallback)

    header = sub.add_parser("header", help="generate the static fallback C header")
    header.add_argument("--rgb565", default=str(DEFAULT_ASSET_DIR / FALLBACK_FILE))
    header.add_argument("--out", required=True)
    header.set_defaults(func=_cmd_header)

    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except AvatarAssetError as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
