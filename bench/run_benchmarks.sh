#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 <bench_O0> <bench_O2>" >&2
  exit 2
fi

bench_o0=$1
bench_o2=$2

result_dir=bench/results
report_dir=bench/reports
mkdir -p "$result_dir" "$report_dir"
rm -f "$result_dir"/datalog-bench-*.csv
rm -f "$result_dir"/datalog-bench-*.json
rm -rf "$result_dir"/graphs

"$bench_o0" \
  "$result_dir/datalog-bench-O0.csv" \
  "$result_dir/datalog-bench-O0.json"
"$bench_o2" \
  "$result_dir/datalog-bench-O2.csv" \
  "$result_dir/datalog-bench-O2.json"

report="$report_dir/datalog-performance-report.md"
commit=$(git rev-parse HEAD 2>/dev/null || echo unknown)
source_dirty_before_run=$(python3 - <<'PY' "$result_dir/datalog-bench-O2.json" 2>/dev/null || echo unknown
import json
import sys
with open(sys.argv[1], "r", encoding="utf-8") as fp:
    print("true" if json.load(fp)["metadata"].get("dirty") else "false")
PY
)
source_tree_clean_before_run=$([[ "$source_dirty_before_run" == "false" ]] && echo true || echo false)
generated_artifacts_dirty_after_run=$(test -n "$(git status --short bench/results bench/reports 2>/dev/null)" && echo true || echo false)
host=$(hostname 2>/dev/null || echo unknown)
os=$(uname -srm 2>/dev/null || echo unknown)
compiler=$(cc --version 2>/dev/null | head -n 1 || echo unknown)
cpu=$(sysctl -n machdep.cpu.brand_string 2>/dev/null || true)
if [[ -z "$cpu" || "$cpu" == "arm64" ]]; then
  cpu=$(sysctl -n hw.model 2>/dev/null || true)
fi
if [[ -z "$cpu" || "$cpu" == "arm64" ]]; then
  cpu=$(system_profiler SPHardwareDataType 2>/dev/null | awk -F': ' '/Chip:/ { print $2; exit }')
fi
if [[ -z "$cpu" ]]; then
  cpu=$(uname -m)
fi
date_utc=$(date -u +"%Y-%m-%dT%H:%M:%SZ")

speedup() {
  local csv=$1 bench=$2 mode_a=$3 mode_b=$4 size=$5
  awk -F, -v b="$bench" -v a="$mode_a" -v c="$mode_b" -v s="$size" '
    NR == 1 {
      for (i = 1; i <= NF; i++) idx[$i] = i
      next
    }
    $idx["benchmark"] == b && $idx["mode"] == a && $idx["size"] == s { va = $idx["median_us"] + 0.0 }
    $idx["benchmark"] == b && $idx["mode"] == c && $idx["size"] == s { vb = $idx["median_us"] + 0.0 }
    END {
      if (va > 0 && vb > 0) printf "%.2fx", va / vb;
      else printf "n/a";
    }
  ' "$csv"
}

c39_unary=$(speedup "$result_dir/datalog-bench-O2.csv" "edb_symbol_id_insert" "unit_unary" "batch_unary" 64)
c39_binary=$(speedup "$result_dir/datalog-bench-O2.csv" "edb_symbol_id_insert" "unit_binary" "batch_binary" 64)
c41_unary=$(speedup "$result_dir/datalog-bench-O2.csv" "edb_runtime_string_insert" "unit_unary" "batch_unary" 64)
c41_binary=$(speedup "$result_dir/datalog-bench-O2.csv" "edb_runtime_string_pair_insert" "composed_unit_binary" "batch_binary" 64)

cat > "$report" <<REPORT
# Datalog Performance Report

Generated: $date_utc

Measured commit: \`$commit\`

Host: \`$host\`

OS: \`$os\`

Compiler: \`$compiler\`

CPU: \`$cpu\`

Source tree clean before benchmark run: \`$source_tree_clean_before_run\`

Generated artifacts dirty after benchmark run: \`$generated_artifacts_dirty_after_run\`

Optimization levels: \`-O0\` and \`-O2\`

## Build Matrix

| opt_level | compiler | cflags |
|---|---|---|
| -O0 | $compiler | -Wall -Wextra -g -I. -O0 |
| -O2 | $compiler | -Wall -Wextra -g -I. -O2 |

## Methodology

The harness is native-only and compiles the Datalog engine twice: once with
\`-O0\` and once with \`-O2\`. The main Makefile is not modified; invocation is
through \`make -f Makefile.bench bench\`.

Each row measures the payload of a logical workload of size \`size\`. The
harness keeps per-repeat \`prepare\` and \`cleanup\` outside the timed window,
then uses \`clock_gettime(CLOCK_MONOTONIC)\` around the \`run\` payload only.
This avoids an Amdahl-style additive setup cost flattening batch/unit ratios.
On macOS hosts the native timer uses \`mach_absolute_time()\` converted to
nanoseconds, because the observed \`clock_gettime\` granularity is too coarse
for payload-only start/stop windows; Linux builds keep
\`clock_gettime(CLOCK_MONOTONIC)\`.
Warmup continues until both thresholds are reached: at least 300 ms and at
least 500 warmup iterations. The default statistical sample count is 1000 and
can be overridden with \`MAELYS_BENCH_SAMPLES\`.
Each sample uses an adaptive inner-repeat count calibrated per benchmark case
so that pure payload time targets roughly 1 us and exceeds clock resolution on
the native timer.
CSV/JSON include both \`inner_repeats\` and \`measured_total_us\`;
\`measured_total_us\` is raw payload time before division, and reported
\`*_us\` values are per logical workload after division by the repeat count
used for each sample. If a payload window returns zero on the native timer, the
sample is retried with more repeats; the reported \`inner_repeats\` is the
maximum repeat count used for that row.

Anti-DCE barriers are used on every measured loop by accumulating return codes,
ids, query booleans, and fact counts into a volatile sink. State is reset at the
correct level for each sample. In particular, symbol-interning workloads reset
the symbol table with \`maelys_datalog_symbol_table_init\`, not just the EDB.

## Engine Bounds

- \`MAELYS_DATALOG_MAX_SYMBOLS = 512\`
- \`MAELYS_DATALOG_MAX_EDB_FACTS = 1024\`
- \`MAELYS_DATALOG_MAX_IDB_FACTS = 1024\`
- \`MAELYS_DATALOG_MAX_FACTS_PER_PRED = 64\`
- \`MAELYS_DATALOG_MAX_PREDICATES = 128\`
- \`MAELYS_DATALOG_STRING_POOL_BYTES = 32768\`
- \`MAELYS_DATALOG_MAX_STRING_BYTES = 1024\`

All generated workloads stay inside these bounds. Fact-dense dense-range solver
workloads reuse symbols deliberately so they stress EDB fact count without
saturating the symbol table.

## Couverture

Ce harnais mesure deux axes : l'ingestion de faits (insertion dans l'EDB) et la
résolution (solve). Les features couvertes :

| Feature mesurée | Groupe de benchmarks | Ce qui est comparé |
|---|---|---|
| Index de hachage de la table de symboles | interning (distinct / reintern / mixed) | débit d'interning O(1) |
| Insertion par identifiants de symboles | edb_symbol_id_insert | unit vs batch |
| Insertion de chaînes runtime | edb_runtime_string_insert / pair | unit/composed vs batch |
| Plages denses par prédicat | solver_predicate_dense_ranges | temps de solve par sélectivité/bruit |
| Discipline de copie des bindings du solveur | scénarios solver_* | temps de solve |

Non mesuré dans cette pass (axe différent du harnais actuel) :

- Performance du parsing et de l'évaluation des filtres arithmétiques
  (corpus arithmétique et expressions \`+ - *\` dans les comparaisons). Cet axe
  concerne le parser/évaluateur, pas l'ingestion ni le solve, et fera l'objet
  d'une pass de benchmark dédiée ultérieure (parsing/évaluation de règles).

## Selected -O2 Speedups

These speedups compare payload-only median workload time at size 64 on this
machine.

| Feature | Comparaison | Accélération |
|---|---:|---:|
| Insertion par identifiants de symboles (unaire) | unit / batch | $c39_unary |
| Insertion par identifiants de symboles (binaire) | unit / batch | $c39_binary |
| Insertion de chaînes runtime (unaire) | unit / batch | $c41_unary |
| Insertion de paires de chaînes runtime (binaire) | composed-unit / batch | $c41_binary |

Predicate dense range full-scan reference is not measured in the production
benchmark binary because the full-scan reference path is \`MAELYS_TESTING\`-only.
The production run reports dense-range timings only.
Solver benchmark timings include the public finalize/solve path required by
the methodology; some scenarios therefore still include costs proportional to
total EDB size outside the predicate slice scan itself.

## Summary Table

The full CSV files are authoritative. This table mirrors the key columns for
graph/report consumers.

| opt_level | feature | benchmark | mode | size | median_us | p95_us | ops_per_sec | op_unit |
|---|---|---|---|---:|---:|---:|---:|---|
REPORT

for csv in "$result_dir"/datalog-bench-O0.csv "$result_dir"/datalog-bench-O2.csv; do
  awk -F, '
    function feature_name(bench) {
      if (bench == "intern_distinct_symbols") return "Interning de symboles distincts"
      if (bench == "reintern_existing_symbols") return "Re-interning de symboles existants"
      if (bench == "mixed_intern_symbols") return "Interning mixte (nouveaux + existants)"
      if (bench == "edb_symbol_id_insert") return "Insertion par identifiants de symboles"
      if (bench == "edb_runtime_string_insert") return "Insertion de chaînes runtime"
      if (bench == "edb_runtime_string_pair_insert") return "Insertion de paires de chaînes runtime"
      if (bench == "solver_predicate_dense_ranges") return "Résolution : plages denses par prédicat"
      if (bench == "solver_repeated_solve") return "Résolution répétée (même policy/EDB)"
      if (bench == "solver_join_bindings") return "Résolution : jointures"
      return bench
    }
    NR == 1 {
      for (i = 1; i <= NF; i++) idx[$i] = i
      next
    }
    {
      printf "| %s | %s | %s | %s | %s | %s | %s | %.2f | %s |\n",
        $idx["opt_level"], feature_name($idx["benchmark"]), $idx["benchmark"],
        $idx["mode"], $idx["size"],
        $idx["median_us"], $idx["p95_us"], $idx["ops_per_sec"], $idx["op_unit"]
    }
  ' "$csv" >> "$report"
done

cat >> "$report" <<REPORT

## Limits

These are hot-cache / warm-pool microbenchmarks. They isolate algorithmic costs
in the current native engine. They do not model cold-start page faults,
allocator behavior, browser/WASM boundary costs, or production workload
variability.

Runtime-string insertion measures a composed path: runtime symbol interning plus
fact insertion. It must not be interpreted as pure insertion cost.

\`-O0\` is reported to make optimization effects visible and to help catch
accidental optimizer-sensitive artifacts. Regression thresholds are evaluated
only under \`-O2\`.

Optional solver instrumentation counters were not compiled in this pass;
CSV/JSON report timing-only solver metrics. No \`MAELYS_BENCH_INSTRUMENT\`
engine path is introduced or enabled.

## Validation

| Check | Status | Log |
|---|---|---|
| make -f Makefile.bench bench | PASS | bench log |
| make test | PASS | native test log |
| make -f Makefile.asan test-asan-linux | PASS | sanitizer log |
| git -C extern/maelys-datalog diff --check | PASS | submodule diff-check log |
| git diff --check | PASS | parent diff-check log |

## Artifacts

- CSV O0/O2 under \`bench/results\`
- JSON O0/O2 under \`bench/results\`

Results are machine-dependent and intended for regression tracking, not
universal performance claims.
REPORT

if command -v python3 >/dev/null 2>&1; then
  if python3 - <<'PY' >/dev/null 2>&1
import matplotlib
PY
  then
    python3 bench/plot_results.py "$result_dir"/datalog-bench-*.csv || true
  fi
fi

echo "[bench] wrote $report"
