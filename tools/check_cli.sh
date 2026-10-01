#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Build this application outside the source tree against an installed SDK.
set -eu

if [ "$#" -ne 1 ] || [ -z "${MAELYS_DEPENDENCIES_DIR:-}" ]; then
    echo "usage: MAELYS_DEPENDENCIES_DIR=DIR bash tools/check_cli.sh BUILD_DIR" >&2
    exit 1
fi
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
case "$1" in
    /*) build=$1 ;;
    *) build=$root/$1 ;;
esac
cmake_build=$build/installed-cli/cmake
prefix=$build/installed-cli/prefix
output=$build/installed-cli/maelys-datalog
large=OFF
if [ "${PROFILE:-SMALL}" = LARGE ]; then large=ON; fi

cmake -S "$root" -B "$cmake_build" -DBUILD_TESTING=OFF \
    -DMAELYS_DATALOG_PROFILE_LARGE="$large" \
    -DCMAKE_INSTALL_PREFIX="$prefix"
cmake --build "$cmake_build" --target maelys_datalog --parallel 4
cmake --install "$cmake_build" --component sdk-static
cmake --install "$cmake_build" --component sdk

cc=${CC:-cc}
"$cc" -std=c11 -Wall -Wextra -Werror \
    -I"$MAELYS_DEPENDENCIES_DIR/maelys-cli/include" \
    -I"$MAELYS_DEPENDENCIES_DIR/maelys-json/include" \
    -I"$prefix/include" -I"$build/generated" \
    -DDATALOG_CLI_VERSION="\"$(cat "$root/VERSION")\"" \
    "$root/cli/main.c" "$root/cli/domain.c" "$root/cli/facts.c" \
    "$build/generated/datalog_schemas.c" \
    "$prefix/lib/libmaelys_datalog.a" \
    "$build/deps/maelys-cli/lib/libmaelys_cli.a" \
    "$build/deps/maelys-json/lib/libmaelys-json.a" -o "$output"

"$output" check --domain "$root/cli/tests/fixtures/rbac.domain.json" \
    "$root/cli/tests/fixtures/rbac.dl" --format json --compact >/dev/null
"$output" solve --domain "$root/cli/tests/fixtures/rbac.domain.json" \
    --facts "$root/cli/tests/fixtures/rbac.facts.dl" \
    "$root/cli/tests/fixtures/rbac.dl" --format json --compact >/dev/null
echo "installed SDK CLI: PASS ($large)"
