#include "src/core/maelys_datalog_decision.h"

const char *maelys_datalog_decision_name(maelys_datalog_decision_t decision) {
    switch (decision) {
        case MAELYS_DATALOG_DECISION_ALLOW: return MAELYS_DATALOG_DECISION_NAME_ALLOW;
        case MAELYS_DATALOG_DECISION_REDUCED: return MAELYS_DATALOG_DECISION_NAME_REDUCED;
        case MAELYS_DATALOG_DECISION_DENY_DEFAULT: return MAELYS_DATALOG_DECISION_NAME_DENY_DEFAULT;
        case MAELYS_DATALOG_DECISION_DENY_CONFLICT: return MAELYS_DATALOG_DECISION_NAME_DENY_CONFLICT;
        case MAELYS_DATALOG_DECISION_DENY:
        default: return MAELYS_DATALOG_DECISION_NAME_DENY;
    }
}

maelys_result_t maelys_datalog_decision_from_queries(
    const maelys_datalog_query_result_t *deny_result,
    const maelys_datalog_query_result_t *reduce_result,
    const maelys_datalog_query_result_t *allow_result,
    maelys_datalog_decision_t *out_decision) {
    if (!deny_result || !reduce_result || !allow_result || !out_decision) return MAELYS_ERR_INVALID_ARGUMENT;
    /* Decision precedence is deny > reduce > allow. A deny combined with allow
     * or reduce is surfaced as DENY_CONFLICT. Allow + reduce without deny
     * intentionally yields REDUCED; reduce is treated as conservative allow,
     * not as a conflict. */
    if (deny_result->derived && (allow_result->derived || reduce_result->derived)) {
        *out_decision = MAELYS_DATALOG_DECISION_DENY_CONFLICT;
        return MAELYS_OK;
    }
    if (deny_result->derived) {
        *out_decision = MAELYS_DATALOG_DECISION_DENY;
    } else if (reduce_result->derived) {
        *out_decision = MAELYS_DATALOG_DECISION_REDUCED;
    } else if (allow_result->derived) {
        *out_decision = MAELYS_DATALOG_DECISION_ALLOW;
    } else {
        *out_decision = MAELYS_DATALOG_DECISION_DENY_DEFAULT;
    }
    return MAELYS_OK;
}
