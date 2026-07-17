#!/usr/bin/env bash

set -euo pipefail

readonly LINT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly APGAR_REPO_ROOT="$(cd "${LINT_DIR}/../.." && pwd)"
readonly APGAR_BAZEL_REAL="${BAZEL_REAL:?lint must be launched with 'bazel lint'}"

mode="check"
selected=()

usage() {
  cat <<'EOF'
Usage: bazel lint [--fix] [--only LANGUAGE]...

Run every repository linter with Bazel-pinned tools.

Options:
  --fix              Rewrite files in place instead of checking them.
  --only LANGUAGE    Run one linter (cpp or starlark). May be repeated.
  -h, --help         Show this help.
EOF
}

while (( $# > 0 )); do
  case "$1" in
    --fix)
      mode="fix"
      shift
      ;;
    --only)
      if (( $# < 2 )); then
        echo "bazel lint: --only requires a language" >&2
        exit 2
      fi
      selected+=("$2")
      shift 2
      ;;
    --only=*)
      selected+=("${1#--only=}")
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "bazel lint: unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if (( ${#selected[@]} == 0 )); then
  selected=(cpp starlark)
fi

export APGAR_BAZEL_REAL APGAR_REPO_ROOT
export APGAR_LINT_MODE="${mode}"

for language in "${selected[@]}"; do
  driver="${LINT_DIR}/${language}.sh"
  if [[ ! -x "${driver}" ]]; then
    echo "bazel lint: unknown language: ${language}" >&2
    exit 2
  fi

  echo "==> Linting ${language} (${mode})"
  "${driver}"
done
