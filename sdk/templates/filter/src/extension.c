/* SPDX-License-Identifier: MIT */
#include "extension.h"

static maelys_datalog_status_t inspect_pattern(const unsigned char *pattern, size_t length)
{
    (void)pattern;
    (void)length;
    /* TODO: accept only patterns supported by your documented dialect. */
    return MAELYS_DATALOG_STATUS_UNSUPPORTED;
}

static maelys_datalog_status_t estimate_work(size_t value_length, size_t pattern_length,
                                            size_t *units)
{
    (void)value_length;
    (void)pattern_length;
    if (!units)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    *units = 0;
    /* TODO: return a positive conservative bound with checked overflow. */
    return MAELYS_DATALOG_STATUS_UNSUPPORTED;
}

static maelys_datalog_status_t match_value(
    const unsigned char *value, size_t value_length,
    const unsigned char *pattern, size_t pattern_length, int *matched)
{
    (void)value;
    (void)value_length;
    (void)pattern;
    (void)pattern_length;
    if (!matched)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    *matched = 0;
    /* TODO: distinguish a successful non-match from an execution error. */
    return MAELYS_DATALOG_STATUS_UNSUPPORTED;
}

maelys_datalog_extension_t starter_filter_declaration(void)
{
    static const maelys_datalog_filter_module_t component = {
        .abi_version = MAELYS_DATALOG_MODULE_ABI_VERSION,
        .struct_size = sizeof(maelys_datalog_filter_module_t),
        .name = "starter_filter",
        .semantic_id = "starter.filter.unimplemented.v1",
        .validate_pattern = inspect_pattern,
        .cost = estimate_work,
        .evaluate = match_value
    };
    return (maelys_datalog_extension_t){
        .abi_version = MAELYS_DATALOG_EXTENSION_ABI_VERSION,
        .struct_size = sizeof(maelys_datalog_extension_t),
        .name = "starter_filter",
        .semantic_id = "starter.filter.package.v1",
        .filters = &component,
        .filter_count = 1
    };
}
