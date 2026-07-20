#!/usr/bin/env bash

set -euo pipefail
export LC_ALL=C

if (($# != 0)); then
  echo "usage: tools/workspace_status.sh" >&2
  exit 2
fi

# Source identity is resolved from this checkout, not from caller-selected Git
# plumbing. In particular, an inherited alternate index or work tree must not
# be able to label a different source tree as this repository.
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_COMMON_DIR
unset GIT_OBJECT_DIRECTORY GIT_ALTERNATE_OBJECT_DIRECTORIES
unset GIT_CEILING_DIRECTORIES GIT_CONFIG_COUNT GIT_CONFIG_KEY_0 GIT_CONFIG_VALUE_0
unset GIT_CONFIG_PARAMETERS GIT_CONFIG_SYSTEM GIT_EXEC_PATH
unset GIT_EXTERNAL_DIFF GIT_DIFF_OPTS
export GIT_CONFIG_NOSYSTEM=1
export GIT_CONFIG_GLOBAL=/dev/null

script_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
expected_root="$(cd -- "${script_directory}/.." && pwd -P)"
bootstrap_git=(git -c core.fsmonitor=false -c core.untrackedCache=false -C "${expected_root}")
reported_root="$("${bootstrap_git[@]}" rev-parse --show-toplevel)"
repository_root="$(cd -- "${reported_root}" && pwd -P)"
if [[ "${repository_root}" != "${expected_root}" ]]; then
  echo "workspace status: script is not running from its owning Git work tree" >&2
  exit 1
fi
git_command=(git -c core.fsmonitor=false -c core.untrackedCache=false -C "${repository_root}")

commit="$("${git_command[@]}" rev-parse --verify 'HEAD^{commit}')"
if [[ ! "${commit}" =~ ^[0-9a-f]{40}$ ]]; then
  echo "workspace status: git returned an invalid commit identity" >&2
  exit 1
fi

dirty=0
if ! "${git_command[@]}" diff --quiet --ignore-submodules=none --; then
  dirty=1
fi
if ! "${git_command[@]}" diff --cached --quiet --ignore-submodules=none --; then
  dirty=1
fi
if [[ -n "$("${git_command[@]}" ls-files --others --exclude-standard)" ]]; then
  dirty=1
fi

# Git deliberately suppresses work-tree checks for assume-unchanged and
# skip-worktree entries. Either bit makes a clean publication stamp
# unverifiable, so conservatively classify the checkout as dirty even if the
# indexed bytes currently happen to match HEAD.
index_listing="$("${git_command[@]}" ls-files -v)"
while IFS= read -r entry; do
  tag="${entry:0:1}"
  if [[ "${tag}" == S || "${tag}" == [[:lower:]] ]]; then
    dirty=1
    break
  fi
done <<<"${index_listing}"

printf 'STABLE_APGAR_GIT_COMMIT %s\n' "${commit}"
printf 'STABLE_APGAR_GIT_DIRTY %s\n' "${dirty}"
