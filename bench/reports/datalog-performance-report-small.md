# Datalog Performance Report

Generated: 2026-06-28T11:47:01Z

Measured commit: `3be8b71340786098d5befda29fcb6e6cf4f27050`

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
| Insertion par identifiants de symboles (unaire) | unit / batch | 1.14x |
| Insertion par identifiants de symboles (binaire) | unit / batch | 1.14x |
| Insertion de chaînes runtime (unaire) | unit / batch | 1.15x |
| Insertion de paires de chaînes runtime (binaire) | composed-unit / batch | 1.14x |

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
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 1 | 0.035714 | 0.041857 | 28682999.59 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 1 | 0.029143 | 0.033206 | 34157764.41 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 1 | 0.034935 | 0.039000 | 28605861.06 | symbols/sec |
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 8 | 0.208143 | 0.220286 | 38610810.61 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 8 | 0.178714 | 0.196286 | 44311054.10 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 8 | 0.208429 | 0.232143 | 37896346.72 | symbols/sec |
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 16 | 0.389000 | 0.430667 | 40539273.69 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 16 | 0.333333 | 0.375000 | 46653351.02 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 16 | 0.375000 | 0.402667 | 42018792.91 | symbols/sec |
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 32 | 0.750000 | 0.791333 | 42403276.36 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 32 | 0.666667 | 0.680667 | 48032590.11 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 32 | 0.722333 | 0.750000 | 44321493.34 | symbols/sec |
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 64 | 1.458000 | 1.500000 | 43791854.58 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 64 | 1.333000 | 1.375000 | 47965800.38 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 64 | 1.375000 | 1.417000 | 46104427.97 | symbols/sec |
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 128 | 2.833000 | 2.917000 | 45248788.80 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 128 | 2.666000 | 2.708000 | 48215662.63 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 128 | 2.834000 | 2.917000 | 44588614.30 | symbols/sec |
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 256 | 5.959000 | 6.125000 | 42611364.22 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 256 | 5.583000 | 5.750000 | 45651457.90 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 256 | 5.791000 | 5.958000 | 44119317.25 | symbols/sec |
| -O0 | Interning de symboles distincts | intern_distinct_symbols | distinct | 512 | 13.500000 | 13.834000 | 37761019.36 | symbols/sec |
| -O0 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 512 | 13.125000 | 15.417000 | 38206922.27 | symbols/sec |
| -O0 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 512 | 13.542000 | 15.834000 | 37048888.18 | symbols/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 1 | 0.077000 | 0.095286 | 13169579.03 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 1 | 0.063867 | 0.072200 | 15654466.27 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 1 | 0.077600 | 0.083533 | 13054466.72 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 1 | 0.069667 | 0.080533 | 14158877.71 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 1 | 0.101286 | 0.124857 | 9790415.18 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 1 | 0.119143 | 0.131000 | 8297112.49 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 1 | 0.142857 | 0.160143 | 6998264.43 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 1 | 0.153000 | 0.167000 | 6416288.82 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 8 | 0.791000 | 0.792000 | 10297666.93 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 8 | 0.569333 | 0.583667 | 13988304.61 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 8 | 0.819333 | 0.833667 | 9791302.47 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 8 | 0.611000 | 0.638667 | 13030270.95 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 8 | 1.000000 | 1.042000 | 7927283.03 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 8 | 0.833333 | 0.847333 | 9655348.32 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 8 | 1.292000 | 1.375000 | 6090217.44 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 8 | 1.083000 | 1.292000 | 7131446.83 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 16 | 2.042000 | 2.250000 | 7684409.01 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 16 | 1.625000 | 1.709000 | 9711890.69 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 16 | 2.166000 | 2.209000 | 7420833.62 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 16 | 1.750000 | 1.750000 | 9208039.54 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 16 | 2.541000 | 2.584000 | 6321811.83 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 16 | 2.084000 | 2.125000 | 7621112.58 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 16 | 3.084000 | 3.208000 | 5148571.69 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 16 | 2.583000 | 2.667000 | 6168493.95 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 32 | 6.167000 | 6.459000 | 5117034.58 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 32 | 5.292000 | 5.792000 | 5969364.10 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 32 | 6.292000 | 6.417000 | 5060066.95 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 32 | 5.458000 | 5.625000 | 5846746.90 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 32 | 7.166000 | 7.333000 | 4459072.06 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 32 | 6.250000 | 6.333000 | 5109021.73 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 32 | 8.417000 | 8.583000 | 3795079.92 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 32 | 7.125000 | 8.416000 | 4347897.57 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 64 | 20.750000 | 21.125000 | 3067374.11 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 64 | 18.917000 | 19.084000 | 3365898.05 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 64 | 21.042000 | 21.500000 | 3021541.28 | facts/sec |
| -O0 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 64 | 19.291000 | 19.625000 | 3299332.00 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 64 | 22.417000 | 22.791000 | 2838308.02 | facts/sec |
| -O0 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 64 | 20.625000 | 20.792000 | 3089050.87 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 64 | 25.583000 | 26.708000 | 2481129.36 | facts/sec |
| -O0 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 64 | 22.875000 | 23.500000 | 2783304.64 | facts/sec |
| -O0 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_1pct | 800 | 65.666000 | 68.291000 | 15223.80 | solve_calls/sec |
| -O0 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_10pct | 640 | 256.542000 | 265.333000 | 3878.79 | solve_calls/sec |
| -O0 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_50pct | 128 | 77.041000 | 79.542000 | 12928.34 | solve_calls/sec |
| -O0 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_100pct | 64 | 53.166500 | 56.042000 | 18536.39 | solve_calls/sec |
| -O0 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | noise_total | 64 | 10.583000 | 10.667000 | 94298.66 | solve_calls/sec |
| -O0 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | noise_total | 128 | 22.208000 | 22.334000 | 44918.33 | solve_calls/sec |
| -O0 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | noise_total | 256 | 36.542000 | 38.208000 | 27219.98 | solve_calls/sec |
| -O0 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | noise_total | 512 | 67.208000 | 68.583000 | 14891.36 | solve_calls/sec |
| -O0 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | noise_total | 960 | 117.500000 | 122.333000 | 8480.63 | solve_calls/sec |
| -O0 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 64 | 6.875000 | 6.917000 | 145300.88 | solve_calls/sec |
| -O0 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 64 | 7.834000 | 8.042000 | 126559.27 | solve_calls/sec |
| -O0 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 64 | 9.917000 | 11.042000 | 100016.37 | solve_calls/sec |
| -O0 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 64 | 18.292000 | 18.458000 | 54480.27 | solve_calls/sec |
| -O0 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 64 | 28.167000 | 28.500000 | 37032.93 | solve_calls/sec |
| -O0 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 64 | 54.709000 | 54.917000 | 18249.23 | solve_calls/sec |
| -O0 | Résolution : taille pure | solver_size_pure | size_pure | 64 | 12.000000 | 12.125000 | 86369.58 | solve_calls/sec |
| -O0 | Résolution : taille pure | solver_size_pure | size_pure | 128 | 21.750000 | 21.917000 | 47151.04 | solve_calls/sec |
| -O0 | Résolution : taille pure | solver_size_pure | size_pure | 256 | 52.750000 | 54.250000 | 18847.64 | solve_calls/sec |
| -O0 | Résolution : taille pure | solver_size_pure | size_pure | 512 | 170.917000 | 177.541000 | 5822.16 | solve_calls/sec |
| -O0 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | absent_predicate | 512 | 6.708000 | 6.792000 | 149120.66 | solve_calls/sec |
| -O0 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 1 | 12.084000 | 12.250000 | 83960.07 | solve_calls/sec |
| -O0 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 10 | 89.083500 | 96.917000 | 109332.92 | solve_calls/sec |
| -O0 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 100 | 879.313000 | 934.375000 | 112282.16 | solve_calls/sec |
| -O0 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 1000 | 9328.417000 | 12436.917000 | 98962.85 | solve_calls/sec |
| -O0 | Résolution : jointures | solver_join_bindings | simple_join | 8 | 9.208000 | 9.292000 | 108424.13 | solve_calls/sec |
| -O0 | Résolution : jointures | solver_join_bindings | simple_join | 16 | 18.291000 | 19.375000 | 54178.02 | solve_calls/sec |
| -O0 | Résolution : jointures | solver_join_bindings | simple_join | 32 | 50.750000 | 51.875000 | 19646.84 | solve_calls/sec |
| -O0 | Résolution : jointures | solver_join_bindings | simple_join | 64 | 170.791000 | 174.959000 | 5868.08 | solve_calls/sec |
| -O0 | Résolution : jointures | solver_join_bindings | noisy_join | 128 | 68.458000 | 70.042000 | 14553.98 | solve_calls/sec |
| -O0 | Résolution : jointures | solver_join_bindings | noisy_join | 512 | 116.167000 | 120.500000 | 8575.55 | solve_calls/sec |
| -O0 | Résolution : jointures | solver_join_bindings | noisy_join | 960 | 171.208000 | 177.042000 | 5824.00 | solve_calls/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 1 | 0.016667 | 0.022267 | 61316840.47 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 1 | 0.013905 | 0.021159 | 69911794.62 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 1 | 0.016524 | 0.018571 | 60257846.19 | symbols/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 8 | 0.094467 | 0.111000 | 82892964.46 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 8 | 0.097200 | 0.108400 | 82460001.75 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 8 | 0.091667 | 0.100000 | 86716793.79 | symbols/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 16 | 0.178571 | 0.196429 | 88956594.74 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 16 | 0.178571 | 0.190429 | 89086717.97 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 16 | 0.196429 | 0.202429 | 82103491.45 | symbols/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 32 | 0.347000 | 0.375000 | 91540163.25 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 32 | 0.347000 | 0.361333 | 92752278.95 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 32 | 0.361000 | 0.375000 | 88326787.03 | symbols/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 64 | 0.681000 | 0.708333 | 92927401.92 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 64 | 0.667000 | 0.681000 | 95114060.09 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 64 | 0.708000 | 0.709000 | 91799967.87 | symbols/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 128 | 1.375000 | 1.375000 | 93746864.42 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 128 | 1.334000 | 1.375000 | 94917677.45 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 128 | 1.458000 | 1.500000 | 87272171.90 | symbols/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 256 | 3.000000 | 3.083000 | 85314109.22 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 256 | 2.959000 | 3.000000 | 85830319.49 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 256 | 2.917000 | 2.917000 | 87892710.47 | symbols/sec |
| -O2 | Interning de symboles distincts | intern_distinct_symbols | distinct | 512 | 6.667000 | 6.833000 | 75880753.99 | symbols/sec |
| -O2 | Re-interning de symboles existants | reintern_existing_symbols | reintern | 512 | 6.833000 | 7.000000 | 74418355.87 | symbols/sec |
| -O2 | Interning mixte (nouveaux + existants) | mixed_intern_symbols | mixed | 512 | 6.709000 | 6.958000 | 74983981.69 | symbols/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 1 | 0.042000 | 0.042000 | 24148756.34 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 1 | 0.026935 | 0.032290 | 36496694.11 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 1 | 0.042000 | 0.042000 | 24096966.19 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 1 | 0.029581 | 0.041645 | 34429563.22 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 1 | 0.050067 | 0.055600 | 19675098.54 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 1 | 0.055533 | 0.077800 | 17718474.70 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 1 | 0.063800 | 0.072267 | 15870429.58 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 1 | 0.071286 | 0.083429 | 14364101.24 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 8 | 0.361000 | 0.403000 | 22213830.33 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 8 | 0.220143 | 0.238143 | 36290466.30 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 8 | 0.334000 | 0.375000 | 23304251.57 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 8 | 0.222333 | 0.250000 | 35156541.83 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 8 | 0.444667 | 0.458667 | 17686723.90 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 8 | 0.333333 | 0.347667 | 23976095.83 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 8 | 0.555333 | 0.556000 | 14562055.29 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 8 | 0.430333 | 0.458333 | 18577936.38 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 16 | 0.916000 | 0.917000 | 17744182.13 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 16 | 0.625000 | 0.625000 | 25654647.82 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 16 | 0.875000 | 0.875000 | 18505453.33 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 16 | 0.638667 | 0.666667 | 24997239.89 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 16 | 1.083000 | 1.084000 | 14902754.87 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 16 | 0.833000 | 0.834000 | 19608083.43 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 16 | 1.250000 | 1.250000 | 12861260.76 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 16 | 1.000000 | 1.000000 | 16032754.92 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 32 | 2.458000 | 2.500000 | 13071468.25 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 32 | 1.917000 | 1.958000 | 16700659.47 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 32 | 2.416000 | 2.417000 | 13308385.66 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 32 | 1.917000 | 1.958000 | 16464267.65 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 32 | 2.791000 | 2.792000 | 11505593.16 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 32 | 2.250000 | 2.250000 | 14168413.74 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 32 | 3.125000 | 4.084000 | 9844406.09 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 32 | 2.625000 | 3.334000 | 11834713.43 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_unary | 64 | 7.375000 | 9.583000 | 8307508.66 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_unary | 64 | 6.458000 | 7.917000 | 9671831.73 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | unit_binary | 64 | 7.417000 | 7.791000 | 8544578.00 | facts/sec |
| -O2 | Insertion par identifiants de symboles | edb_symbol_id_insert | batch_binary | 64 | 6.500000 | 8.291000 | 9479930.99 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | unit_unary | 64 | 8.250000 | 10.208000 | 7524992.38 | facts/sec |
| -O2 | Insertion de chaînes runtime | edb_runtime_string_insert | batch_unary | 64 | 7.167000 | 7.750000 | 8793321.36 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | composed_unit_binary | 64 | 8.917000 | 11.625000 | 6713789.07 | facts/sec |
| -O2 | Insertion de paires de chaînes runtime | edb_runtime_string_pair_insert | batch_binary | 64 | 7.792000 | 7.917000 | 8165583.75 | facts/sec |
| -O2 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_1pct | 800 | 26.708000 | 28.708000 | 36610.90 | solve_calls/sec |
| -O2 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_10pct | 640 | 95.208000 | 98.167000 | 10493.68 | solve_calls/sec |
| -O2 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_50pct | 128 | 28.250000 | 28.875000 | 35275.40 | solve_calls/sec |
| -O2 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | selectivity_100pct | 64 | 20.042000 | 20.750000 | 49486.93 | solve_calls/sec |
| -O2 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | noise_total | 64 | 5.416000 | 5.625000 | 182979.67 | solve_calls/sec |
| -O2 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | noise_total | 128 | 12.750000 | 12.875000 | 78006.71 | solve_calls/sec |
| -O2 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | noise_total | 256 | 17.625000 | 17.917000 | 57093.12 | solve_calls/sec |
| -O2 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | noise_total | 512 | 28.042000 | 28.458000 | 36084.24 | solve_calls/sec |
| -O2 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | noise_total | 960 | 46.166000 | 47.875000 | 21550.53 | solve_calls/sec |
| -O2 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 64 | 5.541000 | 5.750000 | 189930.38 | solve_calls/sec |
| -O2 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 64 | 7.791000 | 7.917000 | 129285.76 | solve_calls/sec |
| -O2 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 64 | 8.334000 | 8.458000 | 130335.34 | solve_calls/sec |
| -O2 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 64 | 10.208000 | 10.375000 | 101952.27 | solve_calls/sec |
| -O2 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 64 | 13.625000 | 13.792000 | 73447.35 | solve_calls/sec |
| -O2 | Résolution : sélectivité pure | solver_selectivity_pure | selectivity_pure | 64 | 22.667000 | 22.834000 | 45923.69 | solve_calls/sec |
| -O2 | Résolution : taille pure | solver_size_pure | size_pure | 64 | 5.375000 | 5.417000 | 185813.75 | solve_calls/sec |
| -O2 | Résolution : taille pure | solver_size_pure | size_pure | 128 | 9.125000 | 9.417000 | 108911.82 | solve_calls/sec |
| -O2 | Résolution : taille pure | solver_size_pure | size_pure | 256 | 20.458000 | 21.042000 | 48618.60 | solve_calls/sec |
| -O2 | Résolution : taille pure | solver_size_pure | size_pure | 512 | 63.584000 | 65.792000 | 15638.73 | solve_calls/sec |
| -O2 | Résolution : plages denses par prédicat | solver_predicate_dense_ranges | absent_predicate | 512 | 5.958000 | 6.084000 | 166650.58 | solve_calls/sec |
| -O2 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 1 | 5.291000 | 5.333000 | 188221.90 | solve_calls/sec |
| -O2 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 10 | 79.625000 | 82.417000 | 126095.03 | solve_calls/sec |
| -O2 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 100 | 508.770500 | 819.542000 | 158587.85 | solve_calls/sec |
| -O2 | Résolution répétée (même policy/EDB) | solver_repeated_solve | repeated | 1000 | 4932.375000 | 7959.875000 | 173405.89 | solve_calls/sec |
| -O2 | Résolution : jointures | solver_join_bindings | simple_join | 8 | 8.292000 | 8.417000 | 120615.30 | solve_calls/sec |
| -O2 | Résolution : jointures | solver_join_bindings | simple_join | 16 | 12.125000 | 12.292000 | 82240.56 | solve_calls/sec |
| -O2 | Résolution : jointures | solver_join_bindings | simple_join | 32 | 23.750000 | 25.625000 | 41584.22 | solve_calls/sec |
| -O2 | Résolution : jointures | solver_join_bindings | simple_join | 64 | 72.000000 | 74.375000 | 13800.83 | solve_calls/sec |
| -O2 | Résolution : jointures | solver_join_bindings | noisy_join | 128 | 28.875000 | 29.083000 | 34591.61 | solve_calls/sec |
| -O2 | Résolution : jointures | solver_join_bindings | noisy_join | 512 | 48.375000 | 50.500000 | 20470.61 | solve_calls/sec |
| -O2 | Résolution : jointures | solver_join_bindings | noisy_join | 960 | 71.791000 | 74.042000 | 13914.07 | solve_calls/sec |

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
