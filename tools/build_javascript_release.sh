#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Called with the pinned release Node already first in PATH.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"
runtime="${1:?native or wasm}"
package="${2:?staging directory}"
for profile in small large; do
  bash tools/build_javascript_binding.sh "$runtime" "$profile" "$package"
  if [ "$runtime" = native ]; then test_runtime=node; else test_runtime=wasm; fi
  MAELYS_JS_PACKAGE="$package" MAELYS_JS_RUNTIMES="$test_runtime" MAELYS_PROFILE="$profile" \
    node --test bindings/javascript/test/contract.mjs
done
if [ "$runtime" = native ]; then
  MAELYS_JS_PACKAGE="$package" node bindings/javascript/test/native-workers.mjs
  MAELYS_JS_PACKAGE="$package" node --test bindings/javascript/test/native-loading.mjs
fi
