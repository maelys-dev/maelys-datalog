/* SPDX-License-Identifier: MIT */
#include "extension.h"

static maelys_datalog_status_t select_candidate(
    const maelys_datalog_join_candidate_t *candidates, size_t count, size_t *selected)
{
    (void)candidates;
    (void)count;
    if (!selected)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    *selected = SIZE_MAX;
    /* TODO: select an index from the supplied safe candidates only. */
    return MAELYS_DATALOG_STATUS_UNSUPPORTED;
}

maelys_datalog_extension_t starter_planner_declaration(void)
{
    static const maelys_datalog_planner_module_t component = {
        .abi_version = MAELYS_DATALOG_MODULE_ABI_VERSION,
        .struct_size = sizeof(maelys_datalog_planner_module_t),
        .name = "starter_planner",
        .semantic_id = "starter.planner.unimplemented.v1",
        .choose = select_candidate
    };
    return (maelys_datalog_extension_t){
        .abi_version = MAELYS_DATALOG_EXTENSION_ABI_VERSION,
        .struct_size = sizeof(maelys_datalog_extension_t),
        .name = "starter_planner",
        .semantic_id = "starter.planner.package.v1",
        .planners = &component,
        .planner_count = 1
    };
}
