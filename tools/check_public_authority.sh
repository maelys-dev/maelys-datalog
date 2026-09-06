#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Sensitivity check, not a production variant: deliberately select the wrong
# symbol authority in exactly one temporary translation unit.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$(cd "${1:-$root/build/cmake}" && pwd)"
scratch="$(mktemp -d "${TMPDIR:-/tmp}/maelys-authority.XXXXXX")"
trap 'rm -rf -- "$scratch"' EXIT
cc="${CC:-cc}"
flags=(-std=c11 -Wall -Wextra -Werror -I"$root" -I"$root/include")
if grep -q '^MAELYS_DATALOG_PROFILE_LARGE:BOOL=ON$' "$build/CMakeCache.txt"; then
  flags+=(-DMAELYS_DATALOG_PROFILE_LARGE)
fi
"$cc" "${flags[@]}" "$root/tests/fixtures/public_api_consumer.c" \
  "$build/libmaelys_datalog.a" -o "$scratch/control"
"$scratch/control"
awk '
  /const maelys_datalog_symbol_table_t \*symbols = &result->owner->inputs->working.symbols;/ {
    sub(/inputs->working.symbols/, "inputs->prepared.symbols"); changed++
  }
  { print }
  END { if (changed != 1) exit 1 }
' "$root/src/runtime/maelys_datalog_runtime.c" > "$scratch/wrong-authority.c"
"$cc" "${flags[@]}" -c "$scratch/wrong-authority.c" -o "$scratch/mutant.o"
"$cc" "${flags[@]}" "$root/tests/fixtures/public_api_consumer.c" "$scratch/mutant.o" \
  "$build/libmaelys_datalog.a" -o "$scratch/mutant"
status=0
"$scratch/mutant" > "$scratch/mutant.log" 2>&1 || status=$?
# Exit 9 is precisely the fixture's result-scoped symbol lookup assertion.
if [[ "$status" != 9 ]]; then
  cat "$scratch/mutant.log" >&2
  echo "FAIL: wrong-authority mutant returned $status (expected 9)" >&2
  exit 1
fi
echo 'public authority sensitivity: control PASS, wrong-authority mutant rejected at symbol lookup'
