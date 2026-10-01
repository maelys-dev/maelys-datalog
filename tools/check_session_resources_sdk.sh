#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Installed public headers/libraries only; no source-tree internal includes.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$(cd "${1:?CMake build required}" && pwd)"
scratch="$(mktemp -d "${TMPDIR:-/tmp}/maelys-resources-sdk.XXXXXX")"
trap 'rm -rf -- "$scratch"' EXIT
cmake --build "$build" --target maelys_datalog maelys_datalog_shared --parallel 2
cmake --install "$build" --prefix "$scratch/sdk"
unset CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH LIBRARY_PATH
for linkage in static shared; do
  library="$scratch/sdk/lib/libmaelys_datalog.a"
  if [[ "$linkage" == shared ]]; then
    if [[ "$(uname -s)" == Darwin ]]; then
      library="$scratch/sdk/lib/libmaelys_datalog_shared.dylib"
    else
      library="$scratch/sdk/lib/libmaelys_datalog_shared.so"
    fi
  fi
  "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -UNDEBUG -I"$scratch/sdk/include" \
    "$root/tests/test_maelys_datalog_session_resources.c" "$library" \
    -Wl,-rpath,"$scratch/sdk/lib" -o "$scratch/$linkage"
  "$scratch/$linkage"
done
cat > "$scratch/header.cpp" <<'EOF'
#include <maelys/datalog_resources.h>
#include <maelys/datalog_backend.h>
#include <type_traits>
static_assert(MAELYS_DATALOG_BACKEND_ABI_VERSION == 5u, "ABI 5 retained");
static_assert(std::is_same<decltype(maelys_datalog_backend_t::solve),
    decltype(maelys_datalog_backend_v6_t::solve)>::value, "solve signature");
static_assert(std::is_same<decltype(maelys_datalog_backend_t::commit),
    decltype(maelys_datalog_backend_v6_t::commit)>::value, "commit signature");
int main() {
    maelys_datalog_session_resource_request_t request = MAELYS_DATALOG_RESOURCE_REQUEST_INIT;
    maelys_datalog_session_resources_t resources = MAELYS_DATALOG_RESOURCES_INIT;
    maelys_datalog_session_storage_plan_t plan = MAELYS_DATALOG_SESSION_PLAN_INIT;
    return request.contract_version != resources.contract_version || plan.reserved;
}
EOF
"${CXX:-c++}" -std=c++17 -pedantic -Wall -Wextra -Werror -I"$scratch/sdk/include" \
  "$scratch/header.cpp" -o "$scratch/header"
"$scratch/header"
echo 'Installed resource API: C11 static/shared lifecycle and C++17 declarations PASS'
