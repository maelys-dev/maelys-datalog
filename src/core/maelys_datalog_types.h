#pragma once
#ifndef MAELYS_DATALOG_TYPES_H
#define MAELYS_DATALOG_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAELYS_DATALOG_PROFILE_NAME "MAELYS-DATALOG-TEXT-v1"
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
#define MAELYS_DATALOG_MAX_EDB_FACTS 1024u
#define MAELYS_DATALOG_MAX_IDB_FACTS 1024u
#define MAELYS_DATALOG_MAX_FACTS_PER_PRED 64u
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

#ifdef __cplusplus
}
#endif

#endif
