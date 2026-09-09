/* SPDX-License-Identifier: MIT */
#include "extension.h"

static maelys_datalog_status_t prepare_program(
    const maelys_datalog_program_t *program, void **state)
{
    (void)program;
    if (!state)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    *state = NULL;
    /* TODO: create owned prepared state for supported validated programs. */
    return MAELYS_DATALOG_STATUS_UNSUPPORTED;
}

static maelys_datalog_status_t solve_program(
    void *state, const maelys_datalog_public_fact_t *inputs, size_t count,
    maelys_datalog_backend_output_t *output, void **result,
    maelys_datalog_public_diagnostic_t *diagnostic)
{
    (void)state;
    (void)inputs;
    (void)count;
    (void)output;
    if (!result)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    *result = NULL;
    if (diagnostic)
        *diagnostic = (maelys_datalog_public_diagnostic_t){0};
    /* TODO: charge work and emit the complete IDB, never a partial success. */
    return MAELYS_DATALOG_STATUS_UNSUPPORTED;
}

static void release_result(void *state, void *result)
{
    (void)state;
    (void)result;
    /* TODO: release owned result state when allocations are introduced. */
}

static void release_program(void *state)
{
    (void)state;
    /* TODO: release owned prepared state, including after callback errors. */
}

maelys_datalog_extension_t starter_backend_declaration(void)
{
    static const maelys_datalog_backend_t component = {
        .abi_version = MAELYS_DATALOG_BACKEND_ABI_VERSION,
        .struct_size = sizeof(maelys_datalog_backend_t),
        .name = "starter_backend",
        .semantic_id = "starter.backend.unimplemented.v1",
        .capabilities = 0,
        .prepare = prepare_program,
        .solve = solve_program,
        .destroy_result = release_result,
        .destroy = release_program
    };
    return (maelys_datalog_extension_t){
        .abi_version = MAELYS_DATALOG_EXTENSION_ABI_VERSION,
        .struct_size = sizeof(maelys_datalog_extension_t),
        .name = "starter_backend",
        .semantic_id = "starter.backend.package.v1",
        .backends = &component,
        .backend_count = 1
    };
}
