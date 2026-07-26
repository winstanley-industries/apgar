from __future__ import annotations

import os
import pathlib
import subprocess
import sys
import unittest


def _runfile(rootpath: str) -> pathlib.Path:
    return pathlib.Path(os.environ["TEST_SRCDIR"]) / os.environ["TEST_WORKSPACE"] / rootpath


_MODE = sys.argv[1]
_ROOTPATHS = tuple(sys.argv[2:])


def _matching_symbol_lines(symbols: str, needle: str) -> list[str]:
    return [line for line in symbols.splitlines() if needle in line]


class Phase4H4096SessionV5PreimageLinkSurfaceTest(unittest.TestCase):
    def test_final_binaries_contain_no_execution_capability(self) -> None:
        expected_rootpaths = {
            "generator-only": 2,
            "generator-and-structural-test": 3,
        }
        self.assertIn(_MODE, expected_rootpaths)
        self.assertEqual(len(_ROOTPATHS), expected_rootpaths[_MODE])
        llvm_nm = _runfile(_ROOTPATHS[0])
        for rootpath in _ROOTPATHS[1:]:
            binary = _runfile(rootpath)
            with self.subTest(binary=binary.name):
                result = subprocess.run(
                    [str(llvm_nm), "--defined-only", "--demangle", str(binary)],
                    check=True,
                    text=True,
                    capture_output=True,
                )
                symbols = result.stdout
                for required in (
                    " main",
                    "BuildPhase4CanonicalTrialSpecForCorpusV2(",
                    "ComputePhase4CanonicalAlgorithmBudgetChecksumV1(",
                ):
                    self.assertTrue(
                        _matching_symbol_lines(symbols, required),
                        f"{required!r} is absent from {binary.name}",
                    )
                external_symbols = subprocess.run(
                    [
                        str(llvm_nm),
                        "--defined-only",
                        "--demangle",
                        "--extern-only",
                        str(binary),
                    ],
                    check=True,
                    text=True,
                    capture_output=True,
                ).stdout
                for private_builder in (
                    "BuildPhase4CanonicalTrialSpecForCorpusV2H4096(",
                    "BuildPhase4CanonicalTrialSpecForCorpusV2H4096SessionV5(",
                ):
                    external_matches = _matching_symbol_lines(
                        external_symbols,
                        private_builder,
                    )
                    self.assertFalse(
                        external_matches,
                        f"{private_builder!r} unexpectedly has external linkage:\n"
                        + "\n".join(external_matches[:20]),
                    )
                for forbidden in (
                    "AllocateMultiWorld",
                    "AllocateOneWorld",
                    "BuildPhase4RepresentativeCase",
                    "BuildPhase4Imported",
                    "CandidateStore::",
                    "CpuAStar",
                    "CpuCandidateAllocationSession::",
                    "CpuCandidatePoolPreparer::",
                    "CreatePersistentCpuCandidatePoolPreparer",
                    "ExecuteCpuCandidateAllocation",
                    "ExecutePhase4",
                    "ExecuteSequentialNegotiatedBaseline",
                    "ExecuteTargetedRegeneration",
                    "KiCad",
                    "Kicad",
                    "Phase4RepresentativeCase::",
                    "Phase4TrialArmExecution",
                    "PersistentCpuCandidatePoolPreparer::",
                    "PrepareCpuCandidate",
                    "SequentialNegotiatedBaselineResult",
                    "kicad_fixture",
                ):
                    matches = _matching_symbol_lines(symbols, forbidden)
                    self.assertFalse(
                        matches,
                        f"{forbidden!r} unexpectedly found in {binary.name}:\n"
                        + "\n".join(matches[:20]),
                    )


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
