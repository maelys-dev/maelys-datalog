#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Build Python from this same source revision against the same native profile,
# then use it as an independent oracle for both JavaScript runtimes.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
profile="${1:?small or large}"
python="${2:-python3}"
python="$("$python" -c 'import sys; print(sys.executable)')"
case "$profile" in small) large=OFF ;; large) large=ON ;; *) exit 2 ;; esac
scratch="$(mktemp -d)"
trap 'rm -rf -- "$scratch"' EXIT
# The JS builder removes its private SDK on exit. Build an independent oracle
# from this revision; never rely on ignored directories left by an earlier run.
cmake -S "$root" -B "$scratch/build" -DBUILD_TESTING=OFF \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_LIBDIR=lib \
  -DCMAKE_INSTALL_INCLUDEDIR=include -DMAELYS_DATALOG_PROFILE_LARGE="$large"
cmake --build "$scratch/build" --target maelys_datalog_shared --parallel 2
cmake --install "$scratch/build" --prefix "$scratch/sdk" --component sdk
cmake --install "$scratch/build" --prefix "$scratch/sdk" --component sdk-shared
cp -R "$root/bindings/python" "$scratch/python"
"$python" "$scratch/python/build_cffi.py" --sdk-prefix "$scratch/sdk"
cd "$root"
PYTHONPATH="$scratch/python" PYTHON="$python" MAELYS_PROFILE="$profile" \
  node bindings/javascript/test/python-parity.mjs
