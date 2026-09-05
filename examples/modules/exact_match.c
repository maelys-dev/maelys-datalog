/* SPDX-License-Identifier: MPL-2.0 */
/* Standalone SDK consumer: intentionally has no engine-internal includes. */
#include <maelys/datalog_module.h>
#include <string.h>

static maelys_datalog_status_t validate(const unsigned char *pattern, size_t n) {
    return pattern || n == 0u ? MAELYS_DATALOG_STATUS_OK : MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
}
static maelys_datalog_status_t cost(size_t value_length, size_t pattern_length, size_t *out) {
    (void)value_length;
    if (pattern_length == SIZE_MAX)
        return MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE;
    *out = pattern_length + 1u;
    return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t evaluate(const unsigned char *value, size_t vn,
                                        const unsigned char *pattern, size_t pn, int *out) {
    *out = vn == pn && (pn == 0u || memcmp(value, pattern, pn) == 0);
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t example_exact_match_register(void) {
    const maelys_datalog_filter_module_t module = {MAELYS_DATALOG_MODULE_ABI_VERSION,
                                                   sizeof(maelys_datalog_filter_module_t),
                                                   "exact_match",
                                                   "example.exact-match.bytes-v1",
                                                   validate,
                                                   cost,
                                                   evaluate};
    return maelys_datalog_register_filter_module(&module);
}
