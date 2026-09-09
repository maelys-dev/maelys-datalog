/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_DATALOG_MODULES_INTERNAL_H
#define MAELYS_DATALOG_MODULES_INTERNAL_H
#include "maelys/datalog_extension.h"
void maelys_datalog_modules_seal(void);
int maelys_datalog_identity_valid(const char *, size_t, int);
const maelys_datalog_planner_module_t *maelys_datalog_active_planner(void);
const maelys_datalog_planner_module_t *
maelys_datalog_context_planner(const maelys_datalog_context_t *);
void maelys_datalog_context_retain(maelys_datalog_context_t *);
void maelys_datalog_context_release(maelys_datalog_context_t *);
int maelys_datalog_context_is_sealed(const maelys_datalog_context_t *);
const maelys_datalog_frontend_t *maelys_datalog_context_frontend(const maelys_datalog_context_t *,
                                                                 const char *);
const maelys_datalog_backend_t *maelys_datalog_context_backend(const maelys_datalog_context_t *,
                                                               const char *);
int maelys_datalog_frontend_descriptor_valid(const maelys_datalog_frontend_t *);
int maelys_datalog_backend_descriptor_valid(const maelys_datalog_backend_t *);
#endif
