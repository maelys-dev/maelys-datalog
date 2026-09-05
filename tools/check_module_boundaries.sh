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
