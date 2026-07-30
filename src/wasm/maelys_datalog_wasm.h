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
/* P4-C66 — Canonical Why-true text (MAELYS-DATALOG-WHY-TRUE-TEXT-v1) for a
 * ground IDB fact of the current solved WASM result.
 *
 * These two arity-explicit entry points mirror query_symbol/query_symbol2: the
 * public WASM query surface accepts symbolic terms of arity 1 or 2 only. They
 * are a thin composition of P4-C64 (structured witness) and P4-C65 (text), and
 * never inspect, reorder or reformat anything. Unlike query_symbol they return
 * the real maelys_result_t; errors are never collapsed into a -1 sentinel.
 *
 * out_required and out_found are mandatory and are zeroed before any fallible
 * validation. out_required excludes the terminating NUL, so a successful write
 * needs capacity >= out_required + 1.
 *
 *   - out_required/out_found NULL, capacity < 0, or an inconsistent
 *     buffer/capacity pair -> MAELYS_ERR_INVALID_ARGUMENT. count-only mode is
 *     exactly out_text == NULL with capacity == 0;
 *   - no solved result -> MAELYS_ERR_INVALID_STATE;
 *   - NULL/empty/overlong predicate or term -> the copy_bounded code;
 *   - unknown/non-QUERY predicate or wrong arity -> the shared validator's
 *     code, which always wins over an unknown symbolic term;
 *   - unknown symbolic term -> MAELYS_OK, found = 0, required = 0, no
 *     interning;
 *   - no explainable derived fact -> MAELYS_OK, found = 0, required = 0;
 *   - explainable fact -> MAELYS_OK, found = 1, required > 0, and the complete
 *     NUL-terminated text when capacity >= required + 1;
 *   - explainable fact with insufficient capacity ->
 *     MAELYS_ERR_PAYLOAD_TOO_LARGE, found = 1, exact required, and out_text[0]
 *     = '\0' as the only write (never a prefix presented as valid);
 *   - any other error -> scalars zeroed, empty text when a buffer exists, and
 *     the exact code propagated.
 *
 * The text is caller-owned: the boundary keeps no global text buffer, and the
 * bounded explanation is heap-allocated and freed on every path. */
maelys_result_t maelys_datalog_wasm_explain_symbol_fact_text(const char *predicate,
                                                             const char *arg0,
                                                             char *out_text,
                                                             int32_t capacity,
                                                             int32_t *out_required,
                                                             int32_t *out_found);
maelys_result_t maelys_datalog_wasm_explain_symbol2_fact_text(const char *predicate,
                                                              const char *arg0,
                                                              const char *arg1,
                                                              char *out_text,
                                                              int32_t capacity,
                                                              int32_t *out_required,
                                                              int32_t *out_found);
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
