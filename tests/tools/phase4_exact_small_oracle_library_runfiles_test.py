"""Runfiles-closure regression for library-mode exact-small Oracle consumers."""

from __future__ import annotations

import os
import unittest

from tools import validate_phase4_exact_small_oracle as oracle


class Phase4ExactSmallOracleLibraryRunfilesTest(unittest.TestCase):
    def test_dynamic_handshake_and_replay_are_in_the_library_closure(self) -> None:
        replay = oracle._admission_replay_path()
        self.assertEqual(replay.name, "phase4_exact_small_candidate_admission_replay")
        self.assertTrue(replay.is_file())
        self.assertTrue(os.access(replay, os.X_OK))


if __name__ == "__main__":
    unittest.main()
