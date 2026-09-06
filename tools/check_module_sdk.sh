#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Compile a provider and its consumer with ONLY the installed public headers.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="${1:-$root/build/cmake}"
prefix="$build/module-sdk-install"
cmake --install "$build" --prefix "$prefix"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -I"$prefix/include" \
  -c "$root/examples/modules/exact_match.c" -o "$build/external-filter.o"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pthread -I"$prefix/include" \
  "$root/tests/test_maelys_datalog_modules.c" "$build/external-filter.o" \
  "$prefix/lib/libmaelys_datalog.a" -o "$build/installed-module-consumer"
"$build/installed-module-consumer"
for provider in arrow_frontend naive_backend; do
  "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -I"$prefix/include" \
    -c "$root/examples/modules/$provider.c" -o "$build/external-$provider.o"
done
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -I"$prefix/include" \
  "$root/tests/test_maelys_datalog_compiler.c" "$build/external-arrow_frontend.o" \
  "$build/external-naive_backend.o" "$prefix/lib/libmaelys_datalog.a" \
  -o "$build/installed-compiler-consumer"
"$build/installed-compiler-consumer"
