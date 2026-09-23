/* SPDX-License-Identifier: MPL-2.0 */
#include "maelys/datalog.h"
#include "src/core/maelys_datalog_predicate_registry.h"

maelys_datalog_status_t maelys_datalog_limit_get(
    maelys_datalog_limit_t limit, size_t *out_value) {
    if (!out_value) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    size_t value;
    switch (limit) {
        case MAELYS_DATALOG_LIMIT_MAX_SYMBOLS: value = MAELYS_DATALOG_MAX_SYMBOLS; break;
        case MAELYS_DATALOG_LIMIT_STRING_POOL_BYTES: value = MAELYS_DATALOG_STRING_POOL_BYTES; break;
        case MAELYS_DATALOG_LIMIT_MAX_PREDICATES: value = MAELYS_DATALOG_MAX_PREDICATES; break;
        case MAELYS_DATALOG_LIMIT_MAX_RULES: value = MAELYS_DATALOG_MAX_RULES; break;
        case MAELYS_DATALOG_LIMIT_MAX_ARITY: value = MAELYS_DATALOG_MAX_ARITY; break;
        case MAELYS_DATALOG_LIMIT_MAX_BODY_LITERALS: value = MAELYS_DATALOG_MAX_BODY_LITERALS; break;
        case MAELYS_DATALOG_LIMIT_MAX_DEPTH: value = MAELYS_DATALOG_MAX_DEPTH; break;
        case MAELYS_DATALOG_LIMIT_MAX_EDB_FACTS: value = MAELYS_DATALOG_MAX_EDB_FACTS; break;
        case MAELYS_DATALOG_LIMIT_MAX_IDB_FACTS: value = MAELYS_DATALOG_MAX_IDB_FACTS; break;
        case MAELYS_DATALOG_LIMIT_MAX_FACTS_PER_PRED: value = MAELYS_DATALOG_MAX_FACTS_PER_PRED; break;
        case MAELYS_DATALOG_LIMIT_MAX_STRING_BYTES: value = MAELYS_DATALOG_MAX_STRING_BYTES; break;
        case MAELYS_DATALOG_LIMIT_INPUT_EDB_TEXT_BYTES: value = MAELYS_DATALOG_INPUT_EDB_TEXT_BYTES; break;
        default: return MAELYS_DATALOG_STATUS_UNSUPPORTED;
    }
    *out_value = value;
    return MAELYS_DATALOG_STATUS_OK;
}

