#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Package an already-built native SDK through CMake's sole install inventory.
# Used by release packaging and the SMALL/LARGE archive gate alike.
set -euo pipefail
if [[ $# != 2 ]]; then
  echo 'usage: scripts/package-native-sdk.sh CMAKE_BUILD ARCHIVE' >&2; exit 2
fi
build="$(cd "$1" && pwd)"
archive="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"
scratch="$(mktemp -d "${TMPDIR:-/tmp}/maelys-package-sdk.XXXXXX")"
trap 'rm -rf -- "$scratch"' EXIT
for component in sdk sdk-static release-metadata; do
  cmake --install "$build" --prefix "$scratch/prefix" --component "$component"
done
# Normalize container metadata as well as compiler paths; never include host
# xattrs/AppleDouble. The default epoch belongs to the source commit, not the run.
root="$(cd "$(dirname "$0")/.." && pwd)"
epoch="${SOURCE_DATE_EPOCH:-$(git -C "$root" log -1 --format=%ct)}"
python3 "$root/tools/package_deterministic_tar.py" "$scratch/prefix" "$scratch/sdk.tar.gz" "$epoch"
# Do not touch a prior artifact if staging fails.
mv "$scratch/sdk.tar.gz" "$archive"
