/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_DATALOG_PROGRAM_H
#define MAELYS_DATALOG_PROGRAM_H
#include "datalog.h"
#ifdef __cplusplus
extern "C" {
#endif

#define MAELYS_DATALOG_PROGRAM_ABI_VERSION 2u
#define MAELYS_DATALOG_IR_MAX_BODY 8u
#define MAELYS_DATALOG_IR_MAX_EXPRESSIONS 32u
#define MAELYS_DATALOG_IR_MAX_VARIABLES 32u
#define MAELYS_DATALOG_IR_NO_EXPRESSION UINT32_MAX

/* Capability constants live in datalog.h, also for ordinary consumers.
 * Required language features are computed by the core, never by a frontend. */

typedef enum {
    MAELYS_DATALOG_IR_SYMBOL = 1,
    MAELYS_DATALOG_IR_INTEGER = 2,
    MAELYS_DATALOG_IR_BOOLEAN = 3,
    MAELYS_DATALOG_IR_VARIABLE = 4
} maelys_datalog_ir_term_kind_t;
typedef struct {
    maelys_datalog_ir_term_kind_t kind;
    union {
        const char *symbol;
        int64_t integer;
        int boolean;
        uint32_t variable;
    } as;
} maelys_datalog_ir_term_t;
typedef struct {
    const char *predicate;
    size_t arity;
    maelys_datalog_ir_term_t terms[MAELYS_DATALOG_PUBLIC_MAX_TERMS];
} maelys_datalog_ir_atom_t;
typedef enum {
    MAELYS_DATALOG_IR_ATOM = 1,
    MAELYS_DATALOG_IR_COMPARISON = 2,
    MAELYS_DATALOG_IR_NEGATION = 3,
    MAELYS_DATALOG_IR_FILTER = 4,
    /* atom = source pattern, lhs = local projection variable, rhs = output
     * variable, both in 0..25. Other source variables in 0..25 are group keys
     * bound by ordinary positive atoms; 26..31 are local existentials. Neither
     * projection nor local existentials may escape into the surrounding rule.
     * Other fields inactive. Requires CAP_AGGREGATES. */
    MAELYS_DATALOG_IR_COUNT = 5,
    /* Same active fields and scoping as COUNT; integer projection only.
     * MIN/MAX fail on an empty group. SUM returns zero and sums distinct
     * complete source facts (not distinct projected values). Requires the
     * corresponding CAP_MIN, CAP_MAX or CAP_SUM, independently of COUNT. */
    MAELYS_DATALOG_IR_MIN = 6,
    MAELYS_DATALOG_IR_MAX = 7,
    MAELYS_DATALOG_IR_SUM = 8
} maelys_datalog_ir_literal_kind_t;
typedef enum {
    MAELYS_DATALOG_IR_EQ = 1,
    MAELYS_DATALOG_IR_NE = 2,
    MAELYS_DATALOG_IR_LT = 3,
    MAELYS_DATALOG_IR_LE = 4,
    MAELYS_DATALOG_IR_GT = 5,
    MAELYS_DATALOG_IR_GE = 6
} maelys_datalog_ir_comparison_t;
typedef enum {
    MAELYS_DATALOG_IR_EXPR_INTEGER = 1,
    MAELYS_DATALOG_IR_EXPR_VARIABLE = 2,
    MAELYS_DATALOG_IR_EXPR_ADD = 3,
    MAELYS_DATALOG_IR_EXPR_SUB = 4,
    MAELYS_DATALOG_IR_EXPR_MUL = 5
} maelys_datalog_ir_expression_kind_t;
typedef struct {
    maelys_datalog_ir_expression_kind_t kind;
    /* Binary operands refer to earlier nodes. Each root's expanded tree must
     * also fit MAX_EXPRESSIONS, bounding recursive evaluation of shared edges. */
    uint32_t left, right;
    maelys_datalog_ir_term_t term;
} maelys_datalog_ir_expression_t;
typedef struct {
    maelys_datalog_ir_literal_kind_t kind;
    maelys_datalog_ir_atom_t atom;
    maelys_datalog_ir_term_t lhs, rhs;
    maelys_datalog_ir_comparison_t comparison;
    int has_arithmetic;
    uint32_t lhs_expression, rhs_expression;
    const char *filter_name;
    const char *filter_semantic_id; /* Optional on input; checked if present. */
    const unsigned char *pattern;
    size_t pattern_length;
    maelys_datalog_ir_term_t filter_value;
} maelys_datalog_ir_literal_t;
typedef struct {
    size_t line, column; /* 1-based, or both zero when unavailable. */
} maelys_datalog_source_location_t;
typedef struct {
    maelys_datalog_ir_atom_t head;
    size_t body_count;
    maelys_datalog_ir_literal_t body[MAELYS_DATALOG_IR_MAX_BODY];
    size_t expression_count;
    maelys_datalog_ir_expression_t expressions[MAELYS_DATALOG_IR_MAX_EXPRESSIONS];
    maelys_datalog_source_location_t source;
} maelys_datalog_ir_rule_t;

typedef struct maelys_datalog_program maelys_datalog_program_t;
typedef struct {
    uint32_t abi_version;
    const char *policy_id;
    const char *domain;
    const char *fingerprint;
    size_t predicate_count, fact_count, rule_count;
    size_t max_input_facts, max_derived_facts, max_facts_per_predicate;
    uint64_t required_capabilities;
} maelys_datalog_program_info_t;

/* Views are read-only and borrowed for the lifetime of the program. Backend
 * callbacks may keep them until destroy(). No private layout/serialization ABI
 * is exposed. Rule accessors use zero-based indices; explanation rule IDs are
 * index + 1, preserving the existing normalized-program convention. */
MAELYS_DATALOG_API maelys_datalog_status_t
maelys_datalog_program_info(const maelys_datalog_program_t *program, maelys_datalog_program_info_t *out_info);
/* Typed, length-framed compiled identity: includes domain/schema, source
 * authority, normalized rules, filter semantics and query restrictions. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_program_fingerprint(
    const maelys_datalog_program_t *program, char out_fingerprint[MAELYS_DATALOG_PUBLIC_FINGERPRINT_BYTES]);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_program_predicate(
    const maelys_datalog_program_t *program, size_t index, maelys_datalog_predicate_t *out_predicate);
/* Effective query surface: QUERY predicates admitted by the manifest whitelist
 * when one is enforced. An absent or empty manifest queries list admits none;
 * without whitelist enforcement all QUERY predicates are admitted. This is
 * query authorization, independent of whether any matching facts are derived.
 * Enumeration follows program_predicate order, omitting inadmissible entries;
 * each name/arity pair occurs once. The descriptor retains its original flags
 * and its name is borrowed for the program lifetime. These calls allocate no
 * memory and leave outputs unchanged on failure. NULL arguments return
 * INVALID_ARGUMENT; a zero-based query index >= count returns NOT_FOUND. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_program_query_count(
    const maelys_datalog_program_t *program, size_t *out_count);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_program_query(
    const maelys_datalog_program_t *program, size_t index, maelys_datalog_predicate_t *out_predicate);
MAELYS_DATALOG_API maelys_datalog_status_t
maelys_datalog_program_fact(const maelys_datalog_program_t *program, size_t index, maelys_datalog_ir_atom_t *out_atom);
MAELYS_DATALOG_API maelys_datalog_status_t
maelys_datalog_program_rule(const maelys_datalog_program_t *program, size_t index, maelys_datalog_ir_rule_t *out_rule);


MAELYS_DATALOG_API maelys_datalog_status_t
maelys_datalog_session_program(const maelys_datalog_session_t *session, const maelys_datalog_program_t **out_program);
#ifdef __cplusplus
}
#endif
#endif
