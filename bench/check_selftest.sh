#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

tmp=$(mktemp -d "${TMPDIR:-/tmp}/maelys-check-selftest.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

copy_profile() {
  local profile=$1 dest=$2
  mkdir -p "$dest/results"
  cp "bench/results/datalog-bench-$profile-O0.csv" "$dest/results/"
  cp "bench/results/datalog-bench-$profile-O0.json" "$dest/results/"
  cp "bench/results/datalog-bench-$profile-O2.csv" "$dest/results/"
  cp "bench/results/datalog-bench-$profile-O2.json" "$dest/results/"
}

run_check() {
  local profile=$1 dir=$2 baseline=$3
  MAELYS_BENCH_CSV_O0="$dir/results/datalog-bench-$profile-O0.csv" \
  MAELYS_BENCH_CSV_O2="$dir/results/datalog-bench-$profile-O2.csv" \
  MAELYS_BENCH_JSON_O0="$dir/results/datalog-bench-$profile-O0.json" \
  MAELYS_BENCH_JSON_O2="$dir/results/datalog-bench-$profile-O2.json" \
  MAELYS_BENCH_BASELINE_JSON="$baseline" \
  bash bench/check_regression.sh
}

expect_fail() {
  local name=$1 profile=$2 dir=$3 baseline=$4 expected=$5
  set +e
  local output
  output=$(run_check "$profile" "$dir" "$baseline" 2>&1)
  local rc=$?
  set -e
  printf '%s\n' "$output"
  if [[ "$rc" -eq 0 ]]; then
    echo "SELFTEST: $name FAIL expected non-zero exit"
    exit 1
  fi
  if [[ "$output" != *"$expected"* ]]; then
    echo "SELFTEST: $name FAIL expected output containing: $expected"
    exit 1
  fi
  echo "SELFTEST: $name PASS (check exited $rc as expected)"
}

expect_pass() {
  local name=$1 profile=$2 dir=$3 baseline=$4 expected=$5
  set +e
  local output
  output=$(run_check "$profile" "$dir" "$baseline" 2>&1)
  local rc=$?
  set -e
  printf '%s\n' "$output"
  if [[ "$rc" -ne 0 ]]; then
    echo "SELFTEST: $name FAIL expected exit 0, got $rc"
    exit 1
  fi
  if [[ "$output" != *"$expected"* ]]; then
    echo "SELFTEST: $name FAIL expected output containing: $expected"
    exit 1
  fi
  echo "SELFTEST: $name PASS"
}

break_ratio() {
  local csv=$1
  python3 - "$csv" <<'PY'
import csv
import sys
from pathlib import Path

path = Path(sys.argv[1])
rows = list(csv.DictReader(path.open(newline="")))
unit = None
for row in rows:
    if (row["benchmark"] == "edb_symbol_id_insert" and
            row["mode"] == "unit_unary" and row["size"] == "64"):
        unit = float(row["median_us"])
        break
if unit is None:
    raise SystemExit("unit row not found")
for row in rows:
    if (row["benchmark"] == "edb_symbol_id_insert" and
            row["mode"] == "batch_unary" and row["size"] == "64"):
        row["median_us"] = f"{unit * 6.1:.6f}"
        break
else:
    raise SystemExit("batch row not found")
with path.open("w", newline="") as fp:
    writer = csv.DictWriter(fp, fieldnames=rows[0].keys())
    writer.writeheader()
    writer.writerows(rows)
PY
}

zero_median() {
  local csv=$1
  python3 - "$csv" <<'PY'
import csv
import sys
from pathlib import Path

path = Path(sys.argv[1])
rows = list(csv.DictReader(path.open(newline="")))
rows[0]["median_us"] = "0.000000"
with path.open("w", newline="") as fp:
    writer = csv.DictWriter(fp, fieldnames=rows[0].keys())
    writer.writeheader()
    writer.writerows(rows)
PY
}

set_json_env() {
  local json=$1 profile=$2
  python3 - "$json" "$profile" <<'PY'
import json
import sys
from pathlib import Path

path = Path(sys.argv[1])
profile = sys.argv[2]
data = json.loads(path.read_text())
meta = data.setdefault("metadata", {})
meta["os"] = "Linux 6.8.0"
meta["compiler"] = "clang version 18.1.3"
meta["cpu"] = "aarch64"
meta["opt_level"] = "-O2"
meta["profile"] = profile
path.write_text(json.dumps(data, indent=2) + "\n")
PY
}

delete_profile() {
  local json=$1
  python3 - "$json" <<'PY'
import json
import sys
from pathlib import Path

path = Path(sys.argv[1])
data = json.loads(path.read_text())
data.get("metadata", {}).pop("profile", None)
path.write_text(json.dumps(data, indent=2) + "\n")
PY
}

missing_baseline="$tmp/no-baseline.json"

small_ratio="$tmp/small-ratio"
copy_profile small "$small_ratio"
break_ratio "$small_ratio/results/datalog-bench-small-O2.csv"
expect_fail "small broken ratio" small "$small_ratio" "$missing_baseline" "regression"

large_ratio="$tmp/large-ratio"
copy_profile large "$large_ratio"
break_ratio "$large_ratio/results/datalog-bench-large-O2.csv"
expect_fail "large broken ratio" large "$large_ratio" "$missing_baseline" "regression"

malformed="$tmp/malformed"
copy_profile small "$malformed"
zero_median "$malformed/results/datalog-bench-small-O2.csv"
expect_fail "median_us zero" small "$malformed" "$missing_baseline" "malformed evidence"

profile_mismatch="$tmp/profile-mismatch"
copy_profile small "$profile_mismatch"
set_json_env "$profile_mismatch/results/datalog-bench-small-O2.json" "SMALL"
baseline_mismatch="$profile_mismatch/baseline-large.json"
cp "$profile_mismatch/results/datalog-bench-small-O2.json" "$baseline_mismatch"
set_json_env "$baseline_mismatch" "LARGE"
expect_pass "profile mismatch skips baseline" small "$profile_mismatch" "$baseline_mismatch" "profile mismatch"

legacy_baseline="$tmp/legacy-baseline"
copy_profile small "$legacy_baseline"
set_json_env "$legacy_baseline/results/datalog-bench-small-O2.json" "SMALL"
baseline_legacy="$legacy_baseline/baseline-no-profile.json"
cp "$legacy_baseline/results/datalog-bench-small-O2.json" "$baseline_legacy"
delete_profile "$baseline_legacy"
expect_pass "baseline without profile is SMALL" small "$legacy_baseline" "$baseline_legacy" "ratios+baseline"

echo "SELFTEST: PASS"
