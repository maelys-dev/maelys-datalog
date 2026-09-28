#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Run from the checkout, with a fresh absolute output directory outside git.
set -eu
out=${1:?absolute evidence directory required}
case "$out" in /*) ;; *) echo 'absolute output directory required' >&2; exit 2;; esac
mkdir -p "$out"
[ ! -e "$out/toolchain.txt" ] || { echo 'refusing to overwrite evidence' >&2; exit 2; }
{
    uname -a
    clang --version
    valgrind --version
    cmake --version
    git rev-parse HEAD
    git status --porcelain
} > "$out/toolchain.txt"
python3 - "$out/source-sha256.json" <<'PYCODE'
import hashlib, json, subprocess, sys
from pathlib import Path
paths=subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard','-z']).decode().split('\0')
Path(sys.argv[1]).write_text(json.dumps({p:hashlib.sha256(Path(p).read_bytes()).hexdigest() for p in sorted(set(paths)) if p and Path(p).is_file()},indent=2)+'\n')
PYCODE
printf '%s\n' '{"schema":2,"variants":["A","B","L"],"order":["A1","B1","L1","L2","B2","A2"],"transactions":8}' > "$out/experiment.json"
python3 -m unittest discover -s bench -p test_report_host_delta.py > "$out/reporter-tests.log" 2>&1
for profile in SMALL LARGE; do
    large=OFF
    [ "$profile" != LARGE ] || large=ON
    cmake -S . -B "$out/$profile/sdk-build" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
        -DCMAKE_C_COMPILER=clang -DMAELYS_DATALOG_PROFILE_LARGE="$large" \
        -DCMAKE_INSTALL_PREFIX="$out/$profile/sdk" > "$out/$profile-configure.log" 2>&1
    cmake --build "$out/$profile/sdk-build" --target maelys_datalog -j 4 > "$out/$profile-sdk-build.log" 2>&1
    cmake --install "$out/$profile/sdk-build" --component sdk > "$out/$profile-install.log" 2>&1
    cmake --install "$out/$profile/sdk-build" --component sdk-static >> "$out/$profile-install.log" 2>&1
    make -f bench/Makefile.host-delta host-delta OUT="$out/$profile/bin" SDK="$out/$profile/sdk" \
        DRIVER="$PWD" PROFILE="$profile" COUNT_FLAGS=-DMAELYS_BENCH_COUNT -j 4 > "$out/$profile-build.log" 2>&1
    "$out/$profile/bin/host-delta" check > "$out/$profile/check.csv"
    python3 bench/check_host_delta_mutations.py "$out/$profile/bin" --profile "$profile" > "$out/$profile/mutations.json"
    nm -u "$out/$profile/bin/backend.o" > "$out/$profile/backend-undefined.txt"
    objdump -dr "$out/$profile/bin/backend.o" > "$out/$profile/backend-disassembly.txt"
done
# No builds run during this counterbalanced collection. These are software
# counts in separate processes, never timing or hardware-counter measurements.
for profile in SMALL LARGE; do
    for label in A1 B1 L1 L2 B2 A2; do
        role=$(printf '%s' "$label" | cut -c1)
        for scope in engine caller; do
            dir="$out/$profile/$label-$scope"
            mkdir -p "$dir"
            valgrind --tool=callgrind --cache-sim=yes --branch-sim=no --collect-atstart=no \
                --error-exitcode=3 --callgrind-out-file="$dir/counts" \
                "$out/$profile/bin/host-delta" "$role" "$scope" > "$dir/receipts.csv" 2> "$dir/valgrind.log"
        done
    done
done
for profile in SMALL LARGE; do
    sh bench/observe_host_delta_memory.sh "$out/$profile/memory" "$out/$profile/sdk" "$profile"
done
python3 bench/report_host_delta.py "$out"
