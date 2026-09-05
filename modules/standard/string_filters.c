/* SPDX-License-Identifier: MPL-2.0 */
#include "string_filters.h"
#include <string.h>

static maelys_datalog_status_t validate(const unsigned char *pattern, size_t n) {
    return pattern || n == 0u ? MAELYS_DATALOG_STATUS_OK : MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
}
static maelys_datalog_status_t edge_cost(size_t v, size_t p, size_t *out) {
    (void)v;
    *out = p;
    return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t contains_cost(size_t v, size_t p, size_t *out) {
    v = v == 0u ? 1u : v;
    p = p == 0u ? 1u : p;
    if (v > SIZE_MAX / p)
        return MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE;
    *out = v * p;
    return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t starts_with(const unsigned char *v, size_t vn,
                                           const unsigned char *p, size_t pn, int *out) {
    *out = pn == 0u || (vn >= pn && memcmp(v, p, pn) == 0);
    return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t ends_with(const unsigned char *v, size_t vn, const unsigned char *p,
                                         size_t pn, int *out) {
    *out = pn == 0u || (vn >= pn && memcmp(v + vn - pn, p, pn) == 0);
    return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t contains(const unsigned char *v, size_t vn, const unsigned char *p,
                                        size_t pn, int *out) {
    *out = pn == 0u;
    if (pn == 0u || pn > vn)
        return MAELYS_DATALOG_STATUS_OK;
    for (size_t i = 0u; i <= vn - pn; ++i) {
        if (memcmp(v + i, p, pn) == 0) {
            *out = 1;
            break;
        }
    }
    return MAELYS_DATALOG_STATUS_OK;
}
const maelys_datalog_filter_module_t *maelys_datalog_standard_string_filters(size_t *out_count) {
    static const maelys_datalog_filter_module_t definitions[] = {
        {MAELYS_DATALOG_MODULE_ABI_VERSION, sizeof(maelys_datalog_filter_module_t), "starts_with",
         "string.starts-with.utf8-bytes-v1", validate, edge_cost, starts_with},
        {MAELYS_DATALOG_MODULE_ABI_VERSION, sizeof(maelys_datalog_filter_module_t), "ends_with",
         "string.ends-with.utf8-bytes-v1", validate, edge_cost, ends_with},
        {MAELYS_DATALOG_MODULE_ABI_VERSION, sizeof(maelys_datalog_filter_module_t), "contains",
         "string.contains.utf8-bytes-v1", validate, contains_cost, contains},
    };
    *out_count = sizeof(definitions) / sizeof(definitions[0]);
    return definitions;
}
