#pragma once
#ifndef MAELYS_DATALOG_POLICY_H
#define MAELYS_DATALOG_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/maelys_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAELYS_DATALOG_PROFILE_NAME "MAELYS-DATALOG-TEXT-v1"
#define MAELYS_DATALOG_SHA256_UNSET "unset"
#define MAELYS_DATALOG_MAX_STRING_BYTES 1024u
#define MAELYS_DATALOG_MAX_TOKEN_BYTES 1024u
#define MAELYS_DATALOG_MAX_SYMBOLS 512u
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
#define MAELYS_DATALOG_MAX_INT 2147483647LL
#define MAELYS_DATALOG_PROOF_NO_PARENT UINT16_MAX

#define MAELYS_DATALOG_DECISION_NAME_ALLOW "allow"
#define MAELYS_DATALOG_DECISION_NAME_REDUCED "reduced"
#define MAELYS_DATALOG_DECISION_NAME_DENY "deny"
#define MAELYS_DATALOG_DECISION_NAME_DENY_DEFAULT "deny_default"
#define MAELYS_DATALOG_DECISION_NAME_DENY_CONFLICT "deny_conflict"

_Static_assert(MAELYS_DATALOG_MAX_STRATA <= 8u,
               "stratum boundary array needs widening above 8");
_Static_assert(MAELYS_DATALOG_MAX_PREDICATES <= 128u,
               "strata array indexed by predicate_id");
_Static_assert(MAELYS_DATALOG_MAX_STRATA <= MAELYS_DATALOG_MAX_PREDICATES,
               "strata count cannot exceed predicate id capacity");

typedef uint32_t maelys_datalog_symbol_id_t;
typedef uint16_t maelys_datalog_predicate_id_t;

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

typedef struct {
    char name[64];
    size_t arity;
    unsigned kind_flags;
} maelys_datalog_predicate_def_t;

typedef struct {
    maelys_datalog_predicate_def_t defs[MAELYS_DATALOG_MAX_PREDICATES];
    size_t count;
    char atoms[MAELYS_DATALOG_MAX_ATOMS][64];
    size_t atom_count;
    int frozen;
} maelys_datalog_predicate_registry_t;

typedef struct {
    char storage[MAELYS_DATALOG_STRING_POOL_BYTES];
    size_t used;
    struct {
        uint32_t hash;
        uint32_t offset;
        uint16_t len;
    } entries[MAELYS_DATALOG_MAX_SYMBOLS];
    size_t count;
} maelys_datalog_symbol_table_t;

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

typedef struct {
    maelys_datalog_literal_kind_t kind;
    maelys_datalog_fact_t atom;
    maelys_datalog_term_t lhs;
    maelys_datalog_term_t rhs;
    maelys_datalog_cmp_op_t op;
} maelys_datalog_literal_t;

typedef struct {
    maelys_datalog_fact_t head;
    maelys_datalog_literal_t body[MAELYS_DATALOG_MAX_BODY_LITERALS];
    size_t body_count;
    size_t rule_id;
} maelys_datalog_rule_t;

typedef struct {
    char name[64];
    size_t arity;
} maelys_datalog_query_whitelist_entry_t;

typedef struct {
    int loaded;
    char policy_id[128];
    char domain[64];
    char sha256[65];
    maelys_datalog_query_whitelist_entry_t query_whitelist[MAELYS_DATALOG_MAX_QUERY_WHITELIST];
    size_t query_whitelist_count;
    int enforces_query_whitelist;
    int positive_recursion_supported;
    int negation_supported;
    int negation_recursion_supported;
    uint32_t strata[MAELYS_DATALOG_MAX_PREDICATES];
    uint32_t max_stratum;
    int strata_assigned;
    int has_positive_recursion;
    int test_only;
    maelys_datalog_symbol_table_t symbols;
    maelys_datalog_predicate_registry_t registry;
    maelys_datalog_fact_t facts[MAELYS_DATALOG_MAX_RULE_FACTS];
    size_t fact_count;
    maelys_datalog_rule_t rules[MAELYS_DATALOG_MAX_RULES];
    size_t rule_count;
} maelys_datalog_ruleset_t;

typedef struct {
    maelys_datalog_fact_t *facts;
    size_t fact_capacity;
    size_t fact_count;
    maelys_datalog_fact_set_t fact_set;
    size_t facts_per_pred[MAELYS_DATALOG_MAX_PREDICATES];
    int immutable;
    maelys_datalog_symbol_table_t *symbols;
    const maelys_datalog_predicate_registry_t *registry;
} maelys_datalog_edb_t;

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

typedef enum {
    MAELYS_DATALOG_DECISION_DENY = 0,
    MAELYS_DATALOG_DECISION_ALLOW = 1,
    MAELYS_DATALOG_DECISION_REDUCED = 2,
    MAELYS_DATALOG_DECISION_DENY_DEFAULT = 3,
    MAELYS_DATALOG_DECISION_DENY_CONFLICT = 4
} maelys_datalog_decision_t;

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

typedef struct maelys_datalog_solve_result maelys_datalog_solve_result_t;

typedef enum {
    MAELYS_DATALOG_SOLVE_DIAG_NONE = 0,
    MAELYS_DATALOG_SOLVE_DIAG_MAX_DEPTH,
    MAELYS_DATALOG_SOLVE_DIAG_IDB_OVERFLOW,
    MAELYS_DATALOG_SOLVE_DIAG_COMPARISON_TYPE_ERROR,
    MAELYS_DATALOG_SOLVE_DIAG_MALFORMED_FACT,
    MAELYS_DATALOG_SOLVE_DIAG_MALFORMED_EDB,
    MAELYS_DATALOG_SOLVE_DIAG_INVALID_STATE,
    MAELYS_DATALOG_SOLVE_DIAG_INVALID_ARGUMENT,
    MAELYS_DATALOG_SOLVE_DIAG_INTERNAL_ERROR
} maelys_datalog_solve_diag_category_t;

typedef struct {
    maelys_result_t failure_error;
    maelys_datalog_deny_reason_t failure_reason;
    maelys_datalog_solve_diag_category_t category;
    uint16_t predicate_id;
    uint16_t rule_id;
    uint16_t depth;
    uint16_t depth_limit;
    uint16_t capacity;
    uint16_t count_observed;
    uint8_t lhs_kind;
    uint8_t rhs_kind;
    uint8_t comparison_op;
    uint8_t term_index;
    uint8_t arity_expected;
    uint8_t arity_observed;
    uint8_t _pad[2];
} maelys_datalog_solve_diagnostic_t;

typedef struct {
    int allow_test_only;
} maelys_datalog_manifest_load_options_t;

typedef struct {
    maelys_datalog_ruleset_t policies[8];
    size_t policy_count;
    maelys_datalog_query_whitelist_entry_t query_whitelist[MAELYS_DATALOG_MAX_QUERY_WHITELIST];
    size_t query_whitelist_count;
    int enforces_query_whitelist;
} maelys_datalog_policy_set_t;

const char *maelys_datalog_decision_name(maelys_datalog_decision_t decision);
const char *maelys_datalog_solve_diagnostic_category_name(
    maelys_datalog_solve_diag_category_t category);
maelys_result_t maelys_datalog_solve_once(const maelys_datalog_ruleset_t *ruleset,
                                          const maelys_datalog_edb_t *edb,
                                          maelys_datalog_solve_result_t **out_result);
maelys_result_t maelys_datalog_solve_once_ex(
    const maelys_datalog_ruleset_t *ruleset,
    const maelys_datalog_edb_t *edb,
    maelys_datalog_solve_result_t **out_result,
    maelys_datalog_solve_diagnostic_t *out_diag);
void maelys_datalog_solve_result_free(maelys_datalog_solve_result_t *result);
maelys_result_t maelys_datalog_query_solved_ground_fact(
    const maelys_datalog_solve_result_t *result,
    const char *predicate,
    const maelys_datalog_term_t *terms,
    size_t arity,
    bool *out_present);
const maelys_datalog_proof_tree_t *maelys_datalog_solve_result_proof(
    const maelys_datalog_solve_result_t *result);
maelys_result_t maelys_datalog_extract_proof_for_fact(
    const maelys_datalog_solve_result_t *result,
    const maelys_datalog_fact_t *queried_fact,
    maelys_datalog_proof_tree_t *out_proof);
maelys_result_t maelys_datalog_decision_from_queries(
    const maelys_datalog_query_result_t *deny_result,
    const maelys_datalog_query_result_t *reduce_result,
    const maelys_datalog_query_result_t *allow_result,
    maelys_datalog_decision_t *out_decision);
maelys_result_t maelys_datalog_ruleset_finalize_sha256(maelys_datalog_ruleset_t *ruleset);

#ifdef __cplusplus
}
#endif

#endif
