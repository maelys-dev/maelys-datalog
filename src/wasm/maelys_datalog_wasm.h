#pragma once
#ifndef MAELYS_DATALOG_WASM_H
#define MAELYS_DATALOG_WASM_H

#include <stdint.h>
#include <stddef.h>

#include "common/maelys_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

void maelys_datalog_wasm_get_build_limits(uint32_t *out);

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
/* Enumerates already-derived IDB facts for a QUERY-authorized predicate.
 * Returns the total fact count (including facts beyond capacity), or -1 on
 * invalid state/arguments/accessor failure. out_terms uses three int32 words
 * per term: kind, value_lo, value_hi. capacity == 0 is count-only mode. */
int32_t maelys_datalog_wasm_enumerate_predicate_facts(const char *predicate,
                                                       int32_t arity,
                                                       int32_t *out_terms,
                                                       int32_t capacity);
/* Resolves an id through the current mono-policy symbol table without
 * interning. Returns NULL for an invalid id or absent table. A non-NULL
 * pointer may designate a valid zero-length symbol; callers must distinguish
 * the pointer value before decoding the UTF-8 text. */
const char *maelys_datalog_wasm_symbol_text_by_id(int32_t symbol_id);
/* Returns the derived fact count for the current solved WASM result.
 * Non-negative values are valid counts, including 0. -1 is the sentinel for
 * "no solved result available" or an underlying C accessor failure; it is not
 * the same as zero derived facts. This mirrors the existing query_symbol
 * implicit-state API, except the success range is a count rather than a
 * boolean presence flag. */
int32_t maelys_datalog_wasm_derived_fact_count(void);
void maelys_datalog_wasm_solve_result_free(void);

maelys_result_t maelys_datalog_wasm_load_ruleset(const char *domain_name,
                                                const char *policy_id,
                                                const char *src,
                                                size_t src_len);
uintptr_t maelys_datalog_wasm_ruleset_ptr(void);
const char *maelys_datalog_wasm_last_diag_message(void);
const char *maelys_datalog_wasm_last_diag_hint(void);
int maelys_datalog_wasm_last_diag_code(void);

#ifdef __cplusplus
}
#endif

#endif
