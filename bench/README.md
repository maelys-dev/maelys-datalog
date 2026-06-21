# Maelys Datalog Benchmarks

This directory contains the native benchmark harness for P3-C49.

Run from the repository root:

```sh
make -f Makefile.bench bench
```

The harness builds two production-style native binaries, one with `-O0` and one
with `-O2`, then writes:

- `bench/results/P3-C49-datalog-bench-O0.csv`
- `bench/results/P3-C49-datalog-bench-O2.csv`
- `bench/results/P3-C49-datalog-bench-O0.json`
- `bench/results/P3-C49-datalog-bench-O2.json`
- `bench/reports/P3-C49-performance-report.md`

The default sample count is 1000. Override it only for local iteration:

```sh
MAELYS_BENCH_SAMPLES=100 make -f Makefile.bench bench
```

The benchmarks are hot-cache / warm-pool native microbenchmarks. They are meant
for regression tracking on a known machine, not as universal performance claims.
