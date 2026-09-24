#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Compile consumers copied OUTSIDE the tree, using only a fresh installed SDK.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
scratch="$(mktemp -d "${TMPDIR:-/tmp}/maelys-sdk.XXXXXX")"
trap 'rm -rf -- "$scratch"' EXIT
linkages=(static shared)
if [[ "${1:-}" == --prefix ]]; then
  prefix="$(cd "${2:?--prefix requires an installed/extracted SDK}" && pwd)"
  if [[ "${3:-}" == --static-only && $# == 3 ]]; then
    linkages=(static)
  elif [[ $# != 2 ]]; then
    echo 'usage: check_module_sdk.sh --prefix PREFIX [--static-only]' >&2; exit 2
  fi
else
  if [[ $# -gt 1 ]]; then echo 'usage: check_module_sdk.sh [CMAKE_BUILD]' >&2; exit 2; fi
  build="$(cd "${1:-$root/build/cmake}" && pwd)"
  prefix="$scratch/prefix"
  cmake --install "$build" --prefix "$prefix"
fi
# include/maelys is the public source surface; no manually maintained second list.
(cd "$root/include" && find maelys -type f | LC_ALL=C sort) > "$scratch/expected-headers"
(cd "$prefix/include" && find . -type f | sed 's@^./@@' | LC_ALL=C sort) > "$scratch/actual-headers"
diff -u "$scratch/expected-headers" "$scratch/actual-headers"
if [[ -n "$(find "$prefix/include" -type l -print)" ]]; then
  echo 'FAIL: SDK include tree contains symlinks' >&2; exit 1
fi
cp "$root/tests/fixtures/public_api_consumer.c" "$scratch/"
cp "$root/tests/fixtures/sdk_header.c" "$scratch/"
cp "$root/tests/fixtures/cpp_fact_builders.cpp" "$scratch/"
cp "$root/tests/test_maelys_datalog_predicate_builders.c" "$scratch/"
cp "$root/tests/fixtures/opaque_handle.c" "$scratch/"
cp "$root/tests/fixtures/explanation_storage.c" "$scratch/"
cp "$root/tests/test_maelys_datalog_modules.c" "$scratch/"
cp "$root/tests/test_maelys_datalog_compiler.c" "$scratch/"
cp "$root/tests/test_maelys_datalog_context.c" "$scratch/"
cp "$root/tests/test_maelys_datalog_advanced.c" "$scratch/"
cp "$root/tests/test_maelys_datalog_window.c" "$scratch/"
cp "$root/tests/test_maelys_datalog_group_window.c" "$scratch/"
cp "$root/examples/multi_fact_window.c" "$scratch/"
for mapping in filter:exact_match frontend:arrow_frontend backend:naive_backend; do
  cp "$root/sdk/examples/${mapping%:*}/src/extension.c" "$scratch/${mapping#*:}.c"
done
cd "$scratch"
# No accidental access to private headers through ambient search paths.
unset CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH LIBRARY_PATH
cc="${CC:-cc}"
cxx="${CXX:-c++}"
flags=(-std=c11 -Wall -Wextra -Werror -I"$prefix/include")
for header_path in "$prefix/include/maelys/"*.h; do
  header="$(basename "$header_path")"
  "$cc" "${flags[@]}" -DSDK_HEADER="\"maelys/$header\"" -fsyntax-only sdk_header.c
  "$cxx" -x c++ -std=c++17 -Wall -Wextra -Werror -I"$prefix/include" \
    -DSDK_HEADER="\"maelys/$header\"" -fsyntax-only sdk_header.c
done
# The old aggregation surface and representative private includes must be absent,
# not just unused by successful consumers. Inventory equality above also catches
# a copied private file whose own dependencies would prevent it from compiling.
for header in maelys_datalog.h maelys_datalog_version.h src/core/maelys_datalog_types.h src/manifest/maelys_datalog_manifest.h common/maelys_errors.h; do
  for language in c c++; do
    compiler="$cc"; standard=c11
    if [[ "$language" == c++ ]]; then compiler="$cxx"; standard=c++17; fi
    if "$compiler" -x "$language" -std="$standard" -I"$prefix/include" \
        -DSDK_HEADER="\"$header\"" -fsyntax-only sdk_header.c > private.log 2>&1; then
      echo "FAIL: private header accepted: $header ($language)" >&2; exit 1
    fi
    if ! grep -Eq 'file not found|No such file' private.log; then
      cat private.log >&2; exit 1
    fi
  done
done
echo 'SDK boundary: public inventory matches; legacy/private includes rejected'
"$cxx" -std=c++17 -Wall -Wextra -Werror -pedantic-errors -I"$prefix/include" \
  -fsyntax-only cpp_fact_builders.cpp
"$cc" "${flags[@]}" -Wvla -pedantic-errors explanation_storage.c -o storage-c
./storage-c
"$cxx" -x c++ -std=c++17 -Wall -Wextra -Werror -Wvla -pedantic-errors \
  -I"$prefix/include" explanation_storage.c -o storage-cpp
./storage-cpp
echo 'installed SDK explanation storage: C11/C++17 static/local aligned arrays PASS'
for handle in domain_builder policy session result session_config input_edb prepared_explanation program program_builder context window group_window; do
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
echo 'installed SDK headers: C11/C++17 PASS; twelve opaque layouts rejected in both languages'
for provider in exact_match arrow_frontend naive_backend; do
  "$cc" "${flags[@]}" -c "$provider.c" -o "$provider.o"
done
libdir="$prefix/lib"
if [[ ! -f "$libdir/libmaelys_datalog.a" ]]; then libdir="$prefix/lib64"; fi
# Test-only pipeline counters compile to nothing outside MAELYS_TESTING; the
# installed libraries must not carry their symbol.
for library in "$libdir/libmaelys_datalog.a" "$libdir"/libmaelys_datalog_shared.*; do
  [[ -f "$library" ]] || continue
  nm -g "$library" > symbols.log 2>/dev/null
  if grep -Eq 'maelys_datalog_(pipeline_counts|base_lookup_counts)' symbols.log; then
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
for linkage in "${linkages[@]}"; do
  if [[ "$linkage" == static ]]; then
    libs=("$libdir/libmaelys_datalog.a")
  else
    libs=(-L"$libdir" -lmaelys_datalog_shared "-Wl,-rpath,$libdir")
  fi
  "$cc" "${flags[@]}" public_api_consumer.c "${libs[@]}" -o facade
  ./facade
  "$cc" "${flags[@]}" -UNDEBUG test_maelys_datalog_advanced.c "${libs[@]}" -o advanced
  ./advanced
  "$cc" "${flags[@]}" -UNDEBUG -Wvla -pedantic-errors \
    test_maelys_datalog_predicate_builders.c "${libs[@]}" -o declarations
  ./declarations
  "$cc" "${flags[@]}" -UNDEBUG test_maelys_datalog_window.c "${libs[@]}" -o window
  ./window
  "$cc" "${flags[@]}" -UNDEBUG test_maelys_datalog_group_window.c "${libs[@]}" -o group-window
  ./group-window
  "$cc" "${flags[@]}" multi_fact_window.c "${libs[@]}" -o group-example
  ./group-example
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
