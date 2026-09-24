#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Build an Emscripten SDK, then compile the consumer OUTSIDE the source tree.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
profile="${1:?small or large required}"
output="${2:?output directory required}"
case "$profile" in small) large=OFF ;; large) large=ON ;; *) exit 2 ;; esac
mkdir -p "$output"
output="$(cd "$output" && pwd)"
export EM_CACHE="${EM_CACHE:-$root/build/emscripten-cache}"
build="$root/build/wasm-sdk-$profile"
# Target archive and headers must be installed together; never use host libs.
emcmake cmake -S "$root" -B "$build" -DBUILD_TESTING=OFF \
  -DCMAKE_BUILD_TYPE=Release -DMAELYS_DATALOG_PROFILE_LARGE="$large"
cmake --build "$build" --target maelys_datalog maelys_datalog_shared --parallel 2
scratch="$(mktemp -d "${TMPDIR:-/tmp}/maelys-wasm-consumer.XXXXXX")"
trap 'rm -rf -- "$scratch"' EXIT
prefix="$scratch/sdk"
cmake --install "$build" --prefix "$prefix"
cp "$root/bindings/wasm/maelys_datalog_wasm.c" "$root/bindings/wasm/maelys_datalog_wasm.h" \
   "$root/bindings/wasm/maelys_playground.js" "$root/bindings/wasm/maelys_playground.d.ts" \
   "$root/bindings/wasm/exports.json" "$scratch/"
cd "$scratch"
unset CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH LIBRARY_PATH
flags=(-std=c11 -O2 -Wall -Wextra -Werror -I"$prefix/include")
emcc "${flags[@]}" -MMD -MF consumer.d -c maelys_datalog_wasm.c -o consumer.o
# Compilation includes must come from the installed SDK, never the checkout.
if grep -F "$root/" consumer.d; then echo 'consumer reached engine checkout' >&2; exit 1; fi
# Negative controls: a private include and an incompatible API must fail.
printf '#include <src/core/maelys_datalog_types.h>\n' > private.c
if emcc "${flags[@]}" -fsyntax-only private.c > private.log 2>&1; then
  echo 'private header unexpectedly available' >&2; exit 1
fi
grep -q 'file not found' private.log
for version in 1 3; do
  cp "$prefix/include/maelys/datalog.h" original.h
  python3 - "$prefix/include/maelys/datalog.h" "$version" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]); s=p.read_text()
assert '#define MAELYS_DATALOG_PUBLIC_API_VERSION 2u' in s
p.write_text(s.replace('#define MAELYS_DATALOG_PUBLIC_API_VERSION 2u',
                       '#define MAELYS_DATALOG_PUBLIC_API_VERSION '+sys.argv[2]+'u'))
PY
  if emcc "${flags[@]}" -fsyntax-only maelys_datalog_wasm.c > version.log 2>&1; then
    echo 'incompatible API admitted' >&2; exit 1
  fi
  grep -q 'Review consumer API changes' version.log
  mv original.h "$prefix/include/maelys/datalog.h"
done
# Compile the same SDK-only oracle as a separate executable; it does not use
# the JavaScript encoder or decoder and compares canonical text independently.
mkdir -p bindings/wasm
cp maelys_datalog_wasm.h bindings/wasm/
cp "$root/tests/test_maelys_datalog_wasm_builder.c" oracle.c
emcc "${flags[@]}" -UNDEBUG -I. oracle.c consumer.o "$prefix/lib/libmaelys_datalog.a" \
  -sALLOW_MEMORY_GROWTH=1 -sASSERTIONS=1 -sENVIRONMENT=node -o oracle.cjs
"${NODE:-node}" oracle.cjs
emcc -O2 consumer.o "$prefix/lib/libmaelys_datalog.a" \
  -sEXPORTED_FUNCTIONS=@exports.json \
  -sEXPORTED_RUNTIME_METHODS='["ccall","UTF8ToString","HEAPU8"]' \
  -sALLOW_MEMORY_GROWTH=1 -sMODULARIZE=1 -sEXPORT_NAME=MaelysDatalogDynamic \
  -sASSERTIONS=1 \
  -o maelys_datalog_dynamic.js
cp maelys_datalog_dynamic.js maelys_datalog_dynamic.wasm maelys_playground.js \
   maelys_playground.d.ts exports.json "$output/"
echo "installed Emscripten SDK consumer: $profile PASS"
