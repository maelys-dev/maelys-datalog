/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_DATALOG_WASM_H
#define MAELYS_DATALOG_WASM_H
#include <maelys/datalog.h>
#include <stdint.h>

/* Binding transport 1, independent of the engine's consumer/extension ABIs.
 * Frames are little-endian uint32 words. A fact is 15 words: predicate text
 * offset/byte length, arity, then four (kind, low, high) term triples. Symbols
 * use offset/length, integers use signed int64 bits, booleans use 0/1 and 0.
 * Text spans include a trailing NUL outside their declared length. They are
 * borrowed only during a call. Buffers must be aligned, live, and disjoint.
 * Domain declarations use four words per predicate (offset,length,arity,flags)
 * followed by two per atom (offset,length). No native layout crosses into JS.
 * One wrapper owns one module; open twice and every operation after close fail.
 */
#define MAELYS_WASM_TRANSPORT_VERSION 1u
#define MAELYS_WASM_FACT_WORDS 15u
uint32_t maelys_datalog_wasm_transport_version(void);
int maelys_datalog_wasm_open(void);
int maelys_datalog_wasm_close(void);
int maelys_datalog_wasm_limit(uint32_t id, uint32_t *out);
int maelys_datalog_wasm_register_domain(const char *name, const uint32_t *words,
    uint32_t word_count, uint32_t predicates, uint32_t atoms,
    const char *text, uint32_t text_bytes);
int maelys_datalog_wasm_load_policy(const char *domain, const char *id,
    const char *source, uint32_t source_bytes);
int maelys_datalog_wasm_clear_facts(void);
int maelys_datalog_wasm_add_facts(const uint32_t *words, uint32_t word_count,
    uint32_t count, const char *text, uint32_t text_bytes);
int maelys_datalog_wasm_input_usage(uint32_t *out_three);
int maelys_datalog_wasm_solve(void);
int maelys_datalog_wasm_free_result(void);
int maelys_datalog_wasm_query(const uint32_t *words, uint32_t word_count,
    const char *text, uint32_t text_bytes, uint32_t *present);
int maelys_datalog_wasm_enumerate(const char *predicate, uint32_t arity,
    uint32_t *out_terms, uint32_t capacity, uint32_t *count);
const char *maelys_datalog_wasm_symbol_text(uint32_t id);
int maelys_datalog_wasm_explain(uint32_t kind, const uint32_t *words,
    uint32_t word_count, const char *text, uint32_t text_bytes,
    char *out_text, uint32_t capacity, uint32_t *required);
int maelys_datalog_wasm_derived_count(uint32_t *out);
int maelys_datalog_wasm_fingerprint(uint32_t kind, char *out, uint32_t capacity);
/* Diagnostics are read-only scalar/string copies, never a struct mirror.
 * scalar 0..23: status, source, code, present low/high, line,column,arity,
 * observed,limit,depth,depth_limit,rule,comparison_result,expected_kind,
 * lhs_kind,rhs_kind,comparison_op,limit_kind,term_index,expected_arity,
 * observed_arity,diagnostic ABI,consumer API.
 * text 0..9: phase,message,hint,file,predicate,token,field,domain,code name,
 * status name. Strings are valid until the next operation (copy immediately).
 */
uint32_t maelys_datalog_wasm_diagnostic_scalar(uint32_t field);
const char *maelys_datalog_wasm_diagnostic_text(uint32_t field);
#endif
