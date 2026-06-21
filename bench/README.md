# Maelys Datalog Benchmarks

This directory contains the native benchmark harness for P3-C49.

Run from the repository root:

```sh
make -f Makefile.bench bench
```

The harness builds two production-style native binaries, one with `-O0` and one
with `-O2`, then writes:

- `bench/results/datalog-bench-O0.csv`
- `bench/results/datalog-bench-O2.csv`
- `bench/results/datalog-bench-O0.json`
- `bench/results/datalog-bench-O2.json`
- `bench/reports/datalog-performance-report.md`

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

`check` reads the existing CSV/JSON artifacts. It does not build the engine and
does not rerun benchmarks. Missing `-O2` artifacts are reported as an evidence
setup error; malformed artifacts or regressions return a non-zero exit code.

Two classes of guard are intentionally separate:

- Ratios are evaluated everywhere, native and container. For every configured
  batch/unit pair at sizes `N >= 16`, the check fails if the batch median is
  more than `5x` slower than the unit or composed-unit path. A batch path that
  is merely slower than unit is reported as a warning, not a hard regression.
- Absolute baseline comparison is evaluated only when the current `-O2` JSON
  metadata matches the container baseline environment: Linux, arm64/aarch64,
  clang 18, `-O2`. Native Apple clang runs normally report `ratios-only`.

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

This target runs `bench-linux`, then copies the generated `-O2` JSON to the
baseline path. It is never a dependency of `check`.

Baseline refresh procedure:

- run on a quiet machine, ideally plugged in and not thermally constrained;
- close heavy concurrent work such as builds, local model inference, and large
  browser sessions;
- rebuild the container image through the Makefile target, so the recorded
  compiler and OS labels remain comparable;
- inspect `make -f Makefile.bench check` after refresh before committing the
  baseline.
