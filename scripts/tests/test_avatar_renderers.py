"""Host tests for the avatar renderers, with real gifdec and a stub LVGL.

Each case in fixtures/avatar_renderers_test.cc runs in its own process.
"""

import os
import shlex
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "scripts"
FIXTURES = SCRIPTS / "tests" / "fixtures"
HOST = FIXTURES / "elena_display_host"
BOARD = ROOT / "main" / "boards" / "elena-stackchan"
AVATAR = BOARD / "avatars" / "elena"
DISPLAY = ROOT / "main" / "display"
LVGLD = DISPLAY / "lvgl_display"
GIFD = LVGLD / "gif"
HARNESS = FIXTURES / "avatar_renderers_test.cc"
GIFS_DIR = AVATAR / "gifs"

sys.path.insert(0, str(SCRIPTS))
import elena_avatar_assets as AVA  # noqa: E402

C_FILES = (
    HOST / "lvgl_host.c",
    HOST / "esp_timer_host.c",
    HOST / "esp_lvgl_port_host.c",
    GIFD / "gifdec.c",
)
PRODUCTION_CPP = (
    BOARD / "gif_avatar_renderer.cc",
    BOARD / "image_avatar_renderer.cc",
)
CPP_FILES = (
    HARNESS,
    HOST / "host_support.cc",
    *PRODUCTION_CPP,
    DISPLAY / "display.cc",
    DISPLAY / "lcd_display.cc",
    LVGLD / "lvgl_display.cc",
    LVGLD / "lvgl_theme.cc",
    LVGLD / "emoji_collection.cc",
    LVGLD / "lvgl_image.cc",
    LVGLD / "dynamic_glyph_cache.cc",
    GIFD / "lvgl_gif.cc",
    DISPLAY / "text_glyph.cc",
)
CASES = (
    "gif_attach_centers_without_scaling",
    "gif_same_presentation_keeps_decoder",
    "gif_changes_ignore_notice_flags",
    "gif_new_collection_reapplies",
    "gif_missing_emotion_uses_neutral",
    "gif_corrupted_shows_fallback_without_retry",
    "gif_null_collection_shows_fallback",
    "gif_oom_shows_fallback",
    "gif_recovers_after_reload",
    "gif_power_save_shows_sleeping_then_returns",
    "gif_power_save_without_presentation_returns_to_neutral",
    "gif_before_attach_is_harmless",
    "gif_destructor_stops_timer",
    "image_known_emotion",
    "image_mapped_emotion",
    "image_unknown_uses_neutral",
    "image_empty_collection_shows_name",
    "image_null_collection_shows_name",
    "image_recovers_after_reload",
    "image_power_save_then_returns",
    "image_table_matches_plan",
    "image_before_attach_is_harmless",
    "image_gif_is_a_still_source",
)


def run(command, cwd):
    result = subprocess.run(command, capture_output=True, text=True, cwd=cwd)
    if result.returncode != 0:
        raise RuntimeError(f"command failed: {' '.join(map(str, command))}\n{result.stderr}")
    return result


class AvatarRendererTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls._tmp.cleanup)
        build = Path(cls._tmp.name)
        AVA.write_avatar_fallback_header(AVATAR / "neutral.rgb565", build / "elena_avatar_fallback.h")
        run([sys.executable, str(SCRIPTS / "avatar_pack.py"), "header", "--avatar-dir", str(AVATAR),
             "--out", str(build / "avatar_pack_config.h")], build)
        cc = shlex.split(os.environ.get("CC", "cc"))
        cxx = shlex.split(os.environ.get("CXX", "c++"))
        includes = [f"-I{path}" for path in (HOST, DISPLAY, LVGLD, BOARD, build)]
        objects = []
        for source in C_FILES:
            obj = build / (source.stem + ".o")
            run(cc + ["-std=gnu11", "-Wall", *includes, "-c", str(source), "-o", str(obj)], build)
            objects.append(obj)
        for source in CPP_FILES:
            obj = build / (source.stem + ".o")
            flags = ["-std=c++20", "-Wall"] + (["-Werror"] if source in PRODUCTION_CPP else [])
            run(cxx + flags + includes + ["-c", str(source), "-o", str(obj)], build)
            objects.append(obj)
        cls.binary = build / "avatar_renderers_test"
        run(cxx + [str(obj) for obj in objects] + ["-o", str(cls.binary)], build)

    def run_case(self, name):
        result = subprocess.run([str(self.binary), name, str(GIFS_DIR)],
                                capture_output=True, text=True, timeout=120)
        self.assertEqual(result.returncode, 0, f"{name}\n{result.stdout}\n{result.stderr}")
        self.assertIn(f"PASS {name}", result.stdout)


def _add_case(name):
    setattr(AvatarRendererTests, f"test_{name}", lambda self: self.run_case(name))


for _case in CASES:
    _add_case(_case)


if __name__ == "__main__":
    unittest.main()
