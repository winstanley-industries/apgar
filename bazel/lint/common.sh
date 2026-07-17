#!/usr/bin/env bash

set -euo pipefail

: "${APGAR_REPO_ROOT:?APGAR_REPO_ROOT is required}"

tracked_and_untracked_files() {
  git -C "${APGAR_REPO_ROOT}" ls-files \
    --cached \
    --others \
    --exclude-standard \
    -z
}
