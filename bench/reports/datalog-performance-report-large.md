# Datalog Performance Report

Generated: 2026-06-28T11:49:59Z

Measured commit: `3be8b71340786098d5befda29fcb6e6cf4f27050`

Host: `MAC-QXQWJGVJXW`

OS: `Darwin 24.5.0 arm64`

Compiler: `Apple clang version 17.0.0 (clang-1700.0.13.5)`

CPU: `Apple M2 Max`

Source tree clean before benchmark run: `false`

Generated artifacts dirty after benchmark run: `true`

Optimization levels: `-O0` and `-O2`

Engine size profile: `large`

## Build Matrix

| opt_level | compiler | cflags |
|---|---|---|
| -O0 | Apple clang version 17.0.0 (clang-1700.0.13.5) | -Wall -Wextra -g -I. -DMAELYS_DATALOG_PROFILE_LARGE -O0 |
| -O2 | Apple clang version 17.0.0 (clang-1700.0.13.5) | -Wall -Wextra -g -I. -DMAELYS_DATALOG_PROFILE_LARGE -O2 |

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
correct level for each sample. In particular, symbol-interning workloads reset
the symbol table with `maelys_datalog_symbol_table_init`, not just the EDB.

## Engine Bounds

- `MAELYS_DATALOG_MAX_SYMBOLS = 512`
- `MAELYS_DATALOG_MAX_EDB_FACTS = 2048`
- `MAELYS_DATALOG_MAX_IDB_FACTS = 2048`
- `MAELYS_DATALOG_MAX_FACTS_PER_PRED = 256`
- `MAELYS_DATALOG_MAX_PREDICATES = 128`
- `MAELYS_DATALOG_STRING_POOL_BYTES = 32768`
- `MAELYS_DATALOG_MAX_STRING_BYTES = 1024`

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
| Sélectivité pure du solveur | solver_selectivity_pure | sélectivité variable à taille fixe |
| Taille pure du solveur | solver_size_pure | taille variable à sélectivité fixe |

Non mesuré dans cette pass (axe différent du harnais actuel) :

- Performance du parsing et de l'évaluation des filtres arithmétiques
  (corpus arithmétique et expressions `+ - *` dans les comparaisons). Cet axe
  concerne le parser/évaluateur, pas l'ingestion ni le solve, et fera l'objet
  d'une pass de benchmark dédiée ultérieure (parsing/évaluation de règles).

## Selected -O2 Speedups

These speedups compare payload-only median workload time at size 64 on this
machine.

| Feature | Comparaison | Accélération |
|---|---:|---:|
| Insertion par identifiants de symboles (unaire) | unit / batch | 1.18x |
| Insertion par identifiants de symboles (binaire) | unit / batch | 1.14x |
| Insertion de chaînes runtime (unaire) | unit / batch | 1.15x |
| Insertion de paires de chaînes runtime (binaire) | composed-unit / batch | 1.13x |

Predicate dense range full-scan reference is not measured in the production
benchmark binary because the full-scan reference path is `MAELYS_TESTING`-only.
The production run reports dense-range timings only.
Solver benchmark timings include the public finalize/solve path required by
the methodology; some scenarios therefore still include costs proportional to
total EDB size outside the predicate slice scan itself.

## Summary Table

The full CSV files are authoritative. This table mirrors the key columns for
graph/report consumers.

| opt_level | feature | benchmark | mode | size | median_us | p95_us | ops_per_sec | op_unit |
|---|---|---|---|---:|---:|---:|---:|---|
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 1 | 0.042000 | 0.042000 | 24578781.14 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 1 | 0.030677 | 0.035065 | 33403156.71 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 1 | 0.034903 | 0.039032 | 29014861.22 | symbols/sec |
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 8 | 0.202714 | 0.220143 | 38980710.12 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 8 | 0.184214 | 0.196429 | 43885152.56 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 8 | 0.208571 | 0.226000 | 37773775.56 | symbols/sec |
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 16 | 0.389000 | 0.416667 | 40486069.00 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 16 | 0.347000 | 0.361000 | 46662784.58 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 16 | 0.388667 | 0.403000 | 41602998.19 | symbols/sec |
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 32 | 0.750000 | 0.777333 | 42849166.03 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 32 | 0.653000 | 0.680333 | 48730989.20 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 32 | 0.722333 | 0.750000 | 43803174.09 | symbols/sec |
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 64 | 1.417000 | 1.500000 | 43924639.05 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 64 | 1.333000 | 1.375000 | 48492930.64 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 64 | 1.375000 | 1.417000 | 46434113.98 | symbols/sec |
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 128 | 2.875000 | 2.917000 | 44684702.30 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 128 | 2.667000 | 2.709000 | 48109810.64 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 128 | 2.917000 | 3.000000 | 43682324.39 | symbols/sec |
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 256 | 6.042000 | 6.625000 | 41671509.35 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 256 | 5.584000 | 5.708000 | 45590799.78 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 256 | 5.750000 | 5.875000 | 44335668.11 | symbols/sec |
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 512 | 13.292000 | 13.625000 | 38226453.81 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 512 | 12.958000 | 13.208000 | 39322386.20 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 512 | 13.417000 | 13.666000 | 37942120.18 | symbols/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 1 | 0.083000 | 0.125000 | 12699380.27 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 1 | 0.065429 | 0.083286 | 15551857.67 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 1 | 0.077857 | 0.084000 | 12859916.92 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 1 | 0.069533 | 0.080400 | 14231607.07 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 1 | 0.101286 | 0.119143 | 9678467.48 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 1 | 0.119143 | 0.131000 | 8223568.28 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 1 | 0.125000 | 0.167000 | 6940973.96 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 1 | 0.166000 | 0.167000 | 6481091.42 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 8 | 0.791000 | 0.792000 | 10099441.63 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 8 | 0.583000 | 0.584000 | 13953082.76 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 8 | 0.833000 | 0.834000 | 9755371.86 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 8 | 0.625000 | 0.625000 | 13057536.40 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 8 | 1.000000 | 1.042000 | 7975793.47 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 8 | 0.833000 | 0.875000 | 9664263.49 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 8 | 1.292000 | 1.375000 | 6113875.52 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 8 | 1.083000 | 1.084000 | 7491022.48 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 16 | 2.042000 | 2.084000 | 7785857.77 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 16 | 1.625000 | 1.666000 | 9863142.73 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 16 | 2.125000 | 2.167000 | 7426058.50 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 16 | 1.709000 | 1.750000 | 9293291.64 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 16 | 2.500000 | 2.583000 | 6382478.82 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 16 | 2.084000 | 2.125000 | 7634483.82 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 16 | 3.167000 | 3.292000 | 5032291.59 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 16 | 2.625000 | 2.667000 | 6107476.31 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 32 | 6.250000 | 6.417000 | 5088513.10 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 32 | 5.375000 | 5.541000 | 5929304.53 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 32 | 6.333000 | 6.500000 | 5006029.14 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 32 | 5.459000 | 5.667000 | 5787795.89 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 32 | 7.042000 | 7.250000 | 4489779.37 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 32 | 6.167000 | 6.417000 | 5127694.00 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 32 | 8.292000 | 8.833000 | 3816427.91 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 32 | 7.084000 | 7.167000 | 4490805.15 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 64 | 20.750000 | 21.750000 | 3055027.20 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 64 | 19.250000 | 19.375000 | 3320907.09 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 64 | 21.334000 | 21.500000 | 2985864.27 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 64 | 19.625000 | 20.583000 | 3232411.06 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 64 | 22.500000 | 29.041000 | 2714275.86 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 64 | 20.708000 | 26.375000 | 2972235.65 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 64 | 25.209000 | 32.125000 | 2446912.98 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 64 | 22.583000 | 28.417000 | 2762838.42 | facts/sec |
| -O0 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_1pct | 800 | 25.250000 | 33.000000 | 37383.53 | solve_calls/sec |
| -O0 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_10pct | 640 | 124.708000 | 129.166000 | 7970.11 | solve_calls/sec |
| -O0 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_50pct | 128 | 78.209000 | 85.750000 | 12255.69 | solve_calls/sec |
| -O0 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_100pct | 64 | 62.000000 | 64.167000 | 16072.53 | solve_calls/sec |
| -O0 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | noise_total | 64 | 13.750000 | 14.792000 | 70469.02 | solve_calls/sec |
| -O0 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | noise_total | 128 | 26.792000 | 27.083000 | 38023.71 | solve_calls/sec |
| -O0 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | noise_total | 256 | 26.792000 | 27.875000 | 40873.30 | solve_calls/sec |
| -O0 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | noise_total | 512 | 27.333000 | 28.375000 | 36298.96 | solve_calls/sec |
| -O0 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | noise_total | 960 | 47.750000 | 48.292000 | 21730.87 | solve_calls/sec |
| -O0 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 256 | 27.209000 | 27.666000 | 39385.87 | solve_calls/sec |
| -O0 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 256 | 36.959000 | 42.708000 | 26627.08 | solve_calls/sec |
| -O0 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 256 | 56.583000 | 58.333000 | 17568.54 | solve_calls/sec |
| -O0 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 256 | 129.354500 | 134.750000 | 7764.46 | solve_calls/sec |
| -O0 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 256 | 268.875000 | 278.792000 | 3723.62 | solve_calls/sec |
| -O0 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 256 | 654.834000 | 670.542000 | 1524.50 | solve_calls/sec |
| -O0 | Résolution : taille pure | solver_size_pure | size_pure | 64 | 15.791000 | 16.042000 | 67531.09 | solve_calls/sec |
| -O0 | Résolution : taille pure | solver_size_pure | size_pure | 128 | 29.750000 | 29.959000 | 33686.36 | solve_calls/sec |
| -O0 | Résolution : taille pure | solver_size_pure | size_pure | 256 | 62.458000 | 63.167000 | 16007.06 | solve_calls/sec |
| -O0 | Résolution : taille pure | solver_size_pure | size_pure | 512 | 176.167000 | 183.791000 | 5637.10 | solve_calls/sec |
| -O0 | Résolution : taille pure | solver_size_pure | size_pure | 1024 | 623.937500 | 637.875000 | 1595.12 | solve_calls/sec |
| -O0 | Résolution : taille pure | solver_size_pure | size_pure | 2048 | 2405.041000 | 2500.042000 | 414.77 | solve_calls/sec |
| -O0 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | absent_predicate | 512 | 7.167000 | 8.042000 | 135826.22 | solve_calls/sec |
| -O0 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 1 | 13.625000 | 13.709000 | 73207.12 | solve_calls/sec |
| -O0 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 10 | 121.750000 | 132.416000 | 80843.62 | solve_calls/sec |
| -O0 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 100 | 1233.500000 | 1919.625000 | 74495.07 | solve_calls/sec |
| -O0 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 1000 | 13372.958000 | 19249.042000 | 65949.86 | solve_calls/sec |
| -O0 | Résolution : jointures | solver_join_bindings | simple_join | 8 | 12.083000 | 15.417000 | 74180.17 | solve_calls/sec |
| -O0 | Résolution : jointures | solver_join_bindings | simple_join | 16 | 28.375000 | 28.541000 | 35149.08 | solve_calls/sec |
| -O0 | Résolution : jointures | solver_join_bindings | simple_join | 32 | 60.375000 | 60.584000 | 16939.31 | solve_calls/sec |
| -O0 | Résolution : jointures | solver_join_bindings | simple_join | 64 | 177.791000 | 182.250000 | 5604.19 | solve_calls/sec |
| -O0 | Résolution : jointures | solver_join_bindings | noisy_join | 128 | 75.375000 | 76.500000 | 13555.54 | solve_calls/sec |
| -O0 | Résolution : jointures | solver_join_bindings | noisy_join | 512 | 79.333000 | 84.375000 | 12567.47 | solve_calls/sec |
| -O0 | Résolution : jointures | solver_join_bindings | noisy_join | 960 | 99.792000 | 101.917000 | 10097.77 | solve_calls/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 1 | 0.017419 | 0.024129 | 57672058.07 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 1 | 0.014508 | 0.019127 | 69176340.37 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 1 | 0.016524 | 0.018540 | 60780420.60 | symbols/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 8 | 0.097267 | 0.105667 | 81326213.68 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 8 | 0.099933 | 0.116600 | 79912628.86 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 8 | 0.094400 | 0.113933 | 83655464.97 | symbols/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 16 | 0.178714 | 0.232000 | 85721939.46 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 16 | 0.178571 | 0.226571 | 85977790.09 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 16 | 0.196429 | 0.208429 | 81699525.19 | symbols/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 32 | 0.347000 | 0.375000 | 91909036.86 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 32 | 0.347000 | 0.361000 | 93027041.80 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 32 | 0.361333 | 0.375000 | 87958161.23 | symbols/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 64 | 0.694667 | 0.709000 | 91106228.91 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 64 | 0.680667 | 0.708333 | 93744415.62 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 64 | 0.708333 | 0.722667 | 90040321.18 | symbols/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 128 | 1.375000 | 1.417000 | 92421308.31 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 128 | 1.333000 | 1.334000 | 95927215.23 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 128 | 1.458000 | 1.459000 | 88329529.87 | symbols/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 256 | 2.958000 | 3.083000 | 85569304.95 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 256 | 2.917000 | 2.959000 | 86930271.40 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 256 | 2.916000 | 2.917000 | 88094660.47 | symbols/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 512 | 6.708000 | 6.792000 | 76257610.12 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 512 | 6.958000 | 7.042000 | 73655308.69 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 512 | 6.833000 | 6.917000 | 74807691.63 | symbols/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 1 | 0.042000 | 0.042000 | 24377163.47 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 1 | 0.026871 | 0.033065 | 36129820.27 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 1 | 0.032355 | 0.037613 | 30750093.99 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 1 | 0.028444 | 0.032381 | 35072990.79 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 1 | 0.049774 | 0.055226 | 19864002.07 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 1 | 0.055016 | 0.059129 | 18245661.95 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 1 | 0.063733 | 0.069733 | 15941911.92 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 1 | 0.066800 | 0.075000 | 14509043.00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 8 | 0.347667 | 0.375000 | 22580711.93 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 8 | 0.220143 | 0.226286 | 36582824.49 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 8 | 0.347000 | 0.361333 | 23208205.65 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 8 | 0.226143 | 0.238000 | 35418357.21 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 8 | 0.445000 | 0.458667 | 17753921.77 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 8 | 0.333000 | 0.375000 | 23505767.73 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 8 | 0.541667 | 0.542000 | 14583699.19 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 8 | 0.417000 | 0.444667 | 18814055.98 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 16 | 0.875000 | 0.917000 | 17712948.39 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 16 | 0.625000 | 0.639000 | 25553961.98 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 16 | 0.875000 | 0.875000 | 18531967.06 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 16 | 0.652667 | 0.667000 | 24647983.15 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 16 | 1.083000 | 1.125000 | 14665605.85 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 16 | 0.833000 | 0.875000 | 18961142.69 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 16 | 1.250000 | 1.375000 | 12466010.64 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 16 | 1.000000 | 1.041000 | 15776478.85 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 32 | 2.417000 | 2.417000 | 13218858.02 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 32 | 1.875000 | 1.958000 | 16881314.34 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 32 | 2.375000 | 2.375000 | 13455984.63 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 32 | 1.917000 | 1.917000 | 16611623.36 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 32 | 2.791000 | 2.833000 | 11492952.49 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 32 | 2.292000 | 2.292000 | 13937282.23 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 32 | 3.167000 | 3.209000 | 10082201.45 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 32 | 2.750000 | 2.750000 | 11682724.94 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 64 | 7.500000 | 7.708000 | 8518447.63 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 64 | 6.375000 | 6.500000 | 9992519.66 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 64 | 7.292000 | 7.334000 | 8764709.99 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 64 | 6.375000 | 6.459000 | 9993376.27 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 64 | 8.125000 | 8.292000 | 7852124.86 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 64 | 7.042000 | 7.208000 | 8998882.73 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 64 | 8.812500 | 8.875000 | 7245031.49 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 64 | 7.833000 | 7.917000 | 8158715.57 | facts/sec |
| -O2 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_1pct | 800 | 12.250000 | 13.333000 | 79393.25 | solve_calls/sec |
| -O2 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_10pct | 640 | 47.250000 | 50.084000 | 20920.79 | solve_calls/sec |
| -O2 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_50pct | 128 | 36.708000 | 37.083000 | 28194.21 | solve_calls/sec |
| -O2 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_100pct | 64 | 28.875000 | 29.084000 | 34588.12 | solve_calls/sec |
| -O2 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | noise_total | 64 | 14.625000 | 14.792000 | 68071.82 | solve_calls/sec |
| -O2 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | noise_total | 128 | 16.708000 | 16.792000 | 60335.16 | solve_calls/sec |
| -O2 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | noise_total | 256 | 13.042000 | 16.709000 | 68742.07 | solve_calls/sec |
| -O2 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | noise_total | 512 | 13.542000 | 19.292000 | 65547.72 | solve_calls/sec |
| -O2 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | noise_total | 960 | 18.042000 | 18.625000 | 55146.27 | solve_calls/sec |
| -O2 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 256 | 10.875000 | 11.917000 | 89445.35 | solve_calls/sec |
| -O2 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 256 | 22.458000 | 22.750000 | 45302.57 | solve_calls/sec |
| -O2 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 256 | 29.375000 | 29.625000 | 34671.82 | solve_calls/sec |
| -O2 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 256 | 52.584000 | 53.333000 | 19006.84 | solve_calls/sec |
| -O2 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 256 | 102.583000 | 106.417000 | 9797.95 | solve_calls/sec |
| -O2 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 256 | 242.542000 | 249.625000 | 4109.28 | solve_calls/sec |
| -O2 | Résolution : taille pure | solver_size_pure | size_pure | 64 | 7.750000 | 7.834000 | 127962.48 | solve_calls/sec |
| -O2 | Résolution : taille pure | solver_size_pure | size_pure | 128 | 12.083000 | 12.208000 | 85205.48 | solve_calls/sec |
| -O2 | Résolution : taille pure | solver_size_pure | size_pure | 256 | 28.792000 | 28.958000 | 34677.64 | solve_calls/sec |
| -O2 | Résolution : taille pure | solver_size_pure | size_pure | 512 | 71.291000 | 72.334000 | 14076.76 | solve_calls/sec |
| -O2 | Résolution : taille pure | solver_size_pure | size_pure | 1024 | 229.625000 | 235.958000 | 4341.08 | solve_calls/sec |
| -O2 | Résolution : taille pure | solver_size_pure | size_pure | 2048 | 864.791000 | 901.708000 | 1152.04 | solve_calls/sec |
| -O2 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | absent_predicate | 512 | 5.250000 | 7.416000 | 181141.34 | solve_calls/sec |
| -O2 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 1 | 7.209000 | 9.084000 | 134046.19 | solve_calls/sec |
| -O2 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 10 | 69.375000 | 85.959000 | 138731.12 | solve_calls/sec |
| -O2 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 100 | 685.375000 | 771.125000 | 143428.68 | solve_calls/sec |
| -O2 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 1000 | 7925.583500 | 14239.416000 | 105111.02 | solve_calls/sec |
| -O2 | Résolution : jointures | solver_join_bindings | simple_join | 8 | 7.167000 | 7.250000 | 138487.45 | solve_calls/sec |
| -O2 | Résolution : jointures | solver_join_bindings | simple_join | 16 | 14.541000 | 15.042000 | 68489.13 | solve_calls/sec |
| -O2 | Résolution : jointures | solver_join_bindings | simple_join | 32 | 31.459000 | 31.750000 | 31617.11 | solve_calls/sec |
| -O2 | Résolution : jointures | solver_join_bindings | simple_join | 64 | 80.291000 | 80.834000 | 12719.72 | solve_calls/sec |
| -O2 | Résolution : jointures | solver_join_bindings | noisy_join | 128 | 37.958000 | 38.125000 | 26853.95 | solve_calls/sec |
| -O2 | Résolution : jointures | solver_join_bindings | noisy_join | 512 | 41.333000 | 43.583000 | 24065.76 | solve_calls/sec |
| -O2 | Résolution : jointures | solver_join_bindings | noisy_join | 960 | 42.458000 | 47.833000 | 22811.22 | solve_calls/sec |

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

Optional solver instrumentation counters were not compiled in this pass;
CSV/JSON report timing-only solver metrics. No `MAELYS_BENCH_INSTRUMENT`
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

- CSV O0/O2 under `bench/results`
- JSON O0/O2 under `bench/results`

Results are machine-dependent and intended for regression tracking, not
universal performance claims.
