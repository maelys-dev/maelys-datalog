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
   Each engine object is shared between solver, input, public-session and (when available on
   the candidate) explanation executables.
   All four sequential builds finish before any measurement begins.
3. For each profile, run A1/A2 and A3/A4: two independent A/A pairs.
   Finish all A/A measurements in both profiles before starting A/B.
4. Run A B A B in each profile, without rebuilding. Solver, input and public-session probes
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

A/A measures repeatability of one binary; it does not bound systematic placement
bias between different binaries. An above-floor observation alone does not
establish an algorithmic cause. The #77 control observed 9.94% and 22.77% shifts
on a single solver fixture: neither value is a universal tolerance or upper
bound. Distinguish placement from runner variance before requesting hardware.
Keep every pass and statistic; do not select an index threshold from inconclusive
measurements. There is no global geometric-mean acceptance shortcut.

### Public session materialization workload

`bench_sessions.c` uses only the facade, obtaining capacities through `limit_get`.
It times `session_solve_edb` with prebuilt input and a reused session. Compilation,
append, result enumeration/checks and release are outside timing. Every result
is checked through EDB membership/boundary absence and IDB enumeration; digests include symbol IDs and must
match across revisions and sorted/reverse/permuted inputs. The two policies are
`out(X) :- p00(X).` and `out(X) :- p00(X), p31(X).`, with `p31` always empty.
The second is a common-cost control, not a direct materialization timer:
subtracting it from the first does not isolate derivation time.

There are 200 cases per profile/pass: two policies, integer/symbol values, five
orders (sorted, reverse, permuted, identical duplicates, and values spaced by
4096), and ten entry counts (8, 16, 31, 32, 33, 64, 128, 256, 402 and the runtime
global EDB bound). Distinct facts occupy the minimum number of predicates that respects
the runtime per-predicate limit. Strided values are adversarial low-bit inputs,
not forced hash collisions; the native unit test supplies forced collisions.
The same 50 warmups, 301 samples, two A/A pairs and A B A B protocol applies.
`sessions.md` reports every case with its own noise floor; these simple policies
do not establish gains for recursion or aggregates. Session storage/allocation
claims come from the native contracts, not timing or process RSS.

### Optional instruction and layout diagnosis

Set the manual workflow's `diagnostic_original` input to an earlier candidate
SHA to compare baseline A, that candidate B and revised head C before the full
matrix. `diagnose_solver_layout.sh BASE ORIGINAL HEAD NEW_ABSOLUTE_OUTPUT`
also runs this diagnostic directly on native Linux x86_64 with Clang and
Valgrind installed. It deliberately selects only `solver_size_pure` LARGE/2048;
this is separate from the full comparison's unfiltered inventory.

The driver includes the unchanged historical fixture/payload. It counts only
finalize/solve/query/release under Callgrind, twice per binary, and times outside
Valgrind. All builds precede timing. Four predeclared layouts link identical
objects with 0/16/64/256 unreachable text bytes before the EDB object; symbol
maps verify the displacement. Two A/A pairs per unpadded revision precede two
interleaved rounds of every layout (500 warmups, 1000 samples). All variants,
raw samples, instruction profiles, function annotations, disassembly and hashes
are retained under `bench-diagnostic/` in the run artifact. The complete matrix
is under `bench-comparison/` when both directories are uploaded.

Equal Ir alone does not prove a layout cause or equal cycle/cache cost. The
additional diagnostic driver changes layout relative to the historical binary;
interpret the full rerun separately. No automatic acceptance or merge follows
from the generated `diagnostic.md` report.

The optional `session_diagnostics` workflow input adds a separate count-mode
build of the session harness, using the same engine objects, before any timing.
After the complete matrix, `diagnose_sessions.py` lists every slower session row
by amplitude, preserving its A/A floor. It counts the predeclared controls in
`CONTROLS` regardless of their new timing verdict, plus distinct cases with a
slower metric above the named 9.94% reference from the earlier solver control.
This prioritizes diagnostics; it does not erase smaller effects or calibrate
sessions from a different workload. Each selected case has 50 warmups followed
by one counted `solve_edb`, twice per revision; setup, clocks, oracle and release
stay outside collection. The unchanged oracle digest must match the timed run.
The separate mode changes layout and prior case history and produces no timings.
`sessions-diagnostic.md` and `session-counts/` retain the complete selection,
profiles, exclusive function differences, hashes and checked outputs as artifacts.
Cache simulation reports instruction/data reads and writes and both cache levels;
branch simulation reports conditional/indirect branches and mispredictions. The
I1/LL instruction misses and branch mispredictions also have per-function reports.
Both simulations remain active during warmup; their models do not measure actual
CPU cache traffic, its branch predictor or cycles. Identical modeled events do
not prove identical hardware cost. The declared LARGE/inert/duplicate/integer/
maximum control retains the prior +30% median case even if its next timing falls
below its floor.

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
- public-session summary/raw CSV and `sessions.md`, with all 200 cases per pass;
- explanation summary/raw CSV for eight passes per profile when the candidate
  has the session workspace API, plus `explanations.md` (an explicit skip otherwise);
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

### Session explanation workspace (candidate only)

When `head` exposes `session_config_set_explanation_workspace`, the same run
also compares the **same candidate binary** with two session configurations:
A leaves explanations unconfigured; B reserves TRUE and FALSE at creation.
This is separate from the solver/input comparison of two revisions. Both
profiles compile before any timing, sharing the candidate engine objects.
Two legacy/legacy A/A pairs in each profile finish before any A/B measurement;
then legacy/workspace/legacy/workspace run sequentially with the same compiler
and flags. The report uses the same per-metric floors as the revision report.

The fixed six-case matrix covers Why-true and Why-false over a small policy
with negation, each with a fresh result, alternating symbol queries, and a
repeated query. Fresh-result solve/release is outside timing and clears the
cache. Alternation forces misses; repeated queries measure a cache warmed by
50 iterations. Each of the 301 samples times **measure + write** through the
existing direct-text functions, including their status checks and clock
overhead. Output buffers are preallocated. Compilation, session construction,
solve, result release, output comparison and reporting are outside timing.

Before timing, both modes must produce byte-identical, complete texts with the
expected truth values. Every timed output is checked against that legacy oracle
after timing. Text digests, workspace bytes, mode, profile, revision, compiler
and flags travel with every summary row; disagreement or an incomplete matrix
refuses a final report. `workspace_bytes` is only the extra configured storage
(maximum of TRUE/FALSE bounds), not total memory or the legacy path's temporary
allocations. This probe neither measures end-to-end latency nor establishes an
allocation guarantee, and does not generalize to large/truncated explanations.

Raw samples, summaries and `explanations.md` stay in the workflow artifact.
Use the existing workflow with `head=v0.5.0` (or a later revision) to exercise
this path; older heads retain solver/input coverage and an explicit skip.

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
