/* SPDX-License-Identifier: MPL-2.0 */
#include <maelys/datalog.h>
#include <stdbool.h>

#ifdef TEST_QUERY
#define BUILD(a, b, ...) MAELYS_DATALOG_QUERY(a, b, __VA_ARGS__)
#define INPUT_TYPE const maelys_datalog_result_t
#define OUTPUT_TYPE int
#elif defined(TEST_BATCH)
#define BUILD(a, b, ...) MAELYS_DATALOG_ADD_FACTS(a, b, MAELYS_DATALOG_FACT(__VA_ARGS__))
#define INPUT_TYPE maelys_datalog_input_edb_t
#define OUTPUT_TYPE maelys_datalog_public_diagnostic_t
#else
#define BUILD(a, b, ...) MAELYS_DATALOG_ADD_FACT(a, b, __VA_ARGS__)
#define INPUT_TYPE maelys_datalog_input_edb_t
#define OUTPUT_TYPE maelys_datalog_public_diagnostic_t
#endif

const maelys_datalog_value_t c_symbol = MAELYS_DATALOG_SYMBOL("alice");

const maelys_datalog_predicate_t c_declarations[] = {
    MAELYS_DATALOG_EDB("seed", 1),
    MAELYS_DATALOG_IDB("hidden", 1),
    MAELYS_DATALOG_IDB_QUERY("allow", 1),
};

maelys_datalog_status_t fact_builder_consumer(INPUT_TYPE *edb, OUTPUT_TYPE *diagnostic) {
#if defined(REJECT_FLOAT)
    return BUILD(edb, diagnostic, "bad", 1.5f);
#elif defined(REJECT_DOUBLE)
    return BUILD(edb, diagnostic, "bad", 1.5);
#elif defined(REJECT_POINTER)
    return BUILD(edb, diagnostic, "bad", (void *)0);
#elif defined(REJECT_STRUCT)
    maelys_datalog_value_t value = {0};
    return BUILD(edb, diagnostic, "bad", value);
#elif defined(REJECT_TOO_MANY)
    return BUILD(edb, diagnostic, "bad", 1, 2, 3, 4, 5);
#else
    maelys_datalog_status_t rc = BUILD(edb, diagnostic, "ready");
    if (rc) return rc;
    rc = BUILD(edb, diagnostic, "user", "alice");
    if (rc) return rc;
    rc = BUILD(edb, diagnostic, "owns", "alice", "roadmap.pdf");
    if (rc) return rc;
    const bool enabled = true;
    rc = BUILD(edb, diagnostic, "mixed", "alice", 42, enabled);
    if (rc) return rc;
    return BUILD(edb, diagnostic, "four", (size_t)1,
        (int64_t)-2, (uint64_t)3, MAELYS_DATALOG_BOOL(1 < 2));
#endif
}
