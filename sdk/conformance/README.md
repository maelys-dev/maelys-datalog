# Extension conformance kit

`maelys_conformance.h` is a test-only, header-only C11 kit installed under
`share/maelys-datalog/conformance`. It depends only on the public SDK.
The [standalone examples](../examples/README.md) show the same
build/test workflow. Return value 0 means all supplied checks passed; 1 names
the failing expression and source location.

The composite `bundle` example combines filter fixtures with public-context
integration tests: its frontend depends on a companion filter. Missing or
incompatible filter semantics reject the load; invalid registration publishes
neither component. A single-component frontend helper alone cannot exercise
this composition.

## Fixtures by extension type

| Helper | Checks | Fixtures the author must supply |
| --- | --- | --- |
| `maelys_conformance_frontend` | Register, seal, lower through the real host validator; expected rejection or rule count | Valid syntax, syntax errors, malformed IR including EDB heads, source locations |
| `maelys_conformance_backend` | Solve the same EDB with reference and candidate; compare ground probes and enumeration counts | Every supported feature, empty/absent cases, recursion, all relevant queryable predicates |
| `maelys_conformance_planner` | Successful, in-range, repeatable selection from safe candidates | Candidate counts, ties, bound terms, empty/invalid input errors |
| `maelys_conformance_filter` | Pattern validation, expected errors, positive bounded cost and boolean result | Valid/invalid patterns, empty data, embedded bytes, overflow and evaluation errors |

The backend example uses 20 deterministic five-node graphs and all 25 reachable
pairs per graph. These finite checks are not a proof of algorithmic equivalence.
Add feature-specific suites, including explanations and work limits, before
advertising those capabilities. The frontend helper does not assert source
locations; add public program/diagnostic assertions for your dialect.

The host suites `test_maelys_datalog_modules`,
`test_maelys_datalog_compiler` and `test_maelys_datalog_context` complement
these fixtures with invalid descriptors, malformed IR, provider failures,
capability gates, invalid planner indexes, atomic rollback and context lifetime.
Run them with ASan/UBSan as well as the installed-SDK check.

## Boundaries

A native callback is trusted code, not sandboxed by this kit. Determinism on two
calls does not prove purity; a positive declared cost does not measure CPU time;
a successful fixture does not prove memory safety. Validate callbacks with
sanitizers, fuzzing and workload-specific measurements. The kit does not load
shared libraries, inspect proprietary source or change production behavior.
