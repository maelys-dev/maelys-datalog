# Maelys Datalog Benchmarks

This directory contains the native benchmark harness for the Datalog engine.

## Manual revision comparison (not a PR check)

`bench-compare.yml` runs **only** on `workflow_dispatch`, with exactly two
required inputs, `base` and `head` (branch, tag or commit SHA). It has one
sequential job on GitHub-hosted `ubuntu-latest`, no matrix, no PR trigger,
no write permissions, no release environment and no automatic PR comments.
It is not a required check and does not extend the PR benchmark job.

```sh
gh workflow run bench-compare.yml \
  -f base=c871ec3 \
  -f head=perf/input-edb-string-index
# Or compare the solver/session change:
gh workflow run bench-compare.yml \
  -f base=c871ec3 \
  -f head=feat/reusable-reference-sessions
```

The branch names resolve to immutable commits once, recorded in metadata.
Both revisions must expose the input-EDB API introduced by #53 and use the
same solver harness as this driver. A revision without that API is refused,
not silently omitted. The input harness belongs to this driver and is the
same public-API consumer for both revisions, even if a revision contains an
older copy of `bench/bench_input_edb.c`.

The input source blob hashes are recorded. Comparing main with the sessions
branch (identical input implementations) cannot choose an input-index threshold;
the report says so. Compare main with the index branch to measure that change.
Do not label A/B as linear/indexed without checking the selected implementations.

The workflow normally becomes dispatchable after it exists on the default
branch. Creating this PR is not authorization to merge it or to add a PR trigger
as a workaround. See [GitHub's dispatch contract](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows#workflow_dispatch).

### Fixed measurement protocol

1. Archive base/head; retain the exact compiler, flags, SHA and harness hashes.
2. Compile each revision/profile once with clang `-O2`, SMALL and LARGE.
   Each engine object is shared between solver and input executables.
   All four sequential builds finish before any measurement begins.
3. For each profile, run A1/A2 and A3/A4: two independent A/A pairs.
   Finish all A/A measurements in both profiles before starting A/B.
4. Run A B A B in each profile, without rebuilding. Both solver and input
   run sequentially in every pass. No priority/affinity tuning or case filtering.
5. Compare every case against its observed A/A floor. Under 10 microseconds
   (baseline median of A/A medians), retain the minimum of pass minima.
   Otherwise report median of pass medians and median of pass p95s separately.
   This aggregation of p95s is **not** a pooled percentile.

The per-metric floor is
`max(max(A1/A2,A2/A1)-1, max(A3/A4,A4/A3)-1)`. It is an observation, not a
confidence interval. Any A/B effect at or below this symmetric floor is
**indéterminé**, never zero or a gain. Median and tail can disagree; both remain
visible. Zero-duration clear samples are marked indeterminate by clock
resolution, never interpreted as infinite speedup.

If hosted-runner noise is larger than the effect being investigated, the report
says a dedicated machine is needed. Do not reinterpret a noisy run, change its
statistic, cherry-pick cases, or set an automatic threshold from inconclusive
measurements. There is no global geometric-mean acceptance shortcut.

### Input index crossover and memory

The public-input harness extends the #56 probe with exactly 64, 128, 256, 512 and
1,024 distinct strings, in batch and unit modes. Five string positions per fact
make the capacity-derived distinct bound at most four larger than the tested
count. Each six-byte name is included exactly once in the distinct set; predicate
and term roles share strings. Capacities are fixed at construction, not adapted
from the observed input. The original default-capacity repeated-string and
512-value controls remain, giving 22 cases per profile/pass.

It uses 50 warmup iterations and 301 measured iterations. Allocation and input
construction are outside append timing; clear is timed separately on a populated
EDB, including clock-call overhead (not subtracted). Every raw append/clear
sample is saved; summaries contain minimum/median/p95 and reserved bytes.
The command log includes reserved memory for 128/default/maximum text budgets;
default and maximum currently coincide.

These tables support selecting S later; the workflow neither guesses S nor
changes the production implementation. Check both modes and profiles, the
capacity bound (not only the batch size), noise and repeated-string controls.

### Artifacts and local tooling checks

The run uploads `bench-comparison-RUN_ID-ATTEMPT`, retained for 30 days:

- solver CSV/JSON for all eight passes per profile;
- input summary CSV and `*.samples.csv` with all measured samples;
- `comparison.md`, `metadata.json` and `commands.log`.

Partial artifacts are retained on failure, but an incomplete data set cannot
produce a final comparison report: a failed rendering remains explicitly named
`comparison.incomplete.md`. The driver refuses an existing output path.
Nothing is written to `bench/results`; no results are committed and no PR comment
is generated. Archives/binaries live separately in runner temporary storage.

```sh
actionlint .github/workflows/bench-compare.yml
shellcheck bench/compare_revisions.sh
python3 -m unittest discover -s bench -p 'test_compare*.py' -v
# On Linux, outside any concurrent build:
bash bench/compare_revisions.sh BASE HEAD /absolute/new-output-directory
# Single local input probes (stdout, not acceptance evidence):
make -s -f Makefile.bench bench-input-small CC=clang
make -s -f Makefile.bench bench-input-large CC=clang
```

The orchestration test uses synthetic executables to verify exactly four
sequential builds, all A/A before A/B, A/B/A/B ordering and refusal of malformed
refs or existing output. Synthetic timings and local Docker smoke runs are not
hosted-runner performance evidence.

## Historical single-revision harness

Run from the repository root:

```sh
make -f Makefile.bench bench
```

`bench` is the compatibility alias for the SMALL size profile. The harness also
supports the LARGE profile:

```sh
make -f Makefile.bench bench-small
make -f Makefile.bench bench-large
make -f Makefile.bench bench-all
```

For each profile, the harness builds two production-style native binaries, one
with `-O0` and one with `-O2`, then writes:

- `bench/results/datalog-bench-small-O0.csv`
- `bench/results/datalog-bench-small-O2.csv`
- `bench/results/datalog-bench-small-O0.json`
- `bench/results/datalog-bench-small-O2.json`
- `bench/results/datalog-bench-large-O0.csv`
- `bench/results/datalog-bench-large-O2.csv`
- `bench/results/datalog-bench-large-O0.json`
- `bench/results/datalog-bench-large-O2.json`
- `bench/reports/datalog-performance-report-small.md`
- `bench/reports/datalog-performance-report-large.md`

The SMALL profile is the default and preserves the historical engine bounds.
The LARGE profile is selected globally at compile time with
`-DMAELYS_DATALOG_PROFILE_LARGE`; build outputs are separated by profile and
optimization level under `bench/build/bench-small-O0`, `bench/build/bench-small-O2`,
`bench/build/bench-large-O0`, and `bench/build/bench-large-O2`.

The default sample count is 1000. Override it only for local iteration:

```sh
MAELYS_BENCH_SAMPLES=100 make -f Makefile.bench bench
```

The benchmarks are hot-cache / warm-pool native microbenchmarks. They are meant
for regression tracking on a known machine, not as universal performance claims.

## Regression Check

After a native or container benchmark run, use:

```sh
make -f Makefile.bench check
```

`check` is the compatibility alias for `check-small`. Use profile-specific
targets when both profiles have been produced:

```sh
make -f Makefile.bench check-small
make -f Makefile.bench check-large
make -f Makefile.bench check-all
```

`check` reads the existing CSV/JSON artifacts. It does not build the engine and
does not rerun benchmarks. Missing `-O2` artifacts are reported as an evidence
setup error; malformed artifacts or regressions return a non-zero exit code.

To prove the gate fails on degraded evidence without touching real artifacts:

```sh
make -f Makefile.bench check-selftest
```

The selftest copies artifacts into a temporary directory, corrupts the copies,
and verifies ratio failures, malformed timing failures, profile mismatch skips,
and legacy SMALL baseline matching.

Two classes of guard are intentionally separate:

- Ratios are evaluated everywhere, native and container. For every configured
  batch/unit pair at sizes `N >= 16`, the check fails if the batch median is
  more than `5x` slower than the unit or composed-unit path. A batch path that
  is merely slower than unit is reported as a warning, not a hard regression.
- Absolute baseline comparison is evaluated only when the current `-O2` JSON
  metadata matches the container baseline environment and profile: Linux,
  arm64/aarch64, clang 18, `-O2`, SMALL. Native Apple clang runs normally report
  `ratios-only`; LARGE is ratios-only until a dedicated LARGE baseline exists.

The container baseline lives at:

```text
bench/baseline/linux-arm64-clang18-O2.json
```

When the current artifact matches that baseline environment, every common row
with a baseline median of at least `1.0 us` is checked against a `+30%`
threshold. Rows below the `1.0 us` baseline timing floor are skipped for the
absolute baseline check, but ratio checks still run.

To refresh the container baseline deliberately:

```sh
make -f Makefile.bench bench-baseline-update
```

This target runs the SMALL container benchmark, then copies
`datalog-bench-small-O2.json` to the baseline path. It is never a dependency of
`check`.

Baseline refresh procedure:

- run on a quiet machine, ideally plugged in and not thermally constrained;
- close heavy concurrent work such as builds, local model inference, and large
  browser sessions;
- rebuild the container image through the Makefile target, so the recorded
  compiler and OS labels remain comparable;
- inspect `make -f Makefile.bench check` after refresh before committing the
  baseline.
