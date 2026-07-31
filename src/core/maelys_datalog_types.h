#pragma once
#ifndef MAELYS_DATALOG_TYPES_H
#define MAELYS_DATALOG_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

typedef struct {
    /* Observable engine capacities (shared across profiles). */
    size_t max_symbols;        /* MAELYS_DATALOG_MAX_SYMBOLS */
    size_t string_pool_bytes;  /* MAELYS_DATALOG_STRING_POOL_BYTES */
    size_t max_predicates;     /* MAELYS_DATALOG_MAX_PREDICATES */
    size_t max_rules;          /* MAELYS_DATALOG_MAX_RULES */
    size_t max_arity;          /* MAELYS_DATALOG_MAX_ARITY */
    size_t max_body_literals;  /* MAELYS_DATALOG_MAX_BODY_LITERALS */
    size_t max_depth;          /* MAELYS_DATALOG_MAX_DEPTH */
    /* Profile-specific observable capacities. */
    size_t max_edb_facts;      /* MAELYS_DATALOG_MAX_EDB_FACTS */
    size_t max_idb_facts;      /* MAELYS_DATALOG_MAX_IDB_FACTS */
    size_t max_facts_per_pred; /* MAELYS_DATALOG_MAX_FACTS_PER_PRED */
} maelys_datalog_build_limits_t;

/* Build limits expose user-observable engine capacities, not internal
 * implementation constants. The reported values describe the active build
 * profile; they are not runtime occupancy, EDB usage, solve statistics, or
 * mutable engine state. out_limits is required and must not be NULL. */
void maelys_datalog_get_build_limits(maelys_datalog_build_limits_t *out_limits);

typedef uint32_t maelys_datalog_symbol_id_t;
typedef uint16_t maelys_datalog_predicate_id_t;

#define MAELYS_DATALOG_SYMBOL_ID_INVALID ((maelys_datalog_symbol_id_t)0u)

typedef enum {
    MAELYS_DATALOG_TERM_SYMBOL = 1,
    MAELYS_DATALOG_TERM_INT = 2,
    MAELYS_DATALOG_TERM_BOOL = 3,
    MAELYS_DATALOG_TERM_VAR = 4
} maelys_datalog_term_kind_t;

typedef struct {
    maelys_datalog_term_kind_t kind;
    union {
        maelys_datalog_symbol_id_t symbol;
        long long integer;
        int boolean;
        unsigned variable;
    } as;
} maelys_datalog_term_t;

typedef struct {
    maelys_datalog_predicate_id_t predicate_id;
    uint8_t arity;
    maelys_datalog_term_t terms[MAELYS_DATALOG_MAX_TERMS];
} maelys_datalog_fact_t;

typedef struct {
    maelys_datalog_fact_t *facts;
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
    MAELYS_DATALOG_LITERAL_NEGATED_ATOM = 3
} maelys_datalog_literal_kind_t;

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
    maelys_datalog_term_t term;
} maelys_datalog_arith_expr_node_t;

typedef struct {
    maelys_datalog_literal_kind_t kind;
    maelys_datalog_fact_t atom;
    maelys_datalog_term_t lhs;
    maelys_datalog_term_t rhs;
    maelys_datalog_cmp_op_t op;
    uint8_t lhs_expr_root;
    uint8_t rhs_expr_root;
    uint8_t has_arith_expr;
} maelys_datalog_literal_t;

typedef struct {
    maelys_datalog_fact_t head;
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
    MAELYS_DATALOG_DENY_CONFLICT,
    MAELYS_DATALOG_DENY_POLICY_LOAD_ERROR
} maelys_datalog_deny_reason_t;

typedef struct {
    size_t rule_id;
    maelys_datalog_predicate_id_t predicate_id;
    maelys_datalog_deny_reason_t deny_reason;
    size_t depth;
    maelys_datalog_fact_t derived_fact;
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
 * NOT change maelys_datalog_proof_node_t / maelys_datalog_proof_tree_t, whose
 * layout and bytes remain the historic proof-tree contract.
 * ------------------------------------------------------------------------- */

/* One explanation step per proof node; one premise per body literal. */
#define MAELYS_DATALOG_MAX_EXPLANATION_STEPS \
    MAELYS_DATALOG_MAX_PROOF_NODES
#define MAELYS_DATALOG_MAX_EXPLANATION_PREMISES \
    (MAELYS_DATALOG_MAX_PROOF_NODES * MAELYS_DATALOG_MAX_BODY_LITERALS)
#define MAELYS_DATALOG_EXPLANATION_NO_STEP UINT16_MAX

typedef enum {
    MAELYS_DATALOG_EXPLANATION_PREMISE_POSITIVE_FACT = 1,
    MAELYS_DATALOG_EXPLANATION_PREMISE_NEGATED_ABSENCE = 2,
    MAELYS_DATALOG_EXPLANATION_PREMISE_COMPARISON_TRUE = 3
} maelys_datalog_explanation_premise_kind_t;

typedef enum {
    MAELYS_DATALOG_EXPLANATION_ORIGIN_POLICY_FACT = 1,
    MAELYS_DATALOG_EXPLANATION_ORIGIN_EDB = 2,
    MAELYS_DATALOG_EXPLANATION_ORIGIN_IDB = 3,
    MAELYS_DATALOG_EXPLANATION_ORIGIN_NOT_APPLICABLE = 4
} maelys_datalog_explanation_origin_t;

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
        maelys_datalog_fact_t fact;
        struct {
            maelys_datalog_term_t lhs;
            maelys_datalog_term_t rhs;
        } comparison;
    } as;
} maelys_datalog_explanation_premise_t;

_Static_assert(sizeof(maelys_datalog_explanation_premise_t) <= 96u,
               "explanation premise exceeds 96-byte bound");

/* One derivation step: a rule application that produced derived_fact, whose
 * premises are premises[premise_begin .. premise_begin + premise_count). */
typedef struct {
    size_t rule_id;
    maelys_datalog_fact_t derived_fact;
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

#ifdef __cplusplus
}
#endif

#endif
