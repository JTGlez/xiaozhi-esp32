"""Host tests for the avatar logic that needs no LVGL."""

import os
import shlex
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "scripts" / "tests" / "fixtures" / "avatar_logic_test.cc"
BOARD = ROOT / "main" / "boards" / "elena-stackchan"
PROTOCOLS = ROOT / "main" / "protocols"
DISPLAY = ROOT / "main" / "display"
SOURCES = (BOARD / "avatar_clock.cc", BOARD / "boot_view.cc", PROTOCOLS / "server_endpoint.cc",
           PROTOCOLS / "server_wait.cc")
CASES = (
    "clock_hidden_at_1970",
    "clock_hidden_at_2024",
    "clock_formats_time",
    "clock_small_buffer_is_safe",
    "bubble_width_keeps_short_text",
    "bubble_width_caps_long_text",
    "bubble_width_never_zero",
    "parse_ws_with_port",
    "parse_defaults",
    "parse_limits",
    "parse_rejects",
    "boot_views",
    "boot_countdown",
    "boot_copy_has_only_available_glyphs",
    "wait_reachable_server_is_not_checked_again",
    "wait_counts_down_between_probes",
    "wait_lets_power_save_run_until_it_ends",
    "wait_refreshes_settings_after_a_failure",
    "wait_without_url_asks_for_settings",
    "wait_without_url_keeps_retrying",
    "wait_stops_when_settings_bring_mqtt",
    "wait_skips_everything_with_mqtt",
)


class AvatarLogicTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls._tmp.cleanup)
        cls.binary = Path(cls._tmp.name) / "avatar_logic_test"
        cxx = shlex.split(os.environ.get("CXX", "c++"))
        command = cxx + ["-std=c++20", "-Wall", "-Werror", "-UNDEBUG", f"-I{BOARD}", f"-I{PROTOCOLS}",
                         f"-I{DISPLAY}", str(FIXTURE), *map(str, SOURCES), "-o", str(cls.binary)]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode != 0:
            raise RuntimeError(result.stderr)

    def run_case(self, name):
        result = subprocess.run([str(self.binary), name], capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, f"{name}\n{result.stdout}\n{result.stderr}")
        self.assertIn(f"PASS {name}", result.stdout)


def _add_case(name):
    setattr(AvatarLogicTests, f"test_{name}", lambda self: self.run_case(name))


for _case in CASES:
    _add_case(_case)


if __name__ == "__main__":
    unittest.main()
