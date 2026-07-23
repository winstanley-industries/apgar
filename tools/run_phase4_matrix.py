"""Acquire, validate, aggregate, and resume the frozen Phase 4 evidence matrix."""

from __future__ import annotations

import argparse
import contextlib
import ctypes
import fcntl
import json
import os
import pathlib
import secrets
import signal
import stat
import subprocess
import sys
import tempfile
import time
from collections.abc import Mapping, Sequence
from typing import Any

from tools import aggregate_phase4_matrix as matrix_aggregator
from tools import capture_phase4_operational_measurement as capture_tool
from tools import validate_phase4_per_net_report as report_v1
from tools import validate_phase4_per_net_report_v2 as report_v2
from tools import validate_phase4_raw_evidence as raw_validator
from tools import validate_phase4_same_run_decision_telemetry as same_run
from tools import validate_phase4_statistical_protocol_v4 as protocol_v4

_CANONICAL_COMMIT_LENGTH = 40
_SETUP_NS = 300_000_000_000
_PREPARED_NS = 300_000_000_000
_COLD_NS = 300_000_000_000
_ADDRESS_SPACE_BYTES = 64 << 30
_PEAK_HOST_BYTES = 16 << 30
_MAXIMUM_NETS = 4096
_MAXIMUM_COMPILED_NODES = 100_000_000
_MAXIMUM_COMPILED_HOST_BYTES = 8 << 30
_MAXIMUM_ACTIVE_REGIONS = 250_000
_MAXIMUM_BOARD_ENTITIES = 100_000
_WORKERS = 4
_REPETITIONS = 20
_TARGETS = (
    "phase4_canonical_budget_roster",
    "phase4_evidence_runner",
    "phase4_exact_small_oracle_v2_validator",
    "phase4_exact_small_snapshot_runner",
    "phase4_fixed_query_controls",
    "phase4_matrix_aggregator",
    "phase4_operational_capture",
    "phase4_operational_measurement_validator",
    "phase4_operational_projection",
    "phase4_operational_replay_worker",
    "phase4_per_net_report_runner",
    "phase4_per_net_report_v2_validator",
    "phase4_per_net_report_validator",
    "phase4_raw_evidence_validator",
    "phase4_same_run_decision_telemetry_validator",
    "phase4_statistical_protocol_v4_validator",
    "phase4_stress_evidence",
    "phase4_stress_work_bound_probe",
)


class RunError(RuntimeError):
    """Stable operator failure for the Phase 4 matrix run."""


class CommandTimeout(RunError):
    """A command exceeded its operator timeout after descendant cleanup."""

    def __init__(self, command: Sequence[str], stdout: bytes, stderr: bytes):
        super().__init__(f"command exceeded outer operator timeout: {command[0]}")
        self.command = tuple(command)
        self.stdout = stdout
        self.stderr = stderr


class CommandInterrupted(RunError):
    """The operator process received a termination signal after cleanup."""

    def __init__(self, signal_number: int):
        self.signal_number = signal_number
        super().__init__(f"matrix command interrupted by {signal.Signals(signal_number).name}")


def _canonical(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":"))


def _arm_command_parent_death(
    expected_parent_pid: int,
    inherited_signal_mask: set[signal.Signals],
) -> None:
    try:
        if os.getppid() != expected_parent_pid:
            os.kill(os.getpid(), signal.SIGKILL)
        libc = ctypes.CDLL(None, use_errno=True)
        libc.prctl.restype = ctypes.c_int
        if libc.prctl(1, signal.SIGKILL, 0, 0, 0) != 0:
            os._exit(127)
        if os.getppid() != expected_parent_pid:
            os.kill(os.getpid(), signal.SIGKILL)
        signal.pthread_sigmask(signal.SIG_SETMASK, inherited_signal_mask)
    except BaseException:
        os._exit(127)


def _run(
    command: Sequence[str],
    *,
    cwd: pathlib.Path,
    timeout: int,
    stdout: int | None = subprocess.PIPE,
) -> subprocess.CompletedProcess[bytes]:
    environment = os.environ.copy()
    for variable in (
        "JAVA_RUNFILES",
        "PYTHON_RUNFILES",
        "RUNFILES_DIR",
        "RUNFILES_MANIFEST_FILE",
        "TEST_SRCDIR",
        "TEST_WORKSPACE",
    ):
        environment.pop(variable, None)
    subreaper_was_enabled = True
    descendant_authority = False
    process: subprocess.Popen[bytes] | None = None
    cleanup_errors: list[BaseException] = []
    previous_signal_handlers: dict[int, Any] = {}
    termination_signals = (signal.SIGINT, signal.SIGTERM, signal.SIGHUP)
    signal_mask_before_install: set[signal.Signals] | None = None
    interruption_started = False

    def controlled_termination(signal_number: int, _frame: Any) -> None:
        nonlocal interruption_started
        if interruption_started:
            return
        interruption_started = True
        # Ignore every further operator signal while the first one unwinds
        # through descendant containment.
        for ignored_signal in termination_signals:
            signal.signal(ignored_signal, signal.SIG_IGN)
        if signal_number == signal.SIGINT:
            raise KeyboardInterrupt
        raise CommandInterrupted(signal_number)

    try:
        signal_mask_before_install = signal.pthread_sigmask(
            signal.SIG_BLOCK,
            termination_signals,
        )
        for signal_number in termination_signals:
            previous_signal_handlers[signal_number] = signal.signal(
                signal_number,
                controlled_termination,
            )
        subreaper_was_enabled = capture_tool._child_subreaper_enabled()
        if capture_tool._direct_child_pids():
            raise RunError("matrix runner has pre-existing child processes")
        descendant_authority = True
        if not subreaper_was_enabled:
            capture_tool._set_child_subreaper(True)
        parent_pid = os.getpid()
        process = subprocess.Popen(
            list(command),
            cwd=cwd,
            env=environment,
            stdin=subprocess.DEVNULL,
            stdout=stdout,
            stderr=subprocess.PIPE,
            start_new_session=True,
            preexec_fn=lambda: _arm_command_parent_death(
                parent_pid,
                signal_mask_before_install,
            ),
        )
        signal.pthread_sigmask(signal.SIG_SETMASK, signal_mask_before_install)

        def contain_adopted_descendants() -> bool:
            return capture_tool._terminate_adopted_descendants(
                0,
                time.monotonic_ns() + 10_000_000_000,
            )

        def terminate() -> tuple[bytes, bytes]:
            assert process is not None
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait(timeout=5)
            contain_adopted_descendants()
            terminated_stdout, terminated_stderr = process.communicate(timeout=5)
            return terminated_stdout or b"", terminated_stderr or b""

        try:
            captured_stdout, captured_stderr = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            captured_stdout, captured_stderr = terminate()
            raise CommandTimeout(
                command,
                captured_stdout,
                captured_stderr,
            )
        except BaseException:
            terminate()
            raise
        if contain_adopted_descendants():
            raise RunError(f"command left a surviving descendant: {command[0]}")
        return subprocess.CompletedProcess(
            list(command),
            process.returncode,
            captured_stdout,
            captured_stderr,
        )
    finally:
        active_error = sys.exception()
        try:
            signal.pthread_sigmask(
                signal.SIG_BLOCK,
                termination_signals,
            )
        except BaseException as error:
            cleanup_errors.append(error)
        if process is not None and process.poll() is None:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            try:
                process.wait(timeout=5)
            except BaseException as error:
                cleanup_errors.append(error)
        if descendant_authority:
            descendants_contained = False
            try:
                capture_tool._terminate_adopted_descendants(
                    0,
                    time.monotonic_ns() + 10_000_000_000,
                )
                remaining = capture_tool._direct_child_pids()
                if remaining:
                    raise RunError(
                        f"matrix command cleanup left adopted descendants: {remaining[:8]}"
                    )
                descendants_contained = True
            except BaseException as error:
                cleanup_errors.append(error)
            if descendants_contained:
                try:
                    if capture_tool._child_subreaper_enabled() != subreaper_was_enabled:
                        capture_tool._set_child_subreaper(subreaper_was_enabled)
                except BaseException as error:
                    cleanup_errors.append(error)
        for signal_number, previous_handler in previous_signal_handlers.items():
            try:
                signal.signal(signal_number, previous_handler)
            except BaseException as error:
                cleanup_errors.append(error)
        if signal_mask_before_install is not None:
            try:
                signal.pthread_sigmask(signal.SIG_SETMASK, signal_mask_before_install)
            except BaseException as error:
                cleanup_errors.append(error)
        if cleanup_errors:
            detail = "matrix command descendant cleanup failed: " + "; ".join(
                str(error) for error in cleanup_errors
            )
            if active_error is not None:
                active_error.add_note(detail)
            else:
                raise RunError(detail) from cleanup_errors[0]


def _checked(
    command: Sequence[str],
    *,
    cwd: pathlib.Path,
    timeout: int,
) -> bytes:
    completed = _run(command, cwd=cwd, timeout=timeout)
    if completed.returncode != 0:
        detail = completed.stderr.decode("utf-8", errors="replace")[-4096:]
        raise RunError(f"command failed ({completed.returncode}): {' '.join(command)}\n{detail}")
    if completed.stderr:
        detail = completed.stderr.decode("utf-8", errors="replace")[-4096:]
        raise RunError(f"successful evidence command wrote stderr: {' '.join(command)}\n{detail}")
    return completed.stdout


def _fsync_directory(path: pathlib.Path) -> None:
    descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


@contextlib.contextmanager
def _exclusive_run_lock(root: pathlib.Path) -> Any:
    lock_path = root / ".phase4-run.lock"
    descriptor = os.open(
        lock_path,
        os.O_RDWR | os.O_CREAT | getattr(os, "O_CLOEXEC", 0) | getattr(os, "O_NOFOLLOW", 0),
        0o600,
    )
    try:
        status = os.fstat(descriptor)
        if not stat.S_ISREG(status.st_mode) or status.st_nlink != 1:
            raise RunError(f"evidence-root lock is not a unique regular file: {lock_path}")
        try:
            fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise RunError(
                f"another Phase 4 matrix runner holds the evidence-root lock: {lock_path}"
            ) from error
        yield
    finally:
        os.close(descriptor)


def _install_new(path: pathlib.Path, encoded: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.parent / f".{path.name}.tmp-{os.getpid()}-{secrets.token_hex(8)}"
    descriptor: int | None = None
    installed = False
    identity: tuple[int, int] | None = None
    try:
        descriptor = os.open(
            temporary,
            os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_CLOEXEC", 0),
            0o600,
        )
        offset = 0
        while offset < len(encoded):
            written = os.write(descriptor, encoded[offset:])
            if written <= 0:
                raise RunError("artifact write made no progress")
            offset += written
        os.fsync(descriptor)
        os.close(descriptor)
        descriptor = None
        status = temporary.stat(follow_symlinks=False)
        identity = (status.st_dev, status.st_ino)
        os.link(temporary, path)
        installed = True
        _fsync_directory(path.parent)
        temporary.unlink()
        _fsync_directory(path.parent)
    except FileExistsError as error:
        raise RunError(f"artifact output already exists: {path}") from error
    except OSError as error:
        if installed and identity is not None:
            try:
                status = path.stat(follow_symlinks=False)
                if (status.st_dev, status.st_ino) == identity:
                    path.unlink()
                    _fsync_directory(path.parent)
            except OSError:
                pass
        raise RunError(f"cannot install artifact {path}: {error}") from error
    finally:
        if descriptor is not None:
            os.close(descriptor)
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass


def _prepare_evidence_root(root: pathlib.Path, expected_commit: str) -> None:
    marker = root / ".phase4-root.json"
    if marker.exists() or marker.is_symlink():
        try:
            matrix_aggregator.validate_root_marker(root, expected_commit)
        except matrix_aggregator.MatrixError as error:
            raise RunError(f"evidence-root marker is invalid: {error}") from error
    else:
        try:
            entries = list(root.iterdir())
        except OSError as error:
            raise RunError(f"cannot inspect evidence root before acquisition: {error}") from error
        if entries:
            raise RunError("nonempty evidence root lacks its commit-bound ownership marker")
        value = matrix_aggregator._root_marker(expected_commit)
        _install_new(marker, (_canonical(value) + "\n").encode("utf-8"))
    _verify_partial_inventory(root)


def _verify_partial_inventory(root: pathlib.Path) -> None:
    allowed_files = {
        ".phase4-root.json",
        ".phase4-run.lock",
        "matrix/decision-publication.json",
        "special/fixed-query.json",
        "special/stress-work-bound-probe.json",
        "special/stress.json",
    }
    allowed_directories = {"cells", "logs", "matrix", "special"}
    for case_id, pool, role, evidence in _matrix_rows():
        if evidence not in {"raw_success", "same_run_raw_success"}:
            continue
        cell = pathlib.PurePosixPath("cells", f"{case_id:04d}", f"k{pool}")
        allowed_directories.add(cell.parent.as_posix())
        allowed_directories.add(cell.as_posix())
        for name in (
            "raw.json",
            "per-net-report.json",
            "operational-capture.json",
            "operational-publication.json",
        ):
            allowed_files.add((cell / name).as_posix())
        if evidence == "same_run_raw_success":
            allowed_files.add((cell / "same-run.json").as_posix())
        if role == "exact":
            allowed_files.add((cell / "exact-snapshot.json").as_posix())
            allowed_files.add((cell / "exact-oracle.json").as_posix())
    try:
        top_level = list(root.iterdir())
    except OSError as error:
        raise RunError(f"cannot inspect partial evidence inventory: {error}") from error
    for child in top_level:
        relative = child.relative_to(root).as_posix()
        if relative == "logs":
            if child.is_symlink() or not stat.S_ISDIR(child.lstat().st_mode):
                raise RunError("partial evidence logs path is not a regular directory")
            continue
        if relative in allowed_directories:
            if child.is_symlink() or not stat.S_ISDIR(child.lstat().st_mode):
                raise RunError(f"partial evidence directory is aliased: {relative}")
            continue
        if relative in allowed_files:
            if child.is_symlink() or not stat.S_ISREG(child.lstat().st_mode):
                raise RunError(f"partial evidence file is aliased: {relative}")
            continue
        raise RunError(f"partial evidence root contains an unexpected path: {relative}")
    for top in ("cells", "matrix", "special"):
        start = root / top
        if not start.exists():
            continue
        for directory, names, files in os.walk(start, followlinks=False):
            directory_path = pathlib.Path(directory)
            relative_directory = directory_path.relative_to(root).as_posix()
            if relative_directory not in allowed_directories:
                raise RunError(
                    f"partial evidence root contains an unexpected directory: {relative_directory}"
                )
            for name in names:
                child = directory_path / name
                relative = child.relative_to(root).as_posix()
                if (
                    relative not in allowed_directories
                    or child.is_symlink()
                    or not stat.S_ISDIR(child.lstat().st_mode)
                ):
                    raise RunError(
                        f"partial evidence root contains an unexpected directory: {relative}"
                    )
            for name in files:
                child = directory_path / name
                relative = child.relative_to(root).as_posix()
                if (
                    relative not in allowed_files
                    or child.is_symlink()
                    or not stat.S_ISREG(child.lstat().st_mode)
                ):
                    raise RunError(f"partial evidence root contains an unexpected file: {relative}")


def _write_command_output(
    command: Sequence[str],
    destination: pathlib.Path,
    *,
    repo: pathlib.Path,
    timeout: int,
    allow_stderr: bool = False,
) -> None:
    if destination.exists():
        return
    completed = _run(command, cwd=repo, timeout=timeout)
    if completed.returncode != 0:
        detail = completed.stderr.decode("utf-8", errors="replace")[-4096:]
        raise RunError(
            f"artifact producer failed ({completed.returncode}): {' '.join(command)}\n{detail}"
        )
    if completed.stderr and not allow_stderr:
        detail = completed.stderr.decode("utf-8", errors="replace")[-4096:]
        raise RunError(f"successful artifact producer wrote stderr: {' '.join(command)}\n{detail}")
    if not completed.stdout:
        raise RunError(f"artifact producer wrote empty stdout: {command[0]}")
    _install_new(destination, completed.stdout)


def _write_failure_log(
    root: pathlib.Path,
    *,
    label: str,
    command: Sequence[str],
    returncode: int,
    stdout: bytes,
    stderr: bytes,
) -> None:
    logs = root / "logs"
    logs.mkdir(parents=True, exist_ok=True)
    payload = {
        "schema_version": 1,
        "label": label,
        "returncode": returncode,
        "command": list(command),
        "stdout_utf8": stdout.decode("utf-8", errors="replace"),
        "stderr_utf8": stderr.decode("utf-8", errors="replace"),
    }
    destination = logs / f"failure-{time.time_ns()}-{label}.json"
    _install_new(destination, (_canonical(payload) + "\n").encode("utf-8"))


def _write_raw_v2_pair(
    command: Sequence[str],
    raw_path: pathlib.Path,
    sidecar_path: pathlib.Path,
    *,
    root: pathlib.Path,
    repo: pathlib.Path,
    expected_commit: str,
    timeout: int,
) -> None:
    if raw_path.exists() or sidecar_path.exists():
        if raw_path.is_file() and sidecar_path.is_file():
            return
        raise RunError("resumed Raw-v2 cell has only one half of its same-execution pair")
    raw_path.parent.mkdir(parents=True, exist_ok=True)
    temporary_sidecar = raw_path.parent / (
        f".same-run.runner-{os.getpid()}-{secrets.token_hex(8)}.json"
    )
    actual_command = [
        *command,
        f"--same_run_telemetry_output={temporary_sidecar}",
    ]
    try:
        completed = _run(actual_command, cwd=repo, timeout=timeout)
    except CommandTimeout as error:
        _write_failure_log(
            root,
            label=f"raw-v2-timeout-{raw_path.parent.parent.name}-{raw_path.parent.name}",
            command=actual_command,
            returncode=124,
            stdout=error.stdout,
            stderr=error.stderr,
        )
        try:
            temporary_sidecar.unlink()
        except FileNotFoundError:
            pass
        raise RunError("Raw-v2 acquisition timed out; attempt retained under logs/") from error
    if completed.returncode != 0:
        _write_failure_log(
            root,
            label=f"raw-v2-{raw_path.parent.parent.name}-{raw_path.parent.name}",
            command=actual_command,
            returncode=completed.returncode,
            stdout=completed.stdout,
            stderr=completed.stderr,
        )
        try:
            temporary_sidecar.unlink()
        except FileNotFoundError:
            pass
        raise RunError("Raw-v2 acquisition failed; complete attempt retained under logs/")
    if completed.stderr or not completed.stdout or not temporary_sidecar.is_file():
        _write_failure_log(
            root,
            label=f"raw-v2-shape-{raw_path.parent.parent.name}-{raw_path.parent.name}",
            command=actual_command,
            returncode=completed.returncode,
            stdout=completed.stdout,
            stderr=completed.stderr,
        )
        try:
            temporary_sidecar.unlink()
        except FileNotFoundError:
            pass
        raise RunError(
            "Raw-v2 acquisition did not produce a quiet complete pair; attempt retained under logs/"
        )
    try:
        raw, _ = matrix_aggregator._parse_authority_bytes(
            completed.stdout,
            pathlib.PurePosixPath("generated-raw.json"),
            "raw",
        )
        sidecar, _ = matrix_aggregator._read_authority(
            raw_path.parent,
            pathlib.PurePosixPath(temporary_sidecar.name),
            "same_run",
        )
        same_run.validate_join(raw, sidecar, expected_commit=expected_commit)
    except (matrix_aggregator.MatrixError, raw_validator.EvidenceError) as error:
        _write_failure_log(
            root,
            label=f"raw-v2-invalid-{raw_path.parent.parent.name}-{raw_path.parent.name}",
            command=actual_command,
            returncode=completed.returncode,
            stdout=completed.stdout,
            stderr=str(error).encode("utf-8"),
        )
        temporary_sidecar.unlink(missing_ok=True)
        raise RunError(
            "Raw-v2 acquisition produced an invalid pair; attempt retained under logs/"
        ) from error
    sidecar_bytes = (_canonical(sidecar) + "\n").encode("utf-8")
    temporary_sidecar.unlink()
    _install_new(raw_path, completed.stdout)
    try:
        _install_new(sidecar_path, sidecar_bytes)
    except BaseException:
        raw_path.unlink()
        _fsync_directory(raw_path.parent)
        raise


def _write_raw_v1(
    command: Sequence[str],
    raw_path: pathlib.Path,
    *,
    root: pathlib.Path,
    repo: pathlib.Path,
    expected_commit: str,
    timeout: int,
) -> None:
    if raw_path.exists():
        return
    try:
        completed = _run(command, cwd=repo, timeout=timeout)
    except CommandTimeout as error:
        _write_failure_log(
            root,
            label=f"raw-v1-timeout-{raw_path.parent.parent.name}-{raw_path.parent.name}",
            command=command,
            returncode=124,
            stdout=error.stdout,
            stderr=error.stderr,
        )
        raise RunError("Raw-v1 acquisition timed out; attempt retained under logs/") from error
    if completed.returncode != 0:
        _write_failure_log(
            root,
            label=f"raw-v1-{raw_path.parent.parent.name}-{raw_path.parent.name}",
            command=command,
            returncode=completed.returncode,
            stdout=completed.stdout,
            stderr=completed.stderr,
        )
        raise RunError("Raw-v1 acquisition failed; complete attempt retained under logs/")
    if completed.stderr or not completed.stdout:
        _write_failure_log(
            root,
            label=f"raw-v1-shape-{raw_path.parent.parent.name}-{raw_path.parent.name}",
            command=command,
            returncode=completed.returncode,
            stdout=completed.stdout,
            stderr=completed.stderr,
        )
        raise RunError(
            "Raw-v1 acquisition did not produce quiet complete evidence; "
            "attempt retained under logs/"
        )
    try:
        raw, _ = matrix_aggregator._parse_authority_bytes(
            completed.stdout,
            pathlib.PurePosixPath("generated-raw.json"),
            "raw",
        )
        raw_validator.validate_document(raw, expected_commit=expected_commit)
    except (matrix_aggregator.MatrixError, raw_validator.EvidenceError) as error:
        _write_failure_log(
            root,
            label=f"raw-v1-invalid-{raw_path.parent.parent.name}-{raw_path.parent.name}",
            command=command,
            returncode=completed.returncode,
            stdout=completed.stdout,
            stderr=str(error).encode("utf-8"),
        )
        raise RunError(
            "Raw-v1 acquisition produced invalid evidence; attempt retained under logs/"
        ) from error
    _install_new(raw_path, completed.stdout)


def _read_acquired_authority(
    root: pathlib.Path,
    path: pathlib.Path,
    kind: str,
) -> Mapping[str, Any]:
    try:
        relative = pathlib.PurePosixPath(path.relative_to(root).as_posix())
        document, _ = matrix_aggregator._read_authority(root, relative, kind)
    except (ValueError, matrix_aggregator.MatrixError) as error:
        raise RunError(f"acquired {kind} authority is invalid: {path}: {error}") from error
    return document


def _common_cell_arguments(case_id: int, pool: int, commit: str) -> list[str]:
    return [
        f"--apgar_commit={commit}",
        f"--case_id={case_id}",
        f"--pool_size={pool}",
        f"--workers={_WORKERS}",
        f"--repetitions={_REPETITIONS}",
        f"--setup_ns={_SETUP_NS}",
        f"--prepared_ns={_PREPARED_NS}",
        f"--cold_ns={_COLD_NS}",
        f"--address_space_bytes={_ADDRESS_SPACE_BYTES}",
        f"--peak_host_bytes={_PEAK_HOST_BYTES}",
        f"--maximum_nets={_MAXIMUM_NETS}",
        f"--maximum_compiled_nodes={_MAXIMUM_COMPILED_NODES}",
        f"--maximum_compiled_host_bytes={_MAXIMUM_COMPILED_HOST_BYTES}",
        f"--maximum_active_regions={_MAXIMUM_ACTIVE_REGIONS}",
        f"--maximum_board_entities={_MAXIMUM_BOARD_ENTITIES}",
    ]


def _raw_join_arguments(raw: Mapping[str, Any]) -> list[str]:
    config = raw["config"]
    attempt = raw["attempts"][0]
    paired = attempt["result"]
    if paired is None:
        raise RunError("Raw repetition zero is incomplete")
    baseline = attempt["baseline"]["record"]
    candidate = attempt["candidate"]["record"]
    return [
        *_common_cell_arguments(
            config["case_id"],
            config["requested_pool_size"],
            raw["source_commit"],
        ),
        f"--raw_cell_plan_checksum={raw['cell_plan_checksum']}",
        f"--raw_cell_artifact_checksum={raw['artifact_checksum']}",
        f"--raw_source_envelope_checksum={raw['source_envelope_checksum']}",
        f"--pair_attempt_checksum={attempt['attempt_checksum']}",
        f"--paired_semantic_checksum={paired['semantic_checksum']}",
        f"--paired_artifact_checksum={paired['artifact_checksum']}",
        f"--baseline_semantic_checksum={baseline['semantics']['semantic_checksum']}",
        f"--baseline_arm_artifact_checksum={baseline['artifact_checksum']}",
        f"--candidate_semantic_checksum={candidate['semantics']['semantic_checksum']}",
        f"--candidate_arm_artifact_checksum={candidate['artifact_checksum']}",
    ]


def _cell_directory(root: pathlib.Path, case_id: int, pool: int) -> pathlib.Path:
    return root / "cells" / f"{case_id:04d}" / f"k{pool}"


def _build_tools(repo: pathlib.Path, timeout: int) -> dict[str, pathlib.Path]:
    targets = [f"//:{target}" for target in _TARGETS]
    completed = _run(
        ["bazel", "--batch", "build", "--config=benchmark", *targets],
        cwd=repo,
        timeout=timeout,
    )
    if completed.returncode != 0:
        detail = completed.stderr.decode("utf-8", errors="replace")[-8192:]
        raise RunError(f"benchmark tool build failed:\n{detail}")
    tools: dict[str, pathlib.Path] = {}
    for target in _TARGETS:
        path = repo / "bazel-bin" / target
        if not path.is_file() or not os.access(path, os.X_OK):
            raise RunError(f"built benchmark tool is unavailable: {target}")
        tools[target] = path.resolve()
    return tools


def _verify_clean_source(repo: pathlib.Path, expected_commit: str) -> None:
    head = (
        _checked(
            ["git", "rev-parse", "HEAD"],
            cwd=repo,
            timeout=30,
        )
        .decode("ascii")
        .strip()
    )
    if head != expected_commit:
        raise RunError(f"expected commit {expected_commit} differs from HEAD {head}")
    status = _checked(
        ["git", "status", "--porcelain=v1", "--untracked-files=normal"],
        cwd=repo,
        timeout=30,
    )
    if status:
        raise RunError("Phase 4 measurement requires a completely clean source worktree")


def _validate_cell(
    *,
    tools: Mapping[str, pathlib.Path],
    repo: pathlib.Path,
    commit: str,
    raw_path: pathlib.Path,
    sidecar_path: pathlib.Path | None,
    report_path: pathlib.Path,
    capture_path: pathlib.Path,
    publication_path: pathlib.Path,
    timeout: int,
) -> None:
    if sidecar_path is None:
        _checked(
            [
                str(tools["phase4_raw_evidence_validator"]),
                f"--expected-commit={commit}",
                str(raw_path),
            ],
            cwd=repo,
            timeout=timeout,
        )
        _checked(
            [
                str(tools["phase4_per_net_report_validator"]),
                f"--expected-commit={commit}",
                f"--raw={raw_path}",
                f"--report={report_path}",
            ],
            cwd=repo,
            timeout=timeout,
        )
    else:
        _checked(
            [
                str(tools["phase4_same_run_decision_telemetry_validator"]),
                f"--expected-commit={commit}",
                str(raw_path),
                str(sidecar_path),
            ],
            cwd=repo,
            timeout=timeout,
        )
        _checked(
            [
                str(tools["phase4_per_net_report_v2_validator"]),
                f"--expected-commit={commit}",
                f"--raw={raw_path}",
                f"--same-run-telemetry={sidecar_path}",
                f"--report={report_path}",
            ],
            cwd=repo,
            timeout=timeout,
        )
    operational_command = [
        str(tools["phase4_operational_measurement_validator"]),
        f"--raw={raw_path}",
        f"--capture={capture_path}",
        f"--expected-commit={commit}",
        f"--validate={publication_path}",
    ]
    if sidecar_path is not None:
        operational_command.append(f"--same-run-telemetry={sidecar_path}")
    _checked(operational_command, cwd=repo, timeout=timeout)


def _acquire_success_cell(
    *,
    tools: Mapping[str, pathlib.Path],
    repo: pathlib.Path,
    root: pathlib.Path,
    fixture: pathlib.Path,
    commit: str,
    case_id: int,
    pool: int,
    evidence: str,
    role: str,
    timeout: int,
) -> None:
    cell = _cell_directory(root, case_id, pool)
    cell.mkdir(parents=True, exist_ok=True)
    raw_path = cell / "raw.json"
    sidecar_path = cell / "same-run.json" if evidence == "same_run_raw_success" else None
    raw_command = [
        str(tools["phase4_evidence_runner"]),
        *_common_cell_arguments(case_id, pool, commit),
    ]
    if sidecar_path is None:
        _write_raw_v1(
            raw_command,
            raw_path,
            root=root,
            repo=repo,
            expected_commit=commit,
            timeout=timeout,
        )
    else:
        _write_raw_v2_pair(
            raw_command,
            raw_path,
            sidecar_path,
            root=root,
            repo=repo,
            expected_commit=commit,
            timeout=timeout,
        )
    raw = _read_acquired_authority(root, raw_path, "raw")
    sidecar: Mapping[str, Any] | None = None
    if sidecar_path is None:
        raw_validator.validate_document(raw, expected_commit=commit)
    else:
        sidecar = _read_acquired_authority(root, sidecar_path, "same_run")
        same_run.validate_join(raw, sidecar, expected_commit=commit)
    if (
        raw["config"]["case_id"],
        raw["config"]["requested_pool_size"],
    ) != (case_id, pool):
        raise RunError("Raw authority path differs from its frozen cell identity")
    join_arguments = _raw_join_arguments(raw)

    report_path = cell / "per-net-report.json"
    report_command = [str(tools["phase4_per_net_report_runner"])]
    if sidecar_path is not None:
        report_command.append("--raw_wire_schema_version=2")
    report_command.extend(join_arguments)
    _write_command_output(
        report_command,
        report_path,
        repo=repo,
        timeout=timeout,
    )
    report = _read_acquired_authority(root, report_path, "report")
    if sidecar is None:
        report_v1.validate_join(raw, report, expected_commit=commit)
    else:
        report_v2.validate_join(raw, sidecar, report, expected_commit=commit)

    if role == "exact":
        snapshot_path = cell / "exact-snapshot.json"
        _write_command_output(
            [
                str(tools["phase4_exact_small_snapshot_runner"]),
                "--raw_evidence_schema_version=2",
                *join_arguments,
                f"--per_net_report_artifact_checksum={report['artifact_checksum']}",
                (f"--per_net_report_source_envelope_checksum={report['source_envelope_checksum']}"),
            ],
            snapshot_path,
            repo=repo,
            timeout=timeout,
        )
        oracle_path = cell / "exact-oracle.json"
        _write_command_output(
            [
                str(tools["phase4_exact_small_oracle_v2_validator"]),
                f"--expected-commit={commit}",
                f"--raw={raw_path}",
                f"--same-run-telemetry={sidecar_path}",
                f"--report={report_path}",
                f"--snapshot={snapshot_path}",
            ],
            oracle_path,
            repo=repo,
            timeout=timeout,
        )

    capture_path = cell / "operational-capture.json"
    _write_command_output(
        [
            "bazel",
            "--batch",
            "run",
            "--config=benchmark",
            "//:phase4_operational_capture",
            "--",
            f"--worker={tools['phase4_operational_replay_worker']}",
            f"--fixture={fixture}",
            f"--apgar-commit={commit}",
            f"--case-id={case_id}",
            f"--pool-size={pool}",
            f"--workers={_WORKERS}",
            f"--setup-ns={_SETUP_NS}",
            f"--prepared-ns={_PREPARED_NS}",
            f"--cold-ns={_COLD_NS}",
            f"--address-space-bytes={_ADDRESS_SPACE_BYTES}",
            f"--peak-host-bytes={_PEAK_HOST_BYTES}",
            f"--maximum-nets={_MAXIMUM_NETS}",
            f"--maximum-compiled-nodes={_MAXIMUM_COMPILED_NODES}",
            f"--maximum-compiled-host-bytes={_MAXIMUM_COMPILED_HOST_BYTES}",
            f"--maximum-active-regions={_MAXIMUM_ACTIVE_REGIONS}",
            f"--maximum-board-entities={_MAXIMUM_BOARD_ENTITIES}",
        ],
        capture_path,
        repo=repo,
        timeout=timeout,
        allow_stderr=True,
    )
    publication_path = cell / "operational-publication.json"
    if not publication_path.exists():
        publication_command = [
            str(tools["phase4_operational_measurement_validator"]),
            f"--raw={raw_path}",
            f"--capture={capture_path}",
            f"--expected-commit={commit}",
            f"--output={publication_path}",
        ]
        if sidecar_path is not None:
            publication_command.append(f"--same-run-telemetry={sidecar_path}")
        _checked(publication_command, cwd=repo, timeout=timeout)
    _validate_cell(
        tools=tools,
        repo=repo,
        commit=commit,
        raw_path=raw_path,
        sidecar_path=sidecar_path,
        report_path=report_path,
        capture_path=capture_path,
        publication_path=publication_path,
        timeout=timeout,
    )


def _legacy_projection(
    *,
    tools: Mapping[str, pathlib.Path],
    repo: pathlib.Path,
    commit: str,
    raw_path: pathlib.Path,
    output: pathlib.Path,
    timeout: int,
) -> None:
    _write_command_output(
        [
            str(tools["phase4_operational_projection"]),
            f"--raw={raw_path}",
            f"--expected-commit={commit}",
        ],
        output,
        repo=repo,
        timeout=timeout,
    )


def _acquire_special_authorities(
    *,
    tools: Mapping[str, pathlib.Path],
    repo: pathlib.Path,
    root: pathlib.Path,
    commit: str,
    timeout: int,
) -> None:
    special = root / "special"
    special.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="apgar-phase4-legacy-") as temporary:
        scratch = pathlib.Path(temporary)
        fixed_command = [
            str(tools["phase4_fixed_query_controls"]),
            f"--expected-commit={commit}",
        ]
        for case_id, pool in ((2002, 4), (2003, 8), (2004, 16)):
            cell = _cell_directory(root, case_id, pool)
            projection = scratch / f"operational-{case_id}.json"
            _legacy_projection(
                tools=tools,
                repo=repo,
                commit=commit,
                raw_path=cell / "raw.json",
                output=projection,
                timeout=timeout,
            )
            fixed_command.extend(
                (
                    f"--raw-{case_id}={cell / 'raw.json'}",
                    f"--report-{case_id}={cell / 'per-net-report.json'}",
                    f"--operational-{case_id}={projection}",
                )
            )
        fixed_path = special / "fixed-query.json"
        _write_command_output(
            fixed_command,
            fixed_path,
            repo=repo,
            timeout=timeout,
        )
        _checked(
            [*fixed_command, f"--validate={fixed_path}"],
            cwd=repo,
            timeout=timeout,
        )

        probe_path = special / "stress-work-bound-probe.json"
        _write_command_output(
            [
                str(tools["phase4_stress_work_bound_probe"]),
                f"--runtime-commit={commit}",
            ],
            probe_path,
            repo=repo,
            timeout=timeout,
        )
        stress_cell = _cell_directory(root, 3000, 4)
        stress_projection = scratch / "operational-3000.json"
        _legacy_projection(
            tools=tools,
            repo=repo,
            commit=commit,
            raw_path=stress_cell / "raw.json",
            output=stress_projection,
            timeout=timeout,
        )
        stress_command = [
            str(tools["phase4_stress_evidence"]),
            f"--expected-commit={commit}",
            f"--raw-3000={stress_cell / 'raw.json'}",
            f"--report-3000={stress_cell / 'per-net-report.json'}",
            f"--operational-3000={stress_projection}",
            f"--work-bound-probe={probe_path}",
        ]
        stress_path = special / "stress.json"
        _write_command_output(
            stress_command,
            stress_path,
            repo=repo,
            timeout=timeout,
        )
        _checked(
            [*stress_command, f"--validate={stress_path}"],
            cwd=repo,
            timeout=timeout,
        )


def _matrix_rows() -> list[tuple[int, int, str, str]]:
    return sorted(protocol_v4.expanded_cells())


def _operator_repository() -> pathlib.Path:
    if os.environ.get("BUILD_WORKSPACE_DIRECTORY"):
        raise RunError(
            "matrix runner cannot execute under `bazel run`, which holds the Bazel "
            "output-base lock; build it and execute bazel-bin/phase4_matrix_runner directly"
        )
    return pathlib.Path.cwd().resolve()


def run_matrix(
    *,
    repo: pathlib.Path,
    root: pathlib.Path,
    expected_commit: str,
    timeout: int,
) -> pathlib.Path:
    if len(expected_commit) != _CANONICAL_COMMIT_LENGTH or any(
        character not in "0123456789abcdef" for character in expected_commit
    ):
        raise RunError("expected commit must be 40 lowercase hexadecimal characters")
    repo = repo.resolve(strict=True)
    root = root.resolve(strict=False)
    if root == repo or repo in root.parents or root in repo.parents:
        raise RunError("Phase 4 evidence root must be separate from the Git worktree")
    root.mkdir(parents=True, exist_ok=True)
    root = root.resolve(strict=True)
    if root == repo or repo in root.parents or root in repo.parents:
        raise RunError("Phase 4 evidence root resolves against the Git worktree")
    _prepare_evidence_root(root, expected_commit)
    with _exclusive_run_lock(root):
        return _run_matrix_locked(
            repo=repo,
            root=root,
            expected_commit=expected_commit,
            timeout=timeout,
        )


def _run_matrix_locked(
    *,
    repo: pathlib.Path,
    root: pathlib.Path,
    expected_commit: str,
    timeout: int,
) -> pathlib.Path:
    _verify_clean_source(repo, expected_commit)
    tools = _build_tools(repo, timeout)
    _verify_clean_source(repo, expected_commit)
    fixture = (repo / "tests/fixtures/phase4_supported_multinet_v1.kicad_pcb").resolve(strict=True)
    _checked(
        [str(tools["phase4_statistical_protocol_v4_validator"])],
        cwd=repo,
        timeout=timeout,
    )
    _checked(
        [str(tools["phase4_canonical_budget_roster"])],
        cwd=repo,
        timeout=timeout,
    )
    success_rows = [
        row for row in _matrix_rows() if row[3] in {"raw_success", "same_run_raw_success"}
    ]
    for ordinal, (case_id, pool, role, evidence) in enumerate(success_rows, start=1):
        _verify_clean_source(repo, expected_commit)
        print(
            f"[{ordinal:03d}/100] acquiring case {case_id} pool {pool} ({role}, {evidence})",
            flush=True,
        )
        _acquire_success_cell(
            tools=tools,
            repo=repo,
            root=root,
            fixture=fixture,
            commit=expected_commit,
            case_id=case_id,
            pool=pool,
            evidence=evidence,
            role=role,
            timeout=timeout,
        )
    _verify_clean_source(repo, expected_commit)
    _acquire_special_authorities(
        tools=tools,
        repo=repo,
        root=root,
        commit=expected_commit,
        timeout=timeout,
    )
    _verify_clean_source(repo, expected_commit)
    matrix = root / "matrix"
    matrix.mkdir(parents=True, exist_ok=True)
    decision = matrix / "decision-publication.json"
    if not decision.exists():
        _checked(
            [
                str(tools["phase4_matrix_aggregator"]),
                f"--evidence-root={root}",
                f"--expected-commit={expected_commit}",
                f"--output={decision}",
            ],
            cwd=repo,
            timeout=timeout,
        )
    _checked(
        [
            str(tools["phase4_matrix_aggregator"]),
            f"--evidence-root={root}",
            f"--expected-commit={expected_commit}",
            f"--validate={decision}",
        ],
        cwd=repo,
        timeout=timeout,
    )
    return decision


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence-root", required=True, type=pathlib.Path)
    parser.add_argument("--expected-commit", required=True)
    parser.add_argument(
        "--command-timeout-seconds",
        type=int,
        default=4 * 60 * 60,
    )
    options = parser.parse_args(argv)
    try:
        if options.command_timeout_seconds <= 0:
            raise RunError("command timeout must be positive")
        repo = _operator_repository()
        if not (repo / ".git").exists():
            raise RunError("matrix runner must be launched from the APGAR repository root")
        decision = run_matrix(
            repo=repo,
            root=options.evidence_root,
            expected_commit=options.expected_commit,
            timeout=options.command_timeout_seconds,
        )
        result = _read_acquired_authority(decision.parent, decision, "decision")
        print(
            f"Phase 4 matrix {result['phase4_exit_status']}: {decision}",
            flush=True,
        )
    except (OSError, RunError, raw_validator.EvidenceError) as error:
        print(f"Phase 4 matrix run failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
