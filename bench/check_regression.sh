#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C
export LC_NUMERIC=C

RESULTS_DIR=${MAELYS_BENCH_RESULTS_DIR:-bench/results}
CSV_O0=${MAELYS_BENCH_CSV_O0:-$RESULTS_DIR/datalog-bench-small-O0.csv}
CSV_O2=${MAELYS_BENCH_CSV_O2:-$RESULTS_DIR/datalog-bench-small-O2.csv}
JSON_O0=${MAELYS_BENCH_JSON_O0:-$RESULTS_DIR/datalog-bench-small-O0.json}
JSON_O2=${MAELYS_BENCH_JSON_O2:-$RESULTS_DIR/datalog-bench-small-O2.json}
BASELINE_JSON=${MAELYS_BENCH_BASELINE_JSON:-bench/baseline/linux-arm64-clang18-O2.json}

failures=()
mode="ratios-only"

lower() {
  printf '%s' "$1" | tr '[:upper:]' '[:lower:]'
}

json_valid() {
  local path=$1
  if command -v jq >/dev/null 2>&1; then
    jq -e '.metadata and (.results | type == "array")' "$path" >/dev/null
  elif command -v python3 >/dev/null 2>&1; then
    python3 - "$path" <<'PY' >/dev/null
import json
import sys
with open(sys.argv[1], "r", encoding="utf-8") as fp:
    data = json.load(fp)
assert isinstance(data.get("metadata"), dict)
assert isinstance(data.get("results"), list)
PY
  else
    grep -q '"metadata"' "$path" && grep -q '"results"' "$path"
  fi
}

json_field() {
  local path=$1 field=$2
  if command -v jq >/dev/null 2>&1; then
    jq -r --arg field "$field" '.metadata[$field] // ""' "$path"
  elif command -v python3 >/dev/null 2>&1; then
    python3 - "$path" "$field" <<'PY'
import json
import sys
with open(sys.argv[1], "r", encoding="utf-8") as fp:
    value = json.load(fp).get("metadata", {}).get(sys.argv[2], "")
print("" if value is None else value)
PY
  else
    sed -n "s/.*\"$field\"[[:space:]]*:[[:space:]]*\"\([^\"]*\)\".*/\1/p" "$path" | head -n 1
  fi
}

finish_missing() {
  echo "CHECK: ERROR missing artifacts, exit=2"
  exit 2
}

finish() {
  local exit_code=0
  if ((${#failures[@]} > 0)); then
    exit_code=1
    printf 'CHECK: FAIL (%s), exit=1\n' "$(IFS='; '; echo "${failures[*]}")"
  else
    echo "CHECK: PASS mode=$mode, exit=0"
  fi
  exit "$exit_code"
}

if [[ ! -f "$CSV_O2" || ! -f "$JSON_O2" ]]; then
  echo "SANITY: O2 artifact present: FAIL"
  echo "run \`make -f Makefile.bench bench\` (native) or \`make -f Makefile.bench bench-linux\` (container) first"
  finish_missing
fi
echo "SANITY: O2 artifact present: PASS"

if [[ ! -f "$CSV_O0" || ! -f "$JSON_O0" ]]; then
  echo "SANITY: O0 artifact present: FAIL"
  failures+=("malformed evidence: O0 artifact absent")
else
  echo "SANITY: O0 artifact present: PASS"
fi

if json_valid "$JSON_O2"; then
  echo "SANITY: O2 JSON structure: PASS"
else
  echo "SANITY: O2 JSON structure: FAIL"
  failures+=("malformed evidence: O2 JSON invalid")
fi

if [[ -f "$JSON_O0" ]]; then
  if json_valid "$JSON_O0"; then
    echo "SANITY: O0 JSON structure: PASS"
  else
    echo "SANITY: O0 JSON structure: FAIL"
    failures+=("malformed evidence: O0 JSON invalid")
  fi
fi

if awk -F, -v want="-O2" '
  NR == 1 {
    for (i = 1; i <= NF; i++) idx[$i] = i
    if (!idx["opt_level"]) exit 2
    next
  }
  $idx["opt_level"] == want { found = 1 }
  END { exit found ? 0 : 1 }
' "$CSV_O2"; then
  echo "SANITY: O2 rows opt_level=-O2: PASS"
else
  echo "SANITY: O2 rows opt_level=-O2: FAIL"
  failures+=("malformed evidence: O2 CSV missing opt_level=-O2 rows")
fi

if [[ -f "$CSV_O0" ]]; then
  if awk -F, -v want="-O0" '
    NR == 1 {
      for (i = 1; i <= NF; i++) idx[$i] = i
      if (!idx["opt_level"]) exit 2
      next
    }
    $idx["opt_level"] == want { found = 1 }
    END { exit found ? 0 : 1 }
  ' "$CSV_O0"; then
    echo "SANITY: O0 rows opt_level=-O0: PASS"
  else
    echo "SANITY: O0 rows opt_level=-O0: FAIL"
    failures+=("malformed evidence: O0 CSV missing opt_level=-O0 rows")
  fi
fi

if awk -F, '
  NR == 1 {
    for (i = 1; i <= NF; i++) idx[$i] = i
    if (!idx["median_us"]) exit 2
    next
  }
  ($idx["median_us"] + 0.0) <= 0.0 {
    printf "SANITY: median_us <= 0: FAIL %s/%s/%s size=%s median=%s\n",
      $idx["group"], $idx["benchmark"], $idx["mode"], $idx["size"], $idx["median_us"]
    bad = 1
  }
  END { exit bad ? 1 : 0 }
' "$CSV_O2"; then
  echo "SANITY: all O2 median_us > 0: PASS"
else
  failures+=("malformed evidence: O2 median_us <= 0")
fi

if ! ratio_output=$(awk -F, '
  function store_pair(bench, unit, batch, label, idx) {
    idx = ++pair_count
    p_bench[idx] = bench
    p_unit[idx] = unit
    p_batch[idx] = batch
    p_label[idx] = label
  }
  BEGIN {
    store_pair("edb_symbol_id_insert", "unit_unary", "batch_unary", "unit_unary/batch_unary")
    store_pair("edb_symbol_id_insert", "unit_binary", "batch_binary", "unit_binary/batch_binary")
    store_pair("edb_runtime_string_insert", "unit_unary", "batch_unary", "unit_unary/batch_unary")
    store_pair("edb_runtime_string_pair_insert", "composed_unit_binary", "batch_binary", "composed_unit_binary/batch_binary")
  }
  NR == 1 {
    for (i = 1; i <= NF; i++) idx[$i] = i
    next
  }
  {
    key = $idx["group"] SUBSEP $idx["benchmark"] SUBSEP $idx["mode"] SUBSEP $idx["size"] SUBSEP $idx["selectivity"]
    med[key] = $idx["median_us"] + 0.0
    seen[$idx["group"] SUBSEP $idx["size"] SUBSEP $idx["selectivity"]] = 1
  }
  END {
    failed = 0
    for (p = 1; p <= pair_count; p++) {
      for (seen_key in seen) {
        split(seen_key, parts, SUBSEP)
        group = parts[1]
        size = parts[2]
        selectivity = parts[3]
        if (group != "ingestion" || (size + 0) < 16) continue
        ukey = group SUBSEP p_bench[p] SUBSEP p_unit[p] SUBSEP size SUBSEP selectivity
        bkey = group SUBSEP p_bench[p] SUBSEP p_batch[p] SUBSEP size SUBSEP selectivity
        if (!(ukey in med) || !(bkey in med)) continue
        unit = med[ukey]
        batch = med[bkey]
        ratio = unit / batch
        if (batch > (5.0 * unit)) {
          printf "RATIO: %s %s N=%s selectivity=%s ratio=%.2f FAIL batch=%.6f unit=%.6f\n",
            p_bench[p], p_label[p], size, selectivity, ratio, batch, unit
          failed = 1
        } else if (batch >= unit) {
          printf "RATIO: %s %s N=%s selectivity=%s ratio=%.2f WARN batch=%.6f unit=%.6f\n",
            p_bench[p], p_label[p], size, selectivity, ratio, batch, unit
        } else {
          printf "RATIO: %s %s N=%s selectivity=%s ratio=%.2f PASS\n",
            p_bench[p], p_label[p], size, selectivity, ratio
        }
      }
    }
    exit failed ? 1 : 0
  }
' "$CSV_O2"); then
  printf '%s\n' "$ratio_output"
  failures+=("regression: batch median > 5x unit median")
else
  printf '%s\n' "$ratio_output"
fi

if grep -q ',full_scan,' "$CSV_O2"; then
  echo "DENSE_RANGE: full_scan reference present, ratio check not implemented"
else
  echo "DENSE_RANGE: full_scan reference absent, skipped"
fi

if [[ ! -f "$BASELINE_JSON" ]]; then
  echo "BASELINE: no baseline recorded, ratios-only check"
  finish
fi

current_os=$(json_field "$JSON_O2" os)
current_compiler=$(json_field "$JSON_O2" compiler)
current_opt=$(json_field "$JSON_O2" opt_level)
current_cpu=$(json_field "$JSON_O2" cpu)
current_profile=$(json_field "$JSON_O2" profile)
if [[ -z "$current_profile" ]]; then
  current_profile=SMALL
fi

os_l=$(lower "$current_os")
compiler_l=$(lower "$current_compiler")
cpu_l=$(lower "$current_cpu")
cpu_source="$cpu_l $os_l"

env_matches=true
if [[ "$os_l" != *linux* ]]; then
  env_matches=false
fi
if [[ "$compiler_l" != *clang* || "$compiler_l" != *18* ]]; then
  env_matches=false
fi
if [[ "$current_opt" != "-O2" ]]; then
  env_matches=false
fi
if [[ "$cpu_source" != *arm64* && "$cpu_source" != *aarch64* ]]; then
  echo "BASELINE SKIP: current artifact metadata has no compatible cpu_class label"
  finish
fi

if [[ "$env_matches" != true ]]; then
  echo "BASELINE: env mismatch, ratios-only check"
  finish
fi

baseline_profile=$(json_field "$BASELINE_JSON" profile)
if [[ -z "$baseline_profile" ]]; then
  baseline_profile=SMALL
fi
if [[ "$(lower "$baseline_profile")" != "$(lower "$current_profile")" ]]; then
  echo "BASELINE: profile mismatch baseline=$baseline_profile current=$current_profile, ratios-only check"
  finish
fi

if ! command -v jq >/dev/null 2>&1; then
  echo "BASELINE: jq unavailable for matching baseline, ratios-only check"
  finish
fi

if ! jq -e '.metadata and (.results | type == "array")' "$BASELINE_JSON" >/dev/null; then
  echo "BASELINE: baseline JSON invalid"
  failures+=("malformed evidence: baseline JSON invalid")
  finish
fi

mode="ratios+baseline"
echo "BASELINE: current artifact env linux/arm64/clang-18/-O2 profile=$current_profile matches baseline"

baseline_tmp=$(mktemp "${TMPDIR:-/tmp}/maelys-baseline.XXXXXX")
current_tmp=$(mktemp "${TMPDIR:-/tmp}/maelys-current.XXXXXX")
trap 'rm -f "$baseline_tmp" "$current_tmp"' EXIT

jq -r '.results[] | [.group,.benchmark,.mode,(.size|tostring),(.selectivity|tostring),(.median_us|tostring)] | @tsv' "$BASELINE_JSON" > "$baseline_tmp"
jq -r '.results[] | [.group,.benchmark,.mode,(.size|tostring),(.selectivity|tostring),(.median_us|tostring)] | @tsv' "$JSON_O2" > "$current_tmp"

if ! baseline_output=$(awk -F '\t' '
  NR == FNR {
    key = $1 "|" $2 "|" $3 "|" $4 "|" $5
    base[key] = $6 + 0.0
    next
  }
  {
    key = $1 "|" $2 "|" $3 "|" $4 "|" $5
    if (!(key in base)) next
    current = $6 + 0.0
    baseline = base[key]
    if (baseline < 1.0) {
      printf "BASELINE SKIP: %s baseline median %.6f us below timing floor\n", key, baseline
      skipped++
      next
    }
    checked++
    if (current > (1.30 * baseline)) {
      printf "BASELINE: %s current=%.6f baseline=%.6f FAIL\n", key, current, baseline
      failed++
    }
  }
  END {
    printf "BASELINE: %d common rows checked, %d regressions, %d below timing floor skipped\n",
      checked + 0, failed + 0, skipped + 0
    exit failed ? 1 : 0
  }
' "$baseline_tmp" "$current_tmp"); then
  printf '%s\n' "$baseline_output"
  failures+=("regression: current median > 1.30x baseline median")
else
  printf '%s\n' "$baseline_output"
fi

finish
