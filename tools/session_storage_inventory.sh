#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Layout/reservation inventory only: does not time or execute a solve.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
profile="${1:-SMALL}"
if [[ $# -gt 1 || ( "$profile" != SMALL && "$profile" != LARGE ) ]]; then
  echo 'usage: tools/session_storage_inventory.sh [SMALL|LARGE]' >&2
  exit 2
fi
scratch="$(mktemp -d "${TMPDIR:-/tmp}/maelys-storage-inventory.XXXXXX")"
trap 'rm -rf -- "$scratch"' EXIT
cd "$root"
printf 'source_commit=%s\n' "$(git rev-parse HEAD)" >&2
if [[ -n "$(git status --porcelain --untracked-files=normal)" ]]; then
  echo 'source_tree_state=dirty' >&2
else
  echo 'source_tree_state=clean' >&2
fi
sources=()
while IFS= read -r source; do
  case "$source" in
    ''|'#'*) continue ;;
    src/runtime/maelys_datalog_runtime.c|src/core/maelys_datalog_solver.c) continue ;;
  esac
  sources+=("$source")
done < <(cat build-support/core-sources.txt build-support/standard-sources.txt build-support/native-sources.txt)
cc="${CC:-cc}"
"$cc" --version >&2
"$cc" -std=c11 -Wall -Wextra -Werror -O0 -I. -Iinclude \
  "-DMAELYS_DATALOG_PROFILE_$profile" \
  bench/session_storage_inventory_runtime.c bench/session_storage_inventory_solver.c \
  "${sources[@]}" -o "$scratch/inventory"
"$scratch/inventory"
