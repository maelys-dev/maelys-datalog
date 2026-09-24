#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
set -euo pipefail
test "$#" = 3 || { echo "usage: compare_revisions.sh BASE HEAD NEW_ABSOLUTE_OUTPUT" >&2; exit 2; }
test "$(uname -s)" = Linux || { echo "Linux measurement required" >&2; exit 2; }
driver=$(cd "$(dirname "$0")/.." && pwd)
repo=$(git -C "$driver" rev-parse --show-toplevel)
resolve() {
  local ref=$1
  test -n "$ref" || { echo "empty revision" >&2; return 2; }
  git -C "$repo" rev-parse --verify --end-of-options "$ref^{commit}" 2>/dev/null ||
    git -C "$repo" rev-parse --verify --end-of-options "refs/remotes/origin/$ref^{commit}" 2>/dev/null ||
    { printf 'Unknown commit, branch or tag: %s\n' "$ref" >&2; return 2; }
}
base=$(resolve "$1")
candidate=$(resolve "$2")
output=$3
case "$output" in /*) ;; *) echo "output must be absolute" >&2; exit 2;; esac
test ! -e "$output" || { echo "output already exists: $output" >&2; exit 2; }
mkdir "$output"
output=$(cd "$output" && pwd)
exec > >(tee "$output/commands.log") 2>&1
set -x
uname -a
clang --version
getconf _NPROCESSORS_ONLN
if test -r /proc/cpuinfo; then cat /proc/cpuinfo; fi
git -C "$repo" rev-parse HEAD
printf 'base=%s\nhead=%s\noptimization=O2\npriority=unchanged\naffinity=unchanged\ncase_selection=none\n' "$base" "$candidate"
# Same solver harness on both revisions, including the trusted driver's copy.
for revision in "$base" "$candidate"; do
  test "$(git -C "$repo" rev-parse "$revision:bench/bench_datalog.c")" = \
    "$(git -C "$repo" hash-object "$driver/bench/bench_datalog.c")" ||
    { echo "solver harness differs: $revision; compare identical harnesses" >&2; exit 2; }
done
workspace=$(mktemp -d "${TMPDIR:-/tmp}/datalog-compare.XXXXXX")
printf '%s\n' "$workspace" > "$output/workspace.txt"
mkdir "$workspace/A" "$workspace/B"
git -C "$repo" archive "$base" | tar -x -C "$workspace/A"
git -C "$repo" archive "$candidate" | tar -x -C "$workspace/B"
python3 "$driver/bench/compare_runs.py" metadata "$output" "$base" "$candidate" "$repo"
explanations=0
if grep -q 'maelys_datalog_session_config_set_explanation_workspace(' "$workspace/B/include/maelys/datalog.h"; then
  explanations=1
fi
printf '%s\n' "$explanations" > "$output/explanations-enabled.txt"
unset MAKEFLAGS MFLAGS
session_diagnostics=${SESSION_DIAGNOSTICS:-0}
case "$session_diagnostics" in 0|1) ;; *) echo 'SESSION_DIAGNOSTICS must be 0 or 1' >&2; exit 2;; esac
printf '%s\n' "$session_diagnostics" > "$output/session-diagnostics-enabled.txt"
# No build runs beside a measurement. Each engine object is compiled once
# per revision/profile, reused by the solver and public-input harnesses.
for role in A B; do
  revision=$base
  if test "$role" = B; then revision=$candidate; fi
  build_explanations=0
  if test "$role" = B; then build_explanations=$explanations; fi
  for profile in SMALL LARGE; do
    make -j1 -C "$workspace/$role" -f "$driver/bench/Makefile.compare" \
      DRIVER="$driver" OUT="$workspace/bin-$role-$profile" REVISION="$revision" PROFILE="$profile" \
      EXPLANATIONS="$build_explanations" SESSION_DIAGNOSTICS="$session_diagnostics"
  done
done
if test "${SESSION_FULL_PROOF:-0}" = 1; then
for role in A B; do
  revision=$base
  if test "$role" = B; then revision=$candidate; fi
  for profile in SMALL LARGE; do
    make -j1 -C "$workspace/$role" -f "$driver/bench/Makefile.sessions-proof" proof \
      DRIVER="$driver" OUT="$workspace/bin-$role-$profile" REVISION="$revision" PROFILE="$profile"
  done
done
fi
run_pass() {
  local profile=$1 role=$2 name=$3
  MAELYS_BENCH_SAMPLES=1000 "$workspace/bin-$role-$profile/solver" \
    "$output/$profile-solver-$name.csv" "$output/$profile-solver-$name.json"
  "$workspace/bin-$role-$profile/input" \
    "$output/$profile-input-$name.csv" "$output/$profile-input-$name.samples.csv"
  "$workspace/bin-$role-$profile/sessions" \
    "$output/$profile-sessions-$name.csv" "$output/$profile-sessions-$name.samples.csv"
}
run_explanations() {
  local profile=$1 mode=$2 name=$3
  if test "$explanations" = 1; then
    "$workspace/bin-B-$profile/explanations" "$mode" \
      "$output/$profile-explanations-$name.csv" "$output/$profile-explanations-$name.samples.csv"
  fi
}
# Finish BOTH A/A pairs for BOTH profiles before the first A/B measurement.
for profile in SMALL LARGE; do
  for pass in 1 2 3 4; do run_pass "$profile" A "aa-$pass"; done
  for pass in 1 2 3 4; do run_explanations "$profile" legacy "aa-$pass"; done
done
for profile in SMALL LARGE; do
  for pass in 1 2; do
    run_pass "$profile" A "ab-A$pass"
    run_pass "$profile" B "ab-B$pass"
  done
  for pass in 1 2; do
    run_explanations "$profile" legacy "ab-A$pass"
    run_explanations "$profile" workspace "ab-B$pass"
  done
done
python3 "$driver/bench/compare_runs.py" "$output" > "$output/comparison.incomplete.md"
mv "$output/comparison.incomplete.md" "$output/comparison.md"
python3 "$driver/bench/compare_explanations.py" "$output" > "$output/explanations.incomplete.md"
mv "$output/explanations.incomplete.md" "$output/explanations.md"
python3 "$driver/bench/compare_sessions.py" "$output" > "$output/sessions.incomplete.md"
mv "$output/sessions.incomplete.md" "$output/sessions.md"
if test "$session_diagnostics" = 1; then
  python3 "$driver/bench/diagnose_sessions.py" "$output" "$workspace" > "$output/sessions-diagnostic.incomplete.md"
  mv "$output/sessions-diagnostic.incomplete.md" "$output/sessions-diagnostic.md"
fi
if test "${SESSION_FULL_PROOF:-0}" = 1; then
  python3 "$driver/bench/session_proof.py" "$output" "$workspace"
fi
# Deliberately no git writes, PR comments, release, or bench/results files.
