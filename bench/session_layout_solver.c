/* SPDX-License-Identifier: MPL-2.0 */
#include "src/core/maelys_datalog_solver.c"
#include "session_layout.h"
void maelys_bench_solver_layout(FILE *out, const maelys_datalog_internal_solve_result_t *result) {
    L_TYPE("result", maelys_datalog_internal_solve_result_t);
    L_ADDRESS("result", result);
    L_FIELD("result", maelys_datalog_internal_solve_result_t, result, ruleset);
#ifdef MAELYS_BENCH_SPLIT_STATE
    L_FIELD("result", maelys_datalog_internal_solve_result_t, result, symbols);
    L_ADDRESS("resolved_symbols", result->symbols);
#else
    L_ADDRESS("resolved_symbols", &result->ruleset->symbols);
#endif
    L_ADDRESS("resolved_program", result->ruleset);
    L_FIELD("result", maelys_datalog_internal_solve_result_t, result, facts_per_pred);
    L_FIELD("result", maelys_datalog_internal_solve_result_t, result, stratum_idb_end);
#ifdef MAELYS_BENCH_BORROWED_EDB
    L_FIELD("result", maelys_datalog_internal_solve_result_t, result, borrows_edb);
    /* The optional tail is absent for this prepared-session fixture. */
    L_VALUE("offsetof.result.edb_facts", offsetof(maelys_datalog_internal_solve_result_t, edb_facts));
    L_VALUE("sizeof.result.edb_facts", 0);
    L_ADDRESS("resolved_edb", result->edb_snapshot.facts);
#else
    L_FIELD("result", maelys_datalog_internal_solve_result_t, result, edb_facts);
    L_ADDRESS("resolved_edb", result->edb_facts);
#endif
    L_FIELD("result", maelys_datalog_internal_solve_result_t, result, idb_facts);
    L_FIELD("result", maelys_datalog_internal_solve_result_t, result, idb_proof_index);
    L_FIELD("result", maelys_datalog_internal_solve_result_t, result, edb_ranges);
    L_FIELD("result", maelys_datalog_internal_solve_result_t, result, proof);
    L_FIELD("result", maelys_datalog_internal_solve_result_t, result, premise_pool);
    L_FIELD("result", maelys_datalog_internal_solve_result_t, result, node_premise_begin);
    L_FIELD("result", maelys_datalog_internal_solve_result_t, result, node_premise_count);
    L_FIELD("result", maelys_datalog_internal_solve_result_t, result, node_has_premises);
    L_FIELD("result", maelys_datalog_internal_solve_result_t, result, witness_slots);
}
