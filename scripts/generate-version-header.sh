#!/usr/bin/env bash
#
# Single source of truth for the package version: the VERSION file. This
# script derives include/maelys_datalog_version.h's version macros from it,
# so nothing ever hand-edits both files. maelys_datalog_version.h stays a
# normal committed header (so `#include`-ing it works from a plain checkout,
# with no build step), but `make check-version-header` verifies it was
# produced by this exact script — any manual edit that lets the two drift is
# caught.
#
# Ported from mcp-runtime's scripts/generate-version-header.sh (see
# docs/release-engineering.md, invariant 1). Deviation from that model: the
# version here is pre-release-capable (X.Y.Z or X.Y.Z-alpha.N), so this
# script also emits MAELYS_DATALOG_VERSION_PRERELEASE.
#
# Usage: scripts/generate-version-header.sh [output-path]
#   regenerates include/maelys_datalog_version.h from VERSION
#   an optional output-path overrides the destination (used by
#   `make check-version-header` to render into a scratch file for
#   comparison, without touching the committed header).
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

version="$(cat VERSION)"
if ! [[ "$version" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)(-(.+))?$ ]]; then
  echo "VERSION must be SemVer (X.Y.Z or X.Y.Z-alpha.N), got: $version" >&2
  exit 1
fi
major="${BASH_REMATCH[1]}"
minor="${BASH_REMATCH[2]}"
patch="${BASH_REMATCH[3]}"
prerelease="${BASH_REMATCH[5]:-}"

target="${1:-include/maelys_datalog_version.h}"
generated="$(cat <<HEADER
/*
 * GENERATED FILE — do not edit by hand.
 *
 * Generated from VERSION by scripts/generate-version-header.sh. Edit
 * VERSION and re-run that script to change these values; \`make
 * check-version-header\` fails the build if this file drifts from VERSION.
 */
#ifndef MAELYS_DATALOG_VERSION_H
#define MAELYS_DATALOG_VERSION_H

#define MAELYS_DATALOG_VERSION_STRING "${version}"
#define MAELYS_DATALOG_VERSION_MAJOR ${major}
#define MAELYS_DATALOG_VERSION_MINOR ${minor}
#define MAELYS_DATALOG_VERSION_PATCH ${patch}
#define MAELYS_DATALOG_VERSION_PRERELEASE "${prerelease}"

#endif /* MAELYS_DATALOG_VERSION_H */
HEADER
)"

printf '%s\n' "$generated" > "$target"
echo "wrote $target for version $version"
