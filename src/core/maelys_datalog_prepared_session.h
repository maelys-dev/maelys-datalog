#pragma once
#ifndef MAELYS_DATALOG_PREPARED_SESSION_H
#define MAELYS_DATALOG_PREPARED_SESSION_H

#include <stddef.h>

#include "common/maelys_errors.h"
#include "src/core/maelys_datalog_ruleset.h"
#include "src/core/maelys_datalog_solver.h"
#include "src/core/maelys_datalog_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct maelys_datalog_prepared_session maelys_datalog_prepared_session_t;

/* Prepared sessions are deliberately thread-confined. Distinct sessions are
 * independent, but callers must not invoke operations concurrently on the
 * same session. The implementation adds no shared mutable global state. */

/* A logical input term. SYMBOL values are text rather than symbol ids so a
 * prepared session can assign runtime ids canonically, independent of the
 * caller's insertion order. Strings are borrowed only for the duration of
 * maelys_datalog_prepared_session_solve_ex(). */
typedef struct {
    maelys_datalog_term_kind_t kind;
    union {
        const char *symbol;
        long long integer;
        int boolean;
    } as;
} maelys_datalog_input_term_t;

typedef struct {
    const char *predicate;
    size_t arity;
    maelys_datalog_input_term_t terms[MAELYS_DATALOG_MAX_TERMS];
} maelys_datalog_input_fact_t;

/* Prepare an isolated, immutable ruleset authority. The source ruleset may be
 * released or mutated after this function returns: the session owns its
 * snapshot. A lowercase 64-hex ruleset fingerprint is mandatory. */
maelys_result_t maelys_datalog_prepared_session_create(
    const maelys_datalog_ruleset_t *ruleset,
    maelys_datalog_prepared_session_t **out_session);

/* Destroy a session. Destruction is refused while a solve result produced by
 * this session remains alive. The ordinary maelys_datalog_solve_result_free()
 * function releases that lease; no special result destructor is required. */
maelys_result_t maelys_datalog_prepared_session_destroy(
    maelys_datalog_prepared_session_t *session);

/* Solve a complete EDB against the prepared ruleset. This first implementation
 * intentionally performs a full solve. It canonicalizes all open runtime
 * symbols lexically before materializing and sorting the EDB, making the result
 * independent of input order. Only one result may be alive per session. */
maelys_result_t maelys_datalog_prepared_session_solve(
    maelys_datalog_prepared_session_t *session,
    const maelys_datalog_input_fact_t *facts,
    size_t fact_count,
    maelys_datalog_solve_result_t **out_result);

maelys_result_t maelys_datalog_prepared_session_solve_ex(
    maelys_datalog_prepared_session_t *session,
    const maelys_datalog_input_fact_t *facts,
    size_t fact_count,
    maelys_datalog_solve_result_t **out_result,
    maelys_datalog_solve_diagnostic_t *out_diag);

/* Stable identity of the prepared authority. The returned pointer is owned by
 * the session and remains valid until successful destruction. */
const char *maelys_datalog_prepared_session_fingerprint(
    const maelys_datalog_prepared_session_t *session);

/* Read-only lookup in the symbol table of the current solved transaction.
 * Useful for constructing ground queries without mutating session state. */
maelys_result_t maelys_datalog_prepared_session_lookup_symbol(
    const maelys_datalog_prepared_session_t *session,
    const char *text,
    maelys_datalog_symbol_id_t *out_id,
    int *out_found);

#ifdef __cplusplus
}
#endif

#endif
