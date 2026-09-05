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
