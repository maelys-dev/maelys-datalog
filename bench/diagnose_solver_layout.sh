#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
set -euo pipefail
test "$#" = 4 || { echo 'usage: diagnose_solver_layout.sh BASE ORIGINAL HEAD NEW_ABSOLUTE_OUTPUT' >&2; exit 2; }
test "$(uname -s)/$(uname -m)" = Linux/x86_64 || { echo 'Native Linux x86_64 diagnostic required' >&2; exit 2; }
for tool in clang make valgrind callgrind_annotate nm objdump python3; do command -v "$tool" >/dev/null; done
driver=$(cd "$(dirname "$0")/.." && pwd)
resolve() { git -C "$driver" rev-parse --verify --end-of-options "$1^{commit}"; }
base=$(resolve "$1")
original=$(resolve "$2")
head=$(resolve "$3")
output=$4
case "$output" in /*) ;; *) echo 'output must be absolute' >&2; exit 2;; esac
test ! -e "$output" || { echo 'output already exists' >&2; exit 2; }
mkdir -p "$output"
output=$(cd "$output" && pwd)
exec > >(tee "$output/commands.log") 2>&1
set -x
uname -a
clang --version
valgrind --version
cat /proc/cpuinfo
workspace=$(mktemp -d)
# Archives and symbols stay available until the diagnostic finishes.
trap 'rm -rf "$workspace"' EXIT
printf 'base=%s\noriginal=%s\nhead=%s\npads=0,16,64,256\nprofile=LARGE\ncase=solver_size_pure/2048\npriority=unchanged\naffinity=unchanged\n' "$base" "$original" "$head" > "$output/revisions.txt"
sha256sum "$driver"/bench/{bench_datalog.c,bench_solver_diagnostic.c,Makefile.diagnostic,diagnose_solver_layout.sh,report_solver_layout.py} > "$output/harness.sha256"
unset MAKEFLAGS MFLAGS
# All builds finish before any timed pass. Link variants reuse exact objects.
for role in A B C; do
  revision=$base
  if test "$role" = B; then revision=$original; fi
  if test "$role" = C; then revision=$head; fi
  test "$(git -C "$driver" rev-parse "$revision:bench/bench_datalog.c")" = \
       "$(git -C "$driver" hash-object "$driver/bench/bench_datalog.c")"
  mkdir "$workspace/$role"
  git -C "$driver" archive "$revision" | tar -x -C "$workspace/$role"
  make -j1 -C "$workspace/$role" -f "$driver/bench/Makefile.diagnostic" diagnostic \
    DRIVER="$driver" OUT="$workspace/bin-$role" REVISION="$revision" PROFILE=LARGE
  for pad in 0 16 64 256; do
    binary="$workspace/bin-$role/diagnostic-$pad"
    sha256sum "$binary" >> "$output/binaries.sha256"
    nm -n -S "$binary" > "$output/$role-$pad.nm"
    objdump -d "$binary" | gzip > "$output/$role-$pad.asm.gz"
  done
done
# Two A/A pairs, then two interleaved rounds of all PREDECLARED layouts.
# No variant or sample may be omitted because of its result.
for role in A B C; do
  for pass in 1 2 3 4; do
    "$workspace/bin-$role/diagnostic-0" time "$output/$role-aa-$pass.csv"
  done
done
for pass in 1 2; do
  for pad in 0 16 64 256; do
    for role in A B C; do
      "$workspace/bin-$role/diagnostic-$pad" time "$output/$role-$pad-$pass.csv"
    done
  done
done
# Repeat deterministic counts in separate processes. Collect at instruction
# granularity, excluding preparation and clocks. Never time under Valgrind.
for role in A B C; do
  for pad in 0 16 64 256; do
    for pass in 1 2; do
      prefix="$output/ir-$role-$pad-$pass"
      valgrind --tool=callgrind --collect-atstart=no --dump-instr=yes --error-exitcode=3 \
        --callgrind-out-file="$prefix.out" "$workspace/bin-$role/diagnostic-$pad" count "$prefix.csv" \
        2> "$prefix.log"
      callgrind_annotate --auto=no --threshold=100 "$prefix.out" > "$prefix.functions.txt"
    done
  done
done
python3 "$driver/bench/report_solver_layout.py" "$output" > "$output/diagnostic.incomplete.md"
mv "$output/diagnostic.incomplete.md" "$output/diagnostic.md"
