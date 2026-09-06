/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_DATALOG_PROGRAM_INTERNAL_H
#define MAELYS_DATALOG_PROGRAM_INTERNAL_H
#include "maelys/datalog_backend.h"
#include "src/core/maelys_datalog_ruleset.h"
#include "src/core/maelys_datalog_diagnostic.h"
#include "src/core/maelys_datalog_solver.h"
#include "src/core/maelys_datalog_prepared_session.h"
#include "src/modules/maelys_datalog_modules_internal.h"

struct maelys_datalog_program {
    const maelys_datalog_ruleset_t *ruleset;
    /* Borrowed runtime inputs; private to the built-in reference backend. */
    maelys_datalog_prepared_session_t *prepared_inputs;
};
/* Transient parser provenance, never part of the IR or a retained session.
 * Every normalized OR alternative points to its source clause's checkpoint. */
typedef struct {
    size_t first_rule, first_filter, first_pattern_byte, first_fact;
    maelys_datalog_source_location_t end;
} maelys_datalog_clause_origin_t;
typedef struct {
    maelys_datalog_clause_origin_t rules[MAELYS_DATALOG_MAX_RULES];
    maelys_datalog_source_location_t eof;
} maelys_datalog_parse_origin_t;
struct maelys_datalog_program_builder {
    maelys_datalog_ruleset_t *ruleset;
    maelys_datalog_status_t error;
    maelys_datalog_parse_origin_t *parse_origin;
};
maelys_result_t maelys_datalog_validate_program(maelys_datalog_ruleset_t *, const char *,
                                                const maelys_datalog_parse_origin_t *,
                                                maelys_datalog_diagnostic_t *);
maelys_result_t maelys_datalog_parse_only(maelys_datalog_ruleset_t *, const char *, size_t,
                                         const char *, unsigned, maelys_datalog_parse_origin_t *,
                                         maelys_datalog_diagnostic_t *);
maelys_result_t maelys_datalog_compute_program_fingerprint(const maelys_datalog_ruleset_t *, char[65]);
maelys_result_t maelys_datalog_compile_frontend(const char *, const char *, const char *, size_t,
                                                const maelys_datalog_frontend_t *,
                                                maelys_datalog_ruleset_t *,
                                                maelys_datalog_public_diagnostic_t *);
void maelys_datalog_copy_load_diagnostic(maelys_datalog_public_diagnostic_t *,
                                         const maelys_datalog_diagnostic_t *);
void maelys_datalog_copy_solve_diagnostic(maelys_datalog_public_diagnostic_t *,
                                          const maelys_datalog_solve_diagnostic_t *);
maelys_datalog_status_t maelys_datalog_callback_status(maelys_datalog_status_t);
maelys_result_t maelys_datalog_export_fact(const maelys_datalog_ruleset_t *,
                                           const maelys_datalog_fact_t *,
                                           maelys_datalog_public_fact_t *);
#endif
