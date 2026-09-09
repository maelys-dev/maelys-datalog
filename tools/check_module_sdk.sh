#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Compile consumers copied OUTSIDE the tree, using only a fresh installed SDK.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$(cd "${1:-$root/build/cmake}" && pwd)"
scratch="$(mktemp -d "${TMPDIR:-/tmp}/maelys-sdk.XXXXXX")"
trap 'rm -rf -- "$scratch"' EXIT
prefix="$scratch/prefix"
cmake --install "$build" --prefix "$prefix"
cp "$root/tests/fixtures/public_api_consumer.c" "$scratch/"
cp "$root/tests/fixtures/sdk_header.c" "$scratch/"
cp "$root/tests/fixtures/opaque_handle.c" "$scratch/"
cp "$root/tests/test_maelys_datalog_modules.c" "$scratch/"
cp "$root/tests/test_maelys_datalog_compiler.c" "$scratch/"
cp "$root/tests/test_maelys_datalog_context.c" "$scratch/"
for mapping in filter:exact_match frontend:arrow_frontend backend:naive_backend; do
  cp "$root/sdk/examples/${mapping%:*}/src/extension.c" "$scratch/${mapping#*:}.c"
done
cd "$scratch"
# No accidental access to private headers through ambient search paths.
unset CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH LIBRARY_PATH
cc="${CC:-cc}"
cxx="${CXX:-c++}"
flags=(-std=c11 -Wall -Wextra -Werror -I"$prefix/include")
for header in datalog.h datalog_module.h datalog_program.h datalog_backend.h datalog_extension.h; do
  "$cc" "${flags[@]}" -DSDK_HEADER="\"maelys/$header\"" -fsyntax-only sdk_header.c
  "$cxx" -x c++ -std=c++17 -Wall -Wextra -Werror -I"$prefix/include" \
    -DSDK_HEADER="\"maelys/$header\"" -fsyntax-only sdk_header.c
done
for handle in policy session result program program_builder context; do
  for language in c c++; do
    compiler="$cc"; standard=c11
    if [[ "$language" == c++ ]]; then compiler="$cxx"; standard=c++17; fi
    if "$compiler" -x "$language" -std="$standard" -I"$prefix/include" \
        -DOPAQUE_HANDLE="maelys_datalog_${handle}_t" -fsyntax-only opaque_handle.c \
        > opaque.log 2>&1; then
      echo "FAIL: $handle layout exposed in $language" >&2; exit 1
    fi
    if ! grep -Eq 'incomplete|undefined' opaque.log; then
      cat opaque.log >&2; exit 1
    fi
  done
done
echo 'installed SDK headers: C11/C++17 PASS; six opaque layouts rejected in both languages'
for provider in exact_match arrow_frontend naive_backend; do
  "$cc" "${flags[@]}" -c "$provider.c" -o "$provider.o"
done
libdir="$prefix/lib"
if [[ ! -f "$libdir/libmaelys_datalog.a" ]]; then libdir="$prefix/lib64"; fi
# Test-only pipeline counters compile to nothing outside MAELYS_TESTING; the
# installed libraries must not carry their symbol.
for library in "$libdir/libmaelys_datalog.a" "$libdir"/libmaelys_datalog_shared.*; do
  if nm -g "$library" 2>/dev/null | grep -q maelys_datalog_pipeline_counts; then
    echo "FAIL: test instrumentation symbol in $library" >&2; exit 1
  fi
done
echo 'installed libraries: no test instrumentation symbols'
for role in frontend backend planner filter; do
  starter="$prefix/share/maelys-datalog/templates/$role"
  # A copied starter must keep its license without relying on the repository.
  cmp "$root/sdk/templates/$role/LICENSE" "$starter/LICENSE"
  grep -q '^MIT License$' "$starter/LICENSE"
  for source in include/extension.h src/extension.c tests/smoke.c CMakeLists.txt README.md; do
    grep -q 'SPDX-License-Identifier: MIT' "$starter/$source"
  done
done
for linkage in static shared; do
  if [[ "$linkage" == static ]]; then
    libs=("$libdir/libmaelys_datalog.a")
  else
    libs=(-L"$libdir" -lmaelys_datalog_shared -Wl,-rpath,"$libdir")
  fi
  "$cc" "${flags[@]}" public_api_consumer.c "${libs[@]}" -o facade
  ./facade
  "$cc" "${flags[@]}" -pthread test_maelys_datalog_modules.c exact_match.o \
    "${libs[@]}" -o modules
  ./modules
  "$cc" "${flags[@]}" test_maelys_datalog_compiler.c arrow_frontend.o naive_backend.o \
    "${libs[@]}" -o compiler
  ./compiler
  "$cc" "${flags[@]}" -pthread test_maelys_datalog_context.c arrow_frontend.o naive_backend.o \
    "${libs[@]}" -o context
  ./context
  for role in filter planner frontend backend bundle; do
    cp -R "$root/sdk/examples/$role" "$scratch/$role-$linkage"
    shared=OFF
    if [[ "$linkage" == shared ]]; then shared=ON; fi
    cmake -S "$scratch/$role-$linkage" -B "$scratch/$role-$linkage/build" \
      -DMAELYS_SDK_PREFIX="$prefix" -DMAELYS_SDK_SHARED="$shared"
    cmake --build "$scratch/$role-$linkage/build" --parallel 2
    ctest --test-dir "$scratch/$role-$linkage/build" --output-on-failure
  done
  for role in frontend backend planner filter; do
    cp -R "$prefix/share/maelys-datalog/templates/$role" "$scratch/starter-$role-$linkage"
    shared=OFF
    if [[ "$linkage" == shared ]]; then shared=ON; fi
    cmake -S "$scratch/starter-$role-$linkage" -B "$scratch/starter-$role-$linkage/build" \
      -DCMAKE_BUILD_TYPE=Release -DMAELYS_SDK_PREFIX="$prefix" -DMAELYS_SDK_SHARED="$shared"
    cmake --build "$scratch/starter-$role-$linkage/build" --parallel 2
    ctest --test-dir "$scratch/starter-$role-$linkage/build" --output-on-failure
  done
  echo "installed SDK external consumers and MIT starters: $linkage PASS"
done
