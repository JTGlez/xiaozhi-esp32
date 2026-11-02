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
from unittest import mock

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
TOTAL_GIF_BYTES = 1345542
CANVAS_WIDTH = 320
CANVAS_HEIGHT = 240
RGB565_SIZE = CANVAS_WIDTH * CANVAS_HEIGHT * 2
# Frames per GIF in one pass, as chosen in scripts/elena_avatar_refit.py.
FRAMES_BY_NAME = {
    "email": 5,
    "happy": 4,
    "listening": 1,
    "look_left": 4,
    "look_right": 4,
    "neutral": 2,
    "pet": 4,
    "sad": 4,
    "shake": 4,
    "sleeping": 2,
    "speaking": 5,
    "thinking": 4,
}
FRAMES_PER_PASS = 43
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


def copy_asset_tree(dst: Path) -> Path:
    """Temporary copy of the committed asset tree; tests never touch the real one."""
    dst.mkdir(parents=True)
    shutil.copytree(GIFS_DIR, dst / "gifs")
    shutil.copy2(ALIASES_JSON, dst / "aliases.json")
    shutil.copy2(SHA256SUMS, dst / "SHA256SUMS")
    shutil.copy2(FALLBACK_RGB565, dst / "neutral.rgb565")
    return dst


def packaged_at(blob: bytes, payload: bytes) -> bool:
    """True if pack_assets_simple stored `payload` verbatim (after its ZZ marker)."""
    start = 12
    while True:
        i = blob.find(payload, start)
        if i < 0:
            return False
        if blob[max(12, i - 2) : i] == b"\x5a\x5a":
            return True
        start = i + 1


def packaged_json_objects(blob: bytes) -> list:
    """JSON objects packaged verbatim in an assets.bin (index.json, aliases)."""
    objects = []
    start = 12
    while True:
        i = blob.find(b"\x5a\x5a{", start)
        if i < 0:
            return objects
        text = blob[i + 2 :].decode("utf-8", "replace")
        try:
            obj, _ = json.JSONDecoder().raw_decode(text)
            objects.append(obj)
            start = i + 2 + len(json.dumps(obj, separators=(",", ":")))
        except json.JSONDecodeError:
            start = i + 2


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
        self.assertEqual(
            sorted(checksums),
            sorted([f"gifs/{n}.gif" for n in GIF_NAMES] + ["neutral.rgb565"]),
        )
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

    def test_text_font_survives_emoji_directory(self):
        # Production change this catches: build_assets_integrated ignoring
        # text_font_path when emoji_directory is set.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            font_src = root / "font_noto_sans_common_20_4.bin"
            font_bytes = bytes((i * 37) % 256 for i in range(2048))
            font_src.write_bytes(font_bytes)
            output = root / "assets.bin"
            self.assertTrue(
                BUILD.build_assets_integrated(
                    None,
                    None,
                    str(font_src),
                    None,
                    None,
                    str(output),
                    font_bundle_id="noto-v1",
                    emoji_directory=str(GIFS_DIR),
                    emoji_aliases=str(ALIASES_JSON),
                )
            )
            packed = output.read_bytes()
            self.assertTrue(packaged_at(packed, font_bytes))
            indices = packaged_json_objects(packed)
            index = next(obj for obj in indices if "emoji_collection" in obj)
            self.assertEqual(index["text_font"], font_src.name)
            aliases = json.loads(ALIASES_JSON.read_text(encoding="utf-8"))
            self.assertEqual(
                len(index["emoji_collection"]), len(GIF_NAMES) + len(aliases)
            )
            by_name = {entry["name"]: entry["file"] for entry in index["emoji_collection"]}
            for name in GIF_NAMES:
                self.assertEqual(by_name[name], f"{name}.gif", name)
                self.assertTrue(packaged_at(packed, (GIFS_DIR / f"{name}.gif").read_bytes()), name)

    def test_srmodels_survive_emoji_directory(self):
        # Production change this catches: build_assets_integrated dropping the
        # srmodels value (or not passing it to generate_index_json) when
        # emoji_directory is set. The real model copy is out of scope here, so
        # process_sr_models is patched; every other step is the real one.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "assets.bin"
            captured = {}
            original_generate = BUILD.generate_index_json

            def spy_generate(assets_dir, srmodels, *args, **kwargs):
                captured["assets_dir"] = assets_dir
                captured["srmodels"] = srmodels
                Path(assets_dir, "srmodels.bin").write_bytes(b"fake-srmodels-payload")
                result = original_generate(assets_dir, srmodels, *args, **kwargs)
                # temp_build is removed in build_assets_integrated's finally
                captured["index_json"] = json.loads(
                    (Path(assets_dir) / "index.json").read_text(encoding="utf-8")
                )
                return result

            with mock.patch.object(
                BUILD, "process_sr_models", return_value="srmodels.bin"
            ), mock.patch.object(BUILD, "generate_index_json", side_effect=spy_generate):
                self.assertTrue(
                    BUILD.build_assets_integrated(
                        ["fake-wakenet-dir"],
                        None,
                        None,
                        None,
                        None,
                        str(output),
                        emoji_directory=str(GIFS_DIR),
                        emoji_aliases=str(ALIASES_JSON),
                    )
                )
            self.assertIn("srmodels", captured, "srmodels never reached generate_index_json")
            self.assertEqual(captured["srmodels"], "srmodels.bin")
            index = captured.get("index_json", {})
            self.assertEqual(index.get("srmodels"), "srmodels.bin")
            self.assertEqual(
                len(index.get("emoji_collection", [])),
                len(GIF_NAMES) + len(json.loads(ALIASES_JSON.read_text(encoding="utf-8"))),
            )
            self.assertTrue(packaged_at(output.read_bytes(), b"fake-srmodels-payload"))

    def test_collection_and_directory_are_mutually_exclusive(self):
        # Production change this catches: build_assets_integrated accepting both
        # emoji_collection_path and emoji_directory in the same build.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "assets.bin"
            self.assertFalse(
                BUILD.build_assets_integrated(
                    None,
                    None,
                    None,
                    str(GIFS_DIR),
                    None,
                    str(output),
                    emoji_directory=str(GIFS_DIR),
                    emoji_aliases=str(ALIASES_JSON),
                )
            )
            self.assertFalse(output.exists())

    def test_header_rejects_wrong_size(self):
        # Production change this catches: write_avatar_fallback_header dropping
        # its input-size check and emitting a truncated/fattened C array.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            short = root / "short.rgb565"
            short.write_bytes(FALLBACK_RGB565.read_bytes()[:4096])
            with self.assertRaises(ValueError):
                AVA.write_avatar_fallback_header(short, root / "short.h")
            long = root / "long.rgb565"
            long.write_bytes(FALLBACK_RGB565.read_bytes() + b"\x00")
            with self.assertRaises(ValueError):
                AVA.write_avatar_fallback_header(long, root / "long.h")
            self.assertFalse((root / "short.h").exists())
            self.assertFalse((root / "long.h").exists())

    def test_fallback_rejects_short_decoder_output(self):
        # Production change this catches: prepare_avatar_fallback no longer
        # checking how many bytes the decoder actually produced. A decoder
        # that exits 0 but writes too few (or too many) bytes must be
        # rejected here; in the oversized case the produced file is even
        # recorded with a fresh hash, so only the produced-size check can
        # stop it before it would validate as a "correct" fallback.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            def make_decoder(name: str, size: int) -> Path:
                decoder = root / name
                decoder.write_text(
                    "#!/usr/bin/env python3\n"
                    "import sys\n"
                    f"open(sys.argv[3], 'wb').write(b'\\x00' * {size})\n",
                    encoding="utf-8",
                )
                decoder.chmod(0o755)
                return decoder

            out = root / "out.rgb565"
            with self.assertRaises(ValueError, msg="1024-byte decoder output accepted"):
                AVA.prepare_avatar_fallback(GIFS_DIR / "neutral.gif", out,
                                            decoder=make_decoder("short_decoder", 1024))
            self.assertLess(out.stat().st_size, RGB565_SIZE)

            oversized = root / "oversized.rgb565"
            with self.assertRaises(ValueError, msg="153601-byte decoder output accepted"):
                AVA.prepare_avatar_fallback(GIFS_DIR / "neutral.gif", oversized,
                                            decoder=make_decoder("fat_decoder", RGB565_SIZE + 1))
            self.assertGreater(oversized.stat().st_size, RGB565_SIZE)

            # With the produced-size check gone, the oversized file above
            # would carry a fresh valid hash in SHA256SUMS and validate
            # clean; the check must reject it before that stage.
            tree = copy_asset_tree(root / "assets")
            fat = tree / "neutral.rgb565"
            fat.write_bytes(oversized.read_bytes())
            digest = hashlib.sha256(fat.read_bytes()).hexdigest()
            lines = SHA256SUMS.read_text(encoding="utf-8").splitlines()
            for index, line in enumerate(lines):
                if line.endswith("  neutral.rgb565"):
                    lines[index] = digest + "  neutral.rgb565"
            (tree / "SHA256SUMS").write_text("\n".join(lines) + "\n", encoding="utf-8")
            with self.assertRaises(ValueError, msg="oversized fallback validated clean"):
                AVA.validate_avatar_assets(tree)

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


class ElenaAvatarValidateGuardsTest(unittest.TestCase):
    def test_validate_rejects_wrong_canvas(self):
        # Production change this catches: the 320x240 canvas check being dropped
        # from validate_avatar_assets or prepare_avatar_fallback. The recorded
        # hashes are refreshed for the edited GIF (and the fallback line), so
        # only the canvas checks can catch the edit.
        with tempfile.TemporaryDirectory() as directory:
            root = copy_asset_tree(Path(directory) / "assets")
            data = bytearray((root / "gifs" / "happy.gif").read_bytes())
            data[6:8] = (318).to_bytes(2, "little")  # logical width, GIF header
            (root / "gifs" / "happy.gif").write_bytes(bytes(data))
            digest = hashlib.sha256(bytes(data)).hexdigest()
            lines = SHA256SUMS.read_text(encoding="utf-8").splitlines()
            for index, line in enumerate(lines):
                if line.endswith("  gifs/happy.gif"):
                    lines[index] = digest + "  gifs/happy.gif"
                elif line.endswith("  neutral.rgb565"):
                    lines[index] = (
                        hashlib.sha256((root / "neutral.rgb565").read_bytes()).hexdigest()
                        + "  neutral.rgb565"
                    )
            (root / "SHA256SUMS").write_text("\n".join(lines) + "\n", encoding="utf-8")
            with self.assertRaises(ValueError):
                AVA.validate_avatar_assets(root)

            broken_gif = root / "gifs" / "happy.gif"
            with self.assertRaises(ValueError):
                AVA.prepare_avatar_fallback(broken_gif, root / "out.rgb565",
                                            decoder=Path(sys.executable))

    def test_validate_rejects_fallback_of_wrong_size(self):
        # Production change this catches: the fallback size check being dropped
        # from validate_avatar_assets. The recorded hash is refreshed for the
        # truncated file, so only the size check can catch it.
        with tempfile.TemporaryDirectory() as directory:
            root = copy_asset_tree(Path(directory) / "assets")
            fallback = root / "neutral.rgb565"
            truncated = fallback.read_bytes()[:1000]
            fallback.write_bytes(truncated)
            digest = hashlib.sha256(truncated).hexdigest()
            lines = SHA256SUMS.read_text(encoding="utf-8").splitlines()
            for index, line in enumerate(lines):
                if line.endswith("  neutral.rgb565"):
                    lines[index] = digest + "  neutral.rgb565"
            (root / "SHA256SUMS").write_text("\n".join(lines) + "\n", encoding="utf-8")
            with self.assertRaises(ValueError):
                AVA.validate_avatar_assets(root)

    def test_validate_rejects_corrupted_fallback(self):
        # Production change this catches: neutral.rgb565 missing from
        # SHA256SUMS and/or validate_avatar_assets not checking its hash.
        # Today this passes validation, which is the defect under test.
        with tempfile.TemporaryDirectory() as directory:
            root = copy_asset_tree(Path(directory) / "assets")
            fallback = root / "neutral.rgb565"
            data = bytearray(fallback.read_bytes())
            data[12345] ^= 0xFF
            self.assertEqual(len(data), RGB565_SIZE)
            fallback.write_bytes(bytes(data))
            with self.assertRaises(ValueError, msg="corrupted fallback accepted"):
                AVA.validate_avatar_assets(root)

        with tempfile.TemporaryDirectory() as directory:
            root = copy_asset_tree(Path(directory) / "assets")
            lines = [
                line for line in SHA256SUMS.read_text(encoding="utf-8").splitlines()
                if not line.endswith("  neutral.rgb565")
            ]
            (root / "SHA256SUMS").write_text("\n".join(lines) + "\n", encoding="utf-8")
            with self.assertRaises(ValueError, msg="fallback entry missing from SHA256SUMS"):
                AVA.validate_avatar_assets(root)


if __name__ == "__main__":
    unittest.main()


class IndependentFrameOracleTests(unittest.TestCase):
    def test_every_v3_frame_matches_independent_compositor(self):
        # Detects common decoder/compositing errors which two cache modes can share.
        oracle = json.loads((GIF_FIXTURES / "v3-frames.json").read_text())["gifs"]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for cache in (False, True):
                decoder = compile_gif_decoder(root / str(cache), cache)
                for name, frames in oracle.items():
                    output = root / "frames.bgra"
                    subprocess.run([decoder,"frames",GIFS_DIR / (name+".gif"),output,str(len(frames)*2)], check=True, capture_output=True)
                    data=output.read_bytes()
                    self.assertEqual(len(data),320*240*4*len(frames)*2)
                    for i in range(len(frames)*2):
                        with self.subTest(name=name, cache=cache, frame=i):
                            actual = hashlib.sha256(data[i*307200:(i+1)*307200]).hexdigest()
                            self.assertEqual(actual, frames[i%len(frames)]["sha256_bgra"])


class GifBoundaryTests(unittest.TestCase):
    def test_metadata_rejects_truncation_at_every_container_boundary(self):
        # Catch an unchecked descriptor read or accepting EOF in place of trailer.
        data = (GIFS_DIR/'neutral.gif').read_bytes()
        with tempfile.TemporaryDirectory() as directory:
            file = Path(directory)/'truncated.gif'
            for length in (12,13,14,790,len(data)-1):
                with self.subTest(length=length):
                    file.write_bytes(data[:length])
                    with self.assertRaises(ValueError): AVA.gif_metadata(file)
            # An image descriptor starts within the file but its fields end outside it.
            prefix = data[:13+3*(2 << (data[10]&7))] + b'\x2c\x00'
            file.write_bytes(prefix)
            with self.assertRaises(ValueError): AVA.gif_metadata(file)
