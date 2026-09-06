/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_DATALOG_PROGRAM_INTERNAL_H
#define MAELYS_DATALOG_PROGRAM_INTERNAL_H
#include "maelys/datalog_backend.h"
#include "src/core/maelys_datalog_ruleset.h"
#include "src/core/maelys_datalog_diagnostic.h"
#include "src/core/maelys_datalog_solver.h"

struct maelys_datalog_program {
    const maelys_datalog_ruleset_t *ruleset;
};
struct maelys_datalog_program_builder {
    maelys_datalog_ruleset_t *ruleset;
    maelys_datalog_status_t error;
};
maelys_result_t maelys_datalog_validate_program(maelys_datalog_ruleset_t *, const char *,
                                                maelys_datalog_diagnostic_t *);
maelys_result_t maelys_datalog_validate_rule(maelys_datalog_ruleset_t *,
                                             const maelys_datalog_rule_t *, const char *, size_t,
                                             size_t, maelys_datalog_diagnostic_t *);
maelys_result_t maelys_datalog_assign_strata(maelys_datalog_ruleset_t *, const char *, size_t,
                                             size_t, maelys_datalog_diagnostic_t *);
maelys_result_t maelys_datalog_compile_frontend(const char *, const char *, const char *, size_t,
                                                const maelys_datalog_frontend_t *,
                                                maelys_datalog_ruleset_t *,
                                                maelys_datalog_public_diagnostic_t *);
void maelys_datalog_copy_load_diagnostic(maelys_datalog_public_diagnostic_t *,
                                         const maelys_datalog_diagnostic_t *);
void maelys_datalog_copy_solve_diagnostic(maelys_datalog_public_diagnostic_t *,
                                          const maelys_datalog_solve_diagnostic_t *);
maelys_datalog_status_t maelys_datalog_callback_status(maelys_datalog_status_t);
int maelys_datalog_identity_valid(const char *, size_t, int);
maelys_result_t maelys_datalog_export_fact(const maelys_datalog_ruleset_t *,
                                           const maelys_datalog_fact_t *,
                                           maelys_datalog_public_fact_t *);
#endif
