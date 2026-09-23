/* SPDX-License-Identifier: MPL-2.0 */
#include "src/public/maelys_datalog_values_internal.h"
#include <string.h>

maelys_datalog_status_t maelys_datalog_validate_value(const maelys_datalog_value_t *value,
                                                       int strict) {
    if (!value) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    switch (value->kind) {
    case MAELYS_DATALOG_VALUE_SYMBOL:
        return value->as.symbol ? MAELYS_DATALOG_STATUS_OK : MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    case MAELYS_DATALOG_VALUE_INTEGER:
        return MAELYS_DATALOG_STATUS_OK;
    case MAELYS_DATALOG_VALUE_BOOLEAN:
        return strict && value->as.boolean != 0 && value->as.boolean != 1
            ? MAELYS_DATALOG_STATUS_INVALID_FIELD : MAELYS_DATALOG_STATUS_OK;
    default:
        return MAELYS_DATALOG_STATUS_INVALID_FIELD;
    }
}
maelys_datalog_status_t
maelys_datalog_resolve_public_terms(const maelys_datalog_symbol_table_t *symbols,
                                    const maelys_datalog_value_t *values, size_t count,
                                    maelys_datalog_internal_term_t *terms, int *found, int strict) {
    if (!symbols || !found || ((!values || !terms) && count) || count > MAELYS_DATALOG_MAX_TERMS)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    *found = 1;
    for (size_t i = 0; i < count; ++i) {
        const maelys_datalog_value_t *value = &values[i];
        maelys_datalog_status_t rc = maelys_datalog_validate_value(value, strict);
        if (rc)
            return rc;
        memset(&terms[i], 0, sizeof(terms[i]));
        terms[i].kind = (maelys_datalog_internal_term_kind_t)value->kind;
        switch (value->kind) {
        case MAELYS_DATALOG_VALUE_SYMBOL: {
            size_t length = strnlen(value->as.symbol, MAELYS_DATALOG_MAX_STRING_BYTES + 1u);
            if (length > MAELYS_DATALOG_MAX_STRING_BYTES)
                return MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE;
            int exists = 0;
            maelys_result_t status = maelys_datalog_symbol_lookup_readonly(
                symbols, value->as.symbol, length, &terms[i].as.symbol, &exists);
            if (status)
                return (maelys_datalog_status_t)status;
            if (!exists)
                *found = 0;
            break;
        }
        case MAELYS_DATALOG_VALUE_INTEGER:
            terms[i].as.integer = value->as.integer;
            break;
        case MAELYS_DATALOG_VALUE_BOOLEAN:
            terms[i].as.boolean = !!value->as.boolean;
            break;
        default:
            return MAELYS_DATALOG_STATUS_INVALID_FIELD;
        }
    }
    return MAELYS_DATALOG_STATUS_OK;
}
