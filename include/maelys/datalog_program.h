/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_DATALOG_PROGRAM_H
#define MAELYS_DATALOG_PROGRAM_H
#include "datalog.h"
#ifdef __cplusplus
extern "C" {
#endif

#define MAELYS_DATALOG_PROGRAM_ABI_VERSION 1u
#define MAELYS_DATALOG_IR_MAX_BODY 8u
#define MAELYS_DATALOG_IR_MAX_EXPRESSIONS 32u
#define MAELYS_DATALOG_IR_MAX_VARIABLES 32u
#define MAELYS_DATALOG_IR_NO_EXPRESSION UINT32_MAX

/* Required language features are computed by the core, never by a frontend. */
#define MAELYS_DATALOG_CAP_POSITIVE (UINT64_C(1) << 0)
#define MAELYS_DATALOG_CAP_NEGATION (UINT64_C(1) << 1)
#define MAELYS_DATALOG_CAP_COMPARISONS (UINT64_C(1) << 2)
#define MAELYS_DATALOG_CAP_ARITHMETIC (UINT64_C(1) << 3)
#define MAELYS_DATALOG_CAP_FILTERS (UINT64_C(1) << 4)
#define MAELYS_DATALOG_CAP_EXPLAIN_TRUE (UINT64_C(1) << 5)
#define MAELYS_DATALOG_CAP_WORK_LIMIT (UINT64_C(1) << 6)
#define MAELYS_DATALOG_CAP_LANGUAGE (UINT64_C(31))
#define MAELYS_DATALOG_CAP_ALL (UINT64_C(127))

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
    MAELYS_DATALOG_IR_FILTER = 4
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
typedef struct maelys_datalog_program_builder maelys_datalog_program_builder_t;
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
maelys_datalog_program_info(const maelys_datalog_program_t *, maelys_datalog_program_info_t *);
/* Typed, length-framed compiled identity: includes domain/schema, source
 * authority, normalized rules, filter semantics and query restrictions. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_program_fingerprint(
    const maelys_datalog_program_t *, char out[MAELYS_DATALOG_PUBLIC_FINGERPRINT_BYTES]);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_program_predicate(
    const maelys_datalog_program_t *, size_t, maelys_datalog_public_predicate_t *);
MAELYS_DATALOG_API maelys_datalog_status_t
maelys_datalog_program_fact(const maelys_datalog_program_t *, size_t, maelys_datalog_ir_atom_t *);
MAELYS_DATALOG_API maelys_datalog_status_t
maelys_datalog_program_rule(const maelys_datalog_program_t *, size_t, maelys_datalog_ir_rule_t *);

/* These are the only mutation operations given to a frontend. Inputs are
 * copied synchronously. The builder is callback-scoped; an error is sticky:
 * ignoring it cannot make a partially constructed program load successfully.
 * The domain is host-selected and cannot be extended by the frontend. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_program_add_fact(
    maelys_datalog_program_builder_t *, const maelys_datalog_ir_atom_t *);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_program_add_rule(
    maelys_datalog_program_builder_t *, const maelys_datalog_ir_rule_t *);

typedef struct {
    uint32_t abi_version;
    size_t struct_size;
    const char *name;
    const char *semantic_id;
    maelys_datalog_status_t (*lower)(const char *, size_t, maelys_datalog_program_builder_t *,
                                     maelys_datalog_public_diagnostic_t *);
} maelys_datalog_frontend_t;

/* Explicit per-load selection, not a mutable global grammar. Frontends are
 * trusted native code. They must not retain source/builder arguments or reenter
 * loading. Successful lowering is ALWAYS followed by core validation. */
MAELYS_DATALOG_API const maelys_datalog_frontend_t *maelys_datalog_frontend_datalog(void);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_policy_load_frontend(
    const char *domain, const char *policy_id, const char *source, size_t source_length,
    const maelys_datalog_frontend_t *, maelys_datalog_policy_t **,
    maelys_datalog_public_diagnostic_t *);

#ifdef __cplusplus
}
#endif
#endif
