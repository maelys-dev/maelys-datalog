/* SPDX-License-Identifier: MPL-2.0 */
#include "src/core/maelys_datalog_filter.h"

static maelys_result_t callback_status(maelys_datalog_status_t status) {
    if (status > MAELYS_DATALOG_STATUS_OK || status < MAELYS_DATALOG_STATUS_INVALID_STATE)
        return MAELYS_ERR_INTERNAL;
    return (maelys_result_t)status;
}

maelys_result_t maelys_datalog_filter_validate_in(const maelys_datalog_context_t *context,
                                               maelys_datalog_filter_kind_t kind,
                                               const unsigned char *pattern,
                                               size_t pattern_length) {
    const maelys_datalog_filter_definition_t *d = maelys_datalog_filter_by_kind_in(context, kind);
    if (!d || (!pattern && pattern_length))
        return MAELYS_ERR_INVALID_ARGUMENT;
    return callback_status(d->module->validate_pattern(pattern, pattern_length));
}

maelys_result_t maelys_datalog_filter_cost_in(const maelys_datalog_context_t *context,
                                           maelys_datalog_filter_kind_t kind, size_t value_length,
                                           size_t pattern_length, size_t *out_cost) {
    const maelys_datalog_filter_definition_t *d = maelys_datalog_filter_by_kind_in(context, kind);
    if (!d || !out_cost)
        return MAELYS_ERR_INVALID_ARGUMENT;
    size_t cost = 0u;
    maelys_result_t rc = callback_status(d->module->cost(value_length, pattern_length, &cost));
    if (rc != MAELYS_OK)
        return rc;
    if (kind > MAELYS_DATALOG_FILTER_CONTAINS && cost == 0u)
        return MAELYS_ERR_INVALID_STATE;
    *out_cost = cost;
    return MAELYS_OK;
}

maelys_result_t maelys_datalog_filter_evaluate_in(const maelys_datalog_context_t *context,
                                               maelys_datalog_filter_kind_t kind,
                                               const unsigned char *value, size_t value_length,
                                               const unsigned char *pattern, size_t pattern_length,
                                               int *out_matched) {
    const maelys_datalog_filter_definition_t *d = maelys_datalog_filter_by_kind_in(context, kind);
    if (!d || !out_matched || (!value && value_length) || (!pattern && pattern_length)) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    int matched = -1;
    maelys_result_t rc = callback_status(
        d->module->evaluate(value, value_length, pattern, pattern_length, &matched));
    if (rc != MAELYS_OK)
        return rc;
    if (matched != 0 && matched != 1)
        return MAELYS_ERR_INVALID_STATE;
    *out_matched = matched;
    return MAELYS_OK;
}

maelys_result_t maelys_datalog_filter_validate(maelys_datalog_filter_kind_t kind,
    const unsigned char *pattern, size_t length) {
    return maelys_datalog_filter_validate_in(NULL, kind, pattern, length);
}
maelys_result_t maelys_datalog_filter_cost(maelys_datalog_filter_kind_t kind,
    size_t value_length, size_t pattern_length, size_t *cost) {
    return maelys_datalog_filter_cost_in(NULL, kind, value_length, pattern_length, cost);
}
maelys_result_t maelys_datalog_filter_evaluate(maelys_datalog_filter_kind_t kind,
    const unsigned char *value, size_t value_length, const unsigned char *pattern,
    size_t pattern_length, int *matched) {
    return maelys_datalog_filter_evaluate_in(NULL, kind, value, value_length, pattern, pattern_length, matched);
}
