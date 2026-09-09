/* SPDX-License-Identifier: MIT */
#include "extension.h"

static maelys_datalog_status_t lower_source(
    const char *source, size_t length, maelys_datalog_program_builder_t *builder,
    maelys_datalog_public_diagnostic_t *diagnostic)
{
    (void)source;
    (void)length;
    (void)builder;
    if (diagnostic)
        *diagnostic = (maelys_datalog_public_diagnostic_t){0};
    /* TODO: translate your language through the public IR builder.
     * Return OK only after complete lowering; retain source locations. */
    return MAELYS_DATALOG_STATUS_UNSUPPORTED;
}

maelys_datalog_extension_t starter_frontend_declaration(void)
{
    static const maelys_datalog_frontend_t component = {
        .abi_version = MAELYS_DATALOG_PROGRAM_ABI_VERSION,
        .struct_size = sizeof(maelys_datalog_frontend_t),
        .name = "starter_frontend",
        .semantic_id = "starter.frontend.unimplemented.v1",
        .lower = lower_source
    };
    return (maelys_datalog_extension_t){
        .abi_version = MAELYS_DATALOG_EXTENSION_ABI_VERSION,
        .struct_size = sizeof(maelys_datalog_extension_t),
        .name = "starter_frontend",
        .semantic_id = "starter.frontend.package.v1",
        .frontends = &component,
        .frontend_count = 1
    };
}
