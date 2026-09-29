#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Compile both consumers exclusively against their installed, matching SDK.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
runtime="${1:?native or wasm required}"
profile="${2:?small or large required}"
output="${3:?package output directory required}"
case "$profile" in small) large=OFF ;; large) large=ON ;; *) exit 2 ;; esac
case "$runtime" in native|wasm) ;; *) exit 2 ;; esac
mkdir -p "$output"
output="$(cd "$output" && pwd)"
scratch="$(mktemp -d "${TMPDIR:-/tmp}/maelys-js-consumer.XXXXXX")"
trap 'rm -rf -- "$scratch"' EXIT
# CMake preserves a compiler selected by an earlier toolchain. A fresh SDK
# prevents a release's pinned emcc from linking objects made by another emcc.
build="$scratch/build"
if [ "$runtime" = native ] && [ "$(uname -s)" = Darwin ]; then
  # Pin every engine and adapter object, including the final link. Do not inherit
  # the build host's OS version or a caller's newer deployment target.
  export MACOSX_DEPLOYMENT_TARGET="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["macosMinimum"])' "$root/tools/javascript-build.json")"
fi
if [ "$runtime" = wasm ]; then
  compiler_key="$(emcc --version | python3 -c 'import hashlib, sys; print(hashlib.sha256(sys.stdin.buffer.read()).hexdigest()[:16])')"
  export EM_CACHE="${EM_CACHE:-$root/build/emscripten-cache-$compiler_key}"
  emcmake cmake -S "$root" -B "$build" -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_INSTALL_INCLUDEDIR=include \
    -DMAELYS_DATALOG_PROFILE_LARGE="$large"
else
  cmake -S "$root" -B "$build" -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_INSTALL_INCLUDEDIR=include \
    -DMAELYS_DATALOG_PROFILE_LARGE="$large" -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_C_VISIBILITY_PRESET=hidden
fi
cmake --build "$build" --target maelys_datalog --parallel 2
cmake --install "$build" --prefix "$scratch/sdk" --component sdk
cmake --install "$build" --prefix "$scratch/sdk" --component sdk-static
cp "$root/bindings/javascript/native/"* "$scratch/"
mkdir -p "$output/src"
cp "$root/bindings/javascript/src/"* "$output/src/"
python3 - "$output" "$root" <<'PY'
from pathlib import Path
import json, sys
out, root = map(Path, sys.argv[1:])
for name, symbol in [('core', 'binding'), ('wasm-transport', 'wasmTransport')]:
    s = (out / 'src' / (name + '.cjs')).read_text()
    (out / 'src' / (name + '.mjs')).write_text(s.replace('module.exports = { '+symbol+' };', 'export { '+symbol+' };'))
p = json.loads((root / 'bindings/javascript/package.json').read_text())
p['version'] = (root / 'VERSION').read_text().strip()
(out / 'package.json').write_text(json.dumps(p, indent=2)+'\n')
for name in ['LICENSE', 'README.md']:
    source = root / 'bindings/javascript' / name
    if not source.exists(): source = root / name
    if source.exists(): (out / name).write_bytes(source.read_bytes())
PY
mkdir -p "$output/licenses/yyjson"
cp "$root/vendor/yyjson/LICENSE" "$output/licenses/yyjson/"
cd "$scratch"
unset CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH LIBRARY_PATH
flags=(-std=c11 -O2 -Wall -Wextra -Werror -I"$scratch/sdk/include")
if [ "$runtime" = wasm ]; then
  mkdir -p "$output/wasm/$profile"
  emcc "${flags[@]}" -MMD -MF consumer.d -c transport.c -o transport.o
  emcc -O2 transport.o "$scratch/sdk/lib/libmaelys_datalog.a" \
    -sEXPORTED_FUNCTIONS='["_malloc","_free","_maelys_js_create","_maelys_js_destroy","_maelys_js_call","_maelys_js_words","_maelys_js_word_count","_maelys_js_text","_maelys_js_scalar","_maelys_js_diagnostic"]' \
    -sEXPORTED_RUNTIME_METHODS='["ccall","UTF8ToString","HEAPU8"]' \
    -sALLOW_MEMORY_GROWTH=1 -sMODULARIZE=1 -sEXPORT_ES6=1 -sASSERTIONS=1 \
    -o "$output/wasm/$profile/engine.mjs"
else
  platform="$(node -p 'process.platform + "-" + process.arch')"
  node_include="${NODE_INCLUDE_DIR:-$(node -p 'require("node:path").resolve(process.execPath, "../../include/node")')}"
  if [ ! -f "$node_include/node_api.h" ]; then
    if [ -n "${NODE_INCLUDE_DIR:-}" ]; then
      echo "NODE_INCLUDE_DIR has no node_api.h: $node_include" >&2; exit 1
    fi
    # setup-node distributions need not carry headers. Fetch only the exact
    # runtime's headers during BUILD and verify the official checksum.
    node_version="$(node -p 'process.version')"
    header_name="node-${node_version}-headers.tar.gz"
    curl --fail --silent --show-error --location "https://nodejs.org/dist/$node_version/$header_name" -o "$header_name"
    curl --fail --silent --show-error --location "https://nodejs.org/dist/$node_version/SHASUMS256.txt" -o SHASUMS256.txt
    python3 - "$header_name" <<'PYHEADERS'
import hashlib, pathlib, sys, tarfile
name = sys.argv[1]
lines = pathlib.Path('SHASUMS256.txt').read_text().splitlines()
expected = [line.split()[0] for line in lines if line.split()[-1] == name]
assert len(expected) == 1 and hashlib.sha256(pathlib.Path(name).read_bytes()).hexdigest() == expected[0], 'Node header checksum mismatch'
with tarfile.open(name) as archive:
    archive.extractall('node-headers', filter='data')
PYHEADERS
    node_include="$scratch/node-headers/node-$node_version/include/node"
  fi
  mkdir -p "$output/prebuilds/$platform"
  cc "${flags[@]}" -fvisibility=hidden -fPIC -MMD -MF consumer.d -c transport.c -o transport.o
  cc "${flags[@]}" -D_POSIX_C_SOURCE=200809L -fvisibility=hidden -fPIC -I"$node_include" -c node.c -o node.o
  if [ "$(uname -s)" = Darwin ]; then
    link=(-bundle -undefined dynamic_lookup -Wl,-exported_symbols_list,node.exports)
  else
    link=(-shared -Wl,--version-script=node.map)
  fi
  cc "${link[@]}" -pthread transport.o node.o "$scratch/sdk/lib/libmaelys_datalog.a" \
    -o "$output/prebuilds/$platform/$profile.node"
  if [ "$(uname -s)" = Darwin ]; then
    strip -S -x "$output/prebuilds/$platform/$profile.node"
  else
    strip --strip-unneeded "$output/prebuilds/$platform/$profile.node"
  fi
  python3 "$root/tools/check_javascript_binary.py" "$output/prebuilds/$platform/$profile.node"
fi
if grep -F "$root/include/" consumer.d || grep -F "$root/src/" consumer.d; then
  echo 'binding reached engine checkout instead of installed SDK' >&2; exit 1
fi
echo "JavaScript binding $runtime/$profile built from installed SDK"
