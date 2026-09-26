/* SPDX-License-Identifier: MPL-2.0 */
/* Same archived source, diagnostic-only accessor appended in this TU. */
#include "src/runtime/maelys_datalog_runtime.c"
#include "session_layout.h"
void maelys_bench_solver_layout(FILE *, const maelys_datalog_internal_solve_result_t *);
void maelys_bench_session_layout(FILE *out, const maelys_datalog_session_t *session) {
    const maelys_datalog_internal_prepared_session_t *inputs = session->inputs;
#ifdef MAELYS_BENCH_SPLIT_STATE
    const maelys_datalog_internal_ruleset_t *program = inputs->prepared;
    const maelys_datalog_symbol_table_t *symbols = &inputs->symbols;
#else
    const maelys_datalog_internal_ruleset_t *program = &inputs->working;
    const maelys_datalog_symbol_table_t *symbols = &inputs->working.symbols;
#endif
    fprintf(out, "key,value\n");
    L_TYPE("session", maelys_datalog_session_t);
    L_ADDRESS("session", session);
    L_FIELD("session", maelys_datalog_session_t, session, inputs);
    L_FIELD("session", maelys_datalog_session_t, session, result_storage);
    L_FIELD("session", maelys_datalog_session_t, session, solve_scratch);
    L_TYPE("inputs", maelys_datalog_internal_prepared_session_t);
    L_ADDRESS("inputs", inputs);
    L_FIELD("inputs", maelys_datalog_internal_prepared_session_t, inputs, prepared);
#ifdef MAELYS_BENCH_SPLIT_STATE
    L_FIELD("inputs", maelys_datalog_internal_prepared_session_t, inputs, symbols);
#else
    L_FIELD("inputs", maelys_datalog_internal_prepared_session_t, inputs, working);
#endif
    L_FIELD("inputs", maelys_datalog_internal_prepared_session_t, inputs, fact_pool);
    L_FIELD("inputs", maelys_datalog_internal_prepared_session_t, inputs, edb);
    L_FIELD("inputs", maelys_datalog_internal_prepared_session_t, inputs, fact_index);
    L_FIELD("inputs", maelys_datalog_internal_prepared_session_t, inputs, result_workspace);
    L_TYPE("program", maelys_datalog_internal_ruleset_t);
    L_ADDRESS("program", program);
    L_FIELD("program", maelys_datalog_internal_ruleset_t, program, symbols);
    L_FIELD("program", maelys_datalog_internal_ruleset_t, program, registry);
    L_FIELD("program", maelys_datalog_internal_ruleset_t, program, facts);
    L_FIELD("program", maelys_datalog_internal_ruleset_t, program, rules);
    L_TYPE("symbols", maelys_datalog_symbol_table_t);
    L_ADDRESS("symbols", symbols);
    L_FIELD("symbols", maelys_datalog_symbol_table_t, symbols, storage);
    L_FIELD("symbols", maelys_datalog_symbol_table_t, symbols, entries);
    L_FIELD("symbols", maelys_datalog_symbol_table_t, symbols, index);
    L_VALUE("stride.symbol_entry", sizeof(symbols->entries[0]));
    L_TYPE("fact", maelys_datalog_internal_fact_t);
    L_TYPE("rule", maelys_datalog_rule_t);
    maelys_bench_solver_layout(out, inputs->result_workspace);
}
