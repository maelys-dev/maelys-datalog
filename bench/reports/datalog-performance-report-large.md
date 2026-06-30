# Datalog Performance Report

Generated: 2026-06-30T12:19:26Z

Measured commit: `53b3ed789ef5b19d91e05e17e7d519b78187175f`

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
| Insertion par identifiants de symboles (unaire) | unit / batch | 1,17x |
| Insertion par identifiants de symboles (binaire) | unit / batch | 1,17x |
| Insertion de chaînes runtime (unaire) | unit / batch | 1,14x |
| Insertion de paires de chaînes runtime (binaire) | composed-unit / batch | 1,14x |

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
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 1 | 0.035714 | 0.041857 | 28147945,00 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 1 | 0.029581 | 0.034903 | 33458966,00 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 1 | 0.033613 | 0.039032 | 29040003,00 | symbols/sec |
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 8 | 0.202571 | 0.220429 | 38627029,00 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 8 | 0.178286 | 0.190714 | 43812604,00 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 8 | 0.208286 | 0.226286 | 36787746,00 | symbols/sec |
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 16 | 0.389000 | 0.472000 | 40145930,00 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 16 | 0.347000 | 0.361333 | 46581093,00 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 16 | 0.375000 | 0.430667 | 41415942,00 | symbols/sec |
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 32 | 0.764000 | 0.805333 | 41784548,00 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 32 | 0.653000 | 0.694333 | 48538044,00 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 32 | 0.722333 | 0.750000 | 43942845,00 | symbols/sec |
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 64 | 1.458000 | 1.542000 | 42856320,00 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 64 | 1.333000 | 1.375000 | 47616355,00 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 64 | 1.375000 | 1.458000 | 45685047,00 | symbols/sec |
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 128 | 2.834000 | 2.917000 | 44657575,00 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 128 | 2.666000 | 2.709000 | 47991067,00 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 128 | 2.917000 | 3.083000 | 42698192,00 | symbols/sec |
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 256 | 6.000000 | 6.333000 | 41513046,00 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 256 | 5.625000 | 5.834000 | 44523318,00 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 256 | 5.833000 | 6.167000 | 42911311,00 | symbols/sec |
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 512 | 13.458000 | 17.208000 | 36907745,00 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 512 | 12.958000 | 16.458000 | 38526870,00 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 512 | 13.459000 | 15.792000 | 37322109,00 | symbols/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 1 | 0.083000 | 0.084000 | 13575520,00 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 1 | 0.063867 | 0.075133 | 15608854,00 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 1 | 0.077800 | 0.083733 | 12526744,00 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 1 | 0.072400 | 0.083000 | 13507052,00 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 1 | 0.101429 | 0.119143 | 9643201,00 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 1 | 0.125000 | 0.125000 | 8307027,00 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 1 | 0.152667 | 0.167000 | 6024580,00 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 1 | 0.153000 | 0.167000 | 6403661,00 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 8 | 0.791000 | 0.792000 | 9968487,00 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 8 | 0.569667 | 0.680333 | 13516869,00 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 8 | 0.833000 | 0.917000 | 9689687,00 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 8 | 0.625000 | 0.625000 | 12743297,00 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 8 | 1.000000 | 1.042000 | 7932006,00 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 8 | 0.833000 | 0.875000 | 9701885,00 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 8 | 1.292000 | 1.375000 | 5940385,00 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 8 | 1.083000 | 1.084000 | 7462867,00 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 16 | 2.084000 | 2.167000 | 7598210,00 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 16 | 1.666000 | 1.709000 | 9667253,00 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 16 | 2.125000 | 2.250000 | 7273686,00 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 16 | 1.708000 | 1.791000 | 9058299,00 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 16 | 2.500000 | 2.625000 | 6200771,00 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 16 | 2.083000 | 2.209000 | 7473974,00 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 16 | 3.125000 | 3.250000 | 4940301,00 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 16 | 2.583000 | 2.708000 | 5980624,00 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 32 | 6.250000 | 6.458000 | 5094726,00 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 32 | 5.334000 | 5.458000 | 5873706,00 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 32 | 6.375000 | 6.458000 | 5010499,00 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 32 | 5.542000 | 5.584000 | 5768433,00 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 32 | 7.041000 | 8.083000 | 4390782,00 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 32 | 6.125000 | 6.209000 | 5186278,00 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 32 | 8.292000 | 8.500000 | 3831093,00 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 32 | 7.208000 | 7.333000 | 4425335,00 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 64 | 20.750000 | 21.125000 | 3053544,00 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 64 | 19.000000 | 21.000000 | 3300642,00 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 64 | 21.042000 | 25.167000 | 2965291,00 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 64 | 19.583000 | 20.167000 | 3222165,00 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 64 | 22.750000 | 26.584000 | 2759787,00 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 64 | 20.917000 | 25.208000 | 2959927,00 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 64 | 25.458000 | 30.166000 | 2445439,00 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 64 | 22.583000 | 22.792000 | 2793339,00 | facts/sec |
| -O0 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_1pct | 800 | 25.208000 | 28.416000 | 38760,00 | solve_calls/sec |
| -O0 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_10pct | 640 | 124.625000 | 135.625000 | 7918,00 | solve_calls/sec |
| -O0 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_50pct | 128 | 78.916000 | 86.708000 | 12478,00 | solve_calls/sec |
| -O0 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_100pct | 64 | 56.083500 | 62.000000 | 17642,00 | solve_calls/sec |
| -O0 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 256 | 20.750000 | 27.125000 | 46185,00 | solve_calls/sec |
| -O0 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 256 | 35.583000 | 39.958000 | 27542,00 | solve_calls/sec |
| -O0 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 256 | 56.583000 | 63.375000 | 17393,00 | solve_calls/sec |
| -O0 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 256 | 125.125000 | 138.708000 | 7814,00 | solve_calls/sec |
| -O0 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 256 | 267.875000 | 291.708000 | 3678,00 | solve_calls/sec |
| -O0 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 256 | 651.104500 | 701.833000 | 1516,00 | solve_calls/sec |
| -O0 | Résolution : taille pure | solver_size_pure | size_pure | 64 | 12.708000 | 13.458000 | 78032,00 | solve_calls/sec |
| -O0 | Résolution : taille pure | solver_size_pure | size_pure | 128 | 23.000000 | 26.459000 | 42639,00 | solve_calls/sec |
| -O0 | Résolution : taille pure | solver_size_pure | size_pure | 256 | 56.625000 | 61.042000 | 17432,00 | solve_calls/sec |
| -O0 | Résolution : taille pure | solver_size_pure | size_pure | 512 | 173.167000 | 184.208000 | 5702,00 | solve_calls/sec |
| -O0 | Résolution : taille pure | solver_size_pure | size_pure | 1024 | 629.687500 | 691.375000 | 1553,00 | solve_calls/sec |
| -O0 | Résolution : taille pure | solver_size_pure | size_pure | 2048 | 2391.916500 | 2481.250000 | 409,00 | solve_calls/sec |
| -O0 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | absent_predicate | 512 | 8.167000 | 8.209000 | 122351,00 | solve_calls/sec |
| -O0 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 1 | 12.959000 | 13.042000 | 76979,00 | solve_calls/sec |
| -O0 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 10 | 121.500000 | 135.083000 | 80865,00 | solve_calls/sec |
| -O0 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 100 | 1894.687500 | 1962.167000 | 60644,00 | solve_calls/sec |
| -O0 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 1000 | 14415.042000 | 19263.292000 | 63938,00 | solve_calls/sec |
| -O0 | Résolution : jointures | solver_join_bindings | simple_join | 8 | 12.083000 | 12.292000 | 82440,00 | solve_calls/sec |
| -O0 | Résolution : jointures | solver_join_bindings | simple_join | 16 | 28.166000 | 28.416000 | 35394,00 | solve_calls/sec |
| -O0 | Résolution : jointures | solver_join_bindings | simple_join | 32 | 60.125000 | 63.250000 | 16548,00 | solve_calls/sec |
| -O0 | Résolution : jointures | solver_join_bindings | simple_join | 64 | 173.958000 | 186.042000 | 5682,00 | solve_calls/sec |
| -O0 | Résolution : jointures | solver_join_bindings | noisy_join | 128 | 75.333000 | 78.542000 | 13200,00 | solve_calls/sec |
| -O0 | Résolution : jointures | solver_join_bindings | noisy_join | 512 | 79.459000 | 81.750000 | 12596,00 | solve_calls/sec |
| -O0 | Résolution : jointures | solver_join_bindings | noisy_join | 960 | 100.125000 | 111.417000 | 9936,00 | solve_calls/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 1 | 0.017714 | 0.023857 | 61804697,00 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 1 | 0.014540 | 0.017873 | 67430666,00 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 1 | 0.016409 | 0.017732 | 60556644,00 | symbols/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 8 | 0.094467 | 0.113933 | 83242864,00 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 8 | 0.097267 | 0.108333 | 81167513,00 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 8 | 0.105533 | 0.111200 | 75560469,00 | symbols/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 16 | 0.178857 | 0.196000 | 88507114,00 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 16 | 0.178571 | 0.190429 | 89134078,00 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 16 | 0.196429 | 0.208286 | 81206496,00 | symbols/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 32 | 0.347333 | 0.375000 | 91606025,00 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 32 | 0.334000 | 0.375000 | 93191035,00 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 32 | 0.375000 | 0.375000 | 87025969,00 | symbols/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 64 | 0.694000 | 0.708667 | 92247686,00 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 64 | 0.680667 | 0.708333 | 94016487,00 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 64 | 0.708333 | 0.722333 | 88101522,00 | symbols/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 128 | 1.375000 | 1.375000 | 93273861,00 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 128 | 1.333000 | 1.334000 | 96232714,00 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 128 | 1.458000 | 1.459000 | 88539771,00 | symbols/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 256 | 2.917000 | 3.000000 | 86772248,00 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 256 | 2.917000 | 3.042000 | 86283632,00 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 256 | 2.917000 | 2.958000 | 87863087,00 | symbols/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 512 | 6.708000 | 8.417000 | 72845460,00 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 512 | 6.958000 | 7.084000 | 71751231,00 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 512 | 6.833000 | 7.125000 | 73985199,00 | symbols/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 1 | 0.032290 | 0.034968 | 30769444,00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 1 | 0.026823 | 0.032226 | 36936465,00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 1 | 0.032258 | 0.037613 | 31146419,00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 1 | 0.027867 | 0.038533 | 34008747,00 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 1 | 0.049933 | 0.052933 | 19574170,00 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 1 | 0.055533 | 0.061067 | 17530983,00 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 1 | 0.061133 | 0.069533 | 16239126,00 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 1 | 0.069333 | 0.069667 | 13994364,00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 8 | 0.361000 | 0.375000 | 22576081,00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 8 | 0.220286 | 0.232143 | 36272977,00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 8 | 0.347000 | 0.361333 | 23138819,00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 8 | 0.226214 | 0.244143 | 35204427,00 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 8 | 0.458000 | 0.458667 | 17742607,00 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 8 | 0.333000 | 0.334000 | 24406614,00 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 8 | 0.541667 | 0.542000 | 14707477,00 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 8 | 0.417000 | 0.445000 | 18253490,00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 16 | 0.875000 | 0.958000 | 17613832,00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 16 | 0.625000 | 0.652667 | 25285249,00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 16 | 0.875000 | 0.875000 | 18424264,00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 16 | 0.625000 | 0.667000 | 25140511,00 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 16 | 1.083000 | 1.084000 | 14585019,00 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 16 | 0.833000 | 0.834000 | 19246244,00 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 16 | 1.250000 | 1.292000 | 12569604,00 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 16 | 1.000000 | 1.042000 | 15895992,00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 32 | 2.417000 | 2.541000 | 13204441,00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 32 | 1.875000 | 1.917000 | 16994122,00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 32 | 2.375000 | 2.417000 | 12888123,00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 32 | 1.917000 | 1.917000 | 16612891,00 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 32 | 2.791000 | 2.792000 | 11505737,00 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 32 | 2.292000 | 2.292000 | 13999989,00 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 32 | 3.250000 | 3.500000 | 9755746,00 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 32 | 2.666000 | 2.667000 | 12052199,00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 64 | 7.459000 | 7.709000 | 8502996,00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 64 | 6.334000 | 6.458000 | 10029191,00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 64 | 7.291000 | 7.333000 | 8751096,00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 64 | 6.375000 | 6.417000 | 9998628,00 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 64 | 8.125000 | 8.167000 | 7836497,00 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 64 | 7.042000 | 7.084000 | 9062761,00 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 64 | 8.792000 | 9.000000 | 7194871,00 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 64 | 7.833000 | 7.916000 | 8008119,00 | facts/sec |
| -O2 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_1pct | 800 | 12.292000 | 13.292000 | 79233,00 | solve_calls/sec |
| -O2 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_10pct | 640 | 47.229500 | 50.875000 | 20778,00 | solve_calls/sec |
| -O2 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_50pct | 128 | 36.834000 | 45.000000 | 26451,00 | solve_calls/sec |
| -O2 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_100pct | 64 | 28.750000 | 28.917000 | 34688,00 | solve_calls/sec |
| -O2 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 256 | 16.417000 | 16.542000 | 60815,00 | solve_calls/sec |
| -O2 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 256 | 21.584000 | 21.792000 | 45960,00 | solve_calls/sec |
| -O2 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 256 | 29.166000 | 32.333000 | 33965,00 | solve_calls/sec |
| -O2 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 256 | 47.500000 | 53.458000 | 20671,00 | solve_calls/sec |
| -O2 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 256 | 98.417000 | 102.750000 | 10087,00 | solve_calls/sec |
| -O2 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 256 | 244.625000 | 259.625000 | 4061,00 | solve_calls/sec |
| -O2 | Résolution : taille pure | solver_size_pure | size_pure | 64 | 14.167000 | 14.417000 | 70000,00 | solve_calls/sec |
| -O2 | Résolution : taille pure | solver_size_pure | size_pure | 128 | 17.917000 | 18.000000 | 55740,00 | solve_calls/sec |
| -O2 | Résolution : taille pure | solver_size_pure | size_pure | 256 | 29.208000 | 36.166000 | 33466,00 | solve_calls/sec |
| -O2 | Résolution : taille pure | solver_size_pure | size_pure | 512 | 67.417000 | 73.750000 | 14592,00 | solve_calls/sec |
| -O2 | Résolution : taille pure | solver_size_pure | size_pure | 1024 | 230.333000 | 242.875000 | 4308,00 | solve_calls/sec |
| -O2 | Résolution : taille pure | solver_size_pure | size_pure | 2048 | 867.750000 | 928.583000 | 1140,00 | solve_calls/sec |
| -O2 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | absent_predicate | 512 | 9.000000 | 9.125000 | 110265,00 | solve_calls/sec |
| -O2 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 1 | 10.125000 | 10.250000 | 98597,00 | solve_calls/sec |
| -O2 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 10 | 68.708000 | 76.750000 | 142524,00 | solve_calls/sec |
| -O2 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 100 | 678.500000 | 758.292000 | 145876,00 | solve_calls/sec |
| -O2 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 1000 | 6997.166500 | 13552.125000 | 127074,00 | solve_calls/sec |
| -O2 | Résolution : jointures | solver_join_bindings | simple_join | 8 | 7.250000 | 7.334000 | 134916,00 | solve_calls/sec |
| -O2 | Résolution : jointures | solver_join_bindings | simple_join | 16 | 11.209000 | 11.292000 | 87631,00 | solve_calls/sec |
| -O2 | Résolution : jointures | solver_join_bindings | simple_join | 32 | 24.666000 | 27.834000 | 38811,00 | solve_calls/sec |
| -O2 | Résolution : jointures | solver_join_bindings | simple_join | 64 | 79.937000 | 89.583000 | 12753,00 | solve_calls/sec |
| -O2 | Résolution : jointures | solver_join_bindings | noisy_join | 128 | 31.084000 | 38.125000 | 31363,00 | solve_calls/sec |
| -O2 | Résolution : jointures | solver_join_bindings | noisy_join | 512 | 34.417000 | 39.000000 | 28558,00 | solve_calls/sec |
| -O2 | Résolution : jointures | solver_join_bindings | noisy_join | 960 | 41.083000 | 48.625000 | 23613,00 | solve_calls/sec |

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
