#include "src/core/maelys_datalog_types.h"

#include <assert.h>

void maelys_datalog_get_build_limits(maelys_datalog_build_limits_t *out_limits) {
    assert(out_limits != NULL);
    if (!out_limits) return;

    out_limits->max_symbols = (size_t)MAELYS_DATALOG_MAX_SYMBOLS;
    out_limits->string_pool_bytes = (size_t)MAELYS_DATALOG_STRING_POOL_BYTES;
    out_limits->max_predicates = (size_t)MAELYS_DATALOG_MAX_PREDICATES;
    out_limits->max_rules = (size_t)MAELYS_DATALOG_MAX_RULES;
    out_limits->max_arity = (size_t)MAELYS_DATALOG_MAX_ARITY;
    out_limits->max_body_literals = (size_t)MAELYS_DATALOG_MAX_BODY_LITERALS;
    out_limits->max_depth = (size_t)MAELYS_DATALOG_MAX_DEPTH;
    out_limits->max_edb_facts = (size_t)MAELYS_DATALOG_MAX_EDB_FACTS;
    out_limits->max_idb_facts = (size_t)MAELYS_DATALOG_MAX_IDB_FACTS;
    out_limits->max_facts_per_pred = (size_t)MAELYS_DATALOG_MAX_FACTS_PER_PRED;
}
