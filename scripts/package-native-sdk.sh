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
# Do not ship AppleDouble entries for host extended attributes. BSD tar would
# silently consume them on local extraction, while other platforms see files.
# GNU tar ignores this environment variable. Preserve actual SDK file contents.
# Do not touch a prior artifact if staging fails.
COPYFILE_DISABLE=1 tar -czf "$scratch/sdk.tar.gz" -C "$scratch/prefix" .
mv "$scratch/sdk.tar.gz" "$archive"
