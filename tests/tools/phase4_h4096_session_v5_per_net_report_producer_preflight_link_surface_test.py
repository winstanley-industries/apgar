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
_EXPECTED_PREFLIGHT_NAMES = frozenset(
    (
        "phase4_confirmatory_h4096_session_v5_per_net_report_producer_preflight_test_runner",
        "phase4_confirmatory_h4096_session_v5_unpublishable_source_per_net_report_producer_preflight_test_runner",
    )
)

_REQUIRED_CAPABILITY_MINIMAL_SYMBOLS = (
    " main",
    "BuildPhase4ConfirmatoryH4096SessionV5OrdinaryPerNetReportProducerIdentity(",
    "PreflightPhase4ConfirmatoryH4096SessionV5OrdinaryPerNetReportProducer(",
    "BuildPhase4ConfirmatoryH4096SessionV5PreflightIdentity(",
    "BuildPhase4ConfirmatoryH4096SessionV5CanonicalCell(",
    "PreflightPhase4ConfirmatoryH4096SessionV5Controller(",
    "BuildPhase4CanonicalTrialSpecForCorpusV2H4096SessionV5(",
    "ComputePhase4CanonicalAlgorithmBudgetChecksumV1(",
    "ComputePhase4PairedBudgetChecksumForAuthorityV1(",
    "PreflightPhase4CorpusV2SessionExecutionAuthority(",
)

_FORBIDDEN_CAPABILITY_SYMBOLS = (
    "AllocateMultiWorld",
    "AllocateOneWorld",
    "AssemblePhase4",
    "BuildPhase4ConfirmatoryH4096OrdinaryPerNetReportArtifact",
    "BuildPhase4ConfirmatoryH4096SameRunPerNetReportArtifact",
    "BuildPhase4Imported",
    "BuildPhase4PerNetReportArtifact",
    "BuildPhase4RepresentativeCase",
    "CandidateStore::",
    "ChildLaunch",
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
    "LaunchPhase4",
    "PersistentCpuCandidatePoolPreparer::",
    "PrepareCpuCandidate",
    "PrepareInitialCpuCandidatePools",
    "PreflightPhase4ConfirmatoryH4096SessionV5Worker(",
    "ReadFile",
    "ReadPhase4TrialWireMessage",
    "RunPhase4",
    "SerializePhase4",
    "ValidatePhase4ConfirmatoryH4096OrdinaryPerNetReportArtifact",
    "ValidatePhase4ConfirmatoryH4096SameRunPerNetReportArtifact",
    "ValidatePhase4PerNetReportArtifact",
    "WorkerLauncher",
    "WriteFile",
    "WritePhase4TrialWireMessage",
    "kicad_fixture",
    "tool_runfiles",
)


def _matching_symbol_lines(symbols: str, needle: str) -> list[str]:
    return [line for line in symbols.splitlines() if needle in line]


class Phase4H4096SessionV5PerNetReportProducerPreflightLinkSurfaceTest(unittest.TestCase):
    def test_both_process_runners_retain_only_the_preflight_boundary(self) -> None:
        self.assertEqual(len(_PREFLIGHTS), 2)
        self.assertEqual(
            {pathlib.PurePosixPath(rootpath).name for rootpath in _PREFLIGHTS},
            _EXPECTED_PREFLIGHT_NAMES,
        )

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

                for required in _REQUIRED_CAPABILITY_MINIMAL_SYMBOLS:
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

    def test_forbidden_matcher_covers_each_closed_capability_class(self) -> None:
        representative_symbols = (
            "BuildPhase4ConfirmatoryH4096OrdinaryPerNetReportArtifact(",
            "ExecutePhase4ConfirmatoryH4096OrdinaryTrialArmDiagnostic(",
            "BuildPhase4RepresentativeCaseV2(",
            "PrepareInitialCpuCandidatePools(",
            "PreflightPhase4ConfirmatoryH4096SessionV5Worker(",
            "AllocateOneWorld(",
            "SerializePhase4PerNetReportArtifactJsonV1(",
            "WritePhase4TrialWireMessageV2ForCorpusV2(",
            "ChildLaunchFailure",
        )
        for symbol in representative_symbols:
            with self.subTest(symbol=symbol):
                self.assertTrue(
                    any(forbidden in symbol for forbidden in _FORBIDDEN_CAPABILITY_SYMBOLS),
                    f"{symbol!r} is not covered by the capability blacklist",
                )


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
