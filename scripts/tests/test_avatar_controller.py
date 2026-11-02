import os
import shlex
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "scripts" / "tests" / "fixtures"
DISPLAY = ROOT / "main" / "display"
BOARD = ROOT / "main" / "boards" / "elena-stackchan"
FIXTURE = FIXTURES / "avatar_controller_test.cc"

CASES = (
    "test_stop_requires_drained",
    "test_new_turn_rejects_stale_callbacks",
    "test_realtime_capture_starts_new_turn",
    "test_empty_stt_is_filtered_at_boundary",
    "test_mood_expires_at_5000_ms",
    "test_wait_expires_at_30000_ms",
    "test_audio_activity_extends_missing_stop_deadline",
    "test_notification_id_is_checked",
    "test_notice_survives_emotion",
    "test_all_aliases_and_invalid_names",
    "test_priority_and_no_deadline_resets",
    "test_mood_survives_long_reply",
    "test_stop_without_idle_keeps_speaking",
    "test_notification_drained_before_finished",
    "test_stop_drain_finish_permutations",
    "test_notification_playback_id_checks",
    "test_cancellation_clears_emotion",
)


class AvatarControllerHarnessTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        build_dir = Path(cls._tmp.name)
        cls.binary = build_dir / "avatar_controller_test"
        command = shlex.split(os.environ.get("CXX", "c++")) + [
            "-std=c++20",
            "-Wall",
            "-Werror",
            f"-I{DISPLAY}",
            f"-I{BOARD}",
            str(FIXTURE),
            str(BOARD / "avatar_controller.cc"),
            "-o",
            str(cls.binary),
        ]
        subprocess.run(command, check=True, cwd=build_dir)

    @classmethod
    def tearDownClass(cls):
        cls._tmp.cleanup()

    def run_case(self, name):
        result = subprocess.run([self.binary, name], capture_output=True, text=True)
        if result.returncode != 0:
            self.fail(f"{name} exited {result.returncode}: {result.stderr.strip()}")
        if result.stderr.strip():
            self.fail(f"{name} printed diagnostics: {result.stderr.strip()}")


def make_case(case_name):
    def test(self):
        self.run_case(case_name)

    test.__name__ = case_name
    return test


for case_name in CASES:
    setattr(AvatarControllerHarnessTest, case_name, make_case(case_name))


class ConversationVisualContractTest(unittest.TestCase):
    """The contract crossing Schedule must never carry spoken text."""

    def test_contract_has_no_transcript_fields(self):
        header = DISPLAY / "conversation_visual.h"
        text = header.read_text(encoding="utf-8")
        for forbidden in ("transcript", "content", "message", "text", "role", "char*"):
            self.assertNotIn(forbidden, text)

    def test_display_has_optional_conversation_hooks(self):
        text = (DISPLAY / "display.h").read_text(encoding="utf-8")
        self.assertIn("virtual bool SupportsConversationVisuals() const { return false; }", text)
        self.assertIn(
            "virtual void OnConversationVisualEvent(const ConversationVisualEvent& event) { "
            "(void)event; }",
            text,
        )
        self.assertIn('#include "conversation_visual.h"', text)

    def test_controller_has_no_lvgl_or_esp_dependency(self):
        for name in ("avatar_controller.h", "avatar_controller.cc"):
            text = (BOARD / name).read_text(encoding="utf-8")
            for forbidden in ("lvgl", "esp_", "driver/", "Application", "EmojiCollection"):
                self.assertNotIn(forbidden, text)


if __name__ == "__main__":
    unittest.main()
