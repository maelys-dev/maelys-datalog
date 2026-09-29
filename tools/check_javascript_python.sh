#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Build Python from this same source revision against the same native profile,
# then use it as an independent oracle for both JavaScript runtimes.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
profile="${1:?small or large}"
python="${2:-python3}"
python="$("$python" -c 'import sys; print(sys.executable)')"
build="$root/build/javascript-native-$profile"
scratch="$(mktemp -d)"
trap 'rm -rf -- "$scratch"' EXIT
cmake --install "$build" --prefix "$scratch/sdk"
cp -R "$root/bindings/python" "$scratch/python"
"$python" "$scratch/python/build_cffi.py" --sdk-prefix "$scratch/sdk"
cd "$root"
PYTHONPATH="$scratch/python" PYTHON="$python" MAELYS_PROFILE="$profile" \
  node bindings/javascript/test/python-parity.mjs
