#pragma once
#ifndef MAELYS_DATALOG_TYPES_H
#define MAELYS_DATALOG_TYPES_H

#include <stdbool.h>
#include "maelys/datalog.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAELYS_DATALOG_PROFILE_NAME "MAELYS-DATALOG-v2"
#define MAELYS_DATALOG_SHA256_UNSET "unset"
#define MAELYS_DATALOG_MAX_STRING_BYTES 1024u
#define MAELYS_DATALOG_MAX_TOKEN_BYTES 1024u
#define MAELYS_DATALOG_MAX_SYMBOLS 512u
#define MAELYS_DATALOG_SYMBOL_INDEX_BUCKETS 1024u
#define MAELYS_DATALOG_STRING_POOL_BYTES 32768u
#define MAELYS_DATALOG_MAX_PREDICATES 128u
#define MAELYS_DATALOG_MAX_ATOMS 256u
#define MAELYS_DATALOG_MAX_ARITY 4u
#define MAELYS_DATALOG_MAX_TERMS 4u
#define MAELYS_DATALOG_MAX_RULES 128u
#define MAELYS_DATALOG_MAX_RULE_FACTS 128u
#define MAELYS_DATALOG_MAX_BODY_LITERALS 8u
#define MAELYS_DATALOG_MAX_STRATA 4u
#define MAELYS_DATALOG_NAMED_VARIABLE_COUNT 26u
#define MAELYS_DATALOG_MAX_RULE_VARIABLES 32u
#if !defined(MAELYS_DATALOG_PROFILE_SMALL) && !defined(MAELYS_DATALOG_PROFILE_LARGE)
#  define MAELYS_DATALOG_PROFILE_SMALL 1
#endif
#if defined(MAELYS_DATALOG_PROFILE_SMALL) && defined(MAELYS_DATALOG_PROFILE_LARGE)
#  error "Define at most one of MAELYS_DATALOG_PROFILE_SMALL / _LARGE"
#endif

#if defined(MAELYS_DATALOG_PROFILE_LARGE)
#  define MAELYS_DATALOG_SIZE_PROFILE_NAME "LARGE"
#  define MAELYS_DATALOG_MAX_EDB_FACTS 2048u
#  define MAELYS_DATALOG_MAX_IDB_FACTS 2048u
#  define MAELYS_DATALOG_MAX_FACTS_PER_PRED 256u
#  define MAELYS_DATALOG_MAX_BATCH_SCRATCH_BYTES (16u * 1024u)
#else
#  define MAELYS_DATALOG_SIZE_PROFILE_NAME "SMALL"
#  define MAELYS_DATALOG_MAX_EDB_FACTS 1024u
#  define MAELYS_DATALOG_MAX_IDB_FACTS 1024u
#  define MAELYS_DATALOG_MAX_FACTS_PER_PRED 64u
#  define MAELYS_DATALOG_MAX_BATCH_SCRATCH_BYTES (8u * 1024u)
#endif
#define MAELYS_DATALOG_MAX_QUERY_WHITELIST 64u
#define MAELYS_DATALOG_MAX_DEPTH 10u
#define MAELYS_DATALOG_MAX_PROOF_NODES 64u
#define MAELYS_DATALOG_MAX_PROOF_DEPTH 10u
#define MAELYS_DATALOG_MAX_ARITH_EXPR_NODES 32u
#define MAELYS_DATALOG_MAX_ARITH_EXPR_DEPTH 8u
#define MAELYS_DATALOG_MAX_FILTER_PROGRAMS 128u
#define MAELYS_DATALOG_FILTER_PATTERN_POOL_BYTES 8192u
#define MAELYS_DATALOG_MAX_FILTER_PATTERN_BYTES 256u
#define MAELYS_DATALOG_MAX_FILTER_EVALUATIONS 8192u
#define MAELYS_DATALOG_MAX_FILTER_COST_UNITS 1048576u
#define MAELYS_DATALOG_MAX_WHY_FALSE_FILTER_COST_UNITS 1048576u
#define MAELYS_DATALOG_MAX_INT 2147483647LL
#define MAELYS_DATALOG_PROOF_NO_PARENT UINT16_MAX

_Static_assert(MAELYS_DATALOG_MAX_STRATA <= 8u,
               "stratum boundary array needs widening above 8");
_Static_assert(MAELYS_DATALOG_MAX_PREDICATES <= 128u,
               "strata array indexed by predicate_id");
_Static_assert(MAELYS_DATALOG_MAX_STRATA <= MAELYS_DATALOG_MAX_PREDICATES,
               "strata count cannot exceed predicate id capacity");
_Static_assert(MAELYS_DATALOG_MAX_FACTS_PER_PRED <= MAELYS_DATALOG_MAX_EDB_FACTS,
               "per-predicate fact cap cannot exceed total EDB capacity");
_Static_assert(MAELYS_DATALOG_MAX_EDB_FACTS <= UINT16_MAX,
               "EDB fact capacity must fit dense range counters");
_Static_assert(MAELYS_DATALOG_MAX_IDB_FACTS <= UINT16_MAX,
               "IDB fact capacity must fit solver counters");
_Static_assert(MAELYS_DATALOG_MAX_FACTS_PER_PRED <= UINT16_MAX,
               "per-predicate fact capacity must fit predicate counters");

typedef uint32_t maelys_datalog_symbol_id_t;
typedef uint16_t maelys_datalog_predicate_id_t;

#define MAELYS_DATALOG_SYMBOL_ID_INVALID ((maelys_datalog_symbol_id_t)0u)

typedef enum {
    MAELYS_DATALOG_TERM_SYMBOL = 1,
    MAELYS_DATALOG_TERM_INT = 2,
    MAELYS_DATALOG_TERM_BOOL = 3,
    MAELYS_DATALOG_TERM_VAR = 4
} maelys_datalog_internal_term_kind_t;

typedef struct {
    maelys_datalog_internal_term_kind_t kind;
    union {
        maelys_datalog_symbol_id_t symbol;
        long long integer;
        int boolean;
        unsigned variable;
    } as;
} maelys_datalog_internal_term_t;

/* Compiled atoms keep their historical layout. Session facts carry separate
 * tags and aligned payloads; no packed types or unaligned integer loads. */
typedef struct {
    maelys_datalog_predicate_id_t predicate_id;
    uint8_t arity;
    maelys_datalog_internal_term_t terms[MAELYS_DATALOG_MAX_TERMS];
} maelys_datalog_internal_atom_t;

typedef union {
    int64_t integer; /* First member: a zero initializer covers all eight bytes. */
    maelys_datalog_symbol_id_t symbol;
    int boolean;
    unsigned variable;
} maelys_datalog_fact_payload_t;

typedef struct {
    maelys_datalog_predicate_id_t predicate_id;
    uint8_t arity;
    uint8_t kind[MAELYS_DATALOG_MAX_TERMS];
    uint8_t reserved;
    maelys_datalog_fact_payload_t payload[MAELYS_DATALOG_MAX_TERMS];
} maelys_datalog_internal_fact_t;

_Static_assert(sizeof(maelys_datalog_internal_fact_t) == 40u,
               "four-term compact fact must occupy 40 bytes");
_Static_assert(offsetof(maelys_datalog_internal_fact_t, payload) == 8u,
               "fact payload must start at the aligned eight-byte header");
_Static_assert(_Alignof(maelys_datalog_internal_fact_t) >= _Alignof(int64_t),
               "fact integer payload must be naturally aligned");
_Static_assert(sizeof(maelys_datalog_internal_atom_t) == 72u,
               "compiled atom layout must remain unchanged");

/* Single-field reads do not materialize a padded temporary term. Each argument
 * is evaluated once; these accessors are single loads in unoptimized builds
 * too. Full expansion stays explicit at historical-term record boundaries. */
#define maelys_datalog_fact_kind(fact, index) \
    ((maelys_datalog_internal_term_kind_t)((fact)->kind[(index)]))
#define maelys_datalog_fact_symbol(fact, index) ((fact)->payload[(index)].symbol)
#define maelys_datalog_fact_integer(fact, index) ((fact)->payload[(index)].integer)
#define maelys_datalog_fact_boolean(fact, index) ((fact)->payload[(index)].boolean)
#define maelys_datalog_fact_variable(fact, index) ((fact)->payload[(index)].variable)

static inline maelys_datalog_internal_term_t maelys_datalog_fact_term(
    const maelys_datalog_internal_fact_t *fact, size_t index) {
    maelys_datalog_internal_term_t term; /* Both defined fields are assigned below. */
    term.kind = (maelys_datalog_internal_term_kind_t)fact->kind[index];
    /* Copy the aligned representation once, without a kind-dependent decode.
     * Writes canonicalize the inactive bytes of narrow union members. */
    _Static_assert(sizeof(term.as) == sizeof(fact->payload[index]),
                   "fact and temporary term payload widths must match");
    memcpy(&term.as, &fact->payload[index], sizeof(term.as));
    return term;
}
/* Byte-canonical expansion for diagnostic records compared as whole images.
 * Returning a struct by value does not promise to preserve its padding. */
static inline void maelys_datalog_fact_copy_term(
    maelys_datalog_internal_term_t *out,
    const maelys_datalog_internal_fact_t *fact, size_t index) {
    memset(out, 0, sizeof(*out));
    out->kind = (maelys_datalog_internal_term_kind_t)fact->kind[index];
    memcpy(&out->as, &fact->payload[index], sizeof(out->as));
}
static inline void maelys_datalog_fact_set_term(maelys_datalog_internal_fact_t *fact,
    size_t index, maelys_datalog_internal_term_t term) {
    fact->kind[index] = (uint8_t)term.kind;
    fact->payload[index].integer = 0;
    switch (term.kind) {
    case MAELYS_DATALOG_TERM_SYMBOL: fact->payload[index].symbol = term.as.symbol; break;
    case MAELYS_DATALOG_TERM_INT: fact->payload[index].integer = term.as.integer; break;
    case MAELYS_DATALOG_TERM_BOOL: fact->payload[index].boolean = term.as.boolean; break;
    case MAELYS_DATALOG_TERM_VAR: fact->payload[index].variable = term.as.variable; break;
    default: break;
    }
}
static inline void maelys_datalog_fact_set_kind(maelys_datalog_internal_fact_t *fact,
    size_t index, unsigned kind) {
    fact->kind[index] = (uint8_t)kind;
    if (!kind) fact->payload[index].integer = 0;
}
static inline void maelys_datalog_fact_set_symbol(maelys_datalog_internal_fact_t *fact,
    size_t index, maelys_datalog_symbol_id_t value) {
    fact->payload[index].integer = 0;
    fact->payload[index].symbol = value;
}
static inline void maelys_datalog_fact_set_integer(maelys_datalog_internal_fact_t *fact,
    size_t index, int64_t value) {
    fact->payload[index].integer = 0;
    fact->payload[index].integer = value;
}
static inline void maelys_datalog_fact_set_boolean(maelys_datalog_internal_fact_t *fact,
    size_t index, int value) {
    fact->payload[index].integer = 0;
    fact->payload[index].boolean = value;
}
static inline void maelys_datalog_fact_set_variable(maelys_datalog_internal_fact_t *fact,
    size_t index, unsigned value) {
    fact->payload[index].integer = 0;
    fact->payload[index].variable = value;
}
/* A temporary term is for immediate read-only calls only, never retained. */
#define MAELYS_DATALOG_FACT_TERM_REF(fact, index) \
    ((const maelys_datalog_internal_term_t[]){maelys_datalog_fact_term((fact), (index))})

static inline maelys_datalog_internal_fact_t maelys_datalog_atom_fact(
    const maelys_datalog_internal_atom_t *atom) {
    maelys_datalog_internal_fact_t fact = {0};
    fact.predicate_id = atom->predicate_id;
    fact.arity = atom->arity;
    for (size_t i = 0; i < atom->arity && i < MAELYS_DATALOG_MAX_TERMS; ++i)
        maelys_datalog_fact_set_term(&fact, i, atom->terms[i]);
    return fact;
}

static inline maelys_datalog_internal_atom_t maelys_datalog_fact_atom(
    const maelys_datalog_internal_fact_t *fact) {
    maelys_datalog_internal_atom_t atom = {0};
    atom.predicate_id = fact->predicate_id;
    atom.arity = fact->arity;
    for (size_t i = 0; i < fact->arity && i < MAELYS_DATALOG_MAX_TERMS; ++i)
        atom.terms[i] = maelys_datalog_fact_term(fact, i);
    return atom;
}
#define MAELYS_DATALOG_ATOM_FACT_REF(atom) \
    ((const maelys_datalog_internal_fact_t[]){maelys_datalog_atom_fact((atom))})
#define MAELYS_DATALOG_FACT_ATOM_REF(fact) \
    ((const maelys_datalog_internal_atom_t[]){maelys_datalog_fact_atom((fact))})

typedef struct {
    maelys_datalog_internal_fact_t *facts;
    size_t count;
    size_t capacity;
    int sorted;
} maelys_datalog_fact_set_t;

typedef enum {
    MAELYS_DATALOG_PRED_KIND_EDB = 1u << 0,
    MAELYS_DATALOG_PRED_KIND_IDB = 1u << 1,
    MAELYS_DATALOG_PRED_KIND_QUERY = 1u << 2,
    MAELYS_DATALOG_PRED_KIND_POLICY_FACT = 1u << 3
} maelys_datalog_predicate_kind_t;

typedef enum {
    MAELYS_DATALOG_LITERAL_ATOM = 1,
    MAELYS_DATALOG_LITERAL_COMPARISON = 2,
    MAELYS_DATALOG_LITERAL_NEGATED_ATOM = 3,
    MAELYS_DATALOG_LITERAL_FILTER = 4,
    MAELYS_DATALOG_LITERAL_COUNT = 5,
    MAELYS_DATALOG_LITERAL_MIN = 6,
    MAELYS_DATALOG_LITERAL_MAX = 7,
    MAELYS_DATALOG_LITERAL_SUM = 8
} maelys_datalog_literal_kind_t;

static inline int maelys_datalog_literal_is_aggregate(unsigned kind) {
    return kind >= MAELYS_DATALOG_LITERAL_COUNT && kind <= MAELYS_DATALOG_LITERAL_SUM;
}

typedef enum {
    MAELYS_DATALOG_FILTER_STARTS_WITH = 1,
    MAELYS_DATALOG_FILTER_ENDS_WITH = 2,
    MAELYS_DATALOG_FILTER_CONTAINS = 3
} maelys_datalog_filter_kind_t;

typedef struct {
    uint32_t pattern_offset;
    uint16_t pattern_length;
    uint8_t kind; /* maelys_datalog_filter_kind_t */
    uint8_t _pad0;
} maelys_datalog_filter_program_t;

_Static_assert(sizeof(maelys_datalog_filter_program_t) == 8u,
               "filter program must remain a pointer-free POD");

typedef enum {
    MAELYS_DATALOG_CMP_EQ = 1,
    MAELYS_DATALOG_CMP_NEQ = 2,
    MAELYS_DATALOG_CMP_LT = 3,
    MAELYS_DATALOG_CMP_LTE = 4,
    MAELYS_DATALOG_CMP_GT = 5,
    MAELYS_DATALOG_CMP_GTE = 6
} maelys_datalog_cmp_op_t;

#define MAELYS_DATALOG_ARITH_EXPR_NO_NODE UINT8_MAX

typedef enum {
    MAELYS_DATALOG_ARITH_EXPR_INT_LITERAL = 1,
    MAELYS_DATALOG_ARITH_EXPR_VAR = 2,
    MAELYS_DATALOG_ARITH_EXPR_ADD = 3,
    MAELYS_DATALOG_ARITH_EXPR_SUB = 4,
    MAELYS_DATALOG_ARITH_EXPR_MUL = 5
} maelys_datalog_arith_expr_kind_t;

typedef struct {
    maelys_datalog_arith_expr_kind_t kind;
    uint8_t left;
    uint8_t right;
    uint8_t _pad[2];
    maelys_datalog_internal_term_t term;
} maelys_datalog_arith_expr_node_t;

typedef struct {
    maelys_datalog_literal_kind_t kind;
    maelys_datalog_internal_atom_t atom;
    maelys_datalog_internal_term_t lhs;
    maelys_datalog_internal_term_t rhs;
    maelys_datalog_cmp_op_t op;
    uint8_t lhs_expr_root;
    uint8_t rhs_expr_root;
    uint8_t has_arith_expr;
    uint8_t filter_kind; /* maelys_datalog_filter_kind_t for FILTER */
    uint16_t filter_program_index;
    maelys_datalog_internal_term_t filter_value;
} maelys_datalog_literal_t;

typedef struct {
    maelys_datalog_internal_atom_t head;
    maelys_datalog_literal_t body[MAELYS_DATALOG_MAX_BODY_LITERALS];
    maelys_datalog_arith_expr_node_t expr_nodes[MAELYS_DATALOG_MAX_ARITH_EXPR_NODES];
    uint8_t expr_node_count;
    size_t body_count;
    size_t rule_id;
} maelys_datalog_rule_t;

typedef enum {
    MAELYS_DATALOG_DENY_NONE = 0,
    MAELYS_DATALOG_DENY_EXPLICIT,
    MAELYS_DATALOG_DENY_DEFAULT,
    MAELYS_DATALOG_DENY_MAX_DEPTH,
    MAELYS_DATALOG_DENY_EDB_OVERFLOW,
    MAELYS_DATALOG_DENY_IDB_OVERFLOW,
    MAELYS_DATALOG_DENY_COMPARISON_TYPE_ERROR,
    MAELYS_DATALOG_DENY_FILTER_ERROR,
    MAELYS_DATALOG_DENY_CONFLICT,
    MAELYS_DATALOG_DENY_POLICY_LOAD_ERROR
} maelys_datalog_deny_reason_t;

typedef struct {
    size_t rule_id;
    maelys_datalog_predicate_id_t predicate_id;
    maelys_datalog_deny_reason_t deny_reason;
    size_t depth;
    maelys_datalog_internal_fact_t derived_fact;
    uint16_t parent_index;
} maelys_datalog_proof_node_t;

_Static_assert(sizeof(maelys_datalog_proof_node_t) <= 112u,
               "proof node exceeds expected bound");

typedef struct {
    char policy_id[128];
    char sha256[65];
    maelys_datalog_proof_node_t nodes[MAELYS_DATALOG_MAX_PROOF_NODES];
    size_t node_count;
    int truncated;
    int verbose;
} maelys_datalog_proof_tree_t;

_Static_assert(sizeof(maelys_datalog_proof_tree_t) <= 8192u,
               "proof tree exceeds expected bound");

typedef struct {
    int derived;
    maelys_datalog_deny_reason_t deny_reason;
    char policy_id[128];
    char sha256[65];
    maelys_datalog_proof_tree_t proof;
} maelys_datalog_query_result_t;

/* ---------------------------------------------------------------------------
 * P4-C64 — Bounded Why-true premise provenance.
 *
 * A structured, complete and bounded witness for a retained canonical
 * derivation of an IDB fact. These public types are the caller-owned output of
 * maelys_datalog_explain_solved_fact() (declared in the solver header). They do
 * preserve the public structured records and serialized proof text. These
 * private premise/proof layouts use compact facts independently of public views.
 * ------------------------------------------------------------------------- */

/* One explanation step per proof node; one premise per body literal. */
#define MAELYS_DATALOG_MAX_EXPLANATION_STEPS \
    MAELYS_DATALOG_MAX_PROOF_NODES
#define MAELYS_DATALOG_MAX_EXPLANATION_PREMISES \
    (MAELYS_DATALOG_MAX_PROOF_NODES * MAELYS_DATALOG_MAX_BODY_LITERALS)



static inline int maelys_datalog_premise_is_aggregate(unsigned kind) {
    return kind >= MAELYS_DATALOG_EXPLANATION_PREMISE_COUNT &&
           kind <= MAELYS_DATALOG_EXPLANATION_PREMISE_SUM;
}



/* A single premise of a canonical derivation, in the rule body's lexical
 * position `body_index`.
 *
 * kind == POSITIVE_FACT      -> as.fact is the exact matched ground atom;
 *                               origin is POLICY_FACT / EDB / IDB; for IDB,
 *                               parent_step is the local remapped step that
 *                               derived it, otherwise EXPLANATION_NO_STEP.
 * kind == NEGATED_ABSENCE    -> as.fact is the instantiated ground atom whose
 *                               absence was verified; origin is the store the
 *                               absence was checked in; parent_step is
 *                               EXPLANATION_NO_STEP.
 * kind == COMPARISON_TRUE    -> as.comparison holds the two evaluated ground
 *                               terms; op is the comparison operator; origin is
 *                               NOT_APPLICABLE; parent_step is
 *                               EXPLANATION_NO_STEP.
 *
 * A union is used so the atom and comparison layouts are not both paid for. */
typedef struct {
    uint8_t kind;         /* maelys_datalog_explanation_premise_kind_t */
    uint8_t origin;       /* maelys_datalog_explanation_origin_t */
    uint16_t body_index;  /* lexical position in the rule body */
    uint16_t parent_step; /* local step, or MAELYS_DATALOG_EXPLANATION_NO_STEP */
    uint8_t op;           /* maelys_datalog_cmp_op_t for COMPARISON_TRUE, else 0 */
    uint8_t _pad0;
    union {
        maelys_datalog_internal_fact_t fact;
        struct {
            /* Aggregate metadata follows the compact pattern explicitly;
             * its header bytes are reserved for tags, never overlaid. */
            maelys_datalog_internal_fact_t pattern;
            uint32_t value;
            uint8_t projected_variable;
            uint8_t reserved[3];
        } count;
        struct {
            maelys_datalog_internal_term_t lhs;
            maelys_datalog_internal_term_t rhs;
        } comparison;
        struct {
            maelys_datalog_internal_term_t value;
            uint16_t program_index;
            uint8_t filter_kind;
            uint8_t _pad[5];
        } filter;
    } as;
} maelys_datalog_explanation_premise_t;

_Static_assert(sizeof(((maelys_datalog_explanation_premise_t *)0)->as.count) ==
               sizeof(maelys_datalog_internal_fact_t) + 8u,
               "aggregate metadata must add exactly one aligned eight-byte trailer");
_Static_assert(sizeof(((maelys_datalog_explanation_premise_t *)0)->as) == 48u,
               "aggregate pattern and metadata must bound the whole premise union");
_Static_assert(sizeof(maelys_datalog_explanation_premise_t) <= 96u,
               "explanation premise exceeds 96-byte bound");

/* One derivation step: a rule application that produced derived_fact, whose
 * premises are premises[premise_begin .. premise_begin + premise_count). */
typedef struct {
    size_t rule_id;
    maelys_datalog_internal_fact_t derived_fact;
    uint16_t premise_begin;
    uint16_t premise_count;
    uint8_t _pad[4];
} maelys_datalog_explanation_step_t;

_Static_assert(sizeof(maelys_datalog_explanation_step_t) <= 96u,
               "explanation step exceeds 96-byte bound");

_Static_assert(MAELYS_DATALOG_MAX_EXPLANATION_STEPS <= UINT16_MAX,
               "explanation step capacity must fit uint16 step indices");
_Static_assert(MAELYS_DATALOG_MAX_EXPLANATION_PREMISES <= UINT16_MAX,
               "explanation premise capacity must fit uint16 premise indices");

/* Caller-owned bounded explanation DAG. Passed by pointer; large (bounded to
 * 65536 bytes) and therefore not to be materialized on the stack by callers. */
typedef struct {
    maelys_datalog_explanation_step_t steps[MAELYS_DATALOG_MAX_EXPLANATION_STEPS];
    maelys_datalog_explanation_premise_t premises[MAELYS_DATALOG_MAX_EXPLANATION_PREMISES];
    uint16_t step_count;
    uint16_t premise_count;
    uint8_t found;
    uint8_t truncated;
    uint8_t _pad[2];
} maelys_datalog_explanation_t;

_Static_assert(sizeof(maelys_datalog_explanation_t) <= 65536u,
               "public explanation exceeds 64 KiB bound");

/* ---------------------------------------------------------------------------
 * Bounded Why-false diagnostics.
 *
 * Unlike Why-true, these records are reconstructed after a successful solve
 * against its immutable final EDB / policy-fact / IDB state. They are
 * diagnostic attempts, not derivation witnesses.
 * ------------------------------------------------------------------------- */

#define MAELYS_DATALOG_MAX_WHY_FALSE_DIAGNOSTICS 16u
#define MAELYS_DATALOG_MAX_WHY_FALSE_SUPPORTS \
    MAELYS_DATALOG_MAX_BODY_LITERALS
#define MAELYS_DATALOG_MAX_WHY_FALSE_SUBSTITUTIONS_PER_RULE 4096u









typedef struct {
    size_t max_candidate_rules;
    size_t max_substitutions_per_rule;
    size_t max_depth;
    size_t max_diagnostics;
} maelys_datalog_why_false_limits_t;

typedef struct {
    maelys_datalog_predicate_id_t predicate_id;
    uint8_t arity;
    uint8_t unbound_term_mask;
    maelys_datalog_internal_term_t terms[MAELYS_DATALOG_MAX_TERMS];
} maelys_datalog_why_false_pattern_t;

typedef struct {
    uint16_t body_index;
    uint8_t origin; /* maelys_datalog_explanation_origin_t */
    uint8_t _pad0;
    maelys_datalog_internal_fact_t fact;
} maelys_datalog_why_false_support_t;

typedef struct {
    uint8_t kind;   /* maelys_datalog_why_false_obstacle_kind_t */
    uint8_t origin; /* maelys_datalog_explanation_origin_t */
    uint16_t body_index;
    maelys_datalog_why_false_pattern_t pattern;
    uint8_t op; /* maelys_datalog_cmp_op_t for COMPARISON_FALSE */
    uint8_t _pad0[7];
    maelys_datalog_internal_term_t lhs;
    maelys_datalog_internal_term_t rhs;
    uint16_t filter_program_index;
    uint8_t filter_kind;
    uint8_t _pad1[5];
    maelys_datalog_internal_term_t filter_value;
} maelys_datalog_why_false_obstacle_t;

typedef struct {
    size_t rule_id;
    maelys_datalog_internal_fact_t target_fact;
    uint32_t bound_variable_mask;
    maelys_datalog_internal_term_t substitution[MAELYS_DATALOG_MAX_RULE_VARIABLES];
    maelys_datalog_why_false_support_t
        supports[MAELYS_DATALOG_MAX_WHY_FALSE_SUPPORTS];
    uint16_t support_count;
    uint8_t depth;
    uint8_t _pad0;
    maelys_datalog_why_false_obstacle_t obstacle;
} maelys_datalog_why_false_diagnostic_t;

typedef struct {
    maelys_datalog_internal_fact_t query;
    uint8_t status;       /* maelys_datalog_why_false_status_t */
    uint8_t summary;      /* maelys_datalog_why_false_summary_t */
    uint8_t query_origin; /* maelys_datalog_explanation_origin_t */
    uint8_t limit_hits;   /* bitset of maelys_datalog_why_false_limit_t */
    size_t candidate_rule_count;
    size_t substitution_count;
    size_t diagnostic_count;
    size_t filter_cost_units;
    maelys_datalog_why_false_diagnostic_t
        diagnostics[MAELYS_DATALOG_MAX_WHY_FALSE_DIAGNOSTICS];
} maelys_datalog_why_false_explanation_t;



_Static_assert(sizeof(maelys_datalog_why_false_explanation_t) <= 65536u,
               "public Why-false explanation exceeds 64 KiB bound");

#ifdef __cplusplus
}
#endif

#endif
