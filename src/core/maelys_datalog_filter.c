#include "src/core/maelys_datalog_filter.h"

#include <limits.h>
#include <string.h>

static const maelys_datalog_filter_definition_t DEFINITIONS[] = {
    {MAELYS_DATALOG_FILTER_STARTS_WITH,
     "starts_with",
     "string.starts-with.utf8-bytes-v1"},
    {MAELYS_DATALOG_FILTER_ENDS_WITH,
     "ends_with",
     "string.ends-with.utf8-bytes-v1"},
    {MAELYS_DATALOG_FILTER_CONTAINS,
     "contains",
     "string.contains.utf8-bytes-v1"},
};

const maelys_datalog_filter_definition_t *maelys_datalog_filter_by_name(
    const char *name) {
    if (!name) return NULL;
    for (size_t i = 0; i < sizeof(DEFINITIONS) / sizeof(DEFINITIONS[0]); i++) {
        if (strcmp(DEFINITIONS[i].name, name) == 0) return &DEFINITIONS[i];
    }
    return NULL;
}

const maelys_datalog_filter_definition_t *maelys_datalog_filter_by_kind(
    maelys_datalog_filter_kind_t kind) {
    for (size_t i = 0; i < sizeof(DEFINITIONS) / sizeof(DEFINITIONS[0]); i++) {
        if (DEFINITIONS[i].kind == kind) return &DEFINITIONS[i];
    }
    return NULL;
}

maelys_result_t maelys_datalog_filter_cost(
    maelys_datalog_filter_kind_t kind,
    size_t value_length,
    size_t pattern_length,
    size_t *out_cost) {
    if (!out_cost || !maelys_datalog_filter_by_kind(kind)) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    if (kind == MAELYS_DATALOG_FILTER_CONTAINS) {
        const size_t value_units = value_length == 0u ? 1u : value_length;
        const size_t pattern_units = pattern_length == 0u ? 1u : pattern_length;
        if (value_units > SIZE_MAX / pattern_units) return MAELYS_ERR_PAYLOAD_TOO_LARGE;
        *out_cost = value_units * pattern_units;
    } else {
        *out_cost = pattern_length;
    }
    return MAELYS_OK;
}

maelys_result_t maelys_datalog_filter_evaluate(
    maelys_datalog_filter_kind_t kind,
    const unsigned char *value,
    size_t value_length,
    const unsigned char *pattern,
    size_t pattern_length,
    int *out_matched) {
    if ((!value && value_length != 0u) || (!pattern && pattern_length != 0u) ||
        !out_matched || !maelys_datalog_filter_by_kind(kind)) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    int matched = 0;
    if (pattern_length == 0u) {
        matched = 1;
    } else if (kind == MAELYS_DATALOG_FILTER_STARTS_WITH) {
        matched = value_length >= pattern_length &&
                  memcmp(value, pattern, pattern_length) == 0;
    } else if (kind == MAELYS_DATALOG_FILTER_ENDS_WITH) {
        matched = value_length >= pattern_length &&
                  memcmp(value + value_length - pattern_length,
                         pattern,
                         pattern_length) == 0;
    } else {
        for (size_t i = 0; i + pattern_length <= value_length; i++) {
            if (memcmp(value + i, pattern, pattern_length) == 0) {
                matched = 1;
                break;
            }
        }
    }
    *out_matched = matched;
    return MAELYS_OK;
}
