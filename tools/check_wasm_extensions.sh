#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Native C extension contracts, statically linked into one WASM module per test.
# This does not add selectors to the shipped JavaScript binding.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
profile="${1:-small}"
flags=(-std=c11 -O1 -Wall -Wextra -Werror)
case "$profile" in
  small) ;;
  large) flags+=(-DMAELYS_DATALOG_PROFILE_LARGE) ;;
  *) echo 'expected small or large' >&2; exit 1 ;;
esac
scratch="$(mktemp -d "${TMPDIR:-/tmp}/maelys-wasm-sdk.XXXXXX")"
trap 'rm -rf -- "$scratch"' EXIT
objects=()
while IFS= read -r source; do
  [[ -z "$source" || "$source" == \#* ]] && continue
  object="$scratch/$(basename "${source%.c}").o"
  emcc "${flags[@]}" -D_POSIX_C_SOURCE=200809L -I"$root" -I"$root/include" \
    -c "$root/$source" -o "$object"
  objects+=("$object")
done < <(cat "$root/build-support/core-sources.txt" \
             "$root/build-support/standard-sources.txt" \
             "$root/build-support/native-sources.txt")
emar rcs "$scratch/engine.a" "${objects[@]}"
for role in frontend backend planner filter bundle; do
  emcc "${flags[@]}" -I"$root/include" -I"$root/sdk/conformance" \
    -I"$root/sdk/examples/$role/include" \
    "$root/sdk/examples/$role/src/extension.c" \
    "$root/sdk/examples/$role/tests/conformance.c" "$scratch/engine.a" \
    -sENVIRONMENT=node -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=2097152 \
    -o "$scratch/$role.cjs"
  "${NODE:-node}" "$scratch/$role.cjs"
done
echo "WASM statically linked extension examples: $profile PASS"
