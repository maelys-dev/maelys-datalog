/* SPDX-License-Identifier: MPL-2.0 */
#include "src/core/maelys_datalog_query_internal.h"
#include <string.h>

int maelys_datalog_query_whitelist_contains(const maelys_datalog_ruleset_t *ruleset,
                                            const char *predicate, size_t arity) {
    if (!ruleset || !predicate)
        return 0;
    for (size_t i = 0u; i < ruleset->query_whitelist_count; i++) {
        if (ruleset->query_whitelist[i].arity == arity &&
            strcmp(ruleset->query_whitelist[i].name, predicate) == 0)
            return 1;
    }
    return 0;
}
maelys_result_t maelys_datalog_validate_query_predicate(const maelys_datalog_ruleset_t *ruleset,
                                                        const char *predicate, size_t arity,
                                                        maelys_datalog_predicate_id_t *out_pid) {
    if (ruleset->enforces_query_whitelist &&
        !maelys_datalog_query_whitelist_contains(ruleset, predicate, arity))
        return MAELYS_ERR_FORBIDDEN;
    maelys_datalog_predicate_id_t pid = 0;
    if (!maelys_datalog_predicate_registry_find(&ruleset->registry, predicate, arity, &pid))
        return MAELYS_ERR_INVALID_FIELD;
    const maelys_datalog_predicate_def_t *def =
        maelys_datalog_predicate_registry_get(&ruleset->registry, pid);
    if (!def || !(def->kind_flags & MAELYS_DATALOG_PRED_KIND_QUERY))
        return MAELYS_ERR_INVALID_FIELD;
    if (out_pid)
        *out_pid = pid;
    return MAELYS_OK;
}
