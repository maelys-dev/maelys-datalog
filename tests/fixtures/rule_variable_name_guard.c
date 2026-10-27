/* SPDX-License-Identifier: MPL-2.0 */
/* The same consumer builds against 0.26 and an SDK exposing the additive API. */
#include <maelys/datalog_program.h>
int main(void) {
#if MAELYS_DATALOG_HAS_RULE_VARIABLE_NAMES
    const char *name = 0;
    maelys_datalog_status_t (*accessor)(const maelys_datalog_program_t *, size_t,
                                      uint32_t, const char **) = maelys_datalog_program_rule_variable_name;
    return accessor(0, 0, 0, &name) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT && !name ? 0 : 1;
#else
    return 0;
#endif
}
