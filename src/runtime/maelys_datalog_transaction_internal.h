/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_DATALOG_TRANSACTION_INTERNAL_H
#define MAELYS_DATALOG_TRANSACTION_INTERNAL_H
#include "maelys/datalog.h"

/* Runtime-only publication boundary. A successful candidate leases its session
 * but is not committed and cannot be explained. The window either frees it
 * (abort), or installs it and calls commit once after its last fallible step.
 * These functions are not an application transaction API. */
maelys_datalog_status_t maelys_datalog_session_solve_candidate(
    maelys_datalog_session_t *, const maelys_datalog_fact_t *, size_t,
    maelys_datalog_result_t **, maelys_datalog_diagnostic_t *);
maelys_datalog_status_t maelys_datalog_session_solve_edb_candidate(
    maelys_datalog_session_t *, const maelys_datalog_input_edb_t *,
    maelys_datalog_result_t **, maelys_datalog_diagnostic_t *);
void maelys_datalog_result_commit(maelys_datalog_result_t *);
#endif
