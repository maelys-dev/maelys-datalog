#pragma once
#ifndef MAELYS_DATALOG_WASM_H
#define MAELYS_DATALOG_WASM_H

#include <stdint.h>
#include <stddef.h>

#include "common/maelys_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

maelys_result_t maelys_datalog_wasm_domain_begin(const char *name);
maelys_result_t maelys_datalog_wasm_domain_add_predicate(const char *name,
                                                         unsigned arity,
                                                         unsigned kind_flags);
maelys_result_t maelys_datalog_wasm_domain_commit(void);
maelys_result_t maelys_datalog_wasm_domain_abort(void);

maelys_result_t maelys_datalog_wasm_edb_begin(void);
maelys_result_t maelys_datalog_wasm_edb_intern_runtime_symbol(const char *text,
                                                              int32_t *out_id);
maelys_result_t maelys_datalog_wasm_edb_add_symbol_id_fact(const char *predicate,
                                                           int32_t symbol_id_from_js);
maelys_result_t maelys_datalog_wasm_edb_add_symbol_ids_fact(const char *predicate,
                                                            int32_t left_from_js,
                                                            int32_t right_from_js);
maelys_result_t maelys_datalog_wasm_edb_add_symbol_id_facts(const char *predicate,
                                                            const int32_t *ids,
                                                            int32_t count);
maelys_result_t maelys_datalog_wasm_edb_add_symbol_ids_facts(const char *predicate,
                                                             const int32_t *pairs,
                                                             int32_t pair_count);
maelys_result_t maelys_datalog_wasm_edb_add_runtime_symbol_facts(const char *predicate,
                                                                 const char *packed,
                                                                 int32_t byte_len,
                                                                 int32_t value_count);
maelys_result_t maelys_datalog_wasm_edb_add_runtime_symbol_pair_facts(const char *predicate,
                                                                      const char *packed,
                                                                      int32_t byte_len,
                                                                      int32_t pair_count);
maelys_result_t maelys_datalog_wasm_edb_add_symbol(const char *pred,
                                                   const char *arg0);
maelys_result_t maelys_datalog_wasm_edb_add_symbol2(const char *pred,
                                                    const char *arg0,
                                                    const char *arg1);
maelys_result_t maelys_datalog_wasm_solve(void);
int maelys_datalog_wasm_query_symbol(const char *pred, const char *arg0);
int maelys_datalog_wasm_query_symbol2(const char *pred,
                                      const char *arg0,
                                      const char *arg1);
void maelys_datalog_wasm_solve_result_free(void);

maelys_result_t maelys_datalog_wasm_load_ruleset(const char *domain_name,
                                                const char *policy_id,
                                                const char *src,
                                                size_t src_len);
uintptr_t maelys_datalog_wasm_ruleset_ptr(void);
const char *maelys_datalog_wasm_last_diag_message(void);
int maelys_datalog_wasm_last_diag_code(void);

#ifdef __cplusplus
}
#endif

#endif
