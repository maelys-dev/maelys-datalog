# P3-C49 Datalog Performance Report

Generated: 2026-06-21T08:20:35Z

Measured commit: `8a120be7c8408327512ed88a4fcb83736fe7a6b3`

Host: `MAC-QXQWJGVJXW`

OS: `Darwin 24.5.0 arm64`

Compiler: `Apple clang version 17.0.0 (clang-1700.0.13.5)`

CPU: `Apple M2 Max`

Source tree clean before benchmark run: `true`

Generated artifacts dirty after benchmark run: `true`

Optimization levels: `-O0` and `-O2`

## Build Matrix

| opt_level | compiler | cflags |
|---|---|---|
| -O0 | Apple clang version 17.0.0 (clang-1700.0.13.5) | -Wall -Wextra -g -I. -O0 |
| -O2 | Apple clang version 17.0.0 (clang-1700.0.13.5) | -Wall -Wextra -g -I. -O2 |

## Methodology

The harness is native-only and compiles the Datalog engine twice: once with
`-O0` and once with `-O2`. The main Makefile is not modified; invocation is
through `make -f Makefile.bench bench`.

Each row measures the payload of a logical workload of size `size`. The
harness keeps per-repeat `prepare` and `cleanup` outside the timed window,
then uses `clock_gettime(CLOCK_MONOTONIC)` around the `run` payload only.
This avoids an Amdahl-style additive setup cost flattening batch/unit ratios.
On macOS hosts the native timer uses `mach_absolute_time()` converted to
nanoseconds, because the observed `clock_gettime` granularity is too coarse
for payload-only start/stop windows; Linux builds keep
`clock_gettime(CLOCK_MONOTONIC)`.
Warmup continues until both thresholds are reached: at least 300 ms and at
least 500 warmup iterations. The default statistical sample count is 1000 and
can be overridden with `MAELYS_BENCH_SAMPLES`.
Each sample uses an adaptive inner-repeat count calibrated per benchmark case
so that pure payload time targets roughly 1 us and exceeds clock resolution on
the native timer.
CSV/JSON include both `inner_repeats` and `measured_total_us`;
`measured_total_us` is raw payload time before division, and reported
`*_us` values are per logical workload after division by the repeat count
used for each sample. If a payload window returns zero on the native timer, the
sample is retried with more repeats; the reported `inner_repeats` is the
maximum repeat count used for that row.

Anti-DCE barriers are used on every measured loop by accumulating return codes,
ids, query booleans, and fact counts into a volatile sink. State is reset at the
correct level for each sample. In particular, C40 interning workloads reset the
symbol table with `maelys_datalog_symbol_table_init`, not just the EDB.

## Engine Bounds

- `MAELYS_DATALOG_MAX_SYMBOLS = 512`
- `MAELYS_DATALOG_MAX_EDB_FACTS = 1024`
- `MAELYS_DATALOG_MAX_IDB_FACTS = 1024`
- `MAELYS_DATALOG_MAX_FACTS_PER_PRED = 64`
- `MAELYS_DATALOG_MAX_PREDICATES = 128`
- `MAELYS_DATALOG_STRING_POOL_BYTES = 32768`
- `MAELYS_DATALOG_MAX_STRING_BYTES = 1024`

All generated workloads stay inside these bounds. Fact-dense C42 workloads reuse
symbols deliberately so they stress EDB fact count without saturating the symbol
table.

## Selected -O2 Speedups

These speedups compare payload-only median workload time at size 64 on this
machine.

| Benchmark | Comparison | Speedup |
|---|---:|---:|
| C39 unary symbol-id insert | unit / batch | 1.14x |
| C39 binary symbol-id insert | unit / batch | 1.11x |
| C41 runtime string unary insert | unit / batch | 1.12x |
| C41 runtime string binary insert | composed-unit / batch | 1.09x |

Predicate dense range full-scan reference is not measured in the production
benchmark binary because the accepted C42 full-scan reference is a
`MAELYS_TESTING` path. The production run reports dense-range timings only.
Solver benchmark timings include the public finalize/solve path required by
the methodology; some scenarios therefore still include costs proportional to
total EDB size outside the predicate slice scan itself.

## Summary Table

The full CSV files are authoritative. This table mirrors the key columns for
graph/report consumers.

| opt_level | benchmark | mode | size | median_us | p95_us | ops_per_sec | op_unit |
|---|---|---|---:|---:|---:|---:|---|
| -O0 | intern_distinct_symbols | distinct | 1 | 0.041000 | 0.042000 | 28377649.76 | symbols/sec |
| -O0 | reintern_existing_symbols | reintern | 1 | 0.030935 | 0.037452 | 32030451.66 | symbols/sec |
| -O0 | mixed_intern_symbols | mixed | 1 | 0.035177 | 0.040355 | 28139327.80 | symbols/sec |
| -O0 | intern_distinct_symbols | distinct | 8 | 0.202286 | 0.220286 | 39142964.79 | symbols/sec |
| -O0 | reintern_existing_symbols | reintern | 8 | 0.178714 | 0.196286 | 43228422.07 | symbols/sec |
| -O0 | mixed_intern_symbols | mixed | 8 | 0.208286 | 0.226143 | 38169525.86 | symbols/sec |
| -O0 | intern_distinct_symbols | distinct | 16 | 0.389000 | 0.416667 | 40747201.81 | symbols/sec |
| -O0 | reintern_existing_symbols | reintern | 16 | 0.333333 | 0.361667 | 47030374.76 | symbols/sec |
| -O0 | mixed_intern_symbols | mixed | 16 | 0.375000 | 0.403000 | 42135423.25 | symbols/sec |
| -O0 | intern_distinct_symbols | distinct | 32 | 0.750000 | 0.805333 | 42023851.16 | symbols/sec |
| -O0 | reintern_existing_symbols | reintern | 32 | 0.652667 | 0.680667 | 48882500.22 | symbols/sec |
| -O0 | mixed_intern_symbols | mixed | 32 | 0.722000 | 0.750000 | 44525435.39 | symbols/sec |
| -O0 | intern_distinct_symbols | distinct | 64 | 1.459000 | 1.542000 | 43351475.47 | symbols/sec |
| -O0 | reintern_existing_symbols | reintern | 64 | 1.333000 | 1.375000 | 47871369.63 | symbols/sec |
| -O0 | mixed_intern_symbols | mixed | 64 | 1.417000 | 1.583000 | 44051711.20 | symbols/sec |
| -O0 | intern_distinct_symbols | distinct | 128 | 2.834000 | 2.958000 | 44056441.81 | symbols/sec |
| -O0 | reintern_existing_symbols | reintern | 128 | 2.625000 | 2.667000 | 48244048.55 | symbols/sec |
| -O0 | mixed_intern_symbols | mixed | 128 | 2.875000 | 2.958000 | 43609088.13 | symbols/sec |
| -O0 | intern_distinct_symbols | distinct | 256 | 6.125000 | 7.916000 | 39116092.44 | symbols/sec |
| -O0 | reintern_existing_symbols | reintern | 256 | 5.542000 | 6.875000 | 44272580.75 | symbols/sec |
| -O0 | mixed_intern_symbols | mixed | 256 | 5.708000 | 5.917000 | 43282950.96 | symbols/sec |
| -O0 | intern_distinct_symbols | distinct | 512 | 13.417000 | 13.875000 | 37196826.79 | symbols/sec |
| -O0 | reintern_existing_symbols | reintern | 512 | 13.084000 | 14.833000 | 38266895.30 | symbols/sec |
| -O0 | mixed_intern_symbols | mixed | 512 | 13.459000 | 13.666000 | 37583291.58 | symbols/sec |
| -O0 | edb_symbol_id_insert | unit_unary | 1 | 0.083000 | 0.084000 | 13614889.24 | facts/sec |
| -O0 | edb_symbol_id_insert | batch_unary | 1 | 0.065429 | 0.077429 | 15681671.04 | facts/sec |
| -O0 | edb_symbol_id_insert | unit_binary | 1 | 0.077000 | 0.083429 | 13428123.09 | facts/sec |
| -O0 | edb_symbol_id_insert | batch_binary | 1 | 0.069533 | 0.080533 | 14141231.31 | facts/sec |
| -O0 | edb_runtime_string_insert | unit_unary | 1 | 0.101143 | 0.113143 | 10026527.33 | facts/sec |
| -O0 | edb_runtime_string_insert | batch_unary | 1 | 0.125000 | 0.125000 | 7241863.77 | facts/sec |
| -O0 | edb_runtime_string_pair_insert | composed_unit_binary | 1 | 0.142857 | 0.178429 | 6771757.90 | facts/sec |
| -O0 | edb_runtime_string_pair_insert | batch_binary | 1 | 0.154857 | 0.196571 | 6083359.40 | facts/sec |
| -O0 | edb_symbol_id_insert | unit_unary | 8 | 0.792000 | 0.792000 | 10167302.97 | facts/sec |
| -O0 | edb_symbol_id_insert | batch_unary | 8 | 0.583000 | 0.584000 | 13766188.18 | facts/sec |
| -O0 | edb_symbol_id_insert | unit_binary | 8 | 0.833000 | 0.834000 | 9697898.34 | facts/sec |
| -O0 | edb_symbol_id_insert | batch_binary | 8 | 0.625000 | 0.667000 | 12748171.04 | facts/sec |
| -O0 | edb_runtime_string_insert | unit_unary | 8 | 1.000000 | 1.042000 | 7334187.77 | facts/sec |
| -O0 | edb_runtime_string_insert | batch_unary | 8 | 0.833000 | 0.875000 | 9131544.46 | facts/sec |
| -O0 | edb_runtime_string_pair_insert | composed_unit_binary | 8 | 1.292000 | 1.375000 | 6088011.34 | facts/sec |
| -O0 | edb_runtime_string_pair_insert | batch_binary | 8 | 1.083000 | 1.125000 | 7273897.71 | facts/sec |
| -O0 | edb_symbol_id_insert | unit_unary | 16 | 2.041000 | 2.084000 | 7625780.09 | facts/sec |
| -O0 | edb_symbol_id_insert | batch_unary | 16 | 1.625000 | 1.667000 | 9302990.85 | facts/sec |
| -O0 | edb_symbol_id_insert | unit_binary | 16 | 2.125000 | 2.167000 | 7382229.22 | facts/sec |
| -O0 | edb_symbol_id_insert | batch_binary | 16 | 1.750000 | 1.791000 | 9208701.99 | facts/sec |
| -O0 | edb_runtime_string_insert | unit_unary | 16 | 2.542000 | 2.625000 | 6288203.29 | facts/sec |
| -O0 | edb_runtime_string_insert | batch_unary | 16 | 2.083000 | 2.167000 | 7592535.21 | facts/sec |
| -O0 | edb_runtime_string_pair_insert | composed_unit_binary | 16 | 3.167000 | 3.334000 | 4926361.67 | facts/sec |
| -O0 | edb_runtime_string_pair_insert | batch_binary | 16 | 2.584000 | 3.208000 | 6032495.55 | facts/sec |
| -O0 | edb_symbol_id_insert | unit_unary | 32 | 6.167000 | 6.917000 | 5031669.64 | facts/sec |
| -O0 | edb_symbol_id_insert | batch_unary | 32 | 5.292000 | 5.375000 | 5971682.28 | facts/sec |
| -O0 | edb_symbol_id_insert | unit_binary | 32 | 6.291000 | 6.583000 | 4926112.17 | facts/sec |
| -O0 | edb_symbol_id_insert | batch_binary | 32 | 5.459000 | 5.583000 | 5671730.52 | facts/sec |
| -O0 | edb_runtime_string_insert | unit_unary | 32 | 7.125000 | 8.083000 | 4403368.36 | facts/sec |
| -O0 | edb_runtime_string_insert | batch_unary | 32 | 6.166000 | 6.417000 | 5142886.26 | facts/sec |
| -O0 | edb_runtime_string_pair_insert | composed_unit_binary | 32 | 8.375000 | 8.542000 | 3804332.71 | facts/sec |
| -O0 | edb_runtime_string_pair_insert | batch_binary | 32 | 7.250000 | 7.333000 | 4366371.90 | facts/sec |
| -O0 | edb_symbol_id_insert | unit_unary | 64 | 21.042000 | 25.042000 | 2969722.38 | facts/sec |
| -O0 | edb_symbol_id_insert | batch_unary | 64 | 18.917000 | 23.000000 | 3303046.46 | facts/sec |
| -O0 | edb_symbol_id_insert | unit_binary | 64 | 21.042000 | 23.583000 | 2978610.87 | facts/sec |
| -O0 | edb_symbol_id_insert | batch_binary | 64 | 19.250000 | 19.542000 | 3270915.74 | facts/sec |
| -O0 | edb_runtime_string_insert | unit_unary | 64 | 22.416000 | 26.916000 | 2802371.88 | facts/sec |
| -O0 | edb_runtime_string_insert | batch_unary | 64 | 20.625000 | 25.458000 | 2999196.17 | facts/sec |
| -O0 | edb_runtime_string_pair_insert | composed_unit_binary | 64 | 25.000000 | 25.542000 | 2529553.88 | facts/sec |
| -O0 | edb_runtime_string_pair_insert | batch_binary | 64 | 22.584000 | 22.792000 | 2790397.42 | facts/sec |
| -O0 | solver_predicate_dense_ranges | selectivity_1pct | 800 | 64.583000 | 75.375000 | 15029.16 | solve_calls/sec |
| -O0 | solver_predicate_dense_ranges | selectivity_10pct | 640 | 259.125000 | 283.042000 | 3796.10 | solve_calls/sec |
| -O0 | solver_predicate_dense_ranges | selectivity_50pct | 128 | 74.791500 | 82.875000 | 13190.63 | solve_calls/sec |
| -O0 | solver_predicate_dense_ranges | selectivity_100pct | 64 | 52.500000 | 63.917000 | 18396.14 | solve_calls/sec |
| -O0 | solver_predicate_dense_ranges | noise_total | 64 | 10.834000 | 13.792000 | 88884.62 | solve_calls/sec |
| -O0 | solver_predicate_dense_ranges | noise_total | 128 | 22.209000 | 24.875000 | 44169.03 | solve_calls/sec |
| -O0 | solver_predicate_dense_ranges | noise_total | 256 | 36.708000 | 40.625000 | 26872.03 | solve_calls/sec |
| -O0 | solver_predicate_dense_ranges | noise_total | 512 | 65.417000 | 76.125000 | 14869.99 | solve_calls/sec |
| -O0 | solver_predicate_dense_ranges | noise_total | 960 | 117.625000 | 131.709000 | 8351.21 | solve_calls/sec |
| -O0 | solver_predicate_dense_ranges | absent_predicate | 512 | 4.458000 | 4.709000 | 217395.46 | solve_calls/sec |
| -O0 | solver_repeated_solve | repeated | 1 | 9.167000 | 9.333000 | 108431.96 | solve_calls/sec |
| -O0 | solver_repeated_solve | repeated | 10 | 88.667000 | 100.833000 | 110453.85 | solve_calls/sec |
| -O0 | solver_repeated_solve | repeated | 100 | 952.812000 | 1242.041000 | 100744.21 | solve_calls/sec |
| -O0 | solver_repeated_solve | repeated | 1000 | 9515.937500 | 12300.125000 | 100000.04 | solve_calls/sec |
| -O0 | solver_join_bindings | simple_join | 8 | 8.708000 | 11.667000 | 110278.85 | solve_calls/sec |
| -O0 | solver_join_bindings | simple_join | 16 | 18.666000 | 22.167000 | 51715.14 | solve_calls/sec |
| -O0 | solver_join_bindings | simple_join | 32 | 50.583000 | 59.375000 | 19327.13 | solve_calls/sec |
| -O0 | solver_join_bindings | simple_join | 64 | 167.875000 | 189.875000 | 5799.76 | solve_calls/sec |
| -O0 | solver_join_bindings | noisy_join | 128 | 65.750000 | 75.834000 | 14780.44 | solve_calls/sec |
| -O0 | solver_join_bindings | noisy_join | 512 | 115.458000 | 130.708000 | 8470.24 | solve_calls/sec |
| -O0 | solver_join_bindings | noisy_join | 960 | 171.875000 | 184.708000 | 5747.83 | solve_calls/sec |
| -O2 | intern_distinct_symbols | distinct | 1 | 0.016129 | 0.020129 | 62781631.31 | symbols/sec |
| -O2 | reintern_existing_symbols | reintern | 1 | 0.013937 | 0.017937 | 69335763.39 | symbols/sec |
| -O2 | mixed_intern_symbols | mixed | 1 | 0.016417 | 0.022315 | 60924420.14 | symbols/sec |
| -O2 | intern_distinct_symbols | distinct | 8 | 0.097200 | 0.100067 | 81421732.00 | symbols/sec |
| -O2 | reintern_existing_symbols | reintern | 8 | 0.097267 | 0.102867 | 79643462.77 | symbols/sec |
| -O2 | mixed_intern_symbols | mixed | 8 | 0.091667 | 0.097267 | 86695242.38 | symbols/sec |
| -O2 | intern_distinct_symbols | distinct | 16 | 0.178714 | 0.208286 | 87941079.48 | symbols/sec |
| -O2 | reintern_existing_symbols | reintern | 16 | 0.178714 | 0.196571 | 88393952.91 | symbols/sec |
| -O2 | mixed_intern_symbols | mixed | 16 | 0.196286 | 0.208571 | 79382461.15 | symbols/sec |
| -O2 | intern_distinct_symbols | distinct | 32 | 0.347333 | 0.361333 | 90568259.22 | symbols/sec |
| -O2 | reintern_existing_symbols | reintern | 32 | 0.333333 | 0.375000 | 89849950.58 | symbols/sec |
| -O2 | mixed_intern_symbols | mixed | 32 | 0.361000 | 0.388667 | 86822905.69 | symbols/sec |
| -O2 | intern_distinct_symbols | distinct | 64 | 0.694667 | 0.722000 | 91185367.03 | symbols/sec |
| -O2 | reintern_existing_symbols | reintern | 64 | 0.667000 | 0.709000 | 94153231.44 | symbols/sec |
| -O2 | mixed_intern_symbols | mixed | 64 | 0.708000 | 0.709000 | 89497805.44 | symbols/sec |
| -O2 | intern_distinct_symbols | distinct | 128 | 1.375000 | 1.417000 | 92237095.45 | symbols/sec |
| -O2 | reintern_existing_symbols | reintern | 128 | 1.333000 | 1.334000 | 95496447.23 | symbols/sec |
| -O2 | mixed_intern_symbols | mixed | 128 | 1.458000 | 1.459000 | 87793903.81 | symbols/sec |
| -O2 | intern_distinct_symbols | distinct | 256 | 2.958000 | 3.042000 | 85206602.71 | symbols/sec |
| -O2 | reintern_existing_symbols | reintern | 256 | 2.958000 | 3.125000 | 83780051.18 | symbols/sec |
| -O2 | mixed_intern_symbols | mixed | 256 | 2.875000 | 2.959000 | 87261581.59 | symbols/sec |
| -O2 | intern_distinct_symbols | distinct | 512 | 6.708000 | 6.875000 | 75304759.98 | symbols/sec |
| -O2 | reintern_existing_symbols | reintern | 512 | 6.917000 | 7.125000 | 73497102.68 | symbols/sec |
| -O2 | mixed_intern_symbols | mixed | 512 | 6.792000 | 6.917000 | 74993580.87 | symbols/sec |
| -O2 | edb_symbol_id_insert | unit_unary | 1 | 0.028226 | 0.030935 | 34788697.94 | facts/sec |
| -O2 | edb_symbol_id_insert | batch_unary | 1 | 0.024460 | 0.027143 | 40823256.04 | facts/sec |
| -O2 | edb_symbol_id_insert | unit_binary | 1 | 0.026839 | 0.030935 | 37533583.48 | facts/sec |
| -O2 | edb_symbol_id_insert | batch_binary | 1 | 0.024258 | 0.029645 | 40214094.65 | facts/sec |
| -O2 | edb_runtime_string_insert | unit_unary | 1 | 0.044400 | 0.049933 | 22270160.81 | facts/sec |
| -O2 | edb_runtime_string_insert | batch_unary | 1 | 0.052867 | 0.061067 | 18296252.93 | facts/sec |
| -O2 | edb_runtime_string_pair_insert | composed_unit_binary | 1 | 0.055129 | 0.061839 | 17809520.85 | facts/sec |
| -O2 | edb_runtime_string_pair_insert | batch_binary | 1 | 0.063867 | 0.072200 | 15599877.70 | facts/sec |
| -O2 | edb_symbol_id_insert | unit_unary | 8 | 0.333333 | 0.333667 | 24290809.57 | facts/sec |
| -O2 | edb_symbol_id_insert | batch_unary | 8 | 0.208429 | 0.232143 | 37557048.15 | facts/sec |
| -O2 | edb_symbol_id_insert | unit_binary | 8 | 0.305667 | 0.319667 | 26110626.37 | facts/sec |
| -O2 | edb_symbol_id_insert | batch_binary | 8 | 0.222000 | 0.222667 | 36618802.84 | facts/sec |
| -O2 | edb_runtime_string_insert | unit_unary | 8 | 0.416667 | 0.430667 | 19174582.17 | facts/sec |
| -O2 | edb_runtime_string_insert | batch_unary | 8 | 0.319333 | 0.333333 | 24929755.22 | facts/sec |
| -O2 | edb_runtime_string_pair_insert | composed_unit_binary | 8 | 0.500000 | 0.500000 | 16215997.08 | facts/sec |
| -O2 | edb_runtime_string_pair_insert | batch_binary | 8 | 0.416000 | 0.417000 | 19942913.41 | facts/sec |
| -O2 | edb_symbol_id_insert | unit_unary | 16 | 0.833000 | 0.834000 | 18800504.32 | facts/sec |
| -O2 | edb_symbol_id_insert | batch_unary | 16 | 0.625000 | 0.750000 | 25286567.93 | facts/sec |
| -O2 | edb_symbol_id_insert | unit_binary | 16 | 0.792000 | 0.792000 | 20443078.17 | facts/sec |
| -O2 | edb_symbol_id_insert | batch_binary | 16 | 0.625000 | 0.625000 | 26155714.78 | facts/sec |
| -O2 | edb_runtime_string_insert | unit_unary | 16 | 1.000000 | 1.042000 | 15822612.69 | facts/sec |
| -O2 | edb_runtime_string_insert | batch_unary | 16 | 0.792000 | 0.834000 | 19965509.58 | facts/sec |
| -O2 | edb_runtime_string_pair_insert | composed_unit_binary | 16 | 1.166000 | 1.167000 | 13867762.22 | facts/sec |
| -O2 | edb_runtime_string_pair_insert | batch_binary | 16 | 0.958000 | 1.000000 | 15825445.34 | facts/sec |
| -O2 | edb_symbol_id_insert | unit_unary | 32 | 2.292000 | 2.375000 | 13417894.02 | facts/sec |
| -O2 | edb_symbol_id_insert | batch_unary | 32 | 1.875000 | 1.958000 | 16853911.87 | facts/sec |
| -O2 | edb_symbol_id_insert | unit_binary | 32 | 2.208000 | 2.292000 | 14103906.12 | facts/sec |
| -O2 | edb_symbol_id_insert | batch_binary | 32 | 1.875000 | 2.000000 | 16363473.66 | facts/sec |
| -O2 | edb_runtime_string_insert | unit_unary | 32 | 2.666000 | 2.792000 | 11704321.05 | facts/sec |
| -O2 | edb_runtime_string_insert | batch_unary | 32 | 2.209000 | 2.292000 | 14247050.08 | facts/sec |
| -O2 | edb_runtime_string_pair_insert | composed_unit_binary | 32 | 2.959000 | 3.000000 | 10743056.96 | facts/sec |
| -O2 | edb_runtime_string_pair_insert | batch_binary | 32 | 2.583000 | 2.625000 | 12340974.97 | facts/sec |
| -O2 | edb_symbol_id_insert | unit_unary | 64 | 7.292000 | 7.375000 | 8607949.33 | facts/sec |
| -O2 | edb_symbol_id_insert | batch_unary | 64 | 6.417000 | 6.459000 | 9876674.29 | facts/sec |
| -O2 | edb_symbol_id_insert | unit_binary | 64 | 7.000000 | 7.292000 | 8891917.08 | facts/sec |
| -O2 | edb_symbol_id_insert | batch_binary | 64 | 6.333000 | 6.625000 | 9948211.79 | facts/sec |
| -O2 | edb_runtime_string_insert | unit_unary | 64 | 7.875000 | 8.042000 | 8067185.54 | facts/sec |
| -O2 | edb_runtime_string_insert | batch_unary | 64 | 7.000000 | 7.042000 | 8989074.62 | facts/sec |
| -O2 | edb_runtime_string_pair_insert | composed_unit_binary | 64 | 8.500000 | 8.750000 | 7250209.60 | facts/sec |
| -O2 | edb_runtime_string_pair_insert | batch_binary | 64 | 7.833000 | 8.125000 | 8063692.08 | facts/sec |
| -O2 | solver_predicate_dense_ranges | selectivity_1pct | 800 | 27.000000 | 30.084000 | 36553.20 | solve_calls/sec |
| -O2 | solver_predicate_dense_ranges | selectivity_10pct | 640 | 95.395500 | 110.417000 | 10270.08 | solve_calls/sec |
| -O2 | solver_predicate_dense_ranges | selectivity_50pct | 128 | 27.417000 | 35.291000 | 34566.14 | solve_calls/sec |
| -O2 | solver_predicate_dense_ranges | selectivity_100pct | 64 | 19.834000 | 22.459000 | 49357.64 | solve_calls/sec |
| -O2 | solver_predicate_dense_ranges | noise_total | 64 | 5.250000 | 6.541000 | 182770.52 | solve_calls/sec |
| -O2 | solver_predicate_dense_ranges | noise_total | 128 | 9.208000 | 11.375000 | 105246.63 | solve_calls/sec |
| -O2 | solver_predicate_dense_ranges | noise_total | 256 | 15.167000 | 16.208000 | 65322.88 | solve_calls/sec |
| -O2 | solver_predicate_dense_ranges | noise_total | 512 | 26.000000 | 30.334000 | 37770.55 | solve_calls/sec |
| -O2 | solver_predicate_dense_ranges | noise_total | 960 | 45.708000 | 53.041000 | 21483.99 | solve_calls/sec |
| -O2 | solver_predicate_dense_ranges | absent_predicate | 512 | 3.291000 | 3.292000 | 301498.24 | solve_calls/sec |
| -O2 | solver_repeated_solve | repeated | 1 | 4.667000 | 5.125000 | 209931.08 | solve_calls/sec |
| -O2 | solver_repeated_solve | repeated | 10 | 49.209000 | 50.459000 | 203019.89 | solve_calls/sec |
| -O2 | solver_repeated_solve | repeated | 100 | 450.042000 | 501.083000 | 218748.73 | solve_calls/sec |
| -O2 | solver_repeated_solve | repeated | 1000 | 4557.333000 | 5707.583000 | 209622.04 | solve_calls/sec |
| -O2 | solver_join_bindings | simple_join | 8 | 5.333000 | 5.375000 | 182814.69 | solve_calls/sec |
| -O2 | solver_join_bindings | simple_join | 16 | 9.375000 | 9.459000 | 104317.38 | solve_calls/sec |
| -O2 | solver_join_bindings | simple_join | 32 | 22.708000 | 23.042000 | 43496.76 | solve_calls/sec |
| -O2 | solver_join_bindings | simple_join | 64 | 73.667000 | 86.084000 | 13418.06 | solve_calls/sec |
| -O2 | solver_join_bindings | noisy_join | 128 | 28.458000 | 36.500000 | 33111.09 | solve_calls/sec |
| -O2 | solver_join_bindings | noisy_join | 512 | 48.542000 | 54.208000 | 20195.88 | solve_calls/sec |
| -O2 | solver_join_bindings | noisy_join | 960 | 71.042000 | 80.333000 | 13785.38 | solve_calls/sec |

## Limits

These are hot-cache / warm-pool microbenchmarks. They isolate algorithmic costs
in the current native engine. They do not model cold-start page faults,
allocator behavior, browser/WASM boundary costs, or production workload
variability.

Runtime-string insertion measures a composed path: runtime symbol interning plus
fact insertion. It must not be interpreted as pure insertion cost.

`-O0` is reported to make optimization effects visible and to help catch
accidental optimizer-sensitive artifacts. Regression thresholds are evaluated
only under `-O2`.

Optional C43 instrumentation counters were not compiled in this pass; CSV/JSON
report timing-only solver metrics. No `MAELYS_BENCH_INSTRUMENT` engine path is
introduced or enabled.

## Validation

| Check | Status | Log |
|---|---|---|
| make -f Makefile.bench bench | PASS | /tmp/P3-C49-bench.log |
| make test | PASS | /tmp/P3-C49-native.log |
| make -f Makefile.asan test-asan-linux | PASS | /tmp/P3-C49-asan-linux.log |
| git -C extern/maelys-datalog diff --check | PASS | /tmp/P3-C49-sub-diffcheck.log |
| git diff --check | PASS | /tmp/P3-C49-parent-diffcheck.log |

## Artifacts

- `bench/results/P3-C49-datalog-bench-O0.csv`
- `bench/results/P3-C49-datalog-bench-O2.csv`
- `bench/results/P3-C49-datalog-bench-O0.json`
- `bench/results/P3-C49-datalog-bench-O2.json`

Results are machine-dependent and intended for regression tracking, not
universal performance claims.
