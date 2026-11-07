#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
compiler=${1:-${CC:-cc}}
cxx=${2:-${CXX:-c++}}
scratch=$(mktemp -d "${TMPDIR:-/tmp}/maelys-fact-builders.XXXXXX")
trap 'rm -f "$scratch/diagnostic" "$scratch/storage" "$scratch/booleans"; rmdir "$scratch"' EXIT
trap 'exit 1' HUP INT TERM
fixture="$root/tests/fixtures/c11_fact_builders.c"
# C23 changes the type of true/false, which the builders classify by type, so
# both standards are checked on purpose rather than by the compiler's default.
standards=c11
if printf 'int maelys_probe;\n' | "$compiler" -x c -std=c23 -fsyntax-only - 2>/dev/null; then
    standards="c11 c23"
else
    echo "C23 fact-builder legs SKIPPED: $compiler does not accept -std=c23"
fi
for standard in $standards; do
for builder in TEST_FACT TEST_QUERY TEST_BATCH; do
"$compiler" -std="$standard" -Wall -Wextra -Werror -Wvla -pedantic-errors -I"$root/include" -D"$builder" -fsyntax-only "$fixture"
for rejected in FLOAT DOUBLE POINTER STRUCT TOO_MANY; do
    if "$compiler" -std="$standard" -Wall -Wextra -Werror -pedantic-errors -I"$root/include" \
        -D"$builder" -D"REJECT_$rejected" -fsyntax-only "$fixture" >"$scratch/diagnostic" 2>&1; then
        echo "error: $standard $builder unexpectedly accepted $rejected" >&2
        exit 1
    fi
    # The same fixture must compile above; an unrelated infrastructure failure
    # must not count as a successful negative compilation gate.
    if ! grep -E 'error:|fatal error:' "$scratch/diagnostic" >/dev/null; then
        cat "$scratch/diagnostic" >&2
        echo "error: no compiler diagnostic for $rejected" >&2
        exit 1
    fi
done
done
echo "$standard single/batch/query builders: strict consumers and fifteen rejected inputs PASS"
"$compiler" -std="$standard" -Wall -Wextra -Werror -Wvla -pedantic-errors -I"$root/include" \
    "$root/tests/fixtures/fact_builder_booleans.c" -o "$scratch/booleans"
"$scratch/booleans"
done
if [ "$cxx" != --c-only ]; then
    "$cxx" -std=c++17 -Wall -Wextra -Werror -pedantic-errors -I"$root/include" \
        -fsyntax-only "$root/tests/fixtures/cpp_fact_builders.cpp"
    echo "C++17 fact-builder header compatibility PASS"
fi
for language in c c++; do
    test_compiler=$compiler; standard=c11
    if [ "$language" = c++ ]; then
        [ "$cxx" != --c-only ] || continue
        test_compiler=$cxx; standard=c++17
    fi
    "$test_compiler" -x "$language" -std="$standard" -Wall -Wextra -Werror -Wvla \
        -pedantic-errors -I"$root/include" "$root/tests/fixtures/explanation_storage.c" -o "$scratch/storage"
    "$scratch/storage"
    for rejected in RUNTIME ZERO NEGATIVE FLOAT; do
        if "$test_compiler" -x "$language" -std="$standard" -Wall -Wextra -Werror -Wvla \
            -pedantic-errors -I"$root/include" -D"REJECT_$rejected" -fsyntax-only \
            "$root/tests/fixtures/explanation_storage.c" >"$scratch/diagnostic" 2>&1; then
            echo "error: $standard storage accepted $rejected" >&2; exit 1
        fi
        if ! grep -E 'error:|fatal error:' "$scratch/diagnostic" >/dev/null; then
            cat "$scratch/diagnostic" >&2; exit 1
        fi
    done
    echo "$standard explanation storage: alignment, static/local arrays and rejected non-constant/invalid capacities PASS"
done
