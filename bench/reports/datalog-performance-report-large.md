# Datalog Performance Report

Generated: 2026-09-18T10:25:10Z

Measured commit: `078437943e2a4944dbfdea6906751dbea4b1567e`

Host: `MAC-QXQWJGVJXW`

OS: `Darwin 25.6.0 arm64`

Compiler: `Apple clang version 21.0.0 (clang-2100.1.1.101)`

CPU: `Apple M2 Max`

Source tree clean before benchmark run: `true`

Generated artifacts dirty after benchmark run: `true`

Optimization levels: `-O0` and `-O2`

Engine size profile: `large`

## Build Matrix

| opt_level | compiler | cflags |
|---|---|---|
| -O0 | Apple clang version 21.0.0 (clang-2100.1.1.101) | -Wall -Wextra -g -I. -DMAELYS_DATALOG_PROFILE_LARGE -O0 |
| -O2 | Apple clang version 21.0.0 (clang-2100.1.1.101) | -Wall -Wextra -g -I. -DMAELYS_DATALOG_PROFILE_LARGE -O2 |

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
| Insertion par identifiants de symboles (unaire) | unit / batch | 1.14x |
| Insertion par identifiants de symboles (binaire) | unit / batch | 1.10x |
| Insertion de chaînes runtime (unaire) | unit / batch | 1.14x |
| Insertion de paires de chaînes runtime (binaire) | composed-unit / batch | 1.09x |

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
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 1 | 0.034903 | 0.039000 | 28892683.25 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 1 | 0.029581 | 0.037677 | 33753730.60 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 1 | 0.034935 | 0.039032 | 28744664.85 | symbols/sec |
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 8 | 0.202429 | 0.220286 | 39100848.07 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 8 | 0.178571 | 0.196429 | 44583892.95 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 8 | 0.208429 | 0.226286 | 37784301.57 | symbols/sec |
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 16 | 0.389000 | 0.416667 | 41165003.91 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 16 | 0.333333 | 0.416333 | 46266487.26 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 16 | 0.375000 | 0.403000 | 41638546.19 | symbols/sec |
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 32 | 0.750000 | 0.834000 | 42035959.14 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 32 | 0.653000 | 0.694333 | 48592955.54 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 32 | 0.722333 | 0.763667 | 44126224.90 | symbols/sec |
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 64 | 1.459000 | 1.708000 | 42701454.12 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 64 | 1.375000 | 1.667000 | 45108097.33 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 64 | 1.416000 | 1.667000 | 44924102.83 | symbols/sec |
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 128 | 2.917000 | 3.084000 | 43301788.43 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 128 | 2.625000 | 2.750000 | 48208326.26 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 128 | 2.875000 | 3.000000 | 43926176.57 | symbols/sec |
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 256 | 6.166000 | 6.500000 | 41087575.28 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 256 | 5.584000 | 5.750000 | 45285064.17 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 256 | 5.791000 | 6.333000 | 43693448.13 | symbols/sec |
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 512 | 13.583000 | 14.375000 | 37247884.75 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 512 | 12.959000 | 13.458000 | 39121541.08 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 512 | 13.458000 | 14.458000 | 37562375.00 | symbols/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 1 | 0.071429 | 0.083286 | 14221485.41 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 1 | 0.059571 | 0.077286 | 15936980.63 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 1 | 0.072267 | 0.080600 | 13915741.12 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 1 | 0.065571 | 0.083000 | 14986597.70 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 1 | 0.097333 | 0.111333 | 10046380.79 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 1 | 0.111333 | 0.125000 | 8633913.44 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 1 | 0.136857 | 0.155000 | 7304792.47 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 1 | 0.152667 | 0.167000 | 6653050.09 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 8 | 0.750000 | 0.792000 | 10597724.93 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 8 | 0.542000 | 0.584000 | 14245242.53 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 8 | 0.750000 | 0.792000 | 10335490.01 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 8 | 0.584000 | 0.625000 | 13365879.25 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 8 | 0.959000 | 1.042000 | 8134247.62 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 8 | 0.833000 | 0.917000 | 9194979.54 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 8 | 1.292000 | 1.542000 | 6083969.43 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 8 | 1.042000 | 1.125000 | 7541186.66 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 16 | 2.000000 | 2.084000 | 7845213.93 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 16 | 1.625000 | 1.708000 | 9821717.41 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 16 | 2.125000 | 2.250000 | 7350276.03 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 16 | 1.708000 | 1.792000 | 9187445.13 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 16 | 2.458000 | 2.584000 | 6477601.67 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 16 | 2.083000 | 2.417000 | 7546707.04 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 16 | 3.042000 | 3.167000 | 5238684.28 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 16 | 2.542000 | 2.667000 | 6167345.52 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 32 | 6.166000 | 6.500000 | 5117654.07 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 32 | 5.375000 | 5.666000 | 5893277.17 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 32 | 6.166000 | 6.333000 | 5177221.13 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 32 | 5.417000 | 5.708000 | 5831966.47 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 32 | 7.041000 | 7.709000 | 4459558.63 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 32 | 6.250000 | 6.625000 | 5054780.39 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 32 | 8.166000 | 8.583000 | 3879981.50 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 32 | 7.084000 | 7.542000 | 4459051.55 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 64 | 20.916000 | 22.083000 | 3016774.97 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 64 | 19.166000 | 20.125000 | 3303589.50 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 64 | 20.750000 | 21.250000 | 3055180.77 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 64 | 19.458000 | 22.083000 | 3211267.86 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 64 | 22.292000 | 23.041000 | 2846188.84 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 64 | 20.667000 | 23.334000 | 3028074.27 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 64 | 24.750000 | 25.916000 | 2554098.19 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 64 | 22.583000 | 23.916000 | 2802260.09 | facts/sec |
| -O0 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_1pct | 800 | 21.000000 | 26.542000 | 45615.22 | solve_calls/sec |
| -O0 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_10pct | 640 | 122.292000 | 133.042000 | 8043.68 | solve_calls/sec |
| -O0 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_50pct | 128 | 77.083500 | 85.292000 | 12704.33 | solve_calls/sec |
| -O0 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_100pct | 64 | 54.625000 | 58.208000 | 18096.12 | solve_calls/sec |
| -O0 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 256 | 15.666000 | 19.500000 | 61542.12 | solve_calls/sec |
| -O0 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 256 | 30.958000 | 36.250000 | 31220.35 | solve_calls/sec |
| -O0 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 256 | 52.542000 | 58.250000 | 18662.14 | solve_calls/sec |
| -O0 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 256 | 123.041000 | 135.959000 | 7941.88 | solve_calls/sec |
| -O0 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 256 | 265.437000 | 286.875000 | 3720.67 | solve_calls/sec |
| -O0 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 256 | 655.375000 | 693.833000 | 1513.76 | solve_calls/sec |
| -O0 | Résolution : taille pure | solver_size_pure | size_pure | 64 | 8.959000 | 10.000000 | 108846.03 | solve_calls/sec |
| -O0 | Résolution : taille pure | solver_size_pure | size_pure | 128 | 19.042000 | 22.083000 | 51217.67 | solve_calls/sec |
| -O0 | Résolution : taille pure | solver_size_pure | size_pure | 256 | 52.625000 | 58.375000 | 18720.37 | solve_calls/sec |
| -O0 | Résolution : taille pure | solver_size_pure | size_pure | 512 | 171.812500 | 189.417000 | 5743.05 | solve_calls/sec |
| -O0 | Résolution : taille pure | solver_size_pure | size_pure | 1024 | 625.875000 | 666.167000 | 1579.89 | solve_calls/sec |
| -O0 | Résolution : taille pure | solver_size_pure | size_pure | 2048 | 2416.458500 | 2507.792000 | 411.72 | solve_calls/sec |
| -O0 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | absent_predicate | 512 | 4.458000 | 4.792000 | 222090.20 | solve_calls/sec |
| -O0 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 1 | 10.417000 | 10.625000 | 95648.71 | solve_calls/sec |
| -O0 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 10 | 101.333000 | 105.708000 | 98165.05 | solve_calls/sec |
| -O0 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 100 | 1019.750000 | 1078.084000 | 97304.25 | solve_calls/sec |
| -O0 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 1000 | 10237.250500 | 10513.375000 | 96987.85 | solve_calls/sec |
| -O0 | Résolution : jointures | solver_join_bindings | simple_join | 8 | 10.458000 | 11.000000 | 94533.83 | solve_calls/sec |
| -O0 | Résolution : jointures | solver_join_bindings | simple_join | 16 | 21.250000 | 24.416000 | 46055.91 | solve_calls/sec |
| -O0 | Résolution : jointures | solver_join_bindings | simple_join | 32 | 56.125000 | 61.500000 | 17512.54 | solve_calls/sec |
| -O0 | Résolution : jointures | solver_join_bindings | simple_join | 64 | 181.042000 | 197.500000 | 5433.34 | solve_calls/sec |
| -O0 | Résolution : jointures | solver_join_bindings | noisy_join | 128 | 70.271000 | 77.084000 | 13993.64 | solve_calls/sec |
| -O0 | Résolution : jointures | solver_join_bindings | noisy_join | 512 | 78.750000 | 91.000000 | 12387.57 | solve_calls/sec |
| -O0 | Résolution : jointures | solver_join_bindings | noisy_join | 960 | 93.334000 | 110.083000 | 10425.47 | solve_calls/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 1 | 0.016097 | 0.021452 | 63162441.57 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 1 | 0.014449 | 0.016764 | 68654734.80 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 1 | 0.015905 | 0.018524 | 61745455.83 | symbols/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 8 | 0.095286 | 0.113000 | 82110834.96 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 8 | 0.097200 | 0.105600 | 81950923.69 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 8 | 0.091733 | 0.100133 | 86307526.66 | symbols/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 16 | 0.178571 | 0.190571 | 88139770.79 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 16 | 0.178714 | 0.208286 | 87759576.37 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 16 | 0.196286 | 0.202571 | 82303075.20 | symbols/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 32 | 0.361333 | 0.416667 | 69477212.92 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 32 | 0.347000 | 0.375000 | 92222829.57 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 32 | 0.361000 | 0.375000 | 88094488.68 | symbols/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 64 | 0.694333 | 0.736000 | 91283994.97 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 64 | 0.667000 | 0.750000 | 93173715.11 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 64 | 0.708333 | 0.764000 | 88548346.71 | symbols/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 128 | 1.375000 | 1.458000 | 92744012.76 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 128 | 1.333000 | 1.458000 | 94975851.65 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 128 | 1.458000 | 1.625000 | 87327068.53 | symbols/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 256 | 2.958000 | 3.083000 | 86003356.82 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 256 | 2.959000 | 3.166000 | 84897863.89 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 256 | 2.875000 | 3.041000 | 86885985.90 | symbols/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 512 | 6.708000 | 7.042000 | 75532678.73 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 512 | 6.875000 | 7.167000 | 73434001.98 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 512 | 6.750000 | 7.166000 | 74438426.05 | symbols/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 1 | 0.042000 | 0.042000 | 26094331.01 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 1 | 0.025548 | 0.028226 | 39489238.55 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 1 | 0.025581 | 0.029581 | 38390283.05 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 1 | 0.024226 | 0.028226 | 40834015.00 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 1 | 0.047200 | 0.052733 | 21508521.68 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 1 | 0.052200 | 0.063533 | 19152269.48 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 1 | 0.052867 | 0.077733 | 18078583.99 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 1 | 0.061067 | 0.083333 | 16596151.46 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 8 | 0.333333 | 0.333667 | 24308671.51 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 8 | 0.208333 | 0.222333 | 38176742.41 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 8 | 0.291667 | 0.292000 | 27768103.94 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 8 | 0.208333 | 0.222333 | 38671567.21 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 8 | 0.430333 | 0.444000 | 18626743.35 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 8 | 0.319333 | 0.333333 | 25135048.52 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 8 | 0.486000 | 0.500000 | 16328807.76 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 8 | 0.416000 | 0.417000 | 20015311.71 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 16 | 0.833000 | 0.875000 | 18937757.51 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 16 | 0.611000 | 0.625000 | 26237852.69 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 16 | 0.763667 | 0.791667 | 21006913.90 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 16 | 0.597333 | 0.625000 | 26578367.42 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 16 | 1.041000 | 1.042000 | 15554533.71 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 16 | 0.792000 | 0.834000 | 20055528.75 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 16 | 1.125000 | 1.167000 | 14119171.10 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 16 | 0.958000 | 0.959000 | 16686882.13 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 32 | 2.333000 | 2.417000 | 13670978.01 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 32 | 1.875000 | 1.917000 | 17093542.27 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 32 | 2.167000 | 2.250000 | 14737259.98 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 32 | 1.834000 | 1.875000 | 17245365.44 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 32 | 2.708000 | 2.875000 | 11687905.17 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 32 | 2.208000 | 2.333000 | 14209843.87 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 32 | 2.917000 | 3.000000 | 10825560.64 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 32 | 2.542000 | 2.667000 | 12432447.13 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 64 | 7.250000 | 7.541000 | 8793031.41 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 64 | 6.333000 | 6.583000 | 9965624.82 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 64 | 6.917000 | 7.000000 | 9203649.88 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 64 | 6.292000 | 6.583000 | 10027940.35 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 64 | 7.958000 | 8.125000 | 7986577.56 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 64 | 7.000000 | 7.292000 | 9026966.37 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 64 | 8.459000 | 8.834000 | 7437357.40 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 64 | 7.792000 | 8.208000 | 8062959.62 | facts/sec |
| -O2 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_1pct | 800 | 7.542000 | 10.167000 | 126877.63 | solve_calls/sec |
| -O2 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_10pct | 640 | 42.875000 | 46.250000 | 22996.54 | solve_calls/sec |
| -O2 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_50pct | 128 | 26.584000 | 30.000000 | 36905.33 | solve_calls/sec |
| -O2 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_100pct | 64 | 18.667000 | 19.584000 | 52991.11 | solve_calls/sec |
| -O2 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 256 | 5.959000 | 6.667000 | 163512.96 | solve_calls/sec |
| -O2 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 256 | 11.209000 | 12.792000 | 87498.49 | solve_calls/sec |
| -O2 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 256 | 18.292000 | 20.250000 | 53508.21 | solve_calls/sec |
| -O2 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 256 | 42.750000 | 46.042000 | 23137.30 | solve_calls/sec |
| -O2 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 256 | 92.875000 | 99.708000 | 10577.12 | solve_calls/sec |
| -O2 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 256 | 238.000000 | 254.833000 | 4163.42 | solve_calls/sec |
| -O2 | Résolution : taille pure | solver_size_pure | size_pure | 64 | 3.334000 | 3.917000 | 289921.12 | solve_calls/sec |
| -O2 | Résolution : taille pure | solver_size_pure | size_pure | 128 | 6.875000 | 7.375000 | 142501.46 | solve_calls/sec |
| -O2 | Résolution : taille pure | solver_size_pure | size_pure | 256 | 18.584000 | 21.000000 | 52587.20 | solve_calls/sec |
| -O2 | Résolution : taille pure | solver_size_pure | size_pure | 512 | 61.458000 | 68.458000 | 16016.43 | solve_calls/sec |
| -O2 | Résolution : taille pure | solver_size_pure | size_pure | 1024 | 219.625000 | 241.167000 | 4481.86 | solve_calls/sec |
| -O2 | Résolution : taille pure | solver_size_pure | size_pure | 2048 | 863.334000 | 901.250000 | 1153.42 | solve_calls/sec |
| -O2 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | absent_predicate | 512 | 2.875000 | 3.041000 | 362916.07 | solve_calls/sec |
| -O2 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 1 | 4.542000 | 4.708000 | 219222.29 | solve_calls/sec |
| -O2 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 10 | 45.334000 | 68.667000 | 194946.35 | solve_calls/sec |
| -O2 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 100 | 405.875000 | 423.625000 | 245401.63 | solve_calls/sec |
| -O2 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 1000 | 4025.666500 | 4188.625000 | 247461.07 | solve_calls/sec |
| -O2 | Résolution : jointures | solver_join_bindings | simple_join | 8 | 4.375000 | 4.500000 | 227171.12 | solve_calls/sec |
| -O2 | Résolution : jointures | solver_join_bindings | simple_join | 16 | 8.417000 | 10.042000 | 115746.81 | solve_calls/sec |
| -O2 | Résolution : jointures | solver_join_bindings | simple_join | 32 | 22.209000 | 22.750000 | 44813.76 | solve_calls/sec |
| -O2 | Résolution : jointures | solver_join_bindings | simple_join | 64 | 70.542000 | 74.208000 | 14043.61 | solve_calls/sec |
| -O2 | Résolution : jointures | solver_join_bindings | noisy_join | 128 | 27.041000 | 28.208000 | 36558.15 | solve_calls/sec |
| -O2 | Résolution : jointures | solver_join_bindings | noisy_join | 512 | 29.833000 | 33.250000 | 32745.41 | solve_calls/sec |
| -O2 | Résolution : jointures | solver_join_bindings | noisy_join | 960 | 34.875000 | 40.250000 | 27829.79 | solve_calls/sec |

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
