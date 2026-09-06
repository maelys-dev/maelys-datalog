#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
set -euo pipefail
cd "$(dirname "$0")/.."
# Providers and the public SDK must not include private engine headers.
if grep -Enr '#[[:space:]]*include[[:space:]]*[<"](src/|common/|include/|\.\./)' \
    include/maelys modules/standard examples/modules; then
  echo "error: module SDK boundary includes private headers" >&2
  exit 1
fi
# Test-only pipeline instrumentation is private to the engine sources; the
# public SDK never declares or includes it.
if grep -Enr 'pipeline_counts|COUNT_PIPELINE|pipeline_testing' include; then
  echo "error: pipeline test instrumentation leaks into the public SDK" >&2
  exit 1
fi
