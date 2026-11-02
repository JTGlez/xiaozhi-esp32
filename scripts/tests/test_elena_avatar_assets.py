import hashlib
import importlib.util
import json
import os
import re
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "scripts"
FIXTURES = SCRIPTS / "tests" / "fixtures"
GIF_FIXTURES = FIXTURES / "avatar_gif"
ASSETS = ROOT / "main" / "boards" / "elena-stackchan" / "assets"
GIFS_DIR = ASSETS / "gifs"
ALIASES_JSON = ASSETS / "aliases.json"
FALLBACK_RGB565 = ASSETS / "neutral.rgb565"
SHA256SUMS = ASSETS / "SHA256SUMS"
GIFDEC = ROOT / "main" / "display" / "lvgl_display" / "gif"

sys.path.insert(0, str(SCRIPTS))
import elena_avatar_assets as AVA  # noqa: E402

SPEC = importlib.util.spec_from_file_location(
    "build_default_assets", SCRIPTS / "build_default_assets.py"
)
assert SPEC is not None and SPEC.loader is not None
BUILD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILD)

GIF_NAMES = (
    "neutral",
    "happy",
    "sad",
    "thinking",
    "sleeping",
    "pet",
    "shake",
    "speaking",
    "listening",
    "look_left",
    "look_right",
    "email",
)
TOTAL_GIF_BYTES = 865886
CANVAS_WIDTH = 320
CANVAS_HEIGHT = 240
RGB565_SIZE = CANVAS_WIDTH * CANVAS_HEIGHT * 2
# Frames per GIF in one pass. The kit's own manifest and the firmware decoder
# agree: 12 GIFs, 34 frames per pass, so a host decode of two passes is 68.
FRAMES_BY_NAME = {
    "neutral": 2,
    "sad": 2,
    "email": 3,
    "happy": 3,
    "listening": 3,
    "look_left": 3,
    "look_right": 3,
    "pet": 3,
    "shake": 3,
    "sleeping": 3,
    "speaking": 3,
    "thinking": 3,
}
FRAMES_PER_PASS = 34
PASSES_PER_DECODE = 2


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def compile_gif_decoder(build_dir: Path, cache_decode_data: bool) -> Path:
    return AVA.build_gif_decoder(build_dir, cache_decode_data=cache_decode_data)


def inspect_gif(binary: Path, gif: Path) -> dict:
    result = subprocess.run([binary, "inspect", str(gif)], capture_output=True, text=True)
    if result.returncode != 0:
        raise AssertionError(f"inspect {gif.name} exited {result.returncode}: {result.stderr}")
    if result.stderr.strip():
        raise AssertionError(f"inspect {gif.name} printed diagnostics: {result.stderr}")
    parsed = {"frames": []}
    for line in result.stdout.splitlines():
        parts = line.split()
        if parts[0] == "dim":
            parsed["width"] = int(parts[1])
            parsed["height"] = int(parts[2])
        elif parts[0] == "frame":
            parsed["frames"].append((int(parts[1]), int(parts[2]), parts[3], parts[4]))
        elif parts[0] == "loop":
            parsed.setdefault("loops", {})[int(parts[1])] = int(parts[3])
        elif parts[0] == "frames":
            parsed["total"] = int(parts[1])
    return parsed


class ElenaAvatarAssetsTest(unittest.TestCase):
    def test_manifest_and_all_frames_decode(self):
        manifest = AVA.validate_avatar_assets(ASSETS)
        self.assertEqual(sorted(manifest["gifs"]), sorted(GIF_NAMES))
        self.assertEqual(sum(entry["bytes"] for entry in manifest["gifs"].values()), TOTAL_GIF_BYTES)
        for name, entry in manifest["gifs"].items():
            path = GIFS_DIR / entry["file"]
            self.assertEqual(entry["width"], CANVAS_WIDTH, name)
            self.assertEqual(entry["height"], CANVAS_HEIGHT, name)
            self.assertEqual(entry["frames"], FRAMES_BY_NAME[name], name)
            self.assertEqual(entry["sha256"], sha256(path), name)
        checksums = {}
        for line in SHA256SUMS.read_text(encoding="utf-8").splitlines():
            digest, _, name = line.partition("  ")
            checksums[name] = digest
        self.assertEqual(sorted(checksums), sorted(f"gifs/{n}.gif" for n in GIF_NAMES))
        for name, digest in checksums.items():
            self.assertEqual(digest, sha256(ASSETS / name), name)

        with tempfile.TemporaryDirectory() as directory:
            build_dir = Path(directory)
            decoders = {
                False: compile_gif_decoder(build_dir / "cache-off", False),
                True: compile_gif_decoder(build_dir / "cache-on", True),
            }
            for name in GIF_NAMES:
                gif = GIFS_DIR / f"{name}.gif"
                outputs = {cache: inspect_gif(binary, gif) for cache, binary in decoders.items()}
                for cache, parsed in outputs.items():
                    label = f"{name} LZW_CACHE={int(cache)}"
                    expected = FRAMES_BY_NAME[name]
                    self.assertEqual(parsed["width"], CANVAS_WIDTH, label)
                    self.assertEqual(parsed["height"], CANVAS_HEIGHT, label)
                    self.assertEqual(parsed["total"], expected * PASSES_PER_DECODE, label)
                    self.assertEqual(parsed["loops"][0], expected, label)
                    self.assertEqual(parsed["loops"][1], expected, label)
                    self.assertTrue(all(count == 0 for index, count in parsed["loops"].items()
                                        if index > 1), label)
                    first_loop = [frame[2:] for frame in parsed["frames"] if frame[0] == 0]
                    second_loop = [frame[2:] for frame in parsed["frames"] if frame[0] == 1]
                    self.assertEqual(len(first_loop), expected, label)
                    self.assertEqual(first_loop, second_loop, label)
                self.assertEqual(outputs[False]["frames"], outputs[True]["frames"], name)
            decoded_totals = {
                cache: sum(inspect_gif(binary, GIFS_DIR / f"{n}.gif")["total"] for n in GIF_NAMES)
                for cache, binary in decoders.items()
            }
            self.assertEqual(sum(FRAMES_BY_NAME.values()), FRAMES_PER_PASS)
            self.assertEqual(decoded_totals[False], FRAMES_PER_PASS * PASSES_PER_DECODE)
            self.assertEqual(decoded_totals[True], FRAMES_PER_PASS * PASSES_PER_DECODE)

    def test_aliases_share_files(self):
        manifest = AVA.validate_avatar_assets(ASSETS)
        aliases = json.loads(ALIASES_JSON.read_text(encoding="utf-8"))
        self.assertTrue(aliases)
        for alias, target in aliases.items():
            self.assertTrue(AVA.is_valid_emotion_name(alias), alias)
            self.assertIn(target, GIF_NAMES, alias)
            self.assertNotIn(alias, GIF_NAMES, alias)
        self.assertEqual(aliases, AVA.alias_table_from_harness())
        with tempfile.TemporaryDirectory() as harness_dir:
            regenerated = Path(harness_dir) / "aliases.json"
            subprocess.run(
                [sys.executable, str(SCRIPTS / "elena_avatar_assets.py"), "aliases",
                 "--out", str(regenerated)],
                check=True,
            )
            self.assertEqual(aliases, json.loads(regenerated.read_text(encoding="utf-8")))

        with tempfile.TemporaryDirectory() as directory:
            out_dir = Path(directory) / "assets"
            out_dir.mkdir()
            entries = BUILD.process_emoji_directory(str(GIFS_DIR), str(out_dir), str(ALIASES_JSON))
            self.assertEqual(len(list(out_dir.glob("*.gif"))), len(GIF_NAMES))
            by_name = {entry["name"]: entry["file"] for entry in entries}
            self.assertEqual(len(by_name), len(GIF_NAMES) + len(aliases))
            for name in GIF_NAMES:
                self.assertEqual(by_name[name], f"{name}.gif")
            for alias, target in aliases.items():
                self.assertEqual(by_name[alias], f"{target}.gif", alias)
            self.assertEqual(len({entry["file"] for entry in entries}), len(GIF_NAMES))

            BUILD.generate_index_json(str(out_dir), "srmodels.bin", None, entries)
            index = json.loads((out_dir / "index.json").read_text(encoding="utf-8"))
            self.assertEqual(len(index["emoji_collection"]), len(GIF_NAMES) + len(aliases))
            packed = sorted(p.name for p in out_dir.glob("*.gif"))
            self.assertEqual(packed, sorted(f"{name}.gif" for name in GIF_NAMES))

    def test_preserves_srmodels_and_font(self):
        with tempfile.TemporaryDirectory() as directory:
            out_dir = Path(directory) / "assets"
            out_dir.mkdir()
            entries = BUILD.process_emoji_collection(str(GIFS_DIR), str(out_dir))
            self.assertEqual(len(entries), len(GIF_NAMES))
            BUILD.generate_index_json(
                str(out_dir),
                "srmodels.bin",
                "font_noto_sans_common_20_4.bin",
                entries,
                font_bundle_id="noto-v1",
            )
            index = json.loads((out_dir / "index.json").read_text(encoding="utf-8"))
            self.assertEqual(index["srmodels"], "srmodels.bin")
            self.assertEqual(index["text_font"], "font_noto_sans_common_20_4.bin")
            self.assertEqual(
                index["text_font_meta"], {"charset": "common", "size": 20, "bpp": 4, "bundle": "noto-v1"}
            )
            self.assertEqual(len(index["emoji_collection"]), len(GIF_NAMES))

            output = Path(directory) / "assets.bin"
            self.assertTrue(
                BUILD.build_assets_integrated(
                    None,
                    None,
                    None,
                    None,
                    None,
                    str(output),
                    emoji_directory=str(GIFS_DIR),
                    emoji_aliases=str(ALIASES_JSON),
                )
            )
            packed_bytes = output.stat().st_size
            self.assertGreaterEqual(packed_bytes, TOTAL_GIF_BYTES)
            self.assertLess(packed_bytes, TOTAL_GIF_BYTES + 4096)

    def test_missing_file_or_invalid_alias_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            gifs = root / "gifs"
            shutil.copytree(GIFS_DIR, gifs)
            aliases = json.loads(ALIASES_JSON.read_text(encoding="utf-8"))
            (root / "aliases.json").write_text(json.dumps(aliases), encoding="utf-8")
            shutil.copy2(SHA256SUMS, root / "SHA256SUMS")
            shutil.copy2(FALLBACK_RGB565, root / "neutral.rgb565")

            (gifs / "pet.gif").unlink()
            with self.assertRaises(ValueError):
                BUILD.process_emoji_directory(str(gifs), str(root / "out"), str(root / "aliases.json"))
            with self.assertRaises(ValueError):
                AVA.validate_avatar_assets(root)

        for bad_alias in ("happy name", "../evil", "happy!", "x" * 41, "", "pet"):
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                aliases_path = root / "aliases.json"
                aliases_path.write_text(json.dumps({bad_alias: "happy"}), encoding="utf-8")
                out_dir = root / "out"
                out_dir.mkdir()
                with self.assertRaises(ValueError, msg=bad_alias):
                    BUILD.process_emoji_directory(str(GIFS_DIR), str(out_dir), str(aliases_path))

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            shutil.copytree(GIFS_DIR, root / "gifs")
            shutil.copy2(ALIASES_JSON, root / "aliases.json")
            shutil.copy2(FALLBACK_RGB565, root / "neutral.rgb565")
            lines = SHA256SUMS.read_text(encoding="utf-8").splitlines()
            lines[0] = "0" * 64 + "  " + lines[0].split("  ")[1]
            (root / "SHA256SUMS").write_text("\n".join(lines) + "\n", encoding="utf-8")
            with self.assertRaises(ValueError):
                AVA.validate_avatar_assets(root)

    def test_fallback_matches_neutral_rgb565(self):
        committed = FALLBACK_RGB565.read_bytes()
        self.assertEqual(len(committed), RGB565_SIZE)

        with tempfile.TemporaryDirectory() as directory:
            build_dir = Path(directory)
            for cache in (False, True):
                binary = compile_gif_decoder(build_dir / f"cache-{int(cache)}", cache)
                generated = build_dir / f"neutral-{int(cache)}.rgb565"
                AVA.prepare_avatar_fallback(GIFS_DIR / "neutral.gif", generated, decoder=binary)
                self.assertEqual(len(generated.read_bytes()), RGB565_SIZE)
                self.assertEqual(generated.read_bytes(), committed)

            header = build_dir / "avatar_fallback.h"
            AVA.write_avatar_fallback_header(FALLBACK_RGB565, header)
            source = header.read_text(encoding="utf-8")
            self.assertIn(f"#define ELENA_AVATAR_FALLBACK_SIZE {RGB565_SIZE}", source)
            self.assertIn(f"#define ELENA_AVATAR_FALLBACK_WIDTH {CANVAS_WIDTH}", source)
            self.assertIn(f"#define ELENA_AVATAR_FALLBACK_HEIGHT {CANVAS_HEIGHT}", source)
            match = re.search(
                r"elena_avatar_neutral_rgb565\[ELENA_AVATAR_FALLBACK_SIZE\] = \{(.*?)\};",
                source,
                re.S,
            )
            self.assertIsNotNone(match, "static fallback array missing from the generated header")
            values = [int(token, 16) for token in re.findall(r"0x[0-9a-fA-F]{2}", match.group(1))]
            self.assertEqual(len(values), RGB565_SIZE)
            self.assertEqual(bytes(values), committed)

            driver = build_dir / "header_check.c"
            driver.write_text(
                '#include "avatar_fallback.h"\n'
                "int main(void) {\n"
                "    unsigned long sum = 0;\n"
                "    for (unsigned long i = 0; i < ELENA_AVATAR_FALLBACK_SIZE; ++i) {\n"
                "        sum += elena_avatar_neutral_rgb565[i];\n"
                "    }\n"
                "    return sum == 0 ? 1 : 0;\n"
                "}\n",
                encoding="utf-8",
            )
            command = shlex.split(os.environ.get("CC", "cc")) + [
                "-std=c11",
                "-Wall",
                "-Werror",
                f"-I{build_dir}",
                str(driver),
                "-o",
                str(build_dir / "header_check"),
            ]
            subprocess.run(command, check=True, cwd=build_dir)
            subprocess.run([build_dir / "header_check"], check=True, cwd=build_dir)

    def test_output_changes_after_gif_change(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            def pack(source_gifs: Path, output: Path) -> str:
                self.assertTrue(
                    BUILD.build_assets_integrated(
                        None,
                        None,
                        None,
                        None,
                        None,
                        str(output),
                        emoji_directory=str(source_gifs),
                        emoji_aliases=str(ALIASES_JSON),
                    )
                )
                return sha256(output)

            pristine_hash = pack(GIFS_DIR, root / "pristine.bin")
            modified = root / "modified"
            shutil.copytree(GIFS_DIR, modified)
            data = bytearray((modified / "neutral.gif").read_bytes())
            data[-8] ^= 0x01
            (modified / "neutral.gif").write_bytes(bytes(data))
            modified_hash = pack(modified, root / "modified.bin")
            self.assertNotEqual(pristine_hash, modified_hash)

            shutil.copy2(ALIASES_JSON, modified / "aliases.json")
            shutil.copy2(SHA256SUMS, modified / "SHA256SUMS")
            shutil.copy2(FALLBACK_RGB565, modified / "neutral.rgb565")
            with self.assertRaises(ValueError):
                AVA.validate_avatar_assets(modified)


class ElenaAvatarGifMetadataTest(unittest.TestCase):
    def test_gif_metadata_is_bounded_to_its_own_file(self):
        for name in GIF_NAMES:
            metadata = AVA.gif_metadata(GIFS_DIR / f"{name}.gif")
            self.assertEqual(metadata["width"], CANVAS_WIDTH, name)
            self.assertEqual(metadata["height"], CANVAS_HEIGHT, name)
            self.assertEqual(metadata["frames"], FRAMES_BY_NAME[name], name)

        with tempfile.TemporaryDirectory() as directory:
            broken = Path(directory) / "broken.gif"
            broken.write_bytes(b"GIF89a" + struct.pack("<HH", 4, 4) + b"\x00\x00\x00")
            with self.assertRaises(ValueError):
                AVA.gif_metadata(broken)


if __name__ == "__main__":
    unittest.main()
