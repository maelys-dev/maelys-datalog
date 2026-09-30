#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
profile="${1:?small or large}"
case "$profile" in small) large=OFF ;; large) large=ON ;; *) exit 2 ;; esac
build="$root/build/javascript-asan-$profile"
flags='-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1'
cmake -S "$root" -B "$build" -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=clang -DCMAKE_C_FLAGS="$flags" -DCMAKE_INSTALL_LIBDIR=lib \
  -DMAELYS_DATALOG_PROFILE_LARGE="$large"
cmake --build "$build" --target maelys_datalog maelys_datalog_shared --parallel 2
scratch="$(mktemp -d)"
trap 'rm -rf -- "$scratch"' EXIT
cmake --install "$build" --prefix "$scratch/sdk"
cp "$root/bindings/javascript/native/transport.c" "$root/bindings/javascript/native/transport.h" "$scratch/"
cp "$root/bindings/javascript/test/transport.c" "$scratch/test.c"
cd "$scratch"
unset CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH LIBRARY_PATH
clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1 \
  -I"$scratch/sdk/include" transport.c test.c "$scratch/sdk/lib/libmaelys_datalog.a" -o test
./test
