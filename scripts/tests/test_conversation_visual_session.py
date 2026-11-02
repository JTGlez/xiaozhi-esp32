"""Execute the production session adapter and controller with a recording sink."""
import os
import shlex
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

class SessionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.binary = Path(cls.tmp.name) / 'session'
        subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) + [
            '-std=c++20', '-Wall', '-Wextra', '-Werror', '-pthread',
            '-I'+str(ROOT/'main/display'), '-I'+str(ROOT/'main/boards/elena-stackchan'),
            str(ROOT/'scripts/tests/fixtures/conversation_visual_session_test.cc'),
            str(ROOT/'main/display/conversation_visual_session.cc'),
            str(ROOT/'main/boards/elena-stackchan/avatar_controller.cc'),
            '-o', str(cls.binary)], check=True, capture_output=True, text=True)

    def run_case(self, name):
        result = subprocess.run([self.binary, name], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)

for case in ('stop_state_change_and_drain_permutations', 'realtime_capture_without_state_transition',
             'local_callbacks_are_generation_guarded', 'notification_finish_and_cancel',
             'empty_stt_and_vad_silence_do_not_think', 'operational_notice_lifecycle',
             'no_events_for_unsupported_display', 'audio_events_are_coalesced'):
    setattr(SessionTests, 'test_'+case, lambda self, case=case: self.run_case(case))
