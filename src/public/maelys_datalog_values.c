/* SPDX-License-Identifier: MPL-2.0 */
#include "src/public/maelys_datalog_values_internal.h"
#include <string.h>

maelys_datalog_status_t maelys_datalog_import_public_value(const maelys_datalog_public_value_t *in,
                                                           int strict,
                                                           maelys_datalog_input_term_t *out) {
    if (!in || !out)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    maelys_datalog_input_term_t value = {0};
    value.kind = (maelys_datalog_term_kind_t)in->kind;
    switch (in->kind) {
    case MAELYS_DATALOG_VALUE_SYMBOL:
        if (!in->as.symbol)
            return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
        value.as.symbol = in->as.symbol;
        break;
    case MAELYS_DATALOG_VALUE_INTEGER:
        value.as.integer = in->as.integer;
        break;
    case MAELYS_DATALOG_VALUE_BOOLEAN:
        if (strict && in->as.boolean != 0 && in->as.boolean != 1)
            return MAELYS_DATALOG_STATUS_INVALID_FIELD;
        value.as.boolean = in->as.boolean ? 1 : 0;
        break;
    default:
        return MAELYS_DATALOG_STATUS_INVALID_FIELD;
    }
    *out = value;
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t
maelys_datalog_resolve_public_terms(const maelys_datalog_symbol_table_t *symbols,
                                    const maelys_datalog_public_value_t *values, size_t count,
                                    maelys_datalog_term_t *terms, int *found, int strict) {
    if (!symbols || !found || ((!values || !terms) && count) || count > MAELYS_DATALOG_MAX_TERMS)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    *found = 1;
    for (size_t i = 0; i < count; ++i) {
        maelys_datalog_input_term_t value;
        maelys_datalog_status_t rc = maelys_datalog_import_public_value(&values[i], strict, &value);
        if (rc)
            return rc;
        memset(&terms[i], 0, sizeof(terms[i]));
        terms[i].kind = value.kind;
        switch (value.kind) {
        case MAELYS_DATALOG_TERM_SYMBOL: {
            size_t length = strnlen(value.as.symbol, MAELYS_DATALOG_MAX_STRING_BYTES + 1u);
            if (length > MAELYS_DATALOG_MAX_STRING_BYTES)
                return MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE;
            int exists = 0;
            maelys_result_t status = maelys_datalog_symbol_lookup_readonly(
                symbols, value.as.symbol, length, &terms[i].as.symbol, &exists);
            if (status)
                return (maelys_datalog_status_t)status;
            if (!exists)
                *found = 0;
            break;
        }
        case MAELYS_DATALOG_TERM_INT:
            terms[i].as.integer = value.as.integer;
            break;
        case MAELYS_DATALOG_TERM_BOOL:
            terms[i].as.boolean = value.as.boolean;
            break;
        default:
            return MAELYS_DATALOG_STATUS_INVALID_FIELD;
        }
    }
    return MAELYS_DATALOG_STATUS_OK;
}
