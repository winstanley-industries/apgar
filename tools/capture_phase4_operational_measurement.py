"""Capture isolated Phase 4 operational measurements and replay authorities."""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import os
import pathlib
import platform
import resource
import selectors
import signal
import stat
import sys
import time
from collections.abc import Mapping, Sequence
from typing import Any

from tools import validate_phase4_raw_evidence as raw_validator

_MAXIMUM_WORKER_BYTES = 4 * 1024 * 1024
_MAXIMUM_STDERR_BYTES = 64 * 1024
_MAXIMUM_CAPTURE_BYTES = 32 * 1024 * 1024
_U64_MAX = (1 << 64) - 1
_LINUX_SYS_PIDFD_SEND_SIGNAL = 424
_LINUX_SYS_PIDFD_OPEN = 434
_PR_SET_PDEATHSIG = 1
_PR_SET_CHILD_SUBREAPER = 36
_PR_GET_CHILD_SUBREAPER = 37
_MAXIMUM_ADOPTED_DESCENDANTS = 4096
_WORKER_FIELDS = (
    "schema_version",
    "kind",
    "source_commit",
    "source_stamped",
    "source_tree_dirty",
    "compiler_identity",
    "payload",
    "artifact_checksum",
    "source_envelope_checksum",
)


class CaptureError(ValueError):
    """Stable operational-capture failure."""


def _reject_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise CaptureError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _reject_constant(value: str) -> None:
    raise CaptureError(f"non-finite JSON number: {value}")


def _canonical(value: Mapping[str, Any]) -> str:
    return json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":"))


def _resolve_runfile(relative: str) -> pathlib.Path:
    direct = pathlib.Path(relative)
    if direct.is_absolute() and direct.is_file():
        return direct
    roots: list[pathlib.Path] = []
    runfiles = os.environ.get("RUNFILES_DIR")
    if runfiles:
        root = pathlib.Path(runfiles)
        workspace = os.environ.get("TEST_WORKSPACE")
        if workspace:
            roots.append(root / workspace)
        roots.extend((root / "_main", root))
    roots.append(pathlib.Path(__file__).resolve().parent.parent)
    roots.append(pathlib.Path.cwd())
    for root in roots:
        candidate = root / relative
        if candidate.is_file():
            return candidate
    raise CaptureError(f"cannot resolve required runfile: {relative}")


def _sha256(path: pathlib.Path) -> str:
    hashed = hashlib.sha256()
    try:
        with path.open("rb") as source:
            while chunk := source.read(1024 * 1024):
                hashed.update(chunk)
    except OSError as error:
        raise CaptureError(f"cannot hash provenance file {path}: {error}") from error
    return hashed.hexdigest()


def _sha256_fd(descriptor: int) -> str:
    hashed = hashlib.sha256()
    offset = 0
    try:
        while chunk := os.pread(descriptor, 1024 * 1024, offset):
            hashed.update(chunk)
            offset += len(chunk)
    except OSError as error:
        raise CaptureError(f"cannot hash pinned operational worker: {error}") from error
    return hashed.hexdigest()


def _open_pinned_worker(path: pathlib.Path) -> tuple[int, dict[str, Any]]:
    try:
        path = path.resolve(strict=True)
    except OSError as error:
        raise CaptureError(f"cannot resolve operational worker: {error}") from error
    flags = os.O_RDONLY | getattr(os, "O_CLOEXEC", 0) | getattr(os, "O_NOFOLLOW", 0)
    try:
        descriptor = os.open(path, flags)
    except OSError as error:
        raise CaptureError(f"cannot open operational worker: {error}") from error
    try:
        status = os.fstat(descriptor)
        mode = stat.S_IMODE(status.st_mode)
        if not stat.S_ISREG(status.st_mode) or status.st_size <= 0 or mode & 0o111 == 0:
            raise CaptureError("operational worker descriptor is not a nonempty executable file")
        identity = {
            "device": status.st_dev,
            "inode": status.st_ino,
            "size_bytes": status.st_size,
            "mode": mode,
            "mtime_nanoseconds": status.st_mtime_ns,
            "sha256": _sha256_fd(descriptor),
        }
        return descriptor, identity
    except BaseException:
        os.close(descriptor)
        raise


def _read_text(path: pathlib.Path, maximum: int) -> str:
    try:
        data = path.read_bytes()
    except OSError as error:
        raise CaptureError(f"cannot read provenance file {path}: {error}") from error
    if len(data) > maximum:
        raise CaptureError(f"provenance file exceeds {maximum} bytes: {path}")
    try:
        return data.decode("utf-8")
    except UnicodeError as error:
        raise CaptureError(f"provenance file is not UTF-8: {path}") from error


def _first_cpu_value(label: str) -> str:
    try:
        for line in pathlib.Path("/proc/cpuinfo").read_text(encoding="utf-8").splitlines():
            key, separator, value = line.partition(":")
            if separator and key.strip() == label:
                return value.strip()
    except (OSError, UnicodeError):
        pass
    return ""


def _memory_total_bytes() -> int:
    try:
        for line in pathlib.Path("/proc/meminfo").read_text(encoding="utf-8").splitlines():
            if line.startswith("MemTotal:"):
                parts = line.split()
                if len(parts) == 3 and parts[2] == "kB":
                    return int(parts[1]) * 1024
    except (OSError, UnicodeError, ValueError):
        pass
    raise CaptureError("cannot derive total host memory from /proc/meminfo")


def _optional_cgroup(source: pathlib.Path) -> dict[str, Any]:
    try:
        value = source.read_text(encoding="utf-8").strip()
    except (OSError, UnicodeError):
        return {"status": "unavailable", "reason": "cgroup_file_unavailable"}
    return {"status": "measured", "value": value}


def _unified_cgroup_path(record: str) -> str:
    matches: list[str] = []
    for line in record.splitlines():
        hierarchy, separator, remainder = line.partition(":")
        controllers, second_separator, path = remainder.partition(":")
        if separator and second_separator and hierarchy == "0" and controllers == "":
            matches.append(path)
    if len(matches) != 1:
        raise CaptureError("cannot identify one unified controller cgroup path")
    path = matches[0]
    canonical = pathlib.PurePosixPath(path)
    if (
        not path.startswith("/")
        or str(canonical) != path
        or any(part in {"", ".", ".."} for part in canonical.parts[1:])
    ):
        raise CaptureError("unified controller cgroup path is not canonical absolute")
    return path


def _cgroup_control_ancestry(
    root: pathlib.Path, unified_path: str, control_name: str
) -> list[dict[str, Any]]:
    logical = pathlib.PurePosixPath(unified_path)
    current = root.joinpath(*logical.parts[1:])
    result: list[dict[str, Any]] = []
    while True:
        relative = current.relative_to(root)
        path = "/" if not relative.parts else "/" + "/".join(relative.parts)
        result.append({"path": path, "control": _optional_cgroup(current / control_name)})
        if current == root:
            break
        current = current.parent
    return result


def _cgroup_context(
    cgroup_record_path: pathlib.Path = pathlib.Path("/proc/self/cgroup"),
    cgroup_root: pathlib.Path = pathlib.Path("/sys/fs/cgroup"),
) -> dict[str, Any]:
    unified_path = _unified_cgroup_path(_read_text(cgroup_record_path, 64 * 1024))
    leaf = cgroup_root.joinpath(*pathlib.PurePosixPath(unified_path).parts[1:])
    return {
        "cgroup_unified_path": unified_path,
        "cgroup_cpu_max_ancestry": _cgroup_control_ancestry(cgroup_root, unified_path, "cpu.max"),
        "cgroup_cpuset_effective": _optional_cgroup(leaf / "cpuset.cpus.effective"),
        "cgroup_memory_max_ancestry": _cgroup_control_ancestry(
            cgroup_root, unified_path, "memory.max"
        ),
    }


def _filesystem_identity(path: pathlib.Path) -> dict[str, Any]:
    try:
        status = path.stat()
        link = os.readlink(path) if path.is_symlink() else ""
    except OSError as error:
        raise CaptureError(f"cannot authenticate execution namespace path {path}") from error
    return {
        "device": status.st_dev,
        "inode": status.st_ino,
        "symlink_target": link,
    }


def _execution_environment() -> dict[str, Any]:
    affinity = sorted(os.sched_getaffinity(0))
    if not affinity:
        raise CaptureError("controller has an empty CPU affinity")
    return {
        "affinity_cpu_ids": affinity,
        "cgroup_namespace_identity": _filesystem_identity(pathlib.Path("/proc/self/ns/cgroup")),
        "cgroup_mount_identity": _filesystem_identity(pathlib.Path("/sys/fs/cgroup")),
        "cgroup_ancestry_scope": "namespace_visible_unified_v2_leaf_to_root",
        **_cgroup_context(),
    }


def _process_execution_environment(pid: int) -> dict[str, Any]:
    try:
        affinity = sorted(os.sched_getaffinity(pid))
        cgroup_record = _read_text(pathlib.Path(f"/proc/{pid}/cgroup"), 64 * 1024)
    except OSError as error:
        raise CaptureError("cannot sample operational child execution environment") from error
    return {
        "affinity_cpu_ids": affinity,
        "cgroup_unified_path": _unified_cgroup_path(cgroup_record),
    }


def _require_execution_environment(expected: Mapping[str, Any]) -> None:
    if _execution_environment() != expected:
        raise CaptureError("operational controller execution environment drifted")


def _provenance(
    worker_identity: Mapping[str, Any],
    compiler_identity: str,
    execution_environment: Mapping[str, Any],
    *,
    publication_invocation: str = (
        "bazel --batch run --config=benchmark //:phase4_operational_capture"
    ),
    worker_target: str = "//:phase4_operational_replay_worker",
) -> dict[str, Any]:
    files = {
        "bazel_version_file": _resolve_runfile(".bazelversion"),
        "bazel_configuration": _resolve_runfile(".bazelrc"),
        "module_definition": _resolve_runfile("MODULE.bazel"),
        "module_lock": _resolve_runfile("MODULE.bazel.lock"),
    }
    bazel_release = _read_text(files["bazel_version_file"], 128).strip()
    if not bazel_release:
        raise CaptureError(".bazelversion is empty")
    os_release_path = pathlib.Path("/etc/os-release")
    os_release = _read_text(os_release_path, 64 * 1024)
    os_values: dict[str, str] = {}
    for line in os_release.splitlines():
        key, separator, value = line.partition("=")
        if separator:
            os_values[key] = value.strip().strip('"')
    affinity = execution_environment["affinity_cpu_ids"]
    if not affinity:
        raise CaptureError("controller has an empty CPU affinity")
    online_cpu_count = os.cpu_count()
    if online_cpu_count is None or online_cpu_count <= 0:
        raise CaptureError("controller cannot derive the online CPU count")
    page_size = os.sysconf("SC_PAGE_SIZE")
    if page_size <= 0:
        raise CaptureError("controller cannot derive the host page size")
    uname = platform.uname()
    value: dict[str, Any] = {
        "schema_version": 1,
        "publication_invocation": publication_invocation,
        "bazel_release": bazel_release,
        "worker_target": worker_target,
        "worker_sha256": worker_identity["sha256"],
        "worker_file_identity": dict(worker_identity),
        "compiler_identity": compiler_identity,
        "cplusplus_standard": "c++20",
        "python_implementation": platform.python_implementation(),
        "python_version": platform.python_version(),
        "toolchain_files": [
            {"name": name, "sha256": _sha256(path), "size_bytes": path.stat().st_size}
            for name, path in files.items()
        ],
        "host": {
            "os": uname.system,
            "kernel": uname.release,
            "architecture": uname.machine,
            "os_release_id": os_values.get("ID", ""),
            "os_release_version": os_values.get("VERSION_ID", ""),
            "os_release_sha256": hashlib.sha256(os_release.encode("utf-8")).hexdigest(),
            "libc": list(platform.libc_ver()),
            "cpu_vendor": _first_cpu_value("vendor_id"),
            "cpu_model_name": _first_cpu_value("model name"),
            "cpu_family": _first_cpu_value("cpu family"),
            "cpu_model": _first_cpu_value("model"),
            "cpu_stepping": _first_cpu_value("stepping"),
            "cpu_microcode": _first_cpu_value("microcode"),
            "online_cpu_count": online_cpu_count,
            "affinity_cpu_ids": affinity,
            "affinity_cpu_count": len(affinity),
            "page_size_bytes": page_size,
            "total_host_memory_bytes": _memory_total_bytes(),
            "cgroup_namespace_identity": execution_environment["cgroup_namespace_identity"],
            "cgroup_mount_identity": execution_environment["cgroup_mount_identity"],
            "cgroup_ancestry_scope": execution_environment["cgroup_ancestry_scope"],
            "cgroup_unified_path": execution_environment["cgroup_unified_path"],
            "cgroup_cpu_max_ancestry": execution_environment["cgroup_cpu_max_ancestry"],
            "cgroup_cpuset_effective": execution_environment["cgroup_cpuset_effective"],
            "cgroup_memory_max_ancestry": execution_environment["cgroup_memory_max_ancestry"],
            "monotonic_clock": "CLOCK_MONOTONIC",
        },
        "backend": "cpu_only",
        "gpu": {
            "status": "not_applicable",
            "reason": "cpu_execution_has_no_gpu_dispatch",
        },
        "device_memory": {
            "status": "not_applicable",
            "reason": "cpu_execution_has_no_device_memory",
        },
        "initial_device_upload": {
            "status": "not_applicable",
            "reason": "cpu_execution_has_no_device_upload",
        },
        "compact_readback": {
            "status": "not_applicable",
            "reason": "cpu_execution_has_no_compact_device_readback",
        },
        "compatible_batch_fill": {
            "status": "not_applicable",
            "reason": "cpu_direct_jobs_not_compatibility_batches",
        },
        "prepared_view_cache": {
            "status": "not_applicable",
            "reason": "precompiled_case_owned_views_have_no_runtime_prepared_view_cache",
        },
        "provenance_checksum": 0,
    }
    hashed = raw_validator.StableHashBuilder()
    hashed.string("APGAR-PHASE4-OPERATIONAL-REPRODUCIBILITY-PROVENANCE-V1")
    hashed.string(
        _canonical({key: item for key, item in value.items() if key != "provenance_checksum"})
    )
    value["provenance_checksum"] = hashed.finish()
    return value


def _worker_artifact_checksum(kind: int, compiler: str, payload_checksum: int) -> int:
    hashed = raw_validator.StableHashBuilder()
    hashed.string("APGAR-PHASE4-OPERATIONAL-WORKER-OUTPUT-ARTIFACT-V1")
    hashed.u32(1)
    hashed.byte(kind)
    hashed.string(compiler)
    hashed.u64(payload_checksum)
    return hashed.finish()


def _worker_source_checksum(value: Mapping[str, Any]) -> int:
    hashed = raw_validator.StableHashBuilder()
    hashed.string("APGAR-PHASE4-OPERATIONAL-WORKER-OUTPUT-SOURCE-V1")
    hashed.u32(1)
    hashed.string(value["source_commit"])
    hashed.boolean(value["source_stamped"])
    hashed.boolean(value["source_tree_dirty"])
    hashed.u64(value["artifact_checksum"])
    return hashed.finish()


def _parse_worker(data: bytes, expected_kind: int) -> Mapping[str, Any]:
    if not data or len(data) > _MAXIMUM_WORKER_BYTES:
        raise CaptureError("operational worker output is empty or oversized")
    if data.startswith(b"\xef\xbb\xbf") or not data.endswith(b"\n") or data.endswith(b"\n\n"):
        raise CaptureError("operational worker output is not canonical UTF-8 with one LF")
    try:
        text = data.decode("utf-8")
        value = json.loads(text, object_pairs_hook=_reject_pairs, parse_constant=_reject_constant)
    except (UnicodeError, json.JSONDecodeError, RecursionError) as error:
        raise CaptureError(f"cannot parse operational worker output: {error}") from error
    if not isinstance(value, dict) or tuple(value) != _WORKER_FIELDS:
        raise CaptureError("operational worker output has noncanonical root fields")
    if text != _canonical(value) + "\n":
        raise CaptureError("operational worker output is not canonical compact JSON")
    if value["schema_version"] != 1 or value["kind"] != expected_kind:
        raise CaptureError("operational worker schema or kind is invalid")
    if (
        not isinstance(value["source_commit"], str)
        or len(value["source_commit"]) != 40
        or any(character not in "0123456789abcdef" for character in value["source_commit"])
        or not isinstance(value["source_stamped"], bool)
        or not isinstance(value["source_tree_dirty"], bool)
        or not isinstance(value["compiler_identity"], str)
        or not value["compiler_identity"]
        or not isinstance(value["payload"], dict)
    ):
        raise CaptureError("operational worker source or payload shape is invalid")
    payload_checksum_name = "profile_checksum" if expected_kind == 0 else "authority_checksum"
    payload_checksum = value["payload"].get(payload_checksum_name)
    if (
        isinstance(payload_checksum, bool)
        or not isinstance(payload_checksum, int)
        or not 0 < payload_checksum <= _U64_MAX
    ):
        raise CaptureError("operational worker payload checksum is invalid")
    artifact = _worker_artifact_checksum(
        expected_kind, value["compiler_identity"], payload_checksum
    )
    if value["artifact_checksum"] != artifact:
        raise CaptureError("operational worker artifact checksum is invalid")
    if value["source_envelope_checksum"] != _worker_source_checksum(value):
        raise CaptureError("operational worker source checksum is invalid")
    return value


def _timeval_nanoseconds(value: float, label: str) -> int:
    if not isinstance(value, float) or value < 0:
        raise CaptureError(f"{label} wait4 timeval is invalid")
    result = round(value * 1_000_000_000)
    if result < 0 or result > _U64_MAX:
        raise CaptureError(f"{label} wait4 timeval overflows u64")
    return result


def _proc_start_ticks(stat_record: str) -> str:
    closing_parenthesis = stat_record.rfind(")")
    if closing_parenthesis < 0:
        raise CaptureError("operational child /proc stat has no command terminator")
    fields_after_command = stat_record[closing_parenthesis + 1 :].split()
    # fields_after_command[0] is field 3 (state); starttime is field 22.
    if len(fields_after_command) <= 19 or not fields_after_command[19].isdecimal():
        raise CaptureError("operational child /proc stat has no valid starttime")
    return fields_after_command[19]


def _process_identity(controller: int, pid: int, dispatch: int) -> int:
    try:
        stat_record = pathlib.Path(f"/proc/{pid}/stat").read_text(encoding="ascii")
        start_ticks = _proc_start_ticks(stat_record)
    except (OSError, UnicodeError, CaptureError) as error:
        raise CaptureError("cannot authenticate operational child /proc start identity") from error
    hashed = raw_validator.StableHashBuilder()
    hashed.string("APGAR-PHASE4-OPERATIONAL-PROCESS-INSTANCE-V1")
    hashed.u64(controller)
    hashed.u64(pid)
    hashed.u64(dispatch)
    hashed.string(start_ticks)
    identity = hashed.finish()
    return identity or 1


def _linux_syscall(number: int, *arguments: int) -> int:
    libc = ctypes.CDLL(None, use_errno=True)
    libc.syscall.restype = ctypes.c_long
    result = int(libc.syscall(number, *arguments))
    if result == -1:
        error = ctypes.get_errno()
        raise OSError(error, os.strerror(error))
    return result


def _pidfd_open(pid: int) -> int:
    helper = getattr(os, "pidfd_open", None)
    if helper is not None:
        return int(helper(pid, 0))
    return _linux_syscall(_LINUX_SYS_PIDFD_OPEN, pid, 0)


def _child_subreaper_enabled() -> bool:
    libc = ctypes.CDLL(None, use_errno=True)
    libc.prctl.restype = ctypes.c_int
    enabled = ctypes.c_int()
    if libc.prctl(_PR_GET_CHILD_SUBREAPER, ctypes.byref(enabled), 0, 0, 0) != 0:
        error = ctypes.get_errno()
        raise OSError(error, os.strerror(error))
    return bool(enabled.value)


def _set_child_subreaper(enabled: bool) -> None:
    libc = ctypes.CDLL(None, use_errno=True)
    libc.prctl.restype = ctypes.c_int
    if libc.prctl(_PR_SET_CHILD_SUBREAPER, int(enabled), 0, 0, 0) != 0:
        error = ctypes.get_errno()
        raise OSError(error, os.strerror(error))


def _arm_parent_death_signal(expected_parent_pid: int) -> None:
    if expected_parent_pid <= 1 or os.getppid() != expected_parent_pid:
        os.kill(os.getpid(), signal.SIGKILL)
    libc = ctypes.CDLL(None, use_errno=True)
    libc.prctl.restype = ctypes.c_int
    if libc.prctl(_PR_SET_PDEATHSIG, signal.SIGKILL, 0, 0, 0) != 0:
        error = ctypes.get_errno()
        raise OSError(error, os.strerror(error))
    if os.getppid() != expected_parent_pid:
        os.kill(os.getpid(), signal.SIGKILL)


def _direct_child_pids() -> list[int]:
    try:
        record = pathlib.Path("/proc/thread-self/children").read_text(encoding="ascii")
    except (OSError, UnicodeError) as error:
        raise CaptureError("cannot enumerate operational controller children") from error
    fields = record.split()
    if any(not field.isdecimal() or int(field) <= 0 for field in fields):
        raise CaptureError("operational controller child roster is malformed")
    children = sorted(int(field) for field in fields)
    if len(children) != len(set(children)):
        raise CaptureError("operational controller child roster has duplicate PIDs")
    return children


def _pidfd_send_kill(pidfd: int) -> None:
    helper = getattr(signal, "pidfd_send_signal", None)
    if helper is not None:
        helper(pidfd, signal.SIGKILL)
        return
    _linux_syscall(_LINUX_SYS_PIDFD_SEND_SIGNAL, pidfd, signal.SIGKILL, 0, 0)


def _kill_exact_child(pid: int, pidfd: int | None) -> bool:
    sent = False
    if pidfd is not None:
        try:
            _pidfd_send_kill(pidfd)
            sent = True
        except ProcessLookupError:
            pass
    else:
        try:
            os.kill(pid, signal.SIGKILL)
            sent = True
        except ProcessLookupError:
            pass
    sent = _kill_process_group(pid) or sent
    return sent


def _kill_process_group(pid: int) -> bool:
    try:
        os.killpg(pid, signal.SIGKILL)
        return True
    except ProcessLookupError:
        return False


def _terminate_adopted_descendants(leader_pid: int, deadline: int) -> bool:
    observed = False
    terminated = 0
    cleanup_bound_exceeded = False
    while True:
        if time.monotonic_ns() >= deadline:
            raise CaptureError("operational adopted descendant exceeded cleanup deadline")
        children = [pid for pid in _direct_child_pids() if pid != leader_pid]
        if time.monotonic_ns() >= deadline:
            raise CaptureError("operational adopted descendant exceeded cleanup deadline")
        if not children:
            if cleanup_bound_exceeded:
                raise CaptureError("operational child exceeded adopted-descendant cleanup bound")
            return observed
        observed = True
        for child_pid in children:
            if time.monotonic_ns() >= deadline:
                raise CaptureError("operational adopted descendant exceeded cleanup deadline")
            terminated += 1
            if terminated > _MAXIMUM_ADOPTED_DESCENDANTS:
                cleanup_bound_exceeded = True
            try:
                if time.monotonic_ns() >= deadline:
                    raise CaptureError("operational adopted descendant exceeded cleanup deadline")
                child_pidfd = _pidfd_open(child_pid)
            except ProcessLookupError:
                continue
            try:
                if time.monotonic_ns() >= deadline:
                    raise CaptureError("operational adopted descendant exceeded cleanup deadline")
                try:
                    _pidfd_send_kill(child_pidfd)
                except ProcessLookupError:
                    pass
                while True:
                    if time.monotonic_ns() >= deadline:
                        raise CaptureError(
                            "operational adopted descendant exceeded cleanup deadline"
                        )
                    try:
                        waited, _, _ = os.wait4(child_pid, os.WNOHANG)
                    except ChildProcessError:
                        break
                    if waited == child_pid:
                        break
                    time.sleep(0.001)
            finally:
                os.close(child_pidfd)


def _await_exact_exit_without_reap(pid: int, deadline: int) -> None:
    while True:
        try:
            exit_observation = os.waitid(
                os.P_PID,
                pid,
                os.WEXITED | os.WNOHANG | os.WNOWAIT,
            )
        except ChildProcessError as error:
            raise CaptureError("operational child lost exact waitid authority") from error
        if exit_observation is not None:
            return
        if time.monotonic_ns() >= deadline:
            raise CaptureError("operational exact child exceeded cleanup deadline")
        time.sleep(0.001)


def _launch(
    arguments: Sequence[str],
    *,
    worker_fd: int,
    address_space_bytes: int,
    peak_host_bytes: int,
    deadline_nanoseconds: int,
    controller_identity: int,
    dispatch_ordinal: int,
    expected_kind: int,
    measurement_role: str,
    expected_execution_environment: Mapping[str, Any] | None = None,
    worker_environment: Mapping[str, str] | None = None,
) -> tuple[dict[str, Any], Mapping[str, Any]]:
    if not arguments or worker_fd < 0:
        raise CaptureError("operational worker dispatch is invalid")
    stdout_read = stdout_write = stderr_read = stderr_write = -1
    try:
        stdout_read, stdout_write = os.pipe()
        stderr_read, stderr_write = os.pipe()
    except BaseException:
        for descriptor in (stdout_read, stdout_write, stderr_read, stderr_write):
            if descriptor >= 0:
                os.close(descriptor)
        raise
    start = 0
    controller_pid = os.getpid()
    subreaper_was_enabled = True
    subreaper_changed = False
    try:
        subreaper_was_enabled = _child_subreaper_enabled()
        if _direct_child_pids():
            raise CaptureError("operational controller has pre-existing child processes")
        if not subreaper_was_enabled:
            _set_child_subreaper(True)
            subreaper_changed = True
        start = time.monotonic_ns()
        pid = os.fork()
    except BaseException as error:
        setup_cleanup_errors: list[BaseException] = []
        for descriptor in (stdout_read, stdout_write, stderr_read, stderr_write):
            try:
                os.close(descriptor)
            except OSError as cleanup_error:
                setup_cleanup_errors.append(cleanup_error)
        if subreaper_changed:
            try:
                _set_child_subreaper(False)
            except OSError as cleanup_error:
                setup_cleanup_errors.append(cleanup_error)
        if setup_cleanup_errors:
            error.add_note(
                "operational setup cleanup failed: "
                + "; ".join(str(cleanup_error) for cleanup_error in setup_cleanup_errors)
            )
        if isinstance(error, (OSError, CaptureError)):
            raise CaptureError("cannot establish operational descendant authority") from error
        raise
    if pid == 0:
        try:
            _arm_parent_death_signal(controller_pid)
            os.setsid()
            resource.setrlimit(resource.RLIMIT_AS, (address_space_bytes, address_space_bytes))
            os.dup2(stdout_write, 1)
            os.dup2(stderr_write, 2)
            exec_descriptor = os.dup(worker_fd)
            os.set_inheritable(exec_descriptor, True)
            maximum_fd = int(os.sysconf("SC_OPEN_MAX"))
            os.closerange(3, exec_descriptor)
            os.closerange(exec_descriptor + 1, maximum_fd)
            os.execve(
                f"/proc/self/fd/{exec_descriptor}",
                list(arguments),
                os.environ.copy() if worker_environment is None else dict(worker_environment),
            )
        except BaseException:
            os._exit(127)

    pidfd: int | None = None
    selector: selectors.BaseSelector | None = None
    output = bytearray()
    diagnostics = bytearray()
    wait_status: int | None = None
    wait_observed_nanoseconds: int | None = None
    usage: resource.struct_rusage | None = None
    deadline = start + deadline_nanoseconds
    killed = False
    descendant_cleanup_failed = False

    def terminate_descendants(leader: int, cleanup_deadline: int) -> bool:
        nonlocal descendant_cleanup_failed
        try:
            return _terminate_adopted_descendants(leader, cleanup_deadline)
        except BaseException:
            descendant_cleanup_failed = True
            raise

    try:
        os.close(stdout_write)
        stdout_write = -1
        os.close(stderr_write)
        stderr_write = -1
        try:
            pidfd = _pidfd_open(pid)
        except OSError as error:
            raise CaptureError("cannot obtain pidfd authority for operational child") from error
        process_identity = _process_identity(controller_identity, pid, dispatch_ordinal)
        expected_environment = (
            _execution_environment()
            if expected_execution_environment is None
            else expected_execution_environment
        )
        child_environment = _process_execution_environment(pid)
        if child_environment != {
            "affinity_cpu_ids": expected_environment["affinity_cpu_ids"],
            "cgroup_unified_path": expected_environment["cgroup_unified_path"],
        }:
            raise CaptureError("operational child execution environment differs at dispatch")
        os.set_blocking(stdout_read, False)
        os.set_blocking(stderr_read, False)
        selector = selectors.DefaultSelector()
        selector.register(stdout_read, selectors.EVENT_READ, "stdout")
        selector.register(stderr_read, selectors.EVENT_READ, "stderr")
        while wait_status is None or selector.get_map():
            remaining = deadline - time.monotonic_ns()
            if remaining <= 0:
                if wait_status is None:
                    killed = _kill_exact_child(pid, pidfd)
                    cleanup_deadline = time.monotonic_ns() + 5_000_000_000
                    _await_exact_exit_without_reap(pid, cleanup_deadline)
                    terminate_descendants(pid, cleanup_deadline)
                    waited, wait_status, usage = os.wait4(pid, 0)
                    if waited != pid:
                        raise CaptureError("operational timeout lost exact wait4 authority")
                    wait_observed_nanoseconds = time.monotonic_ns()
                raise CaptureError(f"{measurement_role} operational child timed out")
            if wait_status is None:
                select_timeout = min(remaining / 1_000_000_000, 0.05)
            else:
                select_timeout = min(remaining / 1_000_000_000, 0.05)
            for key, _ in selector.select(select_timeout):
                try:
                    chunk = os.read(key.fd, 64 * 1024)
                except BlockingIOError:
                    continue
                if not chunk:
                    selector.unregister(key.fd)
                    os.close(key.fd)
                    continue
                destination = output if key.data == "stdout" else diagnostics
                maximum = _MAXIMUM_WORKER_BYTES if key.data == "stdout" else _MAXIMUM_STDERR_BYTES
                if len(destination) + len(chunk) > maximum:
                    if wait_status is None:
                        killed = _kill_exact_child(pid, pidfd)
                        cleanup_deadline = time.monotonic_ns() + 5_000_000_000
                        _await_exact_exit_without_reap(pid, cleanup_deadline)
                        terminate_descendants(pid, cleanup_deadline)
                        waited, observed_status, observed_usage = os.wait4(pid, 0)
                        if waited != pid:
                            raise CaptureError("oversized child output lost exact wait4 authority")
                        wait_status = observed_status
                        usage = observed_usage
                        wait_observed_nanoseconds = time.monotonic_ns()
                    raise CaptureError(
                        f"{measurement_role} operational child output exceeded bound"
                    )
                destination.extend(chunk)
            if wait_status is None:
                try:
                    exit_observation = os.waitid(
                        os.P_PID,
                        pid,
                        os.WEXITED | os.WNOHANG | os.WNOWAIT,
                    )
                except ChildProcessError as error:
                    raise CaptureError("operational child lost exact waitid authority") from error
                if exit_observation is not None:
                    if _process_execution_environment(pid) != child_environment:
                        raise CaptureError(
                            "operational child execution environment drifted before exact reap"
                        )
                    surviving_descendant = terminate_descendants(
                        pid, max(deadline, time.monotonic_ns() + 5_000_000_000)
                    )
                    waited, observed_status, observed_usage = os.wait4(pid, 0)
                    if waited != pid:
                        raise CaptureError("operational child lost exact wait4 authority")
                    wait_status = observed_status
                    usage = observed_usage
                    wait_observed_nanoseconds = time.monotonic_ns()
                    if wait_observed_nanoseconds > deadline:
                        raise CaptureError(
                            f"{measurement_role} operational child exceeded its absolute deadline"
                        )
                    if surviving_descendant:
                        raise CaptureError(
                            f"{measurement_role} operational child left a surviving descendant"
                        )
    finally:
        active_error = sys.exception()
        cleanup_errors: list[BaseException] = []
        if selector is not None:
            try:
                selector.close()
            except BaseException as error:
                cleanup_errors.append(error)
        for descriptor in (stdout_read, stdout_write, stderr_read, stderr_write):
            if descriptor < 0:
                continue
            try:
                os.close(descriptor)
            except OSError:
                pass
        if wait_status is None:
            killed = _kill_exact_child(pid, pidfd)
            try:
                cleanup_deadline = time.monotonic_ns() + 5_000_000_000
                _await_exact_exit_without_reap(pid, cleanup_deadline)
                terminate_descendants(pid, cleanup_deadline)
                waited, wait_status, usage = os.wait4(pid, 0)
                if waited != pid:
                    raise CaptureError("operational cleanup lost exact wait4 authority")
            except BaseException as error:
                cleanup_errors.append(error)
        if wait_status is not None:
            try:
                terminate_descendants(0, time.monotonic_ns() + 5_000_000_000)
            except BaseException as error:
                cleanup_errors.append(error)
        if pidfd is not None:
            try:
                os.close(pidfd)
            except OSError as error:
                cleanup_errors.append(error)
        descendants_contained = False
        try:
            remaining_children = _direct_child_pids()
            descendants_contained = not remaining_children
            if remaining_children:
                cleanup_errors.append(
                    CaptureError(
                        "operational descendant cleanup left direct children; "
                        "subreaper authority retained until controller exit"
                    )
                )
        except BaseException as error:
            cleanup_errors.append(error)
        if not subreaper_was_enabled and descendants_contained and not descendant_cleanup_failed:
            try:
                _set_child_subreaper(False)
            except OSError as error:
                cleanup_errors.append(error)
        if cleanup_errors:
            detail = (
                "operational selector or pidfd cleanup failed after exact-child containment: "
                + "; ".join(str(error) for error in cleanup_errors)
            )
            if active_error is not None:
                active_error.add_note(detail)
            else:
                raise CaptureError(detail) from cleanup_errors[0]
    if usage is None or wait_status is None or wait_observed_nanoseconds is None:
        raise CaptureError("operational child has no exact wait4 observation")
    if diagnostics:
        raise CaptureError(
            f"{measurement_role} operational child wrote diagnostics: "
            + diagnostics.decode("utf-8", errors="replace")[:1024]
        )
    if not os.WIFEXITED(wait_status) or os.WEXITSTATUS(wait_status) != 0:
        raise CaptureError(f"{measurement_role} operational child did not exit successfully")
    user_cpu = _timeval_nanoseconds(usage.ru_utime, "user")
    system_cpu = _timeval_nanoseconds(usage.ru_stime, "system")
    total_cpu = user_cpu + system_cpu
    peak_host = int(usage.ru_maxrss) * 1024
    outer = wait_observed_nanoseconds - start
    if total_cpu > _U64_MAX or peak_host <= 0 or peak_host > _U64_MAX or outer <= 0:
        raise CaptureError("operational wait4 observation overflows or is empty")
    if peak_host > peak_host_bytes:
        raise CaptureError(
            f"{measurement_role} operational child exceeded its peak-host-memory cap"
        )
    worker = _parse_worker(bytes(output), expected_kind)
    if expected_kind == 0:
        observation = {
            "schema_version": 1,
            "role": measurement_role,
            "controller_identity": controller_identity,
            "dispatch_ordinal": dispatch_ordinal,
            "process_instance_identity": process_identity,
            "configured_address_space_limit_bytes": address_space_bytes,
            "outer_wall_nanoseconds": outer,
            "user_cpu_nanoseconds": user_cpu,
            "system_cpu_nanoseconds": system_cpu,
            "total_cpu_nanoseconds": total_cpu,
            "peak_host_bytes": peak_host,
            "raw_wait_status": wait_status,
            "process_exit_code": os.WEXITSTATUS(wait_status),
            "terminating_signal": 0,
            "watchdog_kill_sent": killed,
            "isolated_exec": True,
            "measurement_scope": (
                "controller_monotonic_fork_to_exact_wait4_reap_including_exec_setup_warmup_replay_serialization_release"
            ),
        }
    else:
        observation = {
            "schema_version": 1,
            "role": measurement_role,
            "controller_identity": controller_identity,
            "dispatch_ordinal": dispatch_ordinal,
            "process_instance_identity": process_identity,
            "configured_address_space_limit_bytes": address_space_bytes,
            "raw_wait_status": wait_status,
            "process_exit_code": os.WEXITSTATUS(wait_status),
            "terminating_signal": 0,
            "watchdog_kill_sent": killed,
            "isolated_exec": True,
            "resource_measurements": {
                "status": "not_used",
                "reason": "unmeasured_full_preimage_authority_replay",
            },
        }
    return observation, worker


def _cell_arguments(options: argparse.Namespace, mode: str, arm: str) -> list[str]:
    arguments = [
        str(options.worker),
        f"--mode={mode}",
        f"--arm={arm}",
        f"--case_id={options.case_id}",
        f"--pool_size={options.pool_size}",
        f"--workers={options.workers}",
        f"--setup_ns={options.setup_ns}",
        f"--prepared_ns={options.prepared_ns}",
        f"--cold_ns={options.cold_ns}",
        f"--address_space_bytes={options.address_space_bytes}",
        f"--peak_host_bytes={options.peak_host_bytes}",
        f"--maximum_nets={options.maximum_nets}",
        f"--maximum_compiled_nodes={options.maximum_compiled_nodes}",
        f"--maximum_compiled_host_bytes={options.maximum_compiled_host_bytes}",
        f"--maximum_active_regions={options.maximum_active_regions}",
        f"--maximum_board_entities={options.maximum_board_entities}",
    ]
    fixture = getattr(options, "fixture", None)
    if fixture is not None:
        arguments.append(f"--fixture_path={fixture}")
    corpus_version = getattr(options, "corpus_version", None)
    raw_wire_schema_version = getattr(options, "raw_wire_schema_version", None)
    if corpus_version is not None:
        arguments.append(f"--corpus_version={corpus_version}")
    if raw_wire_schema_version is not None:
        arguments.append(f"--raw_wire_schema_version={raw_wire_schema_version}")
    if options.apgar_commit is not None:
        arguments.append(f"--apgar_commit={options.apgar_commit}")
    if getattr(options, "testing_allow_unstamped", options.apgar_commit is None):
        arguments.append("--testing_allow_unstamped=1")
    return arguments


def _artifact_checksum(value: Mapping[str, Any]) -> int:
    payload = {
        key: item
        for key, item in value.items()
        if key not in {"artifact_checksum", "source_envelope_checksum"}
    }
    hashed = raw_validator.StableHashBuilder()
    hashed.string("APGAR-PHASE4-OPERATIONAL-MEASUREMENT-CAPTURE-ARTIFACT-V1")
    hashed.string(_canonical(payload))
    return hashed.finish()


def _source_checksum(value: Mapping[str, Any]) -> int:
    hashed = raw_validator.StableHashBuilder()
    hashed.string("APGAR-PHASE4-OPERATIONAL-MEASUREMENT-CAPTURE-SOURCE-V1")
    hashed.u32(value["schema_version"])
    hashed.string(value["source_commit"])
    hashed.boolean(value["source_stamped"])
    hashed.boolean(value["source_tree_dirty"])
    hashed.u64(value["artifact_checksum"])
    return hashed.finish()


def _capture_with_pinned_worker(
    options: argparse.Namespace,
    worker_fd: int,
    worker_identity: Mapping[str, Any],
) -> dict[str, Any]:
    if signal.getsignal(signal.SIGCHLD) != signal.SIG_DFL:
        raise CaptureError("operational controller requires default SIGCHLD semantics")
    seed = raw_validator.StableHashBuilder()
    seed.string("APGAR-PHASE4-OPERATIONAL-CONTROLLER-V1")
    seed.u64(os.getpid())
    seed.u64(time.monotonic_ns())
    seed.string(platform.node())
    controller_identity = seed.finish() or 1
    per_process_deadline = options.setup_ns + 2 * options.cold_ns
    if per_process_deadline <= 0 or per_process_deadline > _U64_MAX:
        raise CaptureError("operational child deadline is invalid")
    execution_environment = _execution_environment()
    rows: dict[tuple[str, str], tuple[dict[str, Any], Mapping[str, Any]]] = {}
    dispatch = 0
    for mode in ("measured", "authority"):
        for arm in ("baseline", "candidate"):
            _require_execution_environment(execution_environment)
            dispatch += 1
            rows[(mode, arm)] = _launch(
                _cell_arguments(options, mode, arm),
                worker_fd=worker_fd,
                address_space_bytes=options.address_space_bytes,
                peak_host_bytes=options.peak_host_bytes,
                deadline_nanoseconds=per_process_deadline,
                controller_identity=controller_identity,
                dispatch_ordinal=dispatch,
                expected_kind=0 if mode == "measured" else 1,
                measurement_role=f"{mode}_{arm}",
                expected_execution_environment=execution_environment,
                worker_environment=getattr(options, "worker_environment", None),
            )
            _require_execution_environment(execution_environment)
    workers = [worker for _, worker in rows.values()]
    source_identity = {
        (
            worker["source_commit"],
            worker["source_stamped"],
            worker["source_tree_dirty"],
            worker["compiler_identity"],
        )
        for worker in workers
    }
    if len(source_identity) != 1:
        raise CaptureError("operational workers do not share one source/toolchain identity")
    source_commit, source_stamped, source_tree_dirty, compiler_identity = next(
        iter(source_identity)
    )
    require_clean_source = getattr(
        options,
        "require_clean_source",
        options.apgar_commit is not None,
    )
    if (
        require_clean_source
        and options.apgar_commit is not None
        and source_commit != options.apgar_commit
    ):
        raise CaptureError("operational workers do not authenticate the requested commit")
    if require_clean_source and (not source_stamped or source_tree_dirty):
        raise CaptureError("operational workers do not authenticate the requested clean commit")
    process_identities = {
        observation["process_instance_identity"] for observation, _ in rows.values()
    }
    if len(process_identities) != 4 or 0 in process_identities:
        raise CaptureError("operational measured and authority replays are not four distinct execs")
    arms: list[dict[str, Any]] = []
    for arm_index, arm in enumerate(("baseline", "candidate")):
        measured_observation, measured_worker = rows[("measured", arm)]
        authority_observation, authority_worker = rows[("authority", arm)]
        measured_semantics = measured_worker["payload"]["execution"]["semantics"]
        authority_semantics = authority_worker["payload"]["semantics"]
        if measured_semantics != authority_semantics or measured_semantics.get("arm") != arm_index:
            raise CaptureError(f"{arm} measured and authority semantics differ")
        measured_witness = (measured_worker["payload"].get("candidate_session") or {}).get(
            "replay_witness"
        )
        authority_witness = authority_worker["payload"].get("candidate_session_witness")
        if (
            arm == "baseline" and (measured_witness is not None or authority_witness is not None)
        ) or (
            arm == "candidate"
            and (
                measured_witness is None
                or authority_witness is None
                or measured_witness != authority_witness
            )
        ):
            raise CaptureError(f"{arm} full-preimage witness join failed")
        if (
            authority_worker["payload"]["recomputed_full_preimage_session_checksum"]
            != measured_semantics["algorithm_session_checksum"]
        ):
            raise CaptureError(f"{arm} full-preimage checksum does not bind measured semantics")
        arms.append(
            {
                "arm": arm_index,
                "measured_process": measured_observation,
                "measured_worker": measured_worker,
                "authority_process": authority_observation,
                "authority_worker": authority_worker,
            }
        )
    if _sha256_fd(worker_fd) != worker_identity["sha256"]:
        raise CaptureError("pinned operational worker changed during capture")
    provenance = _provenance(
        worker_identity,
        compiler_identity,
        execution_environment,
        publication_invocation=getattr(
            options,
            "publication_invocation",
            "bazel --batch run --config=benchmark //:phase4_operational_capture",
        ),
        worker_target=getattr(
            options,
            "worker_target",
            "//:phase4_operational_replay_worker",
        ),
    )
    result: dict[str, Any] = {
        "schema_version": 1,
        "source_commit": source_commit,
        "source_stamped": source_stamped,
        "source_tree_dirty": source_tree_dirty,
        "standalone_publication_eligible": False,
        "cell_operational_capture_complete": True,
        "controller_identity": controller_identity,
        "capture_run_identity": 0,
        "cell_config": {
            "schema_version": 1,
            "case_id": options.case_id,
            "requested_pool_size": options.pool_size,
            "preparation_worker_count": options.workers,
            "repetitions": 20,
            "maximum_setup_elapsed_nanoseconds": options.setup_ns,
            "external_budget": {
                "maximum_prepared_elapsed_nanoseconds": options.prepared_ns,
                "maximum_cold_elapsed_nanoseconds": options.cold_ns,
                "maximum_address_space_bytes": options.address_space_bytes,
                "maximum_peak_host_bytes": options.peak_host_bytes,
            },
            "corpus_limits": {
                "maximum_nets": options.maximum_nets,
                "maximum_compiled_nodes": options.maximum_compiled_nodes,
                "maximum_compiled_host_bytes": options.maximum_compiled_host_bytes,
                "maximum_active_regions": options.maximum_active_regions,
                "maximum_board_entities": options.maximum_board_entities,
            },
        },
        "reproducibility_provenance": provenance,
        "arms": arms,
        "artifact_checksum": 0,
        "source_envelope_checksum": 0,
    }
    run_hash = raw_validator.StableHashBuilder()
    run_hash.string("APGAR-PHASE4-OPERATIONAL-CAPTURE-RUN-V1")
    run_hash.u64(controller_identity)
    for identity in sorted(process_identities):
        run_hash.u64(identity)
    result["capture_run_identity"] = run_hash.finish() or 1
    result["artifact_checksum"] = _artifact_checksum(result)
    result["source_envelope_checksum"] = _source_checksum(result)
    return result


def capture(options: argparse.Namespace) -> dict[str, Any]:
    worker_fd, worker_identity = _open_pinned_worker(options.worker)
    try:
        return _capture_with_pinned_worker(options, worker_fd, worker_identity)
    finally:
        os.close(worker_fd)


def _strict_positive(value: str) -> int:
    if not value.isdecimal() or (len(value) > 1 and value.startswith("0")):
        raise argparse.ArgumentTypeError("value must be strict positive decimal")
    parsed = int(value)
    if parsed <= 0 or parsed > _U64_MAX:
        raise argparse.ArgumentTypeError("value is outside u64")
    return parsed


def _check_duplicate_options(argv: Sequence[str]) -> None:
    seen: set[str] = set()
    for argument in argv:
        if argument.startswith("--"):
            key = argument.split("=", 1)[0]
            if key in seen:
                raise CaptureError(f"duplicate option: {key}")
            seen.add(key)


def main(argv: Sequence[str] | None = None) -> int:
    arguments = list(sys.argv[1:] if argv is None else argv)
    try:
        _check_duplicate_options(arguments)
    except CaptureError as error:
        print(f"Phase 4 operational capture failed: {error}", file=sys.stderr)
        return 2
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worker", type=pathlib.Path)
    parser.add_argument("--fixture", type=pathlib.Path)
    parser.add_argument("--apgar-commit")
    parser.add_argument("--testing-allow-unstamped", action="store_true")
    parser.add_argument("--case-id", required=True, type=_strict_positive)
    parser.add_argument("--pool-size", required=True, type=_strict_positive)
    parser.add_argument("--workers", type=_strict_positive, default=4)
    parser.add_argument("--setup-ns", type=_strict_positive, default=300_000_000_000)
    parser.add_argument("--prepared-ns", type=_strict_positive, default=300_000_000_000)
    parser.add_argument("--cold-ns", type=_strict_positive, default=300_000_000_000)
    parser.add_argument("--address-space-bytes", type=_strict_positive, default=64 << 30)
    parser.add_argument("--peak-host-bytes", type=_strict_positive, default=16 << 30)
    parser.add_argument("--maximum-nets", type=_strict_positive, default=4096)
    parser.add_argument("--maximum-compiled-nodes", type=_strict_positive, default=100_000_000)
    parser.add_argument("--maximum-compiled-host-bytes", type=_strict_positive, default=8 << 30)
    parser.add_argument("--maximum-active-regions", type=_strict_positive, default=250_000)
    parser.add_argument("--maximum-board-entities", type=_strict_positive, default=100_000)
    options = parser.parse_args(arguments)
    try:
        if options.apgar_commit is not None and options.testing_allow_unstamped:
            raise CaptureError("clean publication and testing-unstamped modes are exclusive")
        if options.apgar_commit is None and not options.testing_allow_unstamped:
            raise CaptureError("--apgar-commit is required outside explicit test mode")
        if options.apgar_commit is not None and (
            len(options.apgar_commit) != 40
            or any(character not in "0123456789abcdef" for character in options.apgar_commit)
        ):
            raise CaptureError("--apgar-commit must be 40 lowercase hexadecimal characters")
        options.worker = (
            options.worker.resolve()
            if options.worker is not None
            else _resolve_runfile("phase4_operational_replay_worker")
        )
        options.fixture = (
            options.fixture.resolve()
            if options.fixture is not None
            else _resolve_runfile("tests/fixtures/phase4_supported_multinet_v1.kicad_pcb")
        )
        if not options.worker.is_file() or not os.access(options.worker, os.X_OK):
            raise CaptureError("operational worker is missing or not executable")
        if not options.fixture.is_file():
            raise CaptureError("imported fixture is missing")
        artifact = capture(options)
        encoded = _canonical(artifact) + "\n"
        if len(encoded.encode("utf-8")) > _MAXIMUM_CAPTURE_BYTES:
            raise CaptureError("operational capture exceeds 32 MiB")
        sys.stdout.write(encoded)
    except (CaptureError, OSError, OverflowError) as error:
        print(f"Phase 4 operational capture failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
