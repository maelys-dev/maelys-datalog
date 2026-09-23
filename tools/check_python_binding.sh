#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Build and test the single Python package outside the checkout, using only an
# installed SDK. Each profile uses fresh extension/library inodes and processes.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$(cd "${1:?CMake build directory required}" && pwd)"
python="${2:-python3}"
python="$("$python" -c 'import sys; print(sys.executable)')"
profile="${3:?expected profile (small or large) required}"
case "$profile" in
  small) large=OFF ;;
  large) large=ON ;;
  *) echo 'expected profile: small or large' >&2; exit 1 ;;
esac
grep -q "^MAELYS_DATALOG_PROFILE_LARGE:BOOL=$large$" "$build/CMakeCache.txt"
grep -Fqx "CMAKE_HOME_DIRECTORY:INTERNAL=$root" "$build/CMakeCache.txt"
cmake --build "$build" --target maelys_datalog_shared maelys_datalog --parallel 2
scratch="$(mktemp -d "${TMPDIR:-/tmp}/maelys-python-sdk.XXXXXX")"
trap 'rm -rf -- "$scratch"' EXIT
prefix="$scratch/sdk"
cmake --install "$build" --prefix "$prefix"
# Copy source files only. A previous extension cannot satisfy the new test.
"$python" - "$root" "$scratch/consumer" <<'PY'
from pathlib import Path
import shutil
import sys
root, consumer = map(Path, sys.argv[1:])
for directory in ("bindings/python", "tests/python"):
    shutil.copytree(root / directory, consumer / directory,
                    ignore=shutil.ignore_patterns("build", "__pycache__", "*.so", "*.dylib", ".pytest_cache"))
PY
cd "$scratch/consumer"
unset CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH LIBRARY_PATH
export MAELYS_DATALOG_SDK_PREFIX="$prefix"
export MAELYS_DATALOG_EXPECT_PROFILE="$profile"
export PYTHONPATH="$PWD/bindings/python"
"$python" bindings/python/build_cffi.py --sdk-prefix "$prefix"
"$python" -m pytest -q tests/python
