#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# The two local gates of a release, run before `maelys-release cut` writes
# anything (RELEASING.md):
#   1. scripts/verify-release.sh on this machine — the compiler of the
#      developer, clang;
#   2. make check CC=gcc in a pinned Ubuntu container — the second compiler,
#      on a disposable copy of the tree, before any tag exists.
# The socle's cut carries the git/gh choreography (signed bump commit, pull
# request, check-run wait, signed tag on the merge commit); these gates are
# what the socle cannot know about this product.
#
# Usage: scripts/release-gates.sh [--skip-container]
#   --skip-container  skips gate 2. Never the default; the script says so.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"
skip_container=0
[ "${1:-}" = "--skip-container" ] && skip_container=1

echo "==> gate 1: scripts/verify-release.sh (local)"
make clean >/dev/null
bash scripts/verify-release.sh local

if [ "$skip_container" -eq 1 ]; then
  echo "WARNING: --skip-container — the GCC/Linux gate is SKIPPED. Never the default." >&2
elif ! command -v docker >/dev/null 2>&1; then
  echo "docker not found — required for the GCC/Linux gate (docs/release-engineering.md)." >&2
  echo "Start Docker, or rerun with --skip-container if you accept the risk." >&2
  exit 1
else
  # Pinned image, disposable copy, never an in-place build on the read-only
  # mount. build-essential suffices: the engine has no third-party runtime
  # dependency (D1, yyjson is vendored).
  echo "==> gate 2: make check CC=gcc in a Linux container"
  docker run --rm --platform linux/arm64 -v "$root:/source:ro" ubuntu:24.04 bash -c '
    set -e
    apt-get update -qq && apt-get install -y -qq --no-install-recommends build-essential
    cp -a /source /work && cd /work && rm -rf build
    make check CC=gcc
  '
fi
echo "==> gates passed; next: maelys-release cut . X.Y.Z"
