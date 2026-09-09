/* SPDX-License-Identifier: MPL-2.0 */
#include <maelys/datalog_extension.h>

static maelys_datalog_status_t choose(const maelys_datalog_join_candidate_t *candidates,
                                      size_t count, size_t *out) {
    if (!candidates || !count || !out)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    size_t best = 0;
    for (size_t i = 1; i < count; ++i) {
        /* Prefer more bound terms, then retain the core's tie-breaking score.
         * All candidates are already safe; never execute a literal here. */
        if (candidates[i].bound_terms > candidates[best].bound_terms ||
            (candidates[i].bound_terms == candidates[best].bound_terms &&
             candidates[i].default_score > candidates[best].default_score))
            best = i;
    }
    *out = best;
    return MAELYS_DATALOG_STATUS_OK;
}
const maelys_datalog_planner_module_t *example_selective_planner(void) {
    static const maelys_datalog_planner_module_t p = {MAELYS_DATALOG_MODULE_ABI_VERSION, sizeof(p),
                                                      "selective", "example.selective.v1", choose};
    return &p;
}
maelys_datalog_extension_t example_planner_extension(void) {
    maelys_datalog_extension_t e = {0};
    e.abi_version = MAELYS_DATALOG_EXTENSION_ABI_VERSION;
    e.struct_size = sizeof(e);
    e.name = "example_planner";
    e.semantic_id = "example.planner.v1";
    e.planners = example_selective_planner();
    e.planner_count = 1;
    return e;
}
