#!/usr/bin/env bash

set -euo pipefail

readonly LINT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${LINT_DIR}/common.sh"

: "${APGAR_BAZEL_REAL:?APGAR_BAZEL_REAL is required}"
: "${APGAR_LINT_MODE:?APGAR_LINT_MODE is required}"

files=()
while IFS= read -r -d '' path; do
  case "${path}" in
    BUILD|*/BUILD|BUILD.bazel|*/BUILD.bazel|MODULE.bazel|*/MODULE.bazel|WORKSPACE|*/WORKSPACE|WORKSPACE.bazel|*/WORKSPACE.bazel|*.bzl)
      files+=("${APGAR_REPO_ROOT}/${path}")
      ;;
  esac
done < <(tracked_and_untracked_files)

if (( ${#files[@]} == 0 )); then
  echo "    no Bazel/Starlark files"
  exit 0
fi

if [[ "${APGAR_LINT_MODE}" == "fix" ]]; then
  args=(-mode=fix -lint=fix)
else
  args=(-mode=check -lint=warn)
fi

"${APGAR_BAZEL_REAL}" run @buildifier_prebuilt//:buildifier -- \
  "${args[@]}" "${files[@]}"
