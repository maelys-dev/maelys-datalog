#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
out=${1:?absolute output directory}
sdk=${2:?installed SDK directory}
profile=${3:?SMALL or LARGE}
mkdir -p "$out"
# Replace collection macros only in an ephemeral driver, leaving measured code
# and the production engine untouched. All explicit host primitives are observed.
python3 - "$out/driver.c" <<'PY'
from pathlib import Path
import sys
text=Path('bench/host_delta.c').read_text()
text=text.replace('#define COLLECT() ((void)0)', '#define COLLECT() delta_memory_collect()')
text=text.replace('#define DUMP(s) ((void)(s))', '#define DUMP(s) delta_memory_dump(s)')
Path(sys.argv[1]).write_text(text)
PY
clang -O2 -g -c bench/host_delta_memory.c -o "$out/memory.o"
make -f bench/Makefile.host-delta host-delta OUT="$out/bin" SDK="$sdk" DRIVER="$PWD" PROFILE="$profile" \
    HOST_DELTA_DRIVER="$out/driver.c" EXTRA_OBJECTS="$out/memory.o" \
    EXTRA_FLAGS="-I$PWD/bench -include $PWD/bench/host_delta_memory.h" -j 4 > "$out/build.log" 2>&1
for role in A B L; do
    for scope in engine caller; do
        "$out/bin/host-delta" "$role" "$scope" > "$out/$role-$scope.csv" 2> "$out/$role-$scope-bytes.csv"
    done
done
