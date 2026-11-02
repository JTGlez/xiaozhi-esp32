"""Tests for scripts/avatar_pack.py: the manifest of an avatar folder."""

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "scripts"
AVATARS = ROOT / "main" / "boards" / "elena-stackchan" / "avatars"

sys.path.insert(0, str(SCRIPTS))
import avatar_pack  # noqa: E402
from avatar_pack import ManifestError, load_manifest, render_cmake, render_header  # noqa: E402

FALLBACK_SIZE = 153600
COLORS = {
    "background": "#F9F0E9",
    "accent": "#D9A45B",
    "accent_text": "#3B2A2A",
    "name_color": "#8A5A1E",
    "icon_color": "#3B2A2A",
}
BASE_FIELDS = {"display_name": "Elena-Chan", **COLORS}
GIF_FIELDS = {
    **BASE_FIELDS,
    "renderer": "gif",
    "emotions": "gifs",
    "aliases": "aliases.json",
    "fallback": "neutral.rgb565",
}
IMAGE_FIELDS = {**BASE_FIELDS, "renderer": "image", "emoji_collection": "noto-color-emoji_64"}
CUSTOM_FIELDS = {**BASE_FIELDS, "renderer": "custom"}


def write_manifest(folder, fields):
    (folder / "avatar.json").write_text(json.dumps(fields), encoding="utf-8")


def write_boot_files(folder):
    for name in ("boot.png", "boot_image.c", "boot_name.c"):
        (folder / name).write_bytes(b"")


def make_gif_avatar(folder, **changes):
    fields = {**GIF_FIELDS, **changes}
    (folder / "gifs").mkdir()
    (folder / "gifs" / "neutral.gif").write_bytes(b"GIF89a")
    (folder / "aliases.json").write_text("{}", encoding="utf-8")
    (folder / "neutral.rgb565").write_bytes(b"\0" * FALLBACK_SIZE)
    write_boot_files(folder)
    write_manifest(folder, fields)
    return fields


def make_image_avatar(folder, **changes):
    fields = {**IMAGE_FIELDS, **changes}
    write_boot_files(folder)
    write_manifest(folder, fields)
    return fields


def make_custom_avatar(folder, with_source=True):
    write_boot_files(folder)
    write_manifest(folder, CUSTOM_FIELDS)
    if with_source:
        (folder / "renderer.cc").write_text("// renderer\n", encoding="utf-8")


class AvatarTestCase(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.dir = Path(temp.name)

    def assert_stops(self, naming):
        with self.assertRaises(ManifestError) as stopped:
            load_manifest(self.dir)
        self.assertIn(naming, str(stopped.exception))


class HeaderTests(AvatarTestCase):
    def test_valid_gif_manifest_generates_header(self):
        make_gif_avatar(self.dir)
        header = render_header(load_manifest(self.dir))
        self.assertIn("#define AVATAR_ACCENT_COLOR 0xD9A45B", header)
        self.assertIn('#define AVATAR_DISPLAY_NAME "Elena-Chan"', header)
        self.assertIn("#define AVATAR_BACKGROUND_COLOR 0xF9F0E9", header)
        self.assertIn("#define AVATAR_ACCENT_TEXT_COLOR 0x3B2A2A", header)
        self.assertIn("#define AVATAR_NAME_COLOR 0x8A5A1E", header)
        self.assertIn("#define AVATAR_ICON_COLOR 0x3B2A2A", header)
        self.assertIn("#define AVATAR_RENDERER_GIF 1", header)
        self.assertNotIn("AVATAR_RENDERER_IMAGE", header)
        self.assertNotIn("AVATAR_RENDERER_CUSTOM", header)

    def test_image_manifest_selects_the_image_renderer(self):
        make_image_avatar(self.dir)
        header = render_header(load_manifest(self.dir))
        self.assertIn("#define AVATAR_RENDERER_IMAGE 1", header)
        self.assertNotIn("AVATAR_RENDERER_GIF", header)


class CmakeTests(AvatarTestCase):
    def test_gif_cmake_names_its_art_with_absolute_paths(self):
        make_gif_avatar(self.dir)
        cmake = render_cmake(load_manifest(self.dir), self.dir)
        folder = self.dir.resolve().as_posix()
        self.assertIn('set(AVATAR_RENDERER "gif")', cmake)
        self.assertIn(f'set(DEFAULT_EMOJI_DIRECTORY "{folder}/gifs")', cmake)
        self.assertIn(f'set(DEFAULT_EMOJI_ALIASES "{folder}/aliases.json")', cmake)
        self.assertIn(f'set(AVATAR_FALLBACK_RGB565 "{folder}/neutral.rgb565")', cmake)
        self.assertNotIn("DEFAULT_EMOJI_COLLECTION", cmake)

    def test_image_cmake_names_its_collection(self):
        make_image_avatar(self.dir)
        cmake = render_cmake(load_manifest(self.dir), self.dir)
        self.assertIn('set(AVATAR_RENDERER "image")', cmake)
        self.assertIn('set(DEFAULT_EMOJI_COLLECTION "noto-color-emoji_64")', cmake)
        self.assertNotIn("DEFAULT_EMOJI_DIRECTORY", cmake)
        self.assertNotIn("AVATAR_FALLBACK_RGB565", cmake)


class ManifestRuleTests(AvatarTestCase):
    def test_missing_manifest_stops(self):
        self.assert_stops("avatar.json")

    def test_invalid_json_stops(self):
        (self.dir / "avatar.json").write_text("{", encoding="utf-8")
        self.assert_stops("avatar.json")

    def test_each_missing_required_field_stops(self):
        cases = (
            (make_gif_avatar, GIF_FIELDS),
            (make_image_avatar, IMAGE_FIELDS),
        )
        for make, fields in cases:
            for field in fields:
                with self.subTest(renderer=fields["renderer"], field=field), \
                        tempfile.TemporaryDirectory() as temp:
                    folder = Path(temp)
                    make(folder)
                    write_manifest(folder, {k: v for k, v in fields.items() if k != field})
                    with self.assertRaises(ManifestError) as stopped:
                        load_manifest(folder)
                    self.assertIn(field, str(stopped.exception))

        for field in CUSTOM_FIELDS:
            with self.subTest(renderer="custom", field=field), \
                    tempfile.TemporaryDirectory() as temp:
                folder = Path(temp)
                make_custom_avatar(folder)
                write_manifest(folder, {k: v for k, v in CUSTOM_FIELDS.items() if k != field})
                with self.assertRaises(ManifestError) as stopped:
                    load_manifest(folder)
                self.assertIn(field, str(stopped.exception))

    def test_unknown_field_stops(self):
        make_gif_avatar(self.dir, backgroud="#FFFFFF")
        self.assert_stops("backgroud")

    def test_field_of_another_renderer_stops(self):
        make_gif_avatar(self.dir, emoji_collection="noto-color-emoji_64")
        self.assert_stops("emoji_collection")

    def test_bad_color_stops(self):
        for value in ("red", "#12345", "#GGGGGG"):
            with self.subTest(value=value):
                make_image_avatar(self.dir, accent=value)
                self.assert_stops("accent")

    def test_unknown_renderer_stops(self):
        make_image_avatar(self.dir, renderer="video")
        self.assert_stops("renderer")

    def test_display_name_with_quote_stops(self):
        for value in ('Elena"', "Ele\\na", "Ele\nna"):
            with self.subTest(value=value):
                make_image_avatar(self.dir, display_name=value)
                self.assert_stops("display_name")

    def test_display_name_must_be_short_and_present(self):
        for value in ("", "x" * 21, 7):
            with self.subTest(value=value):
                make_image_avatar(self.dir, display_name=value)
                self.assert_stops("display_name")

    def test_display_name_at_the_limit_is_valid(self):
        make_image_avatar(self.dir, display_name="x" * 20)
        self.assertEqual(load_manifest(self.dir)["display_name"], "x" * 20)

    def test_bad_emoji_collection_stops(self):
        make_image_avatar(self.dir, emoji_collection="../noto")
        self.assert_stops("emoji_collection")


class ArtRuleTests(AvatarTestCase):
    def test_missing_gifs_folder_stops(self):
        make_gif_avatar(self.dir)
        (self.dir / "gifs" / "neutral.gif").unlink()
        (self.dir / "gifs").rmdir()
        self.assert_stops("gifs")

    def test_gifs_folder_without_gif_stops(self):
        make_gif_avatar(self.dir)
        (self.dir / "gifs" / "neutral.gif").unlink()
        self.assert_stops("gifs")

    def test_emotions_folder_with_another_name_stops(self):
        make_gif_avatar(self.dir, emotions="art")
        (self.dir / "art").mkdir()
        (self.dir / "art" / "neutral.gif").write_bytes(b"GIF89a")
        self.assert_stops("emotions")

    def test_missing_aliases_stops(self):
        make_gif_avatar(self.dir)
        (self.dir / "aliases.json").unlink()
        self.assert_stops("aliases")

    def test_aliases_that_are_not_json_stop(self):
        make_gif_avatar(self.dir)
        (self.dir / "aliases.json").write_text("{", encoding="utf-8")
        self.assert_stops("aliases")

    def test_aliases_outside_the_folder_stop(self):
        with tempfile.TemporaryDirectory() as temp:
            outside = Path(temp) / "aliases.json"
            outside.write_text("{}", encoding="utf-8")
            make_gif_avatar(self.dir, aliases=str(outside))
            self.assert_stops("aliases")

    def test_wrong_size_fallback_stops(self):
        make_gif_avatar(self.dir)
        (self.dir / "neutral.rgb565").write_bytes(b"\0" * (FALLBACK_SIZE - 1))
        self.assert_stops("fallback")

    def test_missing_fallback_stops(self):
        make_gif_avatar(self.dir)
        (self.dir / "neutral.rgb565").unlink()
        self.assert_stops("fallback")

    def test_fallback_not_beside_the_gifs_stops(self):
        make_gif_avatar(self.dir, fallback="gifs/neutral.rgb565")
        (self.dir / "gifs" / "neutral.rgb565").write_bytes(b"\0" * FALLBACK_SIZE)
        self.assert_stops("fallback")

    def test_custom_requires_renderer_cc(self):
        make_custom_avatar(self.dir, with_source=False)
        self.assert_stops("renderer.cc")

    def test_custom_with_renderer_cc_is_valid(self):
        make_custom_avatar(self.dir)
        manifest = load_manifest(self.dir)
        self.assertIn("#define AVATAR_RENDERER_CUSTOM 1", render_header(manifest))
        self.assertIn('set(AVATAR_RENDERER "custom")', render_cmake(manifest, self.dir))

    def test_art_only_avatar_needs_nothing_outside_its_folder(self):
        make_gif_avatar(self.dir, display_name="Nueva")
        manifest = load_manifest(self.dir)
        self.assertIn('#define AVATAR_DISPLAY_NAME "Nueva"', render_header(manifest))
        self.assertIn(self.dir.resolve().as_posix(), render_cmake(manifest, self.dir))


class CommandLineTests(AvatarTestCase):
    def run_cli(self, command, out):
        return subprocess.run(
            [sys.executable, str(SCRIPTS / "avatar_pack.py"), command,
             "--avatar-dir", str(self.dir), "--out", str(out)],
            capture_output=True, text=True, timeout=30, check=False,
        )

    def test_cli_exits_1_with_the_message(self):
        out = self.dir / "out.h"
        make_gif_avatar(self.dir, accent="red")
        for command in ("header", "cmake"):
            with self.subTest(command=command):
                result = self.run_cli(command, out)
                self.assertEqual(result.returncode, 1)
                self.assertIn("accent", result.stderr)
                self.assertFalse(out.exists())

    def test_cli_writes_each_output(self):
        make_gif_avatar(self.dir)
        out = self.dir / "generated" / "out.txt"
        result = self.run_cli("header", out)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("AVATAR_ACCENT_COLOR", out.read_text(encoding="utf-8"))
        result = self.run_cli("cmake", out)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("AVATAR_RENDERER", out.read_text(encoding="utf-8"))

    def test_cli_leaves_an_unchanged_output_alone(self):
        make_gif_avatar(self.dir)
        out = self.dir / "out.h"
        self.assertEqual(self.run_cli("header", out).returncode, 0)
        before = out.stat().st_mtime_ns
        self.assertEqual(self.run_cli("header", out).returncode, 0)
        self.assertEqual(out.stat().st_mtime_ns, before)


class RealAvatarTests(unittest.TestCase):
    def test_real_avatars_are_valid(self):
        elena = load_manifest(AVATARS / "elena")
        self.assertEqual(elena["renderer"], "gif")
        self.assertEqual(elena["display_name"], "Elena-Chan")
        self.assertEqual(elena["background"], "#F9F0E9")
        emoji = load_manifest(AVATARS / "emoji")
        self.assertEqual(emoji["renderer"], "image")
        self.assertEqual(emoji["background"], "#FFFFFF")
        self.assertEqual(emoji["emoji_collection"], "noto-color-emoji_64")
        for field in ("display_name", "accent", "accent_text", "name_color", "icon_color"):
            self.assertEqual(elena[field], emoji[field], field)

    def test_module_is_stdlib_only(self):
        source = (SCRIPTS / "avatar_pack.py").read_text(encoding="utf-8")
        for line in source.splitlines():
            if line.startswith(("import ", "from ")):
                module = line.split()[1].split(".")[0]
                self.assertIn(module, sys.stdlib_module_names, line)
        self.assertTrue(hasattr(avatar_pack, "main"))


if __name__ == "__main__":
    unittest.main()
