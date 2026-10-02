#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
set -euo pipefail
build="$(cd "${1:?candidate CMake build}" && pwd)"
profile="${2:?SMALL or LARGE}"
out="${3:?fresh absolute evidence directory}"
case "$profile" in SMALL) large=OFF;; LARGE) large=ON;; *) exit 2;; esac
case "$out" in /*) ;; *) exit 2;; esac
test ! -e "$out"
root="$(cd "$(dirname "$0")/.." && pwd)"
base=3173f87beb5eb99576bb9c55ff7c88facf180877
mkdir -p "$out/base-source"
git -C "$root" archive "$base" | tar -x -C "$out/base-source"
printf 'base=%s\nprofile=%s\n' "$base" "$profile" > "$out/revisions.txt"
git -C "$root" rev-parse HEAD >> "$out/revisions.txt"
cmake -S "$out/base-source" -B "$out/base-build" -DBUILD_TESTING=OFF \
    -DCMAKE_BUILD_TYPE=Release -DMAELYS_DATALOG_PROFILE_LARGE="$large" \
    -DCMAKE_INSTALL_PREFIX="$out/old-sdk" > "$out/base-build.log" 2>&1
cmake --build "$out/base-build" --parallel 2 >> "$out/base-build.log" 2>&1
cmake --install "$out/base-build" >> "$out/base-build.log" 2>&1
cmake --install "$build" --prefix "$out/new-sdk" > "$out/new-install.log" 2>&1
python3 "$root/tools/check_backend_allocation_sdk.py" --old "$out/old-sdk" \
    --new "$out/new-sdk" --profile "$profile" --output "$out/matrix"
