#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
profile="${1:?small or large required}"
case "$profile" in small) dir=wasm ;; large) dir=wasm-large ;; *) exit 2 ;; esac
scratch="$(mktemp -d "${TMPDIR:-/tmp}/maelys-wasm-tests.XXXXXX")"
trap 'rm -rf -- "$scratch"' EXIT
mkdir -p "$scratch/build/$dir" "$scratch/bindings/wasm" "$scratch/tests/wasm"
cp "$root/build/$dir/maelys_datalog_dynamic.js" "$root/build/$dir/maelys_datalog_dynamic.wasm" "$scratch/build/$dir/"
cp "$root/build/$dir/maelys_playground.js" "$root/build/$dir/maelys_playground.d.ts" "$root/build/$dir/exports.json" "$scratch/bindings/wasm/"
cp "$root/tests/wasm/"*.mjs "$scratch/tests/wasm/"
cd "$scratch"
unset CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH LIBRARY_PATH MAELYS_WASM_BUILD_DIR
MAELYS_WASM_PROFILE="$profile" "${NODE:-node}" --test tests/wasm/test_*.mjs
