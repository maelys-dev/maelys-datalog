#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Select the exact native build even when CMake's POST_BUILD is already up to
# date and the package currently holds libraries from the other size profile.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$(cd "${1:?CMake build directory required}" && pwd)"
python="${2:-python3}"
profile="${3:?expected profile (small or large) required}"
case "$profile" in
  small) large=OFF ;;
  large) large=ON ;;
  *) echo 'expected profile: small or large' >&2; exit 1 ;;
esac
grep -q "^MAELYS_DATALOG_PROFILE_LARGE:BOOL=$large$" "$build/CMakeCache.txt"
grep -Fqx "CMAKE_HOME_DIRECTORY:INTERNAL=$root" "$build/CMakeCache.txt"
cmake --build "$build" --target maelys_py_bind --parallel 2
suffix=so
if [[ "$(uname -s)" == Darwin ]]; then suffix=dylib; fi
# Publish fresh inodes: overwriting a previously loaded Mach-O can leave macOS
# with a stale code-signature page cache when switching SMALL/LARGE profiles.
stage="$(mktemp -d "$root/bindings/python/maelys_datalog/.native-sdk.XXXXXX")"
trap 'rm -rf -- "$stage"' EXIT
for library in maelys_datalog_shared maelys_py_bind; do
  cp "$build/lib$library.$suffix" "$stage/"
  mv -f "$stage/lib$library.$suffix" "$root/bindings/python/maelys_datalog/"
done
cd "$root"
"$python" bindings/python/build_cffi.py
MAELYS_DATALOG_EXPECT_PROFILE="$profile" PYTHONPATH=bindings/python \
  "$python" -m pytest -q tests/python
