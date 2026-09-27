#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# The product's gate on the exact commit being released. The socle renders
# this script into verify_command and runs it on every target runner before
# packaging (docs/conventions.md, "What the release verifies"); the operator
# runs it locally through scripts/release-gates.sh before a version is even
# written. TARGET is informational: the same checks hold for every target,
# wasm32 included, whose Ubuntu runner builds the native suite.
set -eu
cd "$(dirname "$0")/.."
target="${1:?usage: scripts/verify-release.sh TARGET}"
echo "==> make check ($target)"
make check
