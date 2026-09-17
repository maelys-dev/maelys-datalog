#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
compiler=${1:-${CC:-cc}}
cxx=${2:-${CXX:-c++}}
scratch=$(mktemp -d "${TMPDIR:-/tmp}/maelys-fact-builders.XXXXXX")
trap 'rm -f "$scratch/diagnostic"; rmdir "$scratch"' EXIT
trap 'exit 1' HUP INT TERM
fixture="$root/tests/fixtures/c11_fact_builders.c"
for builder in TEST_FACT TEST_QUERY TEST_BATCH; do
"$compiler" -std=c11 -Wall -Wextra -Werror -Wvla -pedantic-errors -I"$root/include" -D"$builder" -fsyntax-only "$fixture"
for rejected in FLOAT DOUBLE POINTER STRUCT TOO_MANY; do
    if "$compiler" -std=c11 -Wall -Wextra -Werror -pedantic-errors -I"$root/include" \
        -D"$builder" -D"REJECT_$rejected" -fsyntax-only "$fixture" >"$scratch/diagnostic" 2>&1; then
        echo "error: C11 $builder unexpectedly accepted $rejected" >&2
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
if [ "$cxx" != --c-only ]; then
    "$cxx" -std=c++17 -Wall -Wextra -Werror -pedantic-errors -I"$root/include" \
        -fsyntax-only "$root/tests/fixtures/cpp_fact_builders.cpp"
    echo "C++17 fact-builder header compatibility PASS"
fi
echo "C11 single/batch/query builders: strict C11 consumers and fifteen rejected inputs PASS"
