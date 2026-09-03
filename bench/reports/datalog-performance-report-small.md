# Datalog Performance Report

Generated: 2026-06-30T12:16:39Z

Measured commit: `53b3ed789ef5b19d91e05e17e7d519b78187175f`

Host: `MAC-QXQWJGVJXW`

OS: `Darwin 24.5.0 arm64`

Compiler: `Apple clang version 17.0.0 (clang-1700.0.13.5)`

CPU: `Apple M2 Max`

Source tree clean before benchmark run: `false`

Generated artifacts dirty after benchmark run: `true`

Optimization levels: `-O0` and `-O2`

Engine size profile: `small`

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
correct level for each sample. In particular, symbol-interning workloads reset
the symbol table with `maelys_datalog_symbol_table_init`, not just the EDB.

## Engine Bounds

- `MAELYS_DATALOG_MAX_SYMBOLS = 512`
- `MAELYS_DATALOG_MAX_EDB_FACTS = 1024`
- `MAELYS_DATALOG_MAX_IDB_FACTS = 1024`
- `MAELYS_DATALOG_MAX_FACTS_PER_PRED = 64`
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
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 1 | 0.041667 | 0.042000 | 20409343,00 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 1 | 0.029581 | 0.036258 | 33378088,00 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 1 | 0.033581 | 0.037645 | 29673874,00 | symbols/sec |
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 8 | 0.202000 | 0.214286 | 39663736,00 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 8 | 0.172571 | 0.184714 | 44836802,00 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 8 | 0.202429 | 0.214429 | 37465085,00 | symbols/sec |
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 16 | 0.389000 | 0.417000 | 40852802,00 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 16 | 0.333333 | 0.347333 | 47525599,00 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 16 | 0.375000 | 0.402667 | 42011437,00 | symbols/sec |
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 32 | 0.750000 | 0.778000 | 42504582,00 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 32 | 0.652667 | 0.667000 | 47718247,00 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 32 | 0.722333 | 0.791667 | 43100255,00 | symbols/sec |
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 64 | 1.416000 | 1.500000 | 42787527,00 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 64 | 1.292000 | 1.584000 | 46156830,00 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 64 | 1.375000 | 1.458000 | 45529468,00 | symbols/sec |
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 128 | 2.792000 | 2.959000 | 43903998,00 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 128 | 2.542000 | 2.625000 | 49984301,00 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 128 | 2.875000 | 3.042000 | 43515834,00 | symbols/sec |
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 256 | 6.084000 | 6.417000 | 41022741,00 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 256 | 5.625000 | 5.875000 | 44640490,00 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 256 | 5.750000 | 6.000000 | 43168260,00 | symbols/sec |
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 512 | 13.333000 | 13.584000 | 37679754,00 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 512 | 13.041000 | 13.250000 | 38745364,00 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 512 | 13.375000 | 13.583000 | 37727184,00 | symbols/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 1 | 0.083000 | 0.084000 | 13906465,00 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 1 | 0.061133 | 0.072400 | 16168776,00 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 1 | 0.083000 | 0.084000 | 12184868,00 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 1 | 0.083000 | 0.084000 | 14158891,00 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 1 | 0.107000 | 0.119143 | 9574155,00 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 1 | 0.119000 | 0.131000 | 8454627,00 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 1 | 0.137000 | 0.154429 | 7031119,00 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 1 | 0.152667 | 0.166667 | 6588541,00 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 8 | 0.791000 | 0.792000 | 9920991,00 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 8 | 0.583000 | 0.584000 | 13386813,00 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 8 | 0.833000 | 0.834000 | 9770515,00 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 8 | 0.625000 | 0.625000 | 12988594,00 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 8 | 1.000000 | 1.042000 | 7964137,00 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 8 | 0.833000 | 0.875000 | 9721526,00 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 8 | 1.333000 | 1.417000 | 6000388,00 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 8 | 1.083000 | 1.084000 | 7493232,00 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 16 | 2.083000 | 2.125000 | 7662160,00 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 16 | 1.625000 | 1.625000 | 9980537,00 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 16 | 2.167000 | 2.250000 | 7231102,00 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 16 | 1.709000 | 1.750000 | 9291758,00 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 16 | 2.500000 | 2.542000 | 6392214,00 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 16 | 2.000000 | 2.042000 | 7917080,00 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 16 | 3.084000 | 3.208000 | 5139654,00 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 16 | 2.583000 | 2.625000 | 6225426,00 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 32 | 6.250000 | 6.292000 | 5093512,00 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 32 | 5.375000 | 5.417000 | 5953598,00 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 32 | 6.417000 | 6.459000 | 4982350,00 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 32 | 5.583000 | 5.625000 | 5750215,00 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 32 | 7.041000 | 7.125000 | 4538900,00 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 32 | 6.125000 | 6.208000 | 5188346,00 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 32 | 8.250000 | 8.417000 | 3867478,00 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 32 | 7.125000 | 7.375000 | 4427892,00 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 64 | 20.750000 | 20.958000 | 3077858,00 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 64 | 18.959000 | 19.125000 | 3366066,00 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 64 | 21.333000 | 22.125000 | 2978001,00 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 64 | 19.625000 | 19.709000 | 3260456,00 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 64 | 22.750000 | 22.875000 | 2809562,00 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 64 | 20.916000 | 21.000000 | 3058384,00 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 64 | 25.000000 | 25.333000 | 2539997,00 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 64 | 22.583000 | 22.750000 | 2825859,00 | facts/sec |
| -O0 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_1pct | 800 | 64.875000 | 75.375000 | 14989,00 | solve_calls/sec |
| -O0 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_10pct | 640 | 257.312000 | 282.875000 | 3833,00 | solve_calls/sec |
| -O0 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_50pct | 128 | 75.584000 | 80.750000 | 13145,00 | solve_calls/sec |
| -O0 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_100pct | 64 | 52.833000 | 57.375000 | 18774,00 | solve_calls/sec |
| -O0 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 64 | 8.500000 | 10.375000 | 115317,00 | solve_calls/sec |
| -O0 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 64 | 7.791000 | 7.958000 | 127747,00 | solve_calls/sec |
| -O0 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 64 | 9.791000 | 10.958000 | 100913,00 | solve_calls/sec |
| -O0 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 64 | 15.000000 | 15.417000 | 66255,00 | solve_calls/sec |
| -O0 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 64 | 28.375000 | 28.583000 | 35248,00 | solve_calls/sec |
| -O0 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 64 | 55.083000 | 58.750000 | 18052,00 | solve_calls/sec |
| -O0 | Résolution : taille pure | solver_size_pure | size_pure | 64 | 9.708000 | 10.000000 | 57332,00 | solve_calls/sec |
| -O0 | Résolution : taille pure | solver_size_pure | size_pure | 128 | 19.333000 | 19.542000 | 51680,00 | solve_calls/sec |
| -O0 | Résolution : taille pure | solver_size_pure | size_pure | 256 | 53.125000 | 59.375000 | 18460,00 | solve_calls/sec |
| -O0 | Résolution : taille pure | solver_size_pure | size_pure | 512 | 170.792000 | 184.750000 | 5770,00 | solve_calls/sec |
| -O0 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | absent_predicate | 512 | 4.333000 | 4.375000 | 224448,00 | solve_calls/sec |
| -O0 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 1 | 9.958000 | 10.042000 | 100399,00 | solve_calls/sec |
| -O0 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 10 | 88.209000 | 98.916000 | 111995,00 | solve_calls/sec |
| -O0 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 100 | 1052.166500 | 1259.375000 | 93149,00 | solve_calls/sec |
| -O0 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 1000 | 9334.874500 | 12287.250000 | 100449,00 | solve_calls/sec |
| -O0 | Résolution : jointures | solver_join_bindings | simple_join | 8 | 8.791000 | 9.292000 | 112501,00 | solve_calls/sec |
| -O0 | Résolution : jointures | solver_join_bindings | simple_join | 16 | 19.709000 | 21.500000 | 48618,00 | solve_calls/sec |
| -O0 | Résolution : jointures | solver_join_bindings | simple_join | 32 | 50.750000 | 55.875000 | 19172,00 | solve_calls/sec |
| -O0 | Résolution : jointures | solver_join_bindings | simple_join | 64 | 170.333000 | 178.000000 | 5834,00 | solve_calls/sec |
| -O0 | Résolution : jointures | solver_join_bindings | noisy_join | 128 | 68.583000 | 75.916000 | 14411,00 | solve_calls/sec |
| -O0 | Résolution : jointures | solver_join_bindings | noisy_join | 512 | 115.875000 | 120.458000 | 8575,00 | solve_calls/sec |
| -O0 | Résolution : jointures | solver_join_bindings | noisy_join | 960 | 171.334000 | 187.041000 | 5751,00 | solve_calls/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 1 | 0.024333 | 0.041667 | 41040689,00 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 1 | 0.013831 | 0.017661 | 70256846,00 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 1 | 0.015905 | 0.019825 | 61688864,00 | symbols/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 8 | 0.097200 | 0.102800 | 81905839,00 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 8 | 0.099933 | 0.102800 | 80666954,00 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 8 | 0.094400 | 0.102800 | 85698431,00 | symbols/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 16 | 0.178714 | 0.190714 | 89094584,00 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 16 | 0.178571 | 0.190714 | 86975294,00 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 16 | 0.190571 | 0.208286 | 81245905,00 | symbols/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 32 | 0.347333 | 0.361333 | 89954095,00 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 32 | 0.347000 | 0.361333 | 92450457,00 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 32 | 0.361000 | 0.375000 | 86906534,00 | symbols/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 64 | 0.681000 | 0.708333 | 91949883,00 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 64 | 0.680333 | 0.708333 | 92982801,00 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 64 | 0.708333 | 0.709000 | 90632943,00 | symbols/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 128 | 1.375000 | 1.417000 | 91720504,00 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 128 | 1.334000 | 1.375000 | 94683660,00 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 128 | 1.458000 | 1.459000 | 87055346,00 | symbols/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 256 | 2.958000 | 2.959000 | 86423449,00 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 256 | 2.958000 | 3.042000 | 86273048,00 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 256 | 2.916000 | 3.125000 | 86512177,00 | symbols/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 512 | 6.708000 | 6.833000 | 74925056,00 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 512 | 6.834000 | 7.000000 | 74220474,00 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 512 | 6.792000 | 6.875000 | 75249377,00 | symbols/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 1 | 0.033333 | 0.036200 | 30743437,00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 1 | 0.026444 | 0.028429 | 37833997,00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 1 | 0.032258 | 0.036258 | 30734454,00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 1 | 0.026903 | 0.032258 | 36528130,00 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 1 | 0.050067 | 0.061067 | 19419109,00 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 1 | 0.052800 | 0.061133 | 18001238,00 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 1 | 0.055667 | 0.083333 | 16521916,00 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 1 | 0.071286 | 0.084000 | 14813341,00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 8 | 0.347333 | 0.375000 | 22527257,00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 8 | 0.222333 | 0.236333 | 36015702,00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 8 | 0.334000 | 0.375000 | 21786017,00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 8 | 0.226143 | 0.238000 | 35517332,00 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 8 | 0.458333 | 0.472000 | 17437779,00 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 8 | 0.333333 | 0.333667 | 24070695,00 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 8 | 0.542000 | 0.542000 | 14837263,00 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 8 | 0.417000 | 0.459000 | 18727731,00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 16 | 0.875000 | 0.917000 | 18050134,00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 16 | 0.625000 | 0.652667 | 25491819,00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 16 | 0.875000 | 0.875000 | 18473041,00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 16 | 0.625000 | 0.653000 | 25255435,00 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 16 | 1.083000 | 1.084000 | 14841749,00 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 16 | 0.833000 | 0.834000 | 19611472,00 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 16 | 1.250000 | 1.250000 | 12901030,00 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 16 | 1.000000 | 1.000000 | 16047468,00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 32 | 2.417000 | 2.500000 | 13125060,00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 32 | 1.875000 | 2.000000 | 16303647,00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 32 | 2.375000 | 2.833000 | 12649626,00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 32 | 1.916000 | 1.958000 | 16648578,00 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 32 | 2.708000 | 3.250000 | 11394883,00 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 32 | 2.250000 | 2.250000 | 14264635,00 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 32 | 3.042000 | 3.875000 | 10064944,00 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 32 | 2.666000 | 2.708000 | 11977002,00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 64 | 7.459000 | 7.625000 | 8424891,00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 64 | 6.416000 | 7.083000 | 9660826,00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 64 | 7.375000 | 7.500000 | 8655955,00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 64 | 6.375000 | 6.500000 | 9837456,00 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 64 | 8.125000 | 8.250000 | 7754431,00 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 64 | 7.042000 | 7.167000 | 8803784,00 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 64 | 8.792000 | 8.958000 | 7252108,00 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 64 | 7.791000 | 7.834000 | 8174699,00 | facts/sec |
| -O2 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_1pct | 800 | 27.042000 | 33.458000 | 35918,00 | solve_calls/sec |
| -O2 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_10pct | 640 | 95.083000 | 98.416000 | 10461,00 | solve_calls/sec |
| -O2 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_50pct | 128 | 29.167000 | 32.541000 | 33433,00 | solve_calls/sec |
| -O2 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_100pct | 64 | 19.333000 | 24.125000 | 50044,00 | solve_calls/sec |
| -O2 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 64 | 4.917000 | 8.709000 | 169458,00 | solve_calls/sec |
| -O2 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 64 | 4.395500 | 4.541000 | 224594,00 | solve_calls/sec |
| -O2 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 64 | 4.708000 | 5.000000 | 208243,00 | solve_calls/sec |
| -O2 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 64 | 10.125000 | 10.542000 | 110911,00 | solve_calls/sec |
| -O2 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 64 | 10.917000 | 11.625000 | 61378,00 | solve_calls/sec |
| -O2 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 64 | 19.792000 | 20.083000 | 50290,00 | solve_calls/sec |
| -O2 | Résolution : taille pure | solver_size_pure | size_pure | 64 | 4.791000 | 4.833000 | 208909,00 | solve_calls/sec |
| -O2 | Résolution : taille pure | solver_size_pure | size_pure | 128 | 9.000000 | 9.042000 | 111050,00 | solve_calls/sec |
| -O2 | Résolution : taille pure | solver_size_pure | size_pure | 256 | 20.958000 | 25.125000 | 46252,00 | solve_calls/sec |
| -O2 | Résolution : taille pure | solver_size_pure | size_pure | 512 | 63.270500 | 71.375000 | 15598,00 | solve_calls/sec |
| -O2 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | absent_predicate | 512 | 5.584000 | 5.667000 | 178688,00 | solve_calls/sec |
| -O2 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 1 | 4.542000 | 4.625000 | 215237,00 | solve_calls/sec |
| -O2 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 10 | 44.667000 | 81.500000 | 188944,00 | solve_calls/sec |
| -O2 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 100 | 445.208500 | 510.542000 | 219332,00 | solve_calls/sec |
| -O2 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 1000 | 4793.771000 | 8022.791000 | 180548,00 | solve_calls/sec |
| -O2 | Résolution : jointures | solver_join_bindings | simple_join | 8 | 4.709000 | 4.792000 | 206448,00 | solve_calls/sec |
| -O2 | Résolution : jointures | solver_join_bindings | simple_join | 16 | 12.209000 | 12.375000 | 89961,00 | solve_calls/sec |
| -O2 | Résolution : jointures | solver_join_bindings | simple_join | 32 | 22.125000 | 22.458000 | 45027,00 | solve_calls/sec |
| -O2 | Résolution : jointures | solver_join_bindings | simple_join | 64 | 70.959000 | 73.333000 | 13985,00 | solve_calls/sec |
| -O2 | Résolution : jointures | solver_join_bindings | noisy_join | 128 | 28.875000 | 33.667000 | 33694,00 | solve_calls/sec |
| -O2 | Résolution : jointures | solver_join_bindings | noisy_join | 512 | 50.084000 | 50.917000 | 19959,00 | solve_calls/sec |
| -O2 | Résolution : jointures | solver_join_bindings | noisy_join | 960 | 71.708500 | 81.250000 | 13792,00 | solve_calls/sec |

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
