from __future__ import annotations

import os
import pathlib
import subprocess
import sys
import unittest


def _runfile(rootpath: str) -> pathlib.Path:
    return pathlib.Path(os.environ["TEST_SRCDIR"]) / os.environ["TEST_WORKSPACE"] / rootpath


_LLVM_NM = sys.argv[1]
_PREFLIGHTS = tuple(sys.argv[2:])

_FORBIDDEN_CAPABILITY_SYMBOLS = (
    "AllocateMultiWorld",
    "AllocateOneWorld",
    "AssemblePhase4",
    "BuildPhase4Imported",
    "BuildPhase4RepresentativeCase",
    "CandidateStore::",
    "CpuAStar",
    "CpuCandidateAllocationSession::",
    "CpuCandidatePoolPreparer::",
    "CreatePersistentCpuCandidatePoolPreparer",
    "DecodePhase4TrialWireMessage",
    "EncodePhase4TrialWireMessage",
    "ExecuteCpuCandidateAllocation",
    "ExecutePhase4",
    "ExecuteSequentialNegotiatedBaseline",
    "ExecuteTargetedRegeneration",
    "FinalizePhase4",
    "KiCad",
    "Kicad",
    "PersistentCpuCandidatePoolPreparer::",
    "PrepareCpuCandidate",
    "ReadFile",
    "ReadPhase4TrialWireMessage",
    "RunPhase4",
    "SerializePhase4",
    "WritePhase4TrialWireMessage",
    "kicad_fixture",
)


def _matching_symbol_lines(symbols: str, needle: str) -> list[str]:
    return [line for line in symbols.splitlines() if needle in line]


class Phase4H4096SessionV5PreflightLinkSurfaceTest(unittest.TestCase):
    def test_fixtureless_binaries_contain_only_preflight_capability(self) -> None:
        self.assertEqual(len(_PREFLIGHTS), 2)
        for rootpath in _PREFLIGHTS:
            binary = _runfile(rootpath)
            with self.subTest(binary=binary.name):
                symbols = subprocess.run(
                    [
                        str(_runfile(_LLVM_NM)),
                        "--defined-only",
                        "--demangle",
                        str(binary),
                    ],
                    check=True,
                    text=True,
                    capture_output=True,
                ).stdout
                for required in (
                    " main",
                    "PreflightPhase4ConfirmatoryH4096SessionV5Controller(",
                    "PreflightPhase4ConfirmatoryH4096SessionV5Worker(",
                    "PreflightPhase4ConfirmatoryH4096SessionV5Spec(",
                    "BuildPhase4CanonicalTrialSpecForCorpusV2H4096SessionV5(",
                    "ComputePhase4CanonicalAlgorithmBudgetChecksumV1(",
                    "ComputePhase4PairedBudgetChecksumForAuthorityV1(",
                ):
                    self.assertTrue(
                        _matching_symbol_lines(symbols, required),
                        f"{required!r} is absent from {binary.name}",
                    )
                for forbidden in _FORBIDDEN_CAPABILITY_SYMBOLS:
                    matches = _matching_symbol_lines(symbols, forbidden)
                    self.assertFalse(
                        matches,
                        f"{forbidden!r} unexpectedly found in {binary.name}:\n"
                        + "\n".join(matches[:20]),
                    )

    def test_forbidden_matcher_covers_independently_linkable_boundary_capabilities(self) -> None:
        representative_symbols = (
            "FinalizePhase4ConfirmatoryH4096OrdinaryTrialArm(",
            "AssemblePhase4ConfirmatoryH4096SameRunPairedTrial(",
            "EncodePhase4TrialWireMessageV1(",
            "DecodePhase4TrialWireMessageV2ForCorpusV2(",
            "ReadPhase4TrialWireMessageV1(",
            "WritePhase4TrialWireMessageV2ForCorpusV2(",
        )
        for symbol in representative_symbols:
            with self.subTest(symbol=symbol):
                self.assertTrue(
                    any(forbidden in symbol for forbidden in _FORBIDDEN_CAPABILITY_SYMBOLS),
                    f"{symbol!r} is not covered by the capability blacklist",
                )


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
