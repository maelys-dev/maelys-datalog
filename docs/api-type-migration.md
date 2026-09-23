# Application type migration for 0.10.0

Application and extension interfaces share a single declaration for each kind
of input data. This is a coordinated source migration; obsolete type names are
removed, without compatibility typedefs. Rebuild C/C++ and CFFI consumers.

| Before | Application declaration |
| --- | --- |
| `maelys_datalog_public_value_t`, `maelys_datalog_input_term_t` | `maelys_datalog_value_t` |
| `maelys_datalog_public_fact_t`, `maelys_datalog_input_fact_t` | `maelys_datalog_fact_t` |
| `maelys_datalog_public_predicate_t` | `maelys_datalog_predicate_t` |
| `maelys_datalog_public_domain_t` | `maelys_datalog_domain_t` |
| `maelys_datalog_public_term_view_t` | `maelys_datalog_term_view_t` |
| `maelys_datalog_public_fact_view_t` | `maelys_datalog_fact_view_t` |

An input value uses `MAELYS_DATALOG_VALUE_SYMBOL`, `VALUE_INTEGER` or
`VALUE_BOOLEAN` (each prefixed with `MAELYS_DATALOG_`). It cannot be a variable.
Symbols are text on input, whereas result views carry result-scoped symbol IDs.
The types are distinct because their identity and lifetime contracts differ.

Native solver facts, terms, EDBs, rulesets, policy sets, prepared sessions and
solve results now have `maelys_datalog_internal_*_t` names. In particular, the
old native `maelys_datalog_fact_t` is `maelys_datalog_internal_fact_t`; do not
reinterpret an old native fact as a new application fact. Materialization
validates and interns application values explicitly. Its input strings are
borrowed only for the call, and boolean normalization does not mutate the input.

Use `maelys_datalog_limit_get` instead of `maelys_datalog_get_build_limits` and
`maelys_datalog_build_limits_t`. These are build capacities, not occupancy or
future per-session quotas. Unknown limit IDs return `UNSUPPORTED` without
changing the output. The Python build-limit object and Wasm packed-limit result
retain their language-level shape while reading the same scalar C accessor.

The common data rename does not change the layout of the application values,
facts or views. The input copy formerly made before materialization is removed;
canonical output storage remains reserved for external backends. No reduction
in session reservation or performance improvement is promised by the rename.
Existing result/explanation leases, admission checks and allocation guarantees
continue to apply. A diagnostic/callback contract change is a separate ABI
change; rebuilding against shared names alone is not proof of compatibility
with such a change.
