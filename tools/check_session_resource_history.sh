#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Reproducible public SDK compatibility boundary, never a private-source fetch.
set -euo pipefail
build="$(cd "${1:?current CMake build}" && pwd)"
profile="${2:?SMALL or LARGE}"
out="${3:?new absolute evidence directory}"
case "$profile" in SMALL) large=OFF;; LARGE) large=ON;; *) exit 2;; esac
case "$out" in /*) ;; *) echo 'absolute output required' >&2; exit 2;; esac
test ! -e "$out" || { echo 'output already exists' >&2; exit 2; }
root="$(cd "$(dirname "$0")/.." && pwd)"
# The frozen contract-only main preceding ABI 6: old caller/provider headers AND
# old host implementation, not new headers relabelled as an old SDK.
base=9338993f5eb82e928ebec99c385bef3df546c67f
mkdir -p "$out/base-source"
git -C "$root" archive "$base" | tar -x -C "$out/base-source"
printf 'base=%s\nprofile=%s\n' "$base" "$profile" > "$out/revisions.txt"
git -C "$root" rev-parse HEAD >> "$out/revisions.txt"
git -C "$root" diff --stat >> "$out/revisions.txt"
cmake -S "$out/base-source" -B "$out/base-build" -DBUILD_TESTING=OFF \
  -DCMAKE_BUILD_TYPE=Release -DMAELYS_DATALOG_PROFILE_LARGE="$large" \
  -DCMAKE_INSTALL_PREFIX="$out/old-sdk" > "$out/base-build.log" 2>&1
cmake --build "$out/base-build" --target maelys_datalog maelys_datalog_shared --parallel 2 >> "$out/base-build.log" 2>&1
cmake --install "$out/base-build" >> "$out/base-build.log" 2>&1
cmake --install "$build" --prefix "$out/new-sdk" > "$out/new-install.log" 2>&1
python3 "$root/tools/check_session_resource_matrix.py" --old "$out/old-sdk" \
  --new "$out/new-sdk" --output "$out/matrix"
