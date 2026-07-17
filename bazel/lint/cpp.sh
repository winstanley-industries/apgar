#!/usr/bin/env bash

set -euo pipefail

readonly LINT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${LINT_DIR}/common.sh"

: "${APGAR_BAZEL_REAL:?APGAR_BAZEL_REAL is required}"
: "${APGAR_LINT_MODE:?APGAR_LINT_MODE is required}"

files=()
while IFS= read -r -d '' path; do
  case "${path}" in
    *.c|*.cc|*.cpp|*.cxx|*.h|*.hh|*.hpp|*.hxx)
      files+=("${APGAR_REPO_ROOT}/${path}")
      ;;
  esac
done < <(tracked_and_untracked_files)

if (( ${#files[@]} == 0 )); then
  echo "    no C/C++ files"
  exit 0
fi

if [[ "${APGAR_LINT_MODE}" == "fix" ]]; then
  args=(-i)
else
  args=(--dry-run --Werror)
fi

"${APGAR_BAZEL_REAL}" run @llvm_toolchain//:clang-format -- \
  "${args[@]}" "${files[@]}"
