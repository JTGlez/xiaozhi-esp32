import os
import shlex
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "scripts" / "tests" / "fixtures" / "screen_stream_core_test.cc"
BOARD = ROOT / "main" / "boards" / "elena-stackchan"

CASES = (
    "test_hash_changes_with_one_pixel",
    "test_hash_depends_on_area",
    "test_hash_ignores_buffer_alignment",
    "test_hash_has_no_collisions_on_random_strips",
    "test_parse_ipv4_host",
    "test_lease_expires_without_renewal",
    "test_lease_reports_new_sessions_only",
    "test_lease_late_renewal_does_not_restart_the_session",
    "test_tracker_new_strip_needs_copy",
    "test_tracker_pending_strip_needs_no_second_copy",
    "test_tracker_acked_strip_is_known",
    "test_tracker_evicts_oldest_other_frame",
    "test_tracker_never_evicts_current_frame",
    "test_tracker_never_evicts_busy_slots",
    "test_tracker_resends_unacked_strips",
    "test_tracker_sends_newest_frame_first",
    "test_tracker_ack_during_send_releases_after_send",
    "test_tracker_forgets_known_strips_when_full",
    "test_tracker_reset_forgets_everything",
    "test_map_replaces_covered_entries",
    "test_map_keeps_partial_overlay_on_top",
    "test_map_reports_overflow",
    "test_chunk_header_bytes",
    "test_map_packet_bytes",
    "test_map_packet_rejects_small_buffer",
    "test_stats_packet_bytes",
    "test_parse_hello",
    "test_parse_ack",
    "test_parse_rejects_malformed_packets",
    "test_pacer_limits_rate",
    "test_pacer_does_not_accumulate_idle_time",
)


class ScreenStreamCoreHarnessTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        build_dir = Path(cls._tmp.name)
        cls.binary = build_dir / "screen_stream_core_test"
        command = shlex.split(os.environ.get("CXX", "c++")) + [
            "-std=c++20",
            "-Wall",
            "-Werror",
            "-UNDEBUG",
            f"-I{BOARD}",
            str(FIXTURE),
            str(BOARD / "screen_stream_core.cc"),
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
    setattr(ScreenStreamCoreHarnessTest, case_name, make_case(case_name))


if __name__ == "__main__":
    unittest.main()
