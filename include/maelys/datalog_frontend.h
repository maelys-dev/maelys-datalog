/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_DATALOG_FRONTEND_H
#define MAELYS_DATALOG_FRONTEND_H
#include "datalog_program.h"
#include "datalog_extension.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct maelys_datalog_program_builder maelys_datalog_program_builder_t;

/* These are the only mutation operations given to a frontend. Inputs are
 * copied synchronously. The builder is callback-scoped; an error is sticky:
 * ignoring it cannot make a partially constructed program load successfully.
 * The domain is host-selected and cannot be extended by the frontend. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_program_add_fact(
    maelys_datalog_program_builder_t *, const maelys_datalog_ir_atom_t *);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_program_add_rule(
    maelys_datalog_program_builder_t *, const maelys_datalog_ir_rule_t *);

typedef struct maelys_datalog_frontend_t {
    uint32_t abi_version;
    size_t struct_size;
    const char *name;
    const char *semantic_id;
    maelys_datalog_status_t (*lower)(const char *, size_t, maelys_datalog_program_builder_t *,
                                     maelys_datalog_diagnostic_t *);
} maelys_datalog_frontend_t;
#ifdef __cplusplus
}
#endif
#endif
