"""Host tests for AvatarStage.

These tests compile and run the *real* production sources:
  main/boards/elena-stackchan/avatar_stage.cc, avatar_clock.cc, gif_avatar_renderer.cc
  main/boards/elena-stackchan/avatar_controller.cc
  main/display/display.cc, main/display/lcd_display.cc,
  main/display/lvgl_display/lvgl_display.cc, lvgl_theme.cc,
  emoji_collection.cc, lvgl_image.cc, dynamic_glyph_cache.cc,
  main/display/text_glyph.cc, main/display/lvgl_display/gif/lvgl_gif.cc,
  main/display/lvgl_display/gif/gifdec.c
against the host adapters under scripts/tests/fixtures/elena_display_host/.
Assertions are on observable widget state (flags/text/src/scale/dims), GIF
decoder identity, decoder-open attempts (workspace allocations made by the
real gifdec.c), the lock-flagged LVGL mutation log, and real esp_timer
callbacks fired from the adapter registry. No test asserts translated text.
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
DISPLAY = ROOT / "main" / "display"
LVGLD = DISPLAY / "lvgl_display"
GIFD = LVGLD / "gif"
HARNESS = FIXTURES / "avatar_stage_test.cc"
CONTRACT_HARNESS = FIXTURES / "avatar_stage_contract_test.cc"
FAKE_FACTORY = FIXTURES / "fake_avatar_factory.cc"
GIFS_DIR = BOARD / "avatars" / "elena" / "gifs"
FALLBACK_RGB565 = BOARD / "avatars" / "elena" / "neutral.rgb565"

sys.path.insert(0, str(SCRIPTS))
import elena_avatar_assets as AVA  # noqa: E402

C_FILES = (
    HOST / "lvgl_host.c",
    HOST / "esp_timer_host.c",
    HOST / "esp_lvgl_port_host.c",
    GIFD / "gifdec.c",
)
SHARED_CPP = (
    HOST / "host_support.cc",
    BOARD / "avatar_stage.cc",
    BOARD / "avatar_clock.cc",
    BOARD / "avatar_controller.cc",
    DISPLAY / "display.cc",
    DISPLAY / "lcd_display.cc",
    LVGLD / "lvgl_display.cc",
    LVGLD / "lvgl_theme.cc",
    LVGLD / "emoji_collection.cc",
    LVGLD / "lvgl_image.cc",
    LVGLD / "dynamic_glyph_cache.cc",
    LVGLD / "gif" / "lvgl_gif.cc",
    DISPLAY / "text_glyph.cc",
)
STAGE_CPP = (
    HARNESS,
    BOARD / "gif_avatar_renderer.cc",
    BOARD / "gif_avatar_factory.cc",
)
CONTRACT_CPP = (
    CONTRACT_HARNESS,
    FAKE_FACTORY,
)
PRODUCTION_CPP = (
    BOARD / "avatar_stage.cc",
    BOARD / "avatar_clock.cc",
    BOARD / "gif_avatar_renderer.cc",
    BOARD / "gif_avatar_factory.cc",
    BOARD / "avatar_controller.cc",
)


class HarnessError(Exception):
    pass


def run(command, cwd):
    result = subprocess.run(command, capture_output=True, text=True, cwd=cwd)
    if result.returncode != 0:
        raise HarnessError(
            f"command failed ({result.returncode}): {' '.join(command)}\n"
            f"stderr:\n{result.stderr}"
        )
    return result


class AvatarStageHarnessTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        cls.build_dir = Path(cls._tmp.name)
        # Generated into the temporary build dir, as the firmware build does.
        AVA.write_avatar_fallback_header(
            FALLBACK_RGB565, cls.build_dir / "elena_avatar_fallback.h"
        )
        run([sys.executable, str(SCRIPTS / "avatar_pack.py"), "header",
             "--avatar-dir", str(BOARD / "avatars" / "elena"),
             "--out", str(cls.build_dir / "avatar_pack_config.h")], cls.build_dir)
        cc = shlex.split(os.environ.get("CC", "cc"))
        cxx = shlex.split(os.environ.get("CXX", "c++"))
        includes = [
            f"-I{HOST}",
            f"-I{DISPLAY}",
            f"-I{LVGLD}",
            f"-I{BOARD}",
            f"-I{FIXTURES}",
            f"-I{cls.build_dir}",
        ]

        def compile_cpp(sources):
            objects = []
            for source in sources:
                obj = cls.build_dir / (source.stem + ".o")
                flags = ["-std=c++20", "-Wall"]
                if source in PRODUCTION_CPP:
                    flags.append("-Werror")
                run(cxx + flags + includes + ["-c", str(source), "-o", str(obj)], cls.build_dir)
                objects.append(obj)
            return objects

        shared = []
        for source in C_FILES:
            obj = cls.build_dir / (source.stem + ".o")
            run(cc + ["-std=gnu11", "-Wall", *includes, "-c", str(source),
                      "-o", str(obj)], cls.build_dir)
            shared.append(obj)
        shared += compile_cpp(SHARED_CPP)
        cls.binary = cls.build_dir / "avatar_stage_test"
        run(cxx + [str(obj) for obj in shared + compile_cpp(STAGE_CPP)] + ["-o", str(cls.binary)],
            cls.build_dir)
        cls.contract_binary = cls.build_dir / "avatar_stage_contract_test"
        run(cxx + [str(obj) for obj in shared + compile_cpp(CONTRACT_CPP)]
            + ["-o", str(cls.contract_binary)], cls.build_dir)

    @classmethod
    def tearDownClass(cls):
        cls._tmp.cleanup()

    def run_case(self, name, binary=None):
        result = subprocess.run(
            [str(binary or self.binary), name, str(GIFS_DIR)],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            self.fail(
                f"{name} exited {result.returncode}\n"
                f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
            )
        if f"PASS {name}" not in result.stdout:
            self.fail(f"{name} did not report completion: {result.stdout}")
        return result.stdout

    def test_conversation_and_notification_text_stays_hidden(self):
        """Mutation targets: forwarding conversation SetChatMessage/SetStatus
        to the base, or honoring SetHideSubtitle(false) with stored text, or
        keeping hidden conversation content as pending operational text, must
        make some CAP-* string visible on some widget."""
        self.run_case("hidden_conversation")

    def test_operational_notice_and_maintenance_text_remain_visible(self):
        """Mutation targets: unconditional suppression of SetStatus/
        SetChatMessage while a semantic notice or maintenance activity owns
        the screen, an emotion clearing an active notice, maintenance being
        ignored, or a routine clock/status clobbering an active notice."""
        self.run_case("operational_notice")
        self.run_case("operational_maintenance")
        self.run_case("spoken_not_operational")

    def test_same_expression_does_not_reopen_gif(self):
        """Mutation targets: deleting the same-asset fast path (new decoder
        per repeat), or recording a pre-SetupUI SetEmotion as applied (no
        first decoder after SetupUI), or double replacement on a real
        change."""
        self.run_case("same_expression_pre_ui")
        self.run_case("same_expression_events")

    def test_missing_and_oom_use_static_elena(self):
        """Mutation targets: the resource existence check, the IsLoaded
        check, allowing more than one neutral attempt, missing failure
        memoization (retry loops on repeated Tick), the static fallback path
        itself, recovery invalidation on a new expression or asset reload,
        recording a failed expression as applied, or falling back to the
        original emoji/font path."""
        self.run_case("missing_resource")
        self.run_case("corrupt_resource")
        self.run_case("mutated_resource")
        self.run_case("truncated_resource")
        self.run_case("oom_resource")
        self.run_case("neutral_missing")
        self.run_case("null_collection")
        self.run_case("invalid_resource")
        self.run_case("retry_no_loop")
        self.run_case("recover_after_reload")

    def test_theme_and_asset_reload_preserve_presentation(self):
        """Mutation targets: caching presentation by emotion string alone,
        caching by Theme pointer alone, or letting the base theme layout
        (opaque top bar, theme background, clock status) persist over the
        Elena layout."""
        self.run_case("theme_switch_during_speaking")
        self.run_case("collection_replaced_in_place")
        self.run_case("assets_removed_then_restored")

    def test_preview_restores_current_state(self):
        """Mutation targets: ignoring the preview request, restoring the old
        cached presentation instead of the current one on close (explicit or
        through the registered preview timer), missing timeout dispatch,
        reopening a GIF for activity changes while a preview is open, an
        unsafe double close, or a preview displacing an active notice."""
        self.run_case("preview_pause_resume_current")
        self.run_case("preview_notice_priority")
        self.run_case("preview_double_close")

    def test_clock_and_notification_timer_do_not_restore_normal_status_text(self):
        """Mutation targets: the inherited notification timer unconditionally
        unhiding the status label (clock), clock leakage through
        UpdateStatusBar at a host clock boundary, dropping one
        ShowNotification overload, or dropping indicator/low-battery
        refresh to hide the clock."""
        self.run_case("notification_expiry_char_overload")
        self.run_case("notification_expiry_string_overload")
        self.run_case("notice_survives_notification")
        self.run_case("status_bar_indicators")
        self.run_case("low_battery_popup")

    def test_layout_and_locking(self):
        """Mutation targets: reintroducing the bottom bar/chat label or an
        opaque top bar, scaling or cropping the 320x240 avatar image,
        re-running SetupUI, mutating graphics or touching GIF ownership
        when the display lock fails, or destroying the display while
        notification/preview timers are still armed."""
        self.run_case("layout_idempotent")
        self.run_case("lock_failure_no_mutation")
        self.run_case("state_text_beside_indicators")
        self.run_case("unchanged_tick_skips_display_lock")
        self.run_case("destruction_with_pending_timers")
        self.run_case("layout_positions")

    def test_status_bubble(self):
        """Mutation targets: showing an empty bubble, letting long text move the
        clock or wrap to a second line, or an error without its color and mark."""
        self.run_case("bubble_hidden_while_empty")
        self.run_case("bubble_long_text_keeps_dots_and_cap")
        self.run_case("error_status_is_red_with_cross")

    def test_clock_and_theme_keep_the_stage(self):
        """Mutation targets: the clock writing the status, or a theme replacing
        the transparent bar or the avatar's background."""
        self.run_case("clock_never_writes_status")
        self.run_case("top_bar_transparent_after_theme")

    def test_renderer_contract(self):
        """The stage drives a fake renderer: Attach once, Present only on changes,
        power save forwarded, and nothing without the display lock."""
        for name in ("attach_once", "present_only_on_changes", "present_on_notice_flag_change",
                     "theme_reload_presents_again", "same_collection_theme_change_may_present_once",
                     "preview_blocks_present", "power_save_forwarded", "failed_lock_calls_nothing",
                     "renderer_gets_no_text"):
            with self.subTest(name=name):
                self.run_case(name, self.contract_binary)


if __name__ == "__main__":
    unittest.main()
