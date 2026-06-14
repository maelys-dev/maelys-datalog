#include "src/core/maelys_datalog_edb.h"
#include "src/core/maelys_datalog_audit.h"
#include "src/core/maelys_datalog_domain_registry.h"
#include "examples/domains/maelys_datalog_example_domains.h"
#include "src/core/maelys_datalog_parser.h"
#include "src/core/maelys_datalog_policy.h"
#include "src/core/maelys_datalog_symbol_table.h"
#include "tests/helpers/test_framework.h"

#include <stdio.h>
#include <string.h>

maelys_result_t maelys_datalog_test_build_static_join_order(
    const maelys_datalog_ruleset_t *ruleset,
    const maelys_datalog_rule_t *rule,
    int delta_body_index,
    uint8_t out_order[MAELYS_DATALOG_MAX_BODY_LITERALS],
    uint8_t *out_count);
maelys_result_t maelys_datalog_test_solve_once_legacy_order(
    const maelys_datalog_ruleset_t *ruleset,
    const maelys_datalog_edb_t *edb,
    maelys_datalog_solve_result_t **out_result,
    maelys_datalog_solve_diagnostic_t *out_diag);
maelys_result_t maelys_datalog_test_solve_result_idb_facts(
    const maelys_datalog_solve_result_t *result,
    const maelys_datalog_fact_t **out_facts,
    size_t *out_count);
maelys_result_t maelys_datalog_test_solve_result_idb_proof_indices(
    const maelys_datalog_solve_result_t *result,
    const uint16_t **out_indices,
    size_t *out_count);

static int init_solver_test_ruleset(maelys_datalog_ruleset_t *r) {
    memset(r, 0, sizeof(*r));
    int rc = maelys_datalog_ruleset_init(r, "policy.test", "graph",
                                         "0000000000000000000000000000000000000000000000000000000000000000", 1);
    if (rc != MAELYS_OK) return rc;
    rc = maelys_datalog_example_domains_install();
    if (rc != MAELYS_OK) return rc;
    static const maelys_datalog_predicate_def_t test_defs[] = {
        {"parent", 2, MAELYS_DATALOG_PRED_KIND_EDB},
        {"q", 2, MAELYS_DATALOG_PRED_KIND_EDB},
        {"r", 2, MAELYS_DATALOG_PRED_KIND_EDB},
        {"edge", 2, MAELYS_DATALOG_PRED_KIND_EDB},
        {"blocked", 1, MAELYS_DATALOG_PRED_KIND_EDB},
        {"backend", 1, MAELYS_DATALOG_PRED_KIND_EDB},
        {"backend_class", 2, MAELYS_DATALOG_PRED_KIND_EDB},
        {"backend_channel", 2, MAELYS_DATALOG_PRED_KIND_EDB},
        {"backend_format", 2, MAELYS_DATALOG_PRED_KIND_EDB},
        {"user", 1, MAELYS_DATALOG_PRED_KIND_EDB},
        {"resource", 1, MAELYS_DATALOG_PRED_KIND_EDB},
        {"safe", 1, MAELYS_DATALOG_PRED_KIND_POLICY_FACT},
        {"allowed_backend_tuple", 3, MAELYS_DATALOG_PRED_KIND_POLICY_FACT},
        {"ancestor", 2, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
        {"path", 2, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
        {"p", 1, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
        {"p_same", 1, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
        {"edge_seen", 1, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
        {"allow", 1, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
        {"deny", 1, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
        {"reduce", 1, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
        {"left", 1, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
        {"right", 1, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
        {"both", 1, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
        {"base_only", 1, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
    };
    for (size_t i = 0; i < sizeof(test_defs) / sizeof(test_defs[0]); i++) {
        rc = maelys_datalog_predicate_registry_add_domain(
            &r->registry, test_defs[i].name, test_defs[i].arity, test_defs[i].kind_flags);
        if (rc != MAELYS_OK) return rc;
    }
    static const char *const atoms[] = {
        "proj-1", "alice", "bob", "carol", "dave", "r1", "r2",
        "cli_pivot", "stdin", "text", "request", "a", "b", "c", "notanint", NULL};
    for (size_t i = 0; atoms[i]; i++) {
        rc = maelys_datalog_predicate_registry_add_atom(&r->registry, atoms[i]);
        if (rc != MAELYS_OK) return rc;
    }
    rc = maelys_datalog_predicate_registry_freeze(&r->registry);
    if (rc != MAELYS_OK) return rc;
    return MAELYS_OK;
}

static int make_ruleset(maelys_datalog_ruleset_t *r, const char *src) {
    int rc = init_solver_test_ruleset(r);
    if (rc != MAELYS_OK) return rc;
    return maelys_datalog_parse_ruleset(r, src, strlen(src));
}

static int parse_solver_test_ruleset_ex(const char *src, maelys_datalog_diagnostic_t *diag) {
    maelys_datalog_ruleset_t r;
    int rc = init_solver_test_ruleset(&r);
    if (rc != MAELYS_OK) return rc;
    rc = maelys_datalog_parse_ruleset_ex(&r, src, strlen(src), "comparison_matrix.dl", diag);
    maelys_datalog_ruleset_clear(&r);
    return rc;
}

static maelys_datalog_term_t sym_term(maelys_datalog_ruleset_t *r, const char *s) {
    maelys_datalog_symbol_id_t id = 0;
    maelys_datalog_symbol_intern(&r->symbols, s, strlen(s), &id);
    maelys_datalog_term_t t = {.kind = MAELYS_DATALOG_TERM_SYMBOL};
    t.as.symbol = id;
    return t;
}

static void add_parent(maelys_datalog_ruleset_t *r, maelys_datalog_edb_t *edb, const char *a, const char *b) {
    maelys_datalog_term_t terms[2] = {sym_term(r, a), sym_term(r, b)};
    (void)maelys_datalog_edb_add_fact(edb, "parent", terms, 2);
}

static void add_symbol_unary(maelys_datalog_ruleset_t *r,
                             maelys_datalog_edb_t *edb,
                             const char *predicate,
                             const char *arg) {
    maelys_datalog_term_t term = sym_term(r, arg);
    (void)maelys_datalog_edb_add_fact(edb, predicate, &term, 1);
}

static void add_int_unary(maelys_datalog_edb_t *edb, const char *predicate, long long arg) {
    maelys_datalog_term_t term = {.kind = MAELYS_DATALOG_TERM_INT};
    term.as.integer = arg;
    (void)maelys_datalog_edb_add_fact(edb, predicate, &term, 1);
}

static void add_int_symbol_binary(maelys_datalog_ruleset_t *r,
                                  maelys_datalog_edb_t *edb,
                                  const char *predicate,
                                  long long lhs,
                                  const char *rhs) {
    maelys_datalog_term_t terms[2];
    terms[0].kind = MAELYS_DATALOG_TERM_INT;
    terms[0].as.integer = lhs;
    terms[1] = sym_term(r, rhs);
    (void)maelys_datalog_edb_add_fact(edb, predicate, terms, 2);
}

static void add_symbol_symbol_binary(maelys_datalog_ruleset_t *r,
                                     maelys_datalog_edb_t *edb,
                                     const char *predicate,
                                     const char *lhs,
                                     const char *rhs) {
    maelys_datalog_term_t terms[2];
    terms[0] = sym_term(r, lhs);
    terms[1] = sym_term(r, rhs);
    (void)maelys_datalog_edb_add_fact(edb, predicate, terms, 2);
}

static void add_symbol_int_binary(maelys_datalog_ruleset_t *r,
                                  maelys_datalog_edb_t *edb,
                                  const char *predicate,
                                  const char *lhs,
                                  long long rhs) {
    maelys_datalog_term_t terms[2];
    terms[0] = sym_term(r, lhs);
    terms[1].kind = MAELYS_DATALOG_TERM_INT;
    terms[1].as.integer = rhs;
    (void)maelys_datalog_edb_add_fact(edb, predicate, terms, 2);
}

static void add_symbol_bool_binary(maelys_datalog_ruleset_t *r,
                                   maelys_datalog_edb_t *edb,
                                   const char *predicate,
                                   const char *lhs,
                                   int rhs) {
    maelys_datalog_term_t terms[2];
    terms[0] = sym_term(r, lhs);
    terms[1].kind = MAELYS_DATALOG_TERM_BOOL;
    terms[1].as.boolean = rhs ? 1 : 0;
    (void)maelys_datalog_edb_add_fact(edb, predicate, terms, 2);
}

static void add_int_int_binary(maelys_datalog_edb_t *edb,
                               const char *predicate,
                               long long lhs,
                               long long rhs) {
    maelys_datalog_term_t terms[2];
    terms[0].kind = MAELYS_DATALOG_TERM_INT;
    terms[0].as.integer = lhs;
    terms[1].kind = MAELYS_DATALOG_TERM_INT;
    terms[1].as.integer = rhs;
    (void)maelys_datalog_edb_add_fact(edb, predicate, terms, 2);
}

static maelys_datalog_term_t int_term(long long value) {
    maelys_datalog_term_t t = {.kind = MAELYS_DATALOG_TERM_INT};
    t.as.integer = value;
    return t;
}

static int query_solved_int_unary(const maelys_datalog_solve_result_t *result,
                                  const char *predicate,
                                  long long value,
                                  bool *present) {
    maelys_datalog_term_t term = int_term(value);
    return maelys_datalog_query_solved_ground_fact(result, predicate, &term, 1, present);
}

static int query_solved_symbol_unary(maelys_datalog_ruleset_t *r,
                                     const maelys_datalog_solve_result_t *result,
                                     const char *predicate,
                                     const char *value,
                                     bool *present) {
    maelys_datalog_term_t term = sym_term(r, value);
    return maelys_datalog_query_solved_ground_fact(result, predicate, &term, 1, present);
}

static int fact_array_contains_semantic(const maelys_datalog_fact_t *facts,
                                        size_t count,
                                        const maelys_datalog_fact_t *needle) {
    for (size_t i = 0; i < count; i++) {
        if (maelys_datalog_fact_equals(&facts[i], needle)) return 1;
    }
    return 0;
}

static int fact_has_var(const maelys_datalog_fact_t *fact) {
    if (!fact) return 0;
    for (size_t i = 0; i < fact->arity && i < MAELYS_DATALOG_MAX_TERMS; i++) {
        if (fact->terms[i].kind == MAELYS_DATALOG_TERM_VAR) return 1;
    }
    return 0;
}

static int make_fact2(maelys_datalog_ruleset_t *r,
                      const char *predicate,
                      const char *a,
                      const char *b,
                      maelys_datalog_fact_t *out) {
    memset(out, 0, sizeof(*out));
    if (!maelys_datalog_predicate_registry_find(&r->registry, predicate, 2, &out->predicate_id)) {
        return 0;
    }
    out->arity = 2;
    out->terms[0] = sym_term(r, a);
    out->terms[1] = sym_term(r, b);
    return 1;
}

static int solve_ancestor_chain(maelys_datalog_ruleset_t *r,
                                maelys_datalog_edb_t *edb,
                                maelys_datalog_fact_t facts[8],
                                maelys_datalog_solve_result_t **out_result) {
    const char *src =
        "ancestor(X, Y) :- parent(X, Y).\n"
        "ancestor(X, Z) :- parent(X, Y), ancestor(Y, Z).";
    if (make_ruleset(r, src) != MAELYS_OK) return 0;
    if (maelys_datalog_edb_init(edb, facts, 8, &r->symbols, &r->registry) != MAELYS_OK) return 0;
    add_parent(r, edb, "alice", "bob");
    add_parent(r, edb, "bob", "carol");
    add_parent(r, edb, "carol", "dave");
    if (maelys_datalog_edb_finalize(edb) != MAELYS_OK) return 0;
    return maelys_datalog_solve_once(r, edb, out_result) == MAELYS_OK;
}

static int test_solver_simple_and_query(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "allow(P) :- blocked(P), blocked(P)."), "%d");
    maelys_datalog_fact_t facts[8];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 8, &r.symbols, &r.registry), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_add_symbol_fact(&edb, "blocked", "proj-1"), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_add_symbol_fact(&edb, "blocked", "proj-1"), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    maelys_datalog_term_t arg = sym_term(&r, "proj-1");
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_query_solved_ground_fact(result, "allow", &arg, 1, &present),
                      "%d");
    TEST_ASSERT_TRUE(present);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_solver_positive_recursion_transitive_closure(void) {
    TEST_BEGIN();
    const char *src =
        "ancestor(X, Y) :- parent(X, Y).\n"
        "ancestor(X, Z) :- parent(X, Y), ancestor(Y, Z).";
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, src), "%d");
    maelys_datalog_fact_t facts[8];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 8, &r.symbols, &r.registry), "%d");
    add_parent(&r, &edb, "alice", "bob");
    add_parent(&r, &edb, "bob", "carol");
    add_parent(&r, &edb, "carol", "dave");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    maelys_datalog_term_t args[2] = {sym_term(&r, "alice"), sym_term(&r, "dave")};
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_query_solved_ground_fact(result, "ancestor", args, 2, &present),
                      "%d");
    TEST_ASSERT_TRUE(present);
    maelys_datalog_solve_result_free(result);
    TEST_ASSERT_TRUE(r.has_positive_recursion);
    TEST_END();
}

static int test_solver_duplicate_and_overflow_bounds(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r,
        "ancestor(X, Y) :- parent(X, Y).\nancestor(X, Y) :- parent(X, Y)."), "%d");
    maelys_datalog_fact_t facts[2];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 2, &r.symbols, &r.registry), "%d");
    add_parent(&r, &edb, "alice", "bob");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    maelys_datalog_term_t args[2] = {sym_term(&r, "alice"), sym_term(&r, "bob")};
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_query_solved_ground_fact(result, "ancestor", args, 2, &present),
                      "%d");
    TEST_ASSERT_TRUE(present);
    maelys_datalog_solve_result_free(result);

    maelys_datalog_ruleset_t r2;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r2, "ancestor(X, Y) :- parent(X, Y)."), "%d");
    maelys_datalog_fact_t facts2[4];
    maelys_datalog_edb_t edb2;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb2, facts2, 4, &r2.symbols, &r2.registry), "%d");
    add_parent(&r2, &edb2, "alice", "bob");
    add_parent(&r2, &edb2, "bob", "carol");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb2), "%d");
    maelys_datalog_solve_result_t *result2 = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r2, &edb2, &result2), "%d");
    maelys_datalog_solve_result_free(result2);
    TEST_END();
}

static int test_solver_query_and_conflict_helper(void) {
    TEST_BEGIN();
    maelys_datalog_query_result_t deny = {.derived = 1};
    maelys_datalog_query_result_t reduce = {.derived = 0};
    maelys_datalog_query_result_t allow = {.derived = 1};
    maelys_datalog_decision_t decision;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_decision_from_queries(&deny, &reduce, &allow, &decision), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DECISION_DENY_CONFLICT, decision, "%d");
    deny.derived = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_decision_from_queries(&deny, &reduce, &allow, &decision), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DECISION_ALLOW, decision, "%d");
    allow.derived = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_decision_from_queries(&deny, &reduce, &allow, &decision), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DECISION_DENY_DEFAULT, decision, "%d");
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "allow(P) :- blocked(P)."), "%d");
    maelys_datalog_term_t var = {.kind = MAELYS_DATALOG_TERM_VAR};
    maelys_datalog_fact_t facts[1];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 1, &r.symbols, &r.registry), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_query_solved_ground_fact(result, "allow", &var, 1, &present),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_query_solved_ground_fact(result, "missing", NULL, 0, &present),
                      "%d");
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_maelys_datalog_query_rejects_variable_args(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "allow(P) :- blocked(P)."), "%d");
    maelys_datalog_term_t var = {.kind = MAELYS_DATALOG_TERM_VAR};
    var.as.variable = 0;
    maelys_datalog_fact_t facts[1];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 1, &r.symbols, &r.registry), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_query_solved_ground_fact(result, "allow", &var, 1, &present),
                      "%d");
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_maelys_datalog_query_not_derived_sets_deny_default(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "allow(P) :- blocked(P)."), "%d");
    maelys_datalog_term_t arg = sym_term(&r, "proj-1");
    maelys_datalog_fact_t facts[1];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 1, &r.symbols, &r.registry), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    bool present = true;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_query_solved_ground_fact(result, "allow", &arg, 1, &present),
                      "%d");
    TEST_ASSERT_FALSE(present);
    maelys_datalog_query_result_t deny = {.derived = 0};
    maelys_datalog_query_result_t reduce = {.derived = 0};
    maelys_datalog_query_result_t allow = {.derived = present ? 1 : 0};
    maelys_datalog_decision_t decision = MAELYS_DATALOG_DECISION_ALLOW;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_decision_from_queries(&deny, &reduce, &allow, &decision), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DECISION_DENY_DEFAULT, decision, "%d");
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_maelys_datalog_decision_name_constants(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL_STRING(MAELYS_DATALOG_DECISION_NAME_ALLOW,
                             maelys_datalog_decision_name(MAELYS_DATALOG_DECISION_ALLOW));
    TEST_ASSERT_EQUAL_STRING(MAELYS_DATALOG_DECISION_NAME_REDUCED,
                             maelys_datalog_decision_name(MAELYS_DATALOG_DECISION_REDUCED));
    TEST_ASSERT_EQUAL_STRING(MAELYS_DATALOG_DECISION_NAME_DENY,
                             maelys_datalog_decision_name(MAELYS_DATALOG_DECISION_DENY));
    TEST_ASSERT_EQUAL_STRING(MAELYS_DATALOG_DECISION_NAME_DENY_DEFAULT,
                             maelys_datalog_decision_name(MAELYS_DATALOG_DECISION_DENY_DEFAULT));
    TEST_ASSERT_EQUAL_STRING(MAELYS_DATALOG_DECISION_NAME_DENY_CONFLICT,
                             maelys_datalog_decision_name(MAELYS_DATALOG_DECISION_DENY_CONFLICT));
    TEST_END();
}

static int test_maelys_datalog_decision_from_queries_deny_conflict(void) {
    TEST_BEGIN();
    maelys_datalog_query_result_t deny = {.derived = 1};
    maelys_datalog_query_result_t reduce = {.derived = 0};
    maelys_datalog_query_result_t allow = {.derived = 1};
    maelys_datalog_decision_t decision = MAELYS_DATALOG_DECISION_ALLOW;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_decision_from_queries(&deny, &reduce, &allow, &decision), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DECISION_DENY_CONFLICT, decision, "%d");
    TEST_END();
}

static int test_maelys_datalog_decision_from_queries_deny_reduce_conflict(void) {
    TEST_BEGIN();
    maelys_datalog_query_result_t deny = {.derived = 1};
    maelys_datalog_query_result_t reduce = {.derived = 1};
    maelys_datalog_query_result_t allow = {.derived = 0};
    maelys_datalog_decision_t decision = MAELYS_DATALOG_DECISION_ALLOW;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_decision_from_queries(&deny, &reduce, &allow, &decision), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DECISION_DENY_CONFLICT, decision, "%d");
    TEST_END();
}

static int test_maelys_datalog_decision_from_queries_priority(void) {
    TEST_BEGIN();
    maelys_datalog_query_result_t deny = {.derived = 0};
    maelys_datalog_query_result_t reduce = {.derived = 1};
    maelys_datalog_query_result_t allow = {.derived = 1};
    maelys_datalog_decision_t decision = MAELYS_DATALOG_DECISION_ALLOW;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_decision_from_queries(&deny, &reduce, &allow, &decision), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DECISION_REDUCED, decision, "%d");
    reduce.derived = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_decision_from_queries(&deny, &reduce, &allow, &decision), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DECISION_ALLOW, decision, "%d");
    TEST_END();
}

static int test_datalog_solve_once_allow_reduce_without_deny_is_reduced(void) {
    TEST_BEGIN();
    maelys_datalog_query_result_t deny = {.derived = 0};
    maelys_datalog_query_result_t reduce = {.derived = 1};
    maelys_datalog_query_result_t allow = {.derived = 1};
    maelys_datalog_decision_t decision = MAELYS_DATALOG_DECISION_ALLOW;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_decision_from_queries(&deny, &reduce, &allow, &decision), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DECISION_REDUCED, decision, "%d");
    TEST_END();
}

static int test_datalog_solve_once_deny_reduce_conflict_is_deny_conflict(void) {
    TEST_BEGIN();
    maelys_datalog_query_result_t deny = {.derived = 1};
    maelys_datalog_query_result_t reduce = {.derived = 1};
    maelys_datalog_query_result_t allow = {.derived = 0};
    maelys_datalog_decision_t decision = MAELYS_DATALOG_DECISION_ALLOW;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_decision_from_queries(&deny, &reduce, &allow, &decision), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DECISION_DENY_CONFLICT, decision, "%d");
    TEST_END();
}

static int test_maelys_datalog_decision_from_queries_default_deny(void) {
    TEST_BEGIN();
    maelys_datalog_query_result_t deny = {.derived = 0};
    maelys_datalog_query_result_t reduce = {.derived = 0};
    maelys_datalog_query_result_t allow = {.derived = 0};
    maelys_datalog_decision_t decision = MAELYS_DATALOG_DECISION_ALLOW;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_decision_from_queries(&deny, &reduce, &allow, &decision), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DECISION_DENY_DEFAULT, decision, "%d");
    TEST_END();
}

static int file_contains_any(const char *path, const char *const *needles) {
    FILE *f = fopen(path, "rb");
    if (!f) return 1;
    char buf[32768];
    size_t n = fread(buf, 1, sizeof(buf) - 1u, f);
    fclose(f);
    buf[n] = '\0';
    for (size_t i = 0; needles[i]; i++) {
        if (strstr(buf, needles[i])) return 1;
    }
    return 0;
}

static int file_block_contains_any(const char *path,
                                   const char *begin_marker,
                                   const char *end_marker,
                                   const char *const *needles) {
    FILE *f = fopen(path, "rb");
    if (!f) return 1;
    char buf[65536];
    size_t n = fread(buf, 1, sizeof(buf) - 1u, f);
    fclose(f);
    buf[n] = '\0';
    char *begin = strstr(buf, begin_marker);
    if (!begin) return 1;
    char *end = strstr(begin, end_marker);
    if (!end) return 1;
    *end = '\0';
    for (size_t i = 0; needles[i]; i++) {
        if (strstr(begin, needles[i])) return 1;
    }
    return 0;
}

static int test_maelys_datalog_query_no_domain_leak(void) {
    TEST_BEGIN();
    char parent_domain[32];
    char parent_domains_fn[64];
    char parent_pbi_registry[64];
    char parent_context_registry[80];
    snprintf(parent_domain, sizeof(parent_domain), "%s_%s", "context", "projection");
    snprintf(parent_domains_fn, sizeof(parent_domains_fn), "%s_%s_%s", "maelys_datalog", "maelys", "domains");
    snprintf(parent_pbi_registry, sizeof(parent_pbi_registry), "%s_%s_%s", "maelys_datalog", "pbi", "registry");
    snprintf(parent_context_registry, sizeof(parent_context_registry), "%s_%s_%s_%s",
             "maelys_datalog", "context", "projection", "registry");
    const char *const forbidden[] = {
        parent_domain,
        parent_domains_fn,
        parent_pbi_registry,
        parent_context_registry,
        NULL
    };
    TEST_ASSERT_FALSE(file_contains_any("src/core/maelys_datalog_policy.h", forbidden));
    TEST_ASSERT_FALSE(file_contains_any("src/core/maelys_datalog_policy.c", forbidden));
    TEST_END();
}

static int test_maelys_datalog_duplicate_derivation_does_not_keep_changed_true(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r,
        "ancestor(X, Y) :- parent(X, Y).\nancestor(X, Y) :- parent(X, Y)."), "%d");
    maelys_datalog_fact_t facts[2];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 2, &r.symbols, &r.registry), "%d");
    add_parent(&r, &edb, "alice", "bob");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    maelys_datalog_term_t args[2] = {sym_term(&r, "alice"), sym_term(&r, "bob")};
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_query_solved_ground_fact(result, "ancestor", args, 2, &present),
                      "%d");
    TEST_ASSERT_TRUE(present);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_maelys_datalog_join_uses_predicate_range(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "allow(P) :- blocked(P), blocked(P)."), "%d");
    maelys_datalog_fact_t facts[16];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 16, &r.symbols, &r.registry), "%d");
    add_parent(&r, &edb, "alice", "bob");
    add_parent(&r, &edb, "bob", "carol");
    add_symbol_unary(&r, &edb, "blocked", "proj-1");
    add_symbol_unary(&r, &edb, "blocked", "proj-1");
    add_symbol_unary(&r, &edb, "user", "alice");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    size_t begin = 0, end = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_fact_set_predicate_range(&edb.fact_set, r.rules[0].body[0].atom.predicate_id, &begin, &end),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)1, end - begin, "%zu");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_symbol_unary(&r, result, "allow", "proj-1", &present), "%d");
    TEST_ASSERT_TRUE(present);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_maelys_datalog_cartesian_product_within_bounds(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "path(U, R) :- user(U), resource(R)."), "%d");
    maelys_datalog_fact_t facts[8];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 8, &r.symbols, &r.registry), "%d");
    add_symbol_unary(&r, &edb, "user", "alice");
    add_symbol_unary(&r, &edb, "user", "bob");
    add_symbol_unary(&r, &edb, "resource", "r1");
    add_symbol_unary(&r, &edb, "resource", "r2");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    static const char *const users[] = {"alice", "bob"};
    static const char *const resources[] = {"r1", "r2"};
    for (size_t u = 0; u < sizeof(users) / sizeof(users[0]); u++) {
        for (size_t q = 0; q < sizeof(resources) / sizeof(resources[0]); q++) {
            maelys_datalog_term_t args[2] = {sym_term(&r, users[u]), sym_term(&r, resources[q])};
            bool present = false;
            TEST_ASSERT_EQUAL(MAELYS_OK,
                              maelys_datalog_query_solved_ground_fact(result, "path", args, 2, &present),
                              "%d");
            TEST_ASSERT_TRUE(present);
        }
    }
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_maelys_datalog_cartesian_product_overflow_fails_closed(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "path(U, R) :- user(U), resource(R)."), "%d");
    maelys_datalog_fact_t facts[32];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 32, &r.symbols, &r.registry), "%d");
    char name[16];
    for (size_t i = 0; i < 9; i++) {
        snprintf(name, sizeof(name), "u%zu", i);
        add_symbol_unary(&r, &edb, "user", name);
        snprintf(name, sizeof(name), "r%zu", i);
        add_symbol_unary(&r, &edb, "resource", name);
    }
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    TEST_ASSERT_NULL(result);
    TEST_END();
}

static int test_datalog_solver_reads_policy_fact_from_ruleset(void) {
    TEST_BEGIN();
    const char *src =
        "allowed_backend_tuple(\"cli_pivot\", \"stdin\", \"text\").\n"
        "p(B) :- backend(B), backend_class(B, K), backend_channel(B, N), backend_format(B, F), allowed_backend_tuple(K, N, F).";
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, src), "%d");
    maelys_datalog_fact_t facts[8];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 8, &r.symbols, &r.registry), "%d");
    add_int_unary(&edb, "backend", 1);
    add_int_symbol_binary(&r, &edb, "backend_class", 1, "cli_pivot");
    add_int_symbol_binary(&r, &edb, "backend_channel", 1, "stdin");
    add_int_symbol_binary(&r, &edb, "backend_format", 1, "text");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(result, "p", 1, &present), "%d");
    TEST_ASSERT_TRUE(present);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_solver_policy_fact_idb_join(void) {
    TEST_BEGIN();
    const char *src =
        "allowed_backend_tuple(\"cli_pivot\", \"stdin\", \"text\").\n"
        "deny(B) :- backend_class(B, K), backend_channel(B, N), backend_format(B, F), allowed_backend_tuple(K, N, F).\n"
        "allow(B) :- backend(B), deny(B).\n"
        "p(B) :- allow(B).";
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, src), "%d");
    maelys_datalog_fact_t facts[8];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 8, &r.symbols, &r.registry), "%d");
    add_int_unary(&edb, "backend", 1);
    add_int_symbol_binary(&r, &edb, "backend_class", 1, "cli_pivot");
    add_int_symbol_binary(&r, &edb, "backend_channel", 1, "stdin");
    add_int_symbol_binary(&r, &edb, "backend_format", 1, "text");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(result, "p", 1, &present), "%d");
    TEST_ASSERT_TRUE(present);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_policy_fact_runtime_symbol_table_not_mutated(void) {
    TEST_BEGIN();
    const char *src =
        "allowed_backend_tuple(\"cli_pivot\", \"stdin\", \"text\").\n"
        "allow(B) :- backend(B), backend_class(B, K), backend_channel(B, N), backend_format(B, F), allowed_backend_tuple(K, N, F).";
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, src), "%d");
    size_t before = r.symbols.count;
    for (size_t i = 0; i < 3; i++) {
        maelys_datalog_fact_t facts[8];
        maelys_datalog_symbol_table_t local_symbols = r.symbols;
        maelys_datalog_edb_t edb;
        TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 8, &local_symbols, &r.registry), "%d");
        add_int_unary(&edb, "backend", 1);
        add_int_symbol_binary(&r, &edb, "backend_class", 1, "cli_pivot");
        add_int_symbol_binary(&r, &edb, "backend_channel", 1, "stdin");
        add_int_symbol_binary(&r, &edb, "backend_format", 1, "text");
        TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
        maelys_datalog_solve_result_t *result = NULL;
        TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
        TEST_ASSERT_EQUAL(before, r.symbols.count, "%zu");
        maelys_datalog_solve_result_free(result);
    }
    TEST_END();
}

static const char *backend_tuple_policy_src(void) {
    return "allowed_backend_tuple(\"cli_pivot\", \"stdin\", \"text\").\n"
           "allow(B) :- backend(B), backend_class(B, K), backend_channel(B, N), backend_format(B, F), allowed_backend_tuple(K, N, F).\n"
           "p(B) :- allow(B).";
}

static int solve_backend_tuple_present(const char *backend_class,
                                       const char *backend_channel,
                                       const char *backend_format,
                                       int *present) {
    maelys_datalog_ruleset_t r;
    if (make_ruleset(&r, backend_tuple_policy_src()) != MAELYS_OK) return 1;
    maelys_datalog_fact_t facts[8];
    maelys_datalog_edb_t edb;
    if (maelys_datalog_edb_init(&edb, facts, 8, &r.symbols, &r.registry) != MAELYS_OK) return 1;
    add_int_unary(&edb, "backend", 1);
    add_int_symbol_binary(&r, &edb, "backend_class", 1, backend_class);
    add_int_symbol_binary(&r, &edb, "backend_channel", 1, backend_channel);
    add_int_symbol_binary(&r, &edb, "backend_format", 1, backend_format);
    if (maelys_datalog_edb_finalize(&edb) != MAELYS_OK) return 1;
    maelys_datalog_solve_result_t *result = NULL;
    if (maelys_datalog_solve_once(&r, &edb, &result) != MAELYS_OK) return 1;
    bool found = false;
    int rc = query_solved_int_unary(result, "p", 1, &found);
    maelys_datalog_solve_result_free(result);
    if (rc != MAELYS_OK) return 1;
    *present = found ? 1 : 0;
    return 0;
}

static int test_datalog_allowed_backend_tuple_direct_policy_fact_accepted(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      make_ruleset(&r, "allowed_backend_tuple(\"cli_pivot\", \"stdin\", \"text\")."),
                      "%d");
    TEST_END();
}

static int test_datalog_allowed_backend_tuple_runtime_edb_rejected(void) {
    TEST_BEGIN();
    const char *src =
        "allowed_backend_tuple(\"cli_pivot\", \"stdin\", \"text\").\n"
        "p(B) :- backend(B), backend_class(B, K), backend_channel(B, N), backend_format(B, F), allowed_backend_tuple(K, N, F).";
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, src), "%d");
    maelys_datalog_fact_t facts[4];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 4, &r.symbols, &r.registry), "%d");
    maelys_datalog_term_t terms[3] = {
        sym_term(&r, "cli_pivot"),
        sym_term(&r, "stdin"),
        sym_term(&r, "text"),
    };
    TEST_ASSERT_EQUAL(MAELYS_ERR_FORBIDDEN,
                      maelys_datalog_edb_add_fact(&edb, "allowed_backend_tuple", terms, 3),
                      "%d");
    TEST_END();
}

static int test_datalog_allowed_backend_tuple_rule_head_rejected(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      make_ruleset(&r,
                                   "allowed_backend_tuple(K, N, F) :- backend_class(B, K), backend_channel(B, N), backend_format(B, F)."),
                      "%d");
    TEST_END();
}

static int test_datalog_allowed_backend_tuple_arity_mismatch_rejected(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      make_ruleset(&r, "allowed_backend_tuple(\"cli_pivot\", \"stdin\")."),
                      "%d");
    TEST_END();
}

static int test_datalog_allowed_backend_tuple_non_ground_direct_fact_rejected(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      make_ruleset(&r, "allowed_backend_tuple(\"cli_pivot\", \"stdin\", X)."),
                      "%d");
    TEST_END();
}

static int test_datalog_backend_runtime_facts_remain_edb(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, backend_tuple_policy_src()), "%d");
    static const char *const runtime_names[] = {
        "backend_class",
        "backend_channel",
        "backend_format",
    };
    for (size_t i = 0; i < sizeof(runtime_names) / sizeof(runtime_names[0]); i++) {
        maelys_datalog_predicate_id_t pid = 0;
        TEST_ASSERT_TRUE(maelys_datalog_predicate_registry_find(&r.registry, runtime_names[i], 2, &pid));
        const maelys_datalog_predicate_def_t *def =
            maelys_datalog_predicate_registry_get(&r.registry, pid);
        TEST_ASSERT_NOT_NULL(def);
        TEST_ASSERT_TRUE(def->kind_flags & MAELYS_DATALOG_PRED_KIND_EDB);
        TEST_ASSERT_FALSE(def->kind_flags & MAELYS_DATALOG_PRED_KIND_POLICY_FACT);
    }
    maelys_datalog_predicate_id_t tuple_pid = 0;
    TEST_ASSERT_TRUE(maelys_datalog_predicate_registry_find(&r.registry, "allowed_backend_tuple", 3,
                                                            &tuple_pid));
    const maelys_datalog_predicate_def_t *tuple_def =
        maelys_datalog_predicate_registry_get(&r.registry, tuple_pid);
    TEST_ASSERT_NOT_NULL(tuple_def);
    TEST_ASSERT_EQUAL((size_t)3u, tuple_def->arity, "%zu");
    TEST_ASSERT_TRUE(tuple_def->kind_flags & MAELYS_DATALOG_PRED_KIND_POLICY_FACT);
    TEST_ASSERT_FALSE(tuple_def->kind_flags & MAELYS_DATALOG_PRED_KIND_EDB);
    TEST_END();
}

static int test_datalog_allowed_backend_tuple_symbol_interning_exact_match(void) {
    TEST_BEGIN();
    int present = 0;
    TEST_ASSERT_EQUAL(0, solve_backend_tuple_present("cli_pivot", "stdin", "text", &present), "%d");
    TEST_ASSERT_TRUE(present);
    TEST_END();
}

static int test_datalog_allowed_backend_tuple_case_variant_denied(void) {
    TEST_BEGIN();
    int present = 1;
    TEST_ASSERT_EQUAL(0, solve_backend_tuple_present("Cli_Pivot", "stdin", "text", &present), "%d");
    TEST_ASSERT_FALSE(present);
    TEST_END();
}

static int test_datalog_allowed_backend_tuple_whitespace_variant_denied(void) {
    TEST_BEGIN();
    int present = 1;
    TEST_ASSERT_EQUAL(0, solve_backend_tuple_present("cli_pivot", " stdin", "text", &present), "%d");
    TEST_ASSERT_FALSE(present);
    TEST_ASSERT_EQUAL(0, solve_backend_tuple_present("cli_pivot", "stdin", "text ", &present), "%d");
    TEST_ASSERT_FALSE(present);
    TEST_END();
}

static int test_datalog_allowed_backend_tuple_format_variant_denied(void) {
    TEST_BEGIN();
    int present = 1;
    TEST_ASSERT_EQUAL(0, solve_backend_tuple_present("cli_pivot", "stdin", "json", &present), "%d");
    TEST_ASSERT_FALSE(present);
    TEST_ASSERT_EQUAL(0, solve_backend_tuple_present("cli_pivot", "stdin", "text!", &present), "%d");
    TEST_ASSERT_FALSE(present);
    TEST_END();
}

static int test_datalog_allowed_backend_tuple_no_cross_product_authorization(void) {
    TEST_BEGIN();
    int present = 1;
    TEST_ASSERT_EQUAL(0, solve_backend_tuple_present("acp", "stdin", "text", &present), "%d");
    TEST_ASSERT_FALSE(present);
    TEST_ASSERT_EQUAL(0, solve_backend_tuple_present("cli_pivot", "stdin", "json_rpc_payload", &present), "%d");
    TEST_ASSERT_FALSE(present);
    TEST_END();
}

static int test_datalog_wildcard_distinct_semantics(void) {
    TEST_BEGIN();
    const char *src =
        "p(X) :- q(X, _), r(X, _).\n"
        "p_same(X) :- q(X, Y), r(X, Y).";
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, src), "%d");
    maelys_datalog_fact_t facts[8];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 8, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    add_int_symbol_binary(&r, &edb, "r", 1, "b");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(result, "p", 1, &present), "%d");
    TEST_ASSERT_TRUE(present);
    present = true;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(result, "p_same", 1, &present), "%d");
    TEST_ASSERT_FALSE(present);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_wildcard_multiple_matches_deduplicate_result(void) {
    TEST_BEGIN();
    const char *src = "p(X) :- q(X, _), r(X, _).";
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, src), "%d");
    maelys_datalog_fact_t facts[8];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 8, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    add_int_symbol_binary(&r, &edb, "q", 1, "b");
    add_int_symbol_binary(&r, &edb, "r", 1, "c");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(result, "p", 1, &present), "%d");
    TEST_ASSERT_TRUE(present);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_wildcard_edge_two_positions_not_reflexive_only(void) {
    TEST_BEGIN();
    const char *src = "edge_seen(X) :- q(X, \"a\"), edge(_, _).";
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, src), "%d");
    maelys_datalog_fact_t facts[8];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 8, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    add_int_int_binary(&edb, "edge", 1, 2);
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(result, "edge_seen", 1, &present), "%d");
    TEST_ASSERT_TRUE(present);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_wildcard_solver_bindings_support_anonymous_capacity(void) {
    TEST_BEGIN();
    const char *src = "p(X) :- q(X, _), edge(_, _).";
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, src), "%d");
    TEST_ASSERT_TRUE(r.rules[0].body[0].atom.terms[1].as.variable >= MAELYS_DATALOG_NAMED_VARIABLE_COUNT);
    TEST_ASSERT_TRUE(r.rules[0].body[1].atom.terms[0].as.variable >= MAELYS_DATALOG_NAMED_VARIABLE_COUNT);
    maelys_datalog_fact_t facts[8];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 8, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    add_int_int_binary(&edb, "edge", 1, 2);
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(result, "p", 1, &present), "%d");
    TEST_ASSERT_TRUE(present);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_wildcard_never_interned(void) {
    TEST_BEGIN();
    const char *src = "p(X) :- q(X, _).";
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, src), "%d");
    maelys_datalog_fact_t facts[8];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 8, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    size_t before = r.symbols.count;
    for (size_t i = 0; i < 3; i++) {
        maelys_datalog_solve_result_t *result = NULL;
        TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
        bool present = false;
        TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(result, "p", 1, &present), "%d");
        TEST_ASSERT_TRUE(present);
        TEST_ASSERT_EQUAL(before, r.symbols.count, "%zu");
        maelys_datalog_solve_result_free(result);
    }
    TEST_END();
}

static int test_datalog_solve_once_success(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, _)."), "%d");
    maelys_datalog_fact_t facts[4];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 4, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    TEST_ASSERT_NOT_NULL(result);
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(result, "p", 1, &present), "%d");
    TEST_ASSERT_TRUE(present);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_solve_once_double_solve_rejected(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, _)."), "%d");
    maelys_datalog_fact_t facts[4];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 4, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_STATE, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_query_before_solve_rejected(void) {
    TEST_BEGIN();
    maelys_datalog_term_t term = int_term(1);
    bool present = true;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_query_solved_ground_fact(NULL, "p", &term, 1, &present),
                      "%d");
    TEST_END();
}

static int test_datalog_edb_mutation_after_solve_rejected(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, _)."), "%d");
    maelys_datalog_fact_t facts[4];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 4, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    maelys_datalog_term_t terms[2] = {int_term(2), sym_term(&r, "b")};
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_STATE, maelys_datalog_edb_add_fact(&edb, "q", terms, 2), "%d");
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_ruleset_not_mutated_by_solve_once(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, _)."), "%d");
    maelys_datalog_fact_t facts[4];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 4, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    size_t symbol_count = r.symbols.count;
    size_t rule_count = r.rule_count;
    size_t fact_count = r.fact_count;
    size_t registry_count = r.registry.count;
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    TEST_ASSERT_EQUAL(symbol_count, r.symbols.count, "%zu");
    TEST_ASSERT_EQUAL(rule_count, r.rule_count, "%zu");
    TEST_ASSERT_EQUAL(fact_count, r.fact_count, "%zu");
    TEST_ASSERT_EQUAL(registry_count, r.registry.count, "%zu");
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_solve_result_lifetime_contract_enforced(void) {
    TEST_BEGIN();
    maelys_datalog_solve_result_free(NULL);
    TEST_END();
}

static int test_datalog_solve_result_no_dangling_edb_pointer(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, _)."), "%d");
    maelys_datalog_fact_t facts[4];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 4, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    maelys_datalog_edb_clear(&edb);
    memset(facts, 0, sizeof(facts));
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(result, "p", 1, &present), "%d");
    TEST_ASSERT_TRUE(present);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_ruleset_shared_across_two_solves(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, _)."), "%d");
    maelys_datalog_fact_t facts1[4], facts2[4];
    maelys_datalog_edb_t edb1, edb2;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb1, facts1, 4, &r.symbols, &r.registry), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb2, facts2, 4, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb1, "q", 1, "a");
    add_int_symbol_binary(&r, &edb2, "q", 2, "b");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb1), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb2), "%d");
    maelys_datalog_solve_result_t *r1 = NULL;
    maelys_datalog_solve_result_t *r2 = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb1, &r1), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb2, &r2), "%d");
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(r1, "p", 1, &present), "%d");
    TEST_ASSERT_TRUE(present);
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(r1, "p", 2, &present), "%d");
    TEST_ASSERT_FALSE(present);
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(r2, "p", 2, &present), "%d");
    TEST_ASSERT_TRUE(present);
    maelys_datalog_solve_result_free(r1);
    maelys_datalog_solve_result_free(r2);
    TEST_END();
}

static int test_datalog_query_ground_fact_absent(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, _)."), "%d");
    maelys_datalog_fact_t facts[4];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 4, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    bool present = true;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(result, "p", 2, &present), "%d");
    TEST_ASSERT_FALSE(present);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_query_unknown_predicate_rejected(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, _)."), "%d");
    maelys_datalog_fact_t facts[1];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 1, &r.symbols, &r.registry), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    bool present = false;
    maelys_datalog_term_t term = int_term(1);
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_query_solved_ground_fact(result, "missing", &term, 1, &present),
                      "%d");
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_query_arity_mismatch_rejected(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, _)."), "%d");
    maelys_datalog_fact_t facts[1];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 1, &r.symbols, &r.registry), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_query_solved_ground_fact(result, "p", NULL, 0, &present),
                      "%d");
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_query_arity_above_max_rejected(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, _)."), "%d");
    maelys_datalog_fact_t facts[1];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 1, &r.symbols, &r.registry), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");

    maelys_datalog_term_t terms[MAELYS_DATALOG_MAX_ARITY + 1u];
    for (size_t i = 0; i < MAELYS_DATALOG_MAX_ARITY + 1u; i++) {
        terms[i] = int_term((long long)i);
    }
    bool present = true;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_query_solved_ground_fact(result,
                                                              "p",
                                                              terms,
                                                              MAELYS_DATALOG_MAX_ARITY + 1u,
                                                              &present),
                      "%d");
    TEST_ASSERT_FALSE(present);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_query_non_ground_rejected(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, _)."), "%d");
    maelys_datalog_fact_t facts[1];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 1, &r.symbols, &r.registry), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    maelys_datalog_term_t var = {.kind = MAELYS_DATALOG_TERM_VAR};
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_query_solved_ground_fact(result, "p", &var, 1, &present),
                      "%d");
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_query_does_not_sort_or_mutate(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, _)."), "%d");
    maelys_datalog_fact_t facts[4];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 4, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    for (size_t i = 0; i < 100; i++) {
        bool present = false;
        TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(result, "p", 1, &present), "%d");
        TEST_ASSERT_TRUE(present);
    }
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_solve_once_no_per_insert_memmove_required(void) {
    TEST_BEGIN();
    static const char *const forbidden[] = {"memmove", NULL};
    TEST_ASSERT_FALSE(file_contains_any("src/core/maelys_datalog_policy.c", forbidden));
    TEST_END();
}

static int test_datalog_solve_once_preserves_policy_fact_boundary(void) {
    TEST_BEGIN();
    const char *src =
        "allowed_backend_tuple(\"cli_pivot\", \"stdin\", \"text\").\n"
        "p(B) :- backend(B), backend_class(B, K), backend_channel(B, N), backend_format(B, F), allowed_backend_tuple(K, N, F).";
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, src), "%d");
    maelys_datalog_fact_t facts[8];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 8, &r.symbols, &r.registry), "%d");
    add_int_unary(&edb, "backend", 1);
    add_int_symbol_binary(&r, &edb, "backend_class", 1, "cli_pivot");
    add_int_symbol_binary(&r, &edb, "backend_channel", 1, "stdin");
    add_int_symbol_binary(&r, &edb, "backend_format", 1, "text");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(result, "p", 1, &present), "%d");
    TEST_ASSERT_TRUE(present);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_solve_once_failed_solve_cleanup(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "path(U, R) :- user(U), resource(R)."), "%d");
    maelys_datalog_fact_t facts[32];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 32, &r.symbols, &r.registry), "%d");
    char name[16];
    for (size_t i = 0; i < 9; i++) {
        snprintf(name, sizeof(name), "u%zu", i);
        add_symbol_unary(&r, &edb, "user", name);
        snprintf(name, sizeof(name), "r%zu", i);
        add_symbol_unary(&r, &edb, "resource", name);
    }
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    TEST_ASSERT_NULL(result);
    TEST_END();
}

static int test_datalog_solve_once_capacity_overflow_no_leak(void) {
    for (size_t i = 0; i < 100; i++) {
        int rc = test_datalog_solve_once_failed_solve_cleanup();
        if (rc != 0) return rc;
    }
    return 0;
}

static int test_datalog_solve_once_repeated_solve_query_free_no_leak(void) {
    for (size_t i = 0; i < 100; i++) {
        int rc = test_datalog_solve_once_success();
        if (rc != 0) return rc;
    }
    return 0;
}

static int test_datalog_solve_once_delta_bounds_invariants(void) {
    TEST_BEGIN();
    const char *src =
        "p(X) :- q(X, _).\n"
        "p(Y) :- p(X), edge(X, Y).";
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, src), "%d");
    maelys_datalog_fact_t facts[8];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 8, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 0, "a");
    add_int_int_binary(&edb, "edge", 0, 1);
    add_int_int_binary(&edb, "edge", 1, 2);
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(result, "p", 2, &present), "%d");
    TEST_ASSERT_TRUE(present);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_solve_once_max_depth_post_loop_failure_path(void) {
    TEST_BEGIN();
    const char *src =
        "p(X) :- q(X, _).\n"
        "p(Y) :- p(X), edge(X, Y).";
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, src), "%d");
    maelys_datalog_fact_t facts[16];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 16, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 0, "a");
    for (long long i = 0; i < 9; i++) add_int_int_binary(&edb, "edge", i, i + 1);
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    TEST_ASSERT_NULL(result);
    TEST_END();
}

typedef struct {
    maelys_result_t rc;
    int result_was_null;
} non_int_ordinal_case_result_t;

static non_int_ordinal_case_result_t run_non_int_ordinal_case(void) {
    non_int_ordinal_case_result_t outcome = {MAELYS_ERR_INTERNAL, 0};
    maelys_datalog_ruleset_t r;
    maelys_result_t rc = make_ruleset(&r, "p(X) :- q(X, _), X > 0.");
    if (rc != MAELYS_OK) {
        outcome.rc = rc;
        return outcome;
    }
    maelys_datalog_fact_t facts[2];
    maelys_datalog_edb_t edb;
    rc = maelys_datalog_edb_init(&edb, facts, 2, &r.symbols, &r.registry);
    if (rc != MAELYS_OK) {
        outcome.rc = rc;
        return outcome;
    }
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    maelys_datalog_term_t terms[2] = {sym_term(&r, "alice"), sym_term(&r, "b")};
    rc = maelys_datalog_edb_add_fact(&edb, "q", terms, 2);
    if (rc != MAELYS_OK) {
        outcome.rc = rc;
        return outcome;
    }
    rc = maelys_datalog_edb_finalize(&edb);
    if (rc != MAELYS_OK) {
        outcome.rc = rc;
        return outcome;
    }
    maelys_datalog_solve_result_t *result = NULL;
    outcome.rc = maelys_datalog_solve_once(&r, &edb, &result);
    outcome.result_was_null = result == NULL;
    if (result) maelys_datalog_solve_result_free(result);
    return outcome;
}

typedef struct {
    maelys_datalog_term_kind_t lhs_kind;
    maelys_datalog_cmp_op_t op;
    maelys_datalog_term_kind_t rhs_kind;
    int expect_accept;
} comparison_matrix_case_t;

static const char *comparison_kind_literal(maelys_datalog_term_kind_t kind, int rhs) {
    switch (kind) {
        case MAELYS_DATALOG_TERM_SYMBOL: return rhs ? "\"b\"" : "\"a\"";
        case MAELYS_DATALOG_TERM_INT: return rhs ? "2" : "1";
        case MAELYS_DATALOG_TERM_BOOL: return rhs ? "false" : "true";
        default: return "_";
    }
}

static const char *comparison_op_text(maelys_datalog_cmp_op_t op) {
    switch (op) {
        case MAELYS_DATALOG_CMP_EQ: return "=";
        case MAELYS_DATALOG_CMP_NEQ: return "!=";
        case MAELYS_DATALOG_CMP_LT: return "<";
        case MAELYS_DATALOG_CMP_LTE: return "<=";
        case MAELYS_DATALOG_CMP_GT: return ">";
        case MAELYS_DATALOG_CMP_GTE: return ">=";
        default: return "?";
    }
}

static int comparison_matrix_accepts(maelys_datalog_term_kind_t lhs_kind,
                                     maelys_datalog_cmp_op_t op,
                                     maelys_datalog_term_kind_t rhs_kind) {
    if (lhs_kind != rhs_kind) return 0;
    if (lhs_kind == MAELYS_DATALOG_TERM_INT) return 1;
    return op == MAELYS_DATALOG_CMP_EQ || op == MAELYS_DATALOG_CMP_NEQ;
}

static maelys_result_t solve_ground_comparison_present(const char *comparison,
                                                       bool *present) {
    char src[256];
    snprintf(src, sizeof(src), "p(X) :- q(X, _), %s.", comparison);
    maelys_datalog_ruleset_t r;
    maelys_result_t rc = make_ruleset(&r, src);
    if (rc != MAELYS_OK) return rc;
    maelys_datalog_fact_t facts[2];
    maelys_datalog_edb_t edb;
    rc = maelys_datalog_edb_init(&edb, facts, 2, &r.symbols, &r.registry);
    if (rc != MAELYS_OK) return rc;
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    rc = maelys_datalog_edb_finalize(&edb);
    if (rc != MAELYS_OK) return rc;
    maelys_datalog_solve_result_t *result = NULL;
    rc = maelys_datalog_solve_once(&r, &edb, &result);
    if (rc != MAELYS_OK) return rc;
    rc = query_solved_int_unary(result, "p", 1, present);
    maelys_datalog_solve_result_free(result);
    return rc;
}

static int test_comparison_parser_ground_matrix_54(void) {
    TEST_BEGIN();
    const maelys_datalog_term_kind_t kinds[] = {
        MAELYS_DATALOG_TERM_SYMBOL,
        MAELYS_DATALOG_TERM_INT,
        MAELYS_DATALOG_TERM_BOOL,
    };
    const maelys_datalog_cmp_op_t ops[] = {
        MAELYS_DATALOG_CMP_EQ,
        MAELYS_DATALOG_CMP_NEQ,
        MAELYS_DATALOG_CMP_LT,
        MAELYS_DATALOG_CMP_LTE,
        MAELYS_DATALOG_CMP_GT,
        MAELYS_DATALOG_CMP_GTE,
    };
    size_t observed = 0;
    size_t accepted = 0;
    size_t rejected = 0;
    for (size_t lhs = 0; lhs < sizeof(kinds) / sizeof(kinds[0]); lhs++) {
        for (size_t op = 0; op < sizeof(ops) / sizeof(ops[0]); op++) {
            for (size_t rhs = 0; rhs < sizeof(kinds) / sizeof(kinds[0]); rhs++) {
                comparison_matrix_case_t c = {
                    kinds[lhs],
                    ops[op],
                    kinds[rhs],
                    comparison_matrix_accepts(kinds[lhs], ops[op], kinds[rhs]),
                };
                char src[256];
                snprintf(src,
                         sizeof(src),
                         "allow(X) :- blocked(X), %s %s %s.",
                         comparison_kind_literal(c.lhs_kind, 0),
                         comparison_op_text(c.op),
                         comparison_kind_literal(c.rhs_kind, 1));
                maelys_datalog_diagnostic_t diag;
                memset(&diag, 0, sizeof(diag));
                maelys_result_t parse_rc = parse_solver_test_ruleset_ex(src, &diag);
                if (c.expect_accept) {
                    TEST_ASSERT_EQUAL(MAELYS_OK, parse_rc, "%d");
                    accepted++;
                } else {
                    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, parse_rc, "%d");
                    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_PARSER_INVALID_COMPARISON, diag.code, "%d");
                    rejected++;
                }
                observed++;
            }
        }
    }
    TEST_ASSERT_EQUAL((size_t)54u, observed, "%zu");
    TEST_ASSERT_EQUAL((size_t)10u, accepted, "%zu");
    TEST_ASSERT_EQUAL((size_t)44u, rejected, "%zu");
    TEST_END();
}

static int test_comparison_solver_ground_truth_values(void) {
    TEST_BEGIN();
    static const struct {
        const char *comparison;
        int expect_present;
    } cases[] = {
        {"\"a\" = \"a\"", 1},
        {"\"a\" = \"b\"", 0},
        {"\"a\" != \"a\"", 0},
        {"\"a\" != \"b\"", 1},
        {"1 = 1", 1},
        {"1 = 2", 0},
        {"1 != 1", 0},
        {"1 != 2", 1},
        {"1 < 2", 1},
        {"1 < 1", 0},
        {"1 <= 1", 1},
        {"2 > 1", 1},
        {"2 >= 2", 1},
        {"1 >= 2", 0},
        {"true = true", 1},
        {"true = false", 0},
        {"true != false", 1},
        {"false != false", 0},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        bool present = false;
        TEST_ASSERT_EQUAL(MAELYS_OK, solve_ground_comparison_present(cases[i].comparison, &present), "%d");
        TEST_ASSERT_EQUAL(cases[i].expect_present, present ? 1 : 0, "%d");
    }
    TEST_END();
}

static int test_comparison_solver_runtime_variables(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    maelys_datalog_fact_t facts[4];
    maelys_datalog_edb_t edb;
    maelys_datalog_solve_result_t *result = NULL;
    bool present = false;

    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, V), V >= 10."), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 4, &r.symbols, &r.registry), "%d");
    add_symbol_int_binary(&r, &edb, "q", "alice", 15);
    add_symbol_int_binary(&r, &edb, "q", "bob", 5);
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_symbol_unary(&r, result, "p", "alice", &present), "%d");
    TEST_ASSERT_TRUE(present);
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_symbol_unary(&r, result, "p", "bob", &present), "%d");
    TEST_ASSERT_FALSE(present);
    maelys_datalog_solve_result_free(result);

    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, N), N = \"alice\"."), "%d");
    result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 4, &r.symbols, &r.registry), "%d");
    add_symbol_symbol_binary(&r, &edb, "q", "user1", "alice");
    add_symbol_symbol_binary(&r, &edb, "q", "user2", "bob");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_symbol_unary(&r, result, "p", "user1", &present), "%d");
    TEST_ASSERT_TRUE(present);
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_symbol_unary(&r, result, "p", "user2", &present), "%d");
    TEST_ASSERT_FALSE(present);
    maelys_datalog_solve_result_free(result);

    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, F), F = true."), "%d");
    result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 4, &r.symbols, &r.registry), "%d");
    add_symbol_bool_binary(&r, &edb, "q", "user1", 1);
    add_symbol_bool_binary(&r, &edb, "q", "user2", 0);
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_symbol_unary(&r, result, "p", "user1", &present), "%d");
    TEST_ASSERT_TRUE(present);
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_symbol_unary(&r, result, "p", "user2", &present), "%d");
    TEST_ASSERT_FALSE(present);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_comparison_runtime_cross_type_deny(void) {
    TEST_BEGIN();
    static const struct {
        const char *src;
        maelys_datalog_cmp_op_t op;
    } cases[] = {
        {"p(X) :- q(X, V), V = 42.", MAELYS_DATALOG_CMP_EQ},
        {"p(X) :- q(X, V), V != 42.", MAELYS_DATALOG_CMP_NEQ},
        {"p(X) :- q(X, V), V < 10.", MAELYS_DATALOG_CMP_LT},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        maelys_datalog_ruleset_t r;
        TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, cases[i].src), "%d");
        maelys_datalog_fact_t facts[2];
        maelys_datalog_edb_t edb;
        TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 2, &r.symbols, &r.registry), "%d");
        add_symbol_symbol_binary(&r, &edb, "q", "alice", "notanint");
        TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
        maelys_datalog_solve_result_t *result = NULL;
        maelys_datalog_solve_diagnostic_t diag;
        TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                          maelys_datalog_solve_once_ex(&r, &edb, &result, &diag),
                          "%d");
        TEST_ASSERT_NULL(result);
        TEST_ASSERT_EQUAL(MAELYS_DATALOG_SOLVE_DIAG_COMPARISON_TYPE_ERROR, diag.category, "%d");
        TEST_ASSERT_EQUAL(MAELYS_DATALOG_DENY_COMPARISON_TYPE_ERROR, diag.failure_reason, "%d");
        TEST_ASSERT_EQUAL((uint8_t)MAELYS_DATALOG_TERM_SYMBOL, diag.lhs_kind, "%u");
        TEST_ASSERT_EQUAL((uint8_t)MAELYS_DATALOG_TERM_INT, diag.rhs_kind, "%u");
        TEST_ASSERT_EQUAL((uint8_t)cases[i].op, diag.comparison_op, "%u");
    }
    TEST_END();
}

static int test_comparison_symbol_table_immutable_at_runtime(void) {
    TEST_BEGIN();
    static const struct {
        const char *src;
        int symbol_symbol_edb;
        int expect_error;
    } cases[] = {
        {"p(X) :- q(X, _), \"alice\" = \"alice\".", 0, 0},
        {"p(X) :- q(X, _), \"alice\" = \"bob\".", 0, 0},
        {"p(X) :- q(X, _), \"alice\" != \"bob\".", 0, 0},
        {"p(X) :- q(X, _), 1 = 1.", 0, 0},
        {"p(X) :- q(X, _), 1 != 2.", 0, 0},
        {"p(X) :- q(X, _), 1 < 2.", 0, 0},
        {"p(X) :- q(X, _), 1 <= 1.", 0, 0},
        {"p(X) :- q(X, _), 2 > 1.", 0, 0},
        {"p(X) :- q(X, _), 2 >= 2.", 0, 0},
        {"p(X) :- q(X, _), true = true.", 0, 0},
        {"p(X) :- q(X, _), true != false.", 0, 0},
        {"p(X) :- q(X, V), V = 42.", 1, 1},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        maelys_datalog_ruleset_t r;
        TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, cases[i].src), "%d");
        maelys_datalog_fact_t facts[2];
        maelys_datalog_edb_t edb;
        TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 2, &r.symbols, &r.registry), "%d");
        if (cases[i].symbol_symbol_edb) {
            add_symbol_symbol_binary(&r, &edb, "q", "alice", "notanint");
        } else {
            add_int_symbol_binary(&r, &edb, "q", 1, "a");
        }
        TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
        size_t baseline = r.symbols.count;
        maelys_datalog_solve_result_t *result = NULL;
        maelys_datalog_solve_diagnostic_t diag;
        maelys_result_t solve_rc = maelys_datalog_solve_once_ex(&r, &edb, &result, &diag);
        if (cases[i].expect_error) {
            TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, solve_rc, "%d");
            TEST_ASSERT_NULL(result);
            TEST_ASSERT_EQUAL(MAELYS_DATALOG_DENY_COMPARISON_TYPE_ERROR, diag.failure_reason, "%d");
        } else {
            TEST_ASSERT_EQUAL(MAELYS_OK, solve_rc, "%d");
            TEST_ASSERT_NOT_NULL(result);
            maelys_datalog_solve_result_free(result);
        }
        TEST_ASSERT_EQUAL(baseline, r.symbols.count, "%zu");
    }
    TEST_END();
}

static int test_symbol_table_immutable_at_query_time(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, \"a\")."), "%d");

    maelys_datalog_fact_t facts[2];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 2, &r.symbols, &r.registry), "%d");
    add_symbol_symbol_binary(&r, &edb, "q", "alice", "a");
    maelys_datalog_term_t alice = sym_term(&r, "alice");
    maelys_datalog_term_t bob = sym_term(&r, "bob");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");

    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    TEST_ASSERT_NOT_NULL(result);

    size_t baseline = r.symbols.count;
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_query_solved_ground_fact(result, "p", &alice, 1u, &present),
                      "%d");
    TEST_ASSERT_TRUE(present);
    TEST_ASSERT_EQUAL(baseline, r.symbols.count, "%zu");

    present = true;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_query_solved_ground_fact(result, "p", &bob, 1u, &present),
                      "%d");
    TEST_ASSERT_FALSE(present);
    TEST_ASSERT_EQUAL(baseline, r.symbols.count, "%zu");

    present = true;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_query_solved_ground_fact(result,
                                                              "unknown_query_predicate",
                                                              &alice,
                                                              1u,
                                                              &present),
                      "%d");
    TEST_ASSERT_FALSE(present);
    TEST_ASSERT_EQUAL(baseline, r.symbols.count, "%zu");

    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_solve_once_eq_uses_term_equal(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, _), X = 1."), "%d");
    maelys_datalog_fact_t facts[4];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 4, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    add_int_symbol_binary(&r, &edb, "q", 2, "b");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(result, "p", 1, &present), "%d");
    TEST_ASSERT_TRUE(present);
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(result, "p", 2, &present), "%d");
    TEST_ASSERT_FALSE(present);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_solve_once_symbol_eq_uses_symbol_id(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, _), X = \"alice\"."), "%d");
    maelys_datalog_fact_t facts[4];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 4, &r.symbols, &r.registry), "%d");
    maelys_datalog_term_t alice[2] = {sym_term(&r, "alice"), sym_term(&r, "a")};
    maelys_datalog_term_t bob[2] = {sym_term(&r, "bob"), sym_term(&r, "b")};
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_add_fact(&edb, "q", alice, 2), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_add_fact(&edb, "q", bob, 2), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_symbol_unary(&r, result, "p", "alice", &present), "%d");
    TEST_ASSERT_TRUE(present);
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_symbol_unary(&r, result, "p", "bob", &present), "%d");
    TEST_ASSERT_FALSE(present);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_solve_once_int_ordinals(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, _), X > 1."), "%d");
    maelys_datalog_fact_t facts[4];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 4, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    add_int_symbol_binary(&r, &edb, "q", 2, "b");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(result, "p", 1, &present), "%d");
    TEST_ASSERT_FALSE(present);
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(result, "p", 2, &present), "%d");
    TEST_ASSERT_TRUE(present);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_solve_once_non_int_ordinal_classified(void) {
    TEST_BEGIN();
    non_int_ordinal_case_result_t outcome = run_non_int_ordinal_case();
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, outcome.rc, "%d");
    TEST_ASSERT_TRUE(outcome.result_was_null);
    TEST_ASSERT_TRUE(MAELYS_ERR_INVALID_FIELD != MAELYS_ERR_PAYLOAD_TOO_LARGE);
    TEST_END();
}

static int test_datalog_solve_once_unbound_comparison_variable_classified(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, _), Y = 1."), "%d");
    maelys_datalog_fact_t facts[2];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 2, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    TEST_ASSERT_NULL(result);
    TEST_END();
}

static int test_datalog_solve_once_comparison_skip_does_not_allow(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, _), X > 10."), "%d");
    maelys_datalog_fact_t facts[2];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 2, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    bool present = true;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(result, "p", 1, &present), "%d");
    TEST_ASSERT_FALSE(present);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_solve_once_non_int_ordinal_fails_closed(void) {
    TEST_BEGIN();
    non_int_ordinal_case_result_t outcome = run_non_int_ordinal_case();
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, outcome.rc, "%d");
    TEST_ASSERT_TRUE(outcome.result_was_null);
    bool invalid_comparison_returned_as_success = outcome.rc == MAELYS_OK;
    TEST_ASSERT_FALSE(invalid_comparison_returned_as_success);
    TEST_END();
}

static int test_datalog_solve_once_comparison_failure_reason_is_comparison_type_error(void) {
    TEST_BEGIN();
    TEST_ASSERT_TRUE(MAELYS_DATALOG_DENY_COMPARISON_TYPE_ERROR != MAELYS_DATALOG_DENY_POLICY_LOAD_ERROR);
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, _), X > 0."), "%d");
    maelys_datalog_fact_t facts[2];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 2, &r.symbols, &r.registry), "%d");
    maelys_datalog_term_t terms[2] = {sym_term(&r, "alice"), sym_term(&r, "b")};
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_add_fact(&edb, "q", terms, 2), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    TEST_ASSERT_NULL(result);
    TEST_END();
}

static int test_datalog_solve_once_unknown_operator_classified(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, _), X = 1."), "%d");
    r.rules[0].body[1].op = (maelys_datalog_cmp_op_t)99;
    maelys_datalog_fact_t facts[2];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 2, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    TEST_ASSERT_NULL(result);
    TEST_END();
}

static int test_datalog_solve_once_unknown_term_kind_classified(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, _), X = 1."), "%d");
    r.rules[0].body[1].rhs.kind = (maelys_datalog_term_kind_t)99;
    maelys_datalog_fact_t facts[2];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 2, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    TEST_ASSERT_NULL(result);
    TEST_END();
}

#define ASSERT_DIAG_CLEARED(diag_ptr) do { \
    TEST_ASSERT_EQUAL(MAELYS_OK, (diag_ptr)->failure_error, "%d"); \
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DENY_NONE, (diag_ptr)->failure_reason, "%d"); \
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_SOLVE_DIAG_NONE, (diag_ptr)->category, "%d"); \
    TEST_ASSERT_EQUAL(0u, (diag_ptr)->_pad[0], "%u"); \
    TEST_ASSERT_EQUAL(0u, (diag_ptr)->_pad[1], "%u"); \
} while (0)

static int test_solve_once_diag_none_on_success(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, _)."), "%d");
    maelys_datalog_fact_t facts[2];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 2, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    maelys_datalog_solve_diagnostic_t diag;
    memset(&diag, 0xA5, sizeof(diag));
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once_ex(&r, &edb, &result, &diag), "%d");
    TEST_ASSERT_NOT_NULL(result);
    ASSERT_DIAG_CLEARED(&diag);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_solve_once_diag_max_depth(void) {
    TEST_BEGIN();
    const char *src =
        "p(X) :- q(X, _).\n"
        "p(Y) :- p(X), edge(X, Y).";
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, src), "%d");
    maelys_datalog_fact_t facts[16];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 16, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 0, "a");
    for (long long i = 0; i < 9; i++) add_int_int_binary(&edb, "edge", i, i + 1);
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    maelys_datalog_solve_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE,
                      maelys_datalog_solve_once_ex(&r, &edb, &result, &diag),
                      "%d");
    TEST_ASSERT_NULL(result);
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_SOLVE_DIAG_MAX_DEPTH, diag.category, "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE, diag.failure_error, "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DENY_MAX_DEPTH, diag.failure_reason, "%d");
    TEST_ASSERT_EQUAL((uint16_t)MAELYS_DATALOG_MAX_DEPTH, diag.depth, "%u");
    TEST_ASSERT_EQUAL((uint16_t)MAELYS_DATALOG_MAX_DEPTH, diag.depth_limit, "%u");
    TEST_END();
}

static int test_solve_once_diag_idb_overflow(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "path(U, R) :- user(U), resource(R)."), "%d");
    maelys_datalog_fact_t facts[32];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 32, &r.symbols, &r.registry), "%d");
    char name[16];
    for (size_t i = 0; i < 9; i++) {
        snprintf(name, sizeof(name), "u%zu", i);
        add_symbol_unary(&r, &edb, "user", name);
        snprintf(name, sizeof(name), "r%zu", i);
        add_symbol_unary(&r, &edb, "resource", name);
    }
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    maelys_datalog_solve_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE,
                      maelys_datalog_solve_once_ex(&r, &edb, &result, &diag),
                      "%d");
    TEST_ASSERT_NULL(result);
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_SOLVE_DIAG_IDB_OVERFLOW, diag.category, "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE, diag.failure_error, "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DENY_IDB_OVERFLOW, diag.failure_reason, "%d");
    TEST_ASSERT_EQUAL((uint16_t)MAELYS_DATALOG_MAX_IDB_FACTS, diag.capacity, "%u");
    TEST_ASSERT_TRUE(diag.count_observed > 0u);
    TEST_END();
}

static int test_solve_once_diag_comparison_type_error(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, _), X > 0."), "%d");
    maelys_datalog_fact_t facts[2];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 2, &r.symbols, &r.registry), "%d");
    maelys_datalog_term_t terms[2] = {sym_term(&r, "alice"), sym_term(&r, "b")};
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_add_fact(&edb, "q", terms, 2), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    maelys_datalog_solve_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_solve_once_ex(&r, &edb, &result, &diag),
                      "%d");
    TEST_ASSERT_NULL(result);
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_SOLVE_DIAG_COMPARISON_TYPE_ERROR, diag.category, "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, diag.failure_error, "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DENY_COMPARISON_TYPE_ERROR, diag.failure_reason, "%d");
    TEST_ASSERT_EQUAL((uint8_t)MAELYS_DATALOG_TERM_SYMBOL, diag.lhs_kind, "%u");
    TEST_ASSERT_EQUAL((uint8_t)MAELYS_DATALOG_TERM_INT, diag.rhs_kind, "%u");
    TEST_ASSERT_EQUAL((uint8_t)MAELYS_DATALOG_CMP_GT, diag.comparison_op, "%u");
    TEST_END();
}

static int test_solve_once_diag_malformed_fact(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, _)."), "%d");
    r.rules[0].head.arity = 2;
    maelys_datalog_fact_t facts[2];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 2, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    maelys_datalog_solve_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_STATE,
                      maelys_datalog_solve_once_ex(&r, &edb, &result, &diag),
                      "%d");
    TEST_ASSERT_NULL(result);
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_SOLVE_DIAG_MALFORMED_FACT, diag.category, "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_STATE, diag.failure_error, "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DENY_POLICY_LOAD_ERROR, diag.failure_reason, "%d");
    TEST_ASSERT_EQUAL((uint8_t)1u, diag.arity_expected, "%u");
    TEST_ASSERT_EQUAL((uint8_t)2u, diag.arity_observed, "%u");
    TEST_END();
}

static int test_solve_once_diag_malformed_edb(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, _)."), "%d");
    maelys_datalog_fact_t facts[2];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 2, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    edb.fact_set.facts[0].arity = 1;
    maelys_datalog_solve_result_t *result = NULL;
    maelys_datalog_solve_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_STATE,
                      maelys_datalog_solve_once_ex(&r, &edb, &result, &diag),
                      "%d");
    TEST_ASSERT_NULL(result);
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_SOLVE_DIAG_MALFORMED_EDB, diag.category, "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_STATE, diag.failure_error, "%d");
    TEST_ASSERT_EQUAL((uint8_t)2u, diag.arity_expected, "%u");
    TEST_ASSERT_EQUAL((uint8_t)1u, diag.arity_observed, "%u");
    TEST_END();
}

static int test_solve_once_diag_invalid_state(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, _)."), "%d");
    maelys_datalog_fact_t facts[1];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 1, &r.symbols, &r.registry), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_diagnostic_t diag;
    maelys_datalog_solve_result_t *result = (maelys_datalog_solve_result_t *)&diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_STATE,
                      maelys_datalog_solve_once_ex(&r, &edb, &result, &diag),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_SOLVE_DIAG_INVALID_STATE, diag.category, "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_STATE, diag.failure_error, "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DENY_NONE, diag.failure_reason, "%d");
    TEST_END();
}

static int test_solve_once_diag_null_arg(void) {
    TEST_BEGIN();
    maelys_datalog_solve_diagnostic_t diag;
    memset(&diag, 0xA5, sizeof(diag));
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_solve_once_ex(NULL, NULL, NULL, &diag),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_SOLVE_DIAG_INVALID_ARGUMENT, diag.category, "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT, diag.failure_error, "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DENY_NONE, diag.failure_reason, "%d");
    TEST_ASSERT_EQUAL(0u, diag._pad[0], "%u");
    TEST_ASSERT_EQUAL(0u, diag._pad[1], "%u");
    TEST_END();
}

static int test_solve_once_diag_available_when_result_null(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, _), X > 0."), "%d");
    maelys_datalog_fact_t facts[2];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 2, &r.symbols, &r.registry), "%d");
    maelys_datalog_term_t terms[2] = {sym_term(&r, "alice"), sym_term(&r, "b")};
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_add_fact(&edb, "q", terms, 2), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    maelys_datalog_solve_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_solve_once_ex(&r, &edb, &result, &diag),
                      "%d");
    TEST_ASSERT_NULL(result);
    TEST_ASSERT_TRUE(diag.category != MAELYS_DATALOG_SOLVE_DIAG_NONE);
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, diag.failure_error, "%d");
    TEST_END();
}

static int test_solve_once_diag_cleared_on_entry(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, _)."), "%d");
    maelys_datalog_fact_t facts[1];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 1, &r.symbols, &r.registry), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    maelys_datalog_solve_diagnostic_t diag;
    memset(&diag, 0xFF, sizeof(diag));
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once_ex(&r, &edb, &result, &diag), "%d");
    TEST_ASSERT_NOT_NULL(result);
    ASSERT_DIAG_CLEARED(&diag);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_solve_once_diag_first_failure_preserved(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, _), X > 0, X = 1."), "%d");
    maelys_datalog_fact_t facts[2];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 2, &r.symbols, &r.registry), "%d");
    maelys_datalog_term_t terms[2] = {sym_term(&r, "alice"), sym_term(&r, "b")};
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_add_fact(&edb, "q", terms, 2), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    maelys_datalog_solve_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_solve_once_ex(&r, &edb, &result, &diag),
                      "%d");
    TEST_ASSERT_NULL(result);
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_SOLVE_DIAG_COMPARISON_TYPE_ERROR, diag.category, "%d");
    TEST_ASSERT_EQUAL((uint8_t)MAELYS_DATALOG_CMP_GT, diag.comparison_op, "%u");
    TEST_END();
}

static int test_solve_once_diag_no_raw_payload(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL_STRING("none",
                             maelys_datalog_solve_diagnostic_category_name(MAELYS_DATALOG_SOLVE_DIAG_NONE));
    TEST_ASSERT_EQUAL_STRING("internal_error",
                             maelys_datalog_solve_diagnostic_category_name(MAELYS_DATALOG_SOLVE_DIAG_INTERNAL_ERROR));
    TEST_ASSERT_TRUE(sizeof(maelys_datalog_solve_diagnostic_t) <= 40u);
    TEST_ASSERT_EQUAL((size_t)2u, sizeof(((maelys_datalog_solve_diagnostic_t *)0)->_pad), "%zu");
    TEST_END();
}

static int test_solve_once_diag_does_not_change_decision(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, _), X > 10."), "%d");
    maelys_datalog_fact_t facts[2];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 2, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    maelys_datalog_solve_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once_ex(&r, &edb, &result, &diag), "%d");
    TEST_ASSERT_NOT_NULL(result);
    ASSERT_DIAG_CLEARED(&diag);
    bool present = true;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(result, "p", 1, &present), "%d");
    TEST_ASSERT_FALSE(present);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_solve_once_compat_wrapper(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, _)."), "%d");
    maelys_datalog_fact_t facts[2];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 2, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    TEST_ASSERT_NOT_NULL(result);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_solve_once_null_diag_accepted(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, _), X > 0."), "%d");
    maelys_datalog_fact_t facts[2];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 2, &r.symbols, &r.registry), "%d");
    maelys_datalog_term_t terms[2] = {sym_term(&r, "alice"), sym_term(&r, "b")};
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_add_fact(&edb, "q", terms, 2), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_solve_once_ex(&r, &edb, &result, NULL),
                      "%d");
    TEST_ASSERT_NULL(result);
    TEST_END();
}

static int test_datalog_fact_equals_matches_fact_set_contains(void) {
    TEST_BEGIN();
    maelys_datalog_fact_t pool[2];
    maelys_datalog_fact_set_t set;
    maelys_datalog_fact_set_init(&set, pool, 2);
    pool[0].predicate_id = 1;
    pool[0].arity = 1;
    pool[0].terms[0].kind = MAELYS_DATALOG_TERM_INT;
    pool[0].terms[0].as.integer = 7;
    set.count = 1;
    set.sorted = 1;
    maelys_datalog_fact_t same = pool[0];
    maelys_datalog_fact_t other = pool[0];
    other.terms[0].as.integer = 8;
    TEST_ASSERT_TRUE(maelys_datalog_fact_equals(&pool[0], &same));
    TEST_ASSERT_TRUE(maelys_datalog_fact_set_contains(&set, &same));
    TEST_ASSERT_FALSE(maelys_datalog_fact_equals(&pool[0], &other));
    TEST_ASSERT_FALSE(maelys_datalog_fact_set_contains(&set, &other));
    TEST_END();
}

static int test_datalog_query_solved_ground_fact_uses_canonical_fact(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, _)."), "%d");
    maelys_datalog_fact_t facts[2];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 2, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 3, "a");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(result, "p", 3, &present), "%d");
    TEST_ASSERT_TRUE(present);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_factstore_rejects_structural_arity_mismatch(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, _)."), "%d");
    maelys_datalog_fact_t facts[2];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 2, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    edb.fact_set.facts[0].arity = 1;
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_STATE, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    TEST_ASSERT_NULL(result);
    TEST_END();
}

static int test_datalog_factstore_rejects_unknown_term_kind(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, _)."), "%d");
    maelys_datalog_fact_t facts[2];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 2, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    edb.fact_set.facts[0].terms[0].kind = (maelys_datalog_term_kind_t)99;
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_STATE, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    TEST_ASSERT_NULL(result);
    TEST_END();
}

static int test_datalog_factstore_rejects_var_in_ground_fact(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, _)."), "%d");
    maelys_datalog_fact_t facts[2];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 2, &r.symbols, &r.registry), "%d");
    maelys_datalog_term_t terms[2] = {{.kind = MAELYS_DATALOG_TERM_VAR}, sym_term(&r, "a")};
    terms[0].as.variable = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_add_fact(&edb, "q", terms, 2), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_STATE, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    TEST_ASSERT_NULL(result);
    TEST_END();
}

static int test_datalog_wildcard_not_query_wildcard(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, _)."), "%d");
    maelys_datalog_fact_t facts[2];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 2, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    maelys_datalog_term_t wildcard_like_query_term = {
        .kind = MAELYS_DATALOG_TERM_VAR,
        .as.variable = MAELYS_DATALOG_MAX_RULE_VARIABLES
    };
    bool present = true;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_query_solved_ground_fact(result,
                                                              "p",
                                                              &wildcard_like_query_term,
                                                              1,
                                                              &present),
                      "%d");
    TEST_ASSERT_FALSE(present);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_wildcard_not_comparison_operand(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, make_ruleset(&r, "p(X) :- q(X, _), _ = X."), "%d");
    TEST_END();
}

static int test_datalog_semi_naive_same_result_as_naive_recursive(void) {
    TEST_BEGIN();
    const char *src =
        "p(X) :- q(X, _).\n"
        "p(Y) :- p(X), edge(X, Y).";
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, src), "%d");
    maelys_datalog_fact_t facts[8];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 8, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    add_int_int_binary(&edb, "edge", 1, 2);
    add_int_int_binary(&edb, "edge", 2, 3);
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(result, "p", 3, &present), "%d");
    TEST_ASSERT_TRUE(present);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_semi_naive_same_result_as_naive_multi_literal(void) {
    TEST_BEGIN();
    const char *src = "p(X) :- q(X, _), r(X, _).";
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, src), "%d");
    maelys_datalog_fact_t facts[8];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 8, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    add_int_symbol_binary(&r, &edb, "r", 1, "b");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(result, "p", 1, &present), "%d");
    TEST_ASSERT_TRUE(present);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_semi_naive_same_result_as_naive_with_wildcard(void) {
    TEST_BEGIN();
    const char *src = "edge_seen(X) :- q(X, _), edge(_, _).";
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, src), "%d");
    maelys_datalog_fact_t facts[8];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 8, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    add_int_int_binary(&edb, "edge", 7, 8);
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(result, "edge_seen", 1, &present), "%d");
    TEST_ASSERT_TRUE(present);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_semi_naive_base_only_first_iteration(void) {
    TEST_BEGIN();
    const char *src = "base_only(X) :- q(X, _).";
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, src), "%d");
    maelys_datalog_fact_t facts[4];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 4, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(result, "base_only", 1, &present), "%d");
    TEST_ASSERT_TRUE(present);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_semi_naive_delta_empty_terminates(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, _)."), "%d");
    maelys_datalog_fact_t facts[1];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 1, &r.symbols, &r.registry), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    bool present = true;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(result, "p", 1, &present), "%d");
    TEST_ASSERT_FALSE(present);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_semi_naive_delta_contains_only_new_facts(void) {
    TEST_BEGIN();
    const char *src =
        "p(X) :- q(X, _).\n"
        "p(X) :- q(X, _).";
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, src), "%d");
    maelys_datalog_fact_t facts[4];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 4, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(result, "p", 1, &present), "%d");
    TEST_ASSERT_TRUE(present);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_semi_naive_new_facts_not_visible_until_next_iteration(void) {
    TEST_BEGIN();
    const char *src =
        "p(X) :- q(X, _).\n"
        "p(Y) :- p(X), edge(X, Y).";
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, src), "%d");
    maelys_datalog_fact_t facts[8];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 8, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    add_int_int_binary(&edb, "edge", 1, 2);
    add_int_int_binary(&edb, "edge", 2, 3);
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(result, "p", 3, &present), "%d");
    TEST_ASSERT_TRUE(present);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_semi_naive_scans_each_idb_literal_as_delta(void) {
    TEST_BEGIN();
    const char *src =
        "left(X) :- q(X, _).\n"
        "right(X) :- left(X).\n"
        "both(X) :- left(X), right(X).";
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, src), "%d");
    maelys_datalog_fact_t facts[4];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 4, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(result, "both", 1, &present), "%d");
    TEST_ASSERT_TRUE(present);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_semi_naive_delta_variants_bounded_by_body_literals(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL((size_t)8u, (size_t)MAELYS_DATALOG_MAX_BODY_LITERALS, "%zu");
    TEST_END();
}

static int test_datalog_semi_naive_positive_only_no_negation(void) {
    TEST_BEGIN();
    static const char *const forbidden[] = {"Tarjan", "SCC", "negative dependency", NULL};
    TEST_ASSERT_FALSE(file_contains_any("src/core/maelys_datalog_policy.c", forbidden));
    TEST_END();
}

static int test_datalog_static_join_order_preserves_simple_join(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- r(X, _), q(X, _)."), "%d");
    maelys_datalog_fact_t facts[8];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 8, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    add_int_symbol_binary(&r, &edb, "r", 1, "b");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *ordered = NULL;
    maelys_datalog_solve_result_t *legacy = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &ordered), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_test_solve_once_legacy_order(&r, &edb, &legacy, NULL), "%d");
    bool ordered_present = false;
    bool legacy_present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(ordered, "p", 1, &ordered_present), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(legacy, "p", 1, &legacy_present), "%d");
    TEST_ASSERT_TRUE(ordered_present);
    TEST_ASSERT_EQUAL(legacy_present, ordered_present, "%d");
    maelys_datalog_solve_result_free(ordered);
    maelys_datalog_solve_result_free(legacy);
    TEST_END();
}

static int test_datalog_static_join_order_preserves_recursive_rule(void) {
    TEST_BEGIN();
    const char *src =
        "p(X) :- q(X, _).\n"
        "p(Y) :- p(X), edge(X, Y).";
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, src), "%d");
    maelys_datalog_fact_t facts[8];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 8, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    add_int_int_binary(&edb, "edge", 1, 2);
    add_int_int_binary(&edb, "edge", 2, 3);
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *ordered = NULL;
    maelys_datalog_solve_result_t *legacy = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &ordered), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_test_solve_once_legacy_order(&r, &edb, &legacy, NULL), "%d");
    bool ordered_present = false;
    bool legacy_present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(ordered, "p", 3, &ordered_present), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(legacy, "p", 3, &legacy_present), "%d");
    TEST_ASSERT_TRUE(ordered_present);
    TEST_ASSERT_EQUAL(legacy_present, ordered_present, "%d");
    maelys_datalog_solve_result_free(ordered);
    maelys_datalog_solve_result_free(legacy);
    TEST_END();
}

static int test_datalog_static_join_order_delta_literal_is_root(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r,
        "p(X) :- q(X, _).\n"
        "p(Y) :- edge(X, Y), p(X)."), "%d");
    uint8_t order[MAELYS_DATALOG_MAX_BODY_LITERALS];
    uint8_t count = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_test_build_static_join_order(&r, &r.rules[1], 1, order, &count),
                      "%d");
    TEST_ASSERT_EQUAL((uint8_t)2u, count, "%u");
    TEST_ASSERT_EQUAL((uint8_t)1u, order[0], "%u");
    TEST_END();
}

static int test_datalog_static_join_order_preserves_semi_naive_delta_literal(void) {
    TEST_BEGIN();
    const char *src =
        "p(X) :- q(X, _).\n"
        "p(Y) :- edge(X, Y), p(X).";
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, src), "%d");
    uint8_t order[MAELYS_DATALOG_MAX_BODY_LITERALS];
    uint8_t count = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_test_build_static_join_order(&r, &r.rules[1], 1, order, &count),
                      "%d");
    TEST_ASSERT_EQUAL((uint8_t)1u, order[0], "%u");
    maelys_datalog_fact_t facts[8];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 8, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    add_int_int_binary(&edb, "edge", 1, 2);
    add_int_int_binary(&edb, "edge", 2, 3);
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(result, "p", 3, &present), "%d");
    TEST_ASSERT_TRUE(present);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_static_join_order_comparison_after_binding(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- X > 1, q(X, _)."), "%d");
    uint8_t order[MAELYS_DATALOG_MAX_BODY_LITERALS];
    uint8_t count = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_test_build_static_join_order(&r, &r.rules[0], -1, order, &count),
                      "%d");
    TEST_ASSERT_EQUAL((uint8_t)1u, order[0], "%u");
    TEST_ASSERT_EQUAL((uint8_t)0u, order[1], "%u");
    maelys_datalog_fact_t facts[4];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 4, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    add_int_symbol_binary(&r, &edb, "q", 2, "b");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(result, "p", 1, &present), "%d");
    TEST_ASSERT_FALSE(present);
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(result, "p", 2, &present), "%d");
    TEST_ASSERT_TRUE(present);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_static_join_order_orphan_comparison_fails_closed(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, _), Y = 1."), "%d");
    uint8_t order[MAELYS_DATALOG_MAX_BODY_LITERALS];
    uint8_t count = 0;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_test_build_static_join_order(&r, &r.rules[0], -1, order, &count),
                      "%d");
    maelys_datalog_fact_t facts[2];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 2, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    maelys_datalog_solve_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_solve_once_ex(&r, &edb, &result, &diag),
                      "%d");
    TEST_ASSERT_NULL(result);
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_SOLVE_DIAG_COMPARISON_TYPE_ERROR, diag.category, "%d");
    TEST_END();
}

static int test_datalog_static_join_order_deterministic_for_same_rule(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "path(U, R) :- user(U), resource(R)."), "%d");
    uint8_t a[MAELYS_DATALOG_MAX_BODY_LITERALS];
    uint8_t b[MAELYS_DATALOG_MAX_BODY_LITERALS];
    uint8_t ac = 0;
    uint8_t bc = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_test_build_static_join_order(&r, &r.rules[0], -1, a, &ac), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_test_build_static_join_order(&r, &r.rules[0], -1, b, &bc), "%d");
    TEST_ASSERT_EQUAL(ac, bc, "%u");
    for (uint8_t i = 0; i < ac; i++) TEST_ASSERT_EQUAL(a[i], b[i], "%u");
    TEST_END();
}

static int test_datalog_static_join_order_tie_breaks_by_original_body_index(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "path(U, R) :- user(U), resource(R)."), "%d");
    uint8_t order[MAELYS_DATALOG_MAX_BODY_LITERALS];
    uint8_t count = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_test_build_static_join_order(&r, &r.rules[0], -1, order, &count),
                      "%d");
    TEST_ASSERT_EQUAL((uint8_t)2u, count, "%u");
    TEST_ASSERT_EQUAL((uint8_t)0u, order[0], "%u");
    TEST_ASSERT_EQUAL((uint8_t)1u, order[1], "%u");
    TEST_END();
}

static int test_datalog_static_join_order_no_runtime_stats_dependency(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "path(U, R) :- user(U), resource(R)."), "%d");
    uint8_t before[MAELYS_DATALOG_MAX_BODY_LITERALS];
    uint8_t after[MAELYS_DATALOG_MAX_BODY_LITERALS];
    uint8_t before_count = 0;
    uint8_t after_count = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_test_build_static_join_order(&r, &r.rules[0], -1, before, &before_count),
                      "%d");
    maelys_datalog_fact_t facts[8];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 8, &r.symbols, &r.registry), "%d");
    add_symbol_unary(&r, &edb, "user", "alice");
    add_symbol_unary(&r, &edb, "user", "bob");
    add_symbol_unary(&r, &edb, "resource", "r1");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_test_build_static_join_order(&r, &r.rules[0], -1, after, &after_count),
                      "%d");
    TEST_ASSERT_EQUAL(before_count, after_count, "%u");
    for (uint8_t i = 0; i < before_count; i++) TEST_ASSERT_EQUAL(before[i], after[i], "%u");
    TEST_END();
}

static int test_datalog_static_join_order_preserves_deny_reduce_allow_precedence(void) {
    TEST_BEGIN();
    maelys_datalog_query_result_t deny = {.derived = 1};
    maelys_datalog_query_result_t reduce = {.derived = 1};
    maelys_datalog_query_result_t allow = {.derived = 1};
    maelys_datalog_decision_t decision = MAELYS_DATALOG_DECISION_ALLOW;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_decision_from_queries(&deny, &reduce, &allow, &decision), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DECISION_DENY_CONFLICT, decision, "%d");
    deny.derived = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_decision_from_queries(&deny, &reduce, &allow, &decision), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DECISION_REDUCED, decision, "%d");
    reduce.derived = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_decision_from_queries(&deny, &reduce, &allow, &decision), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DECISION_ALLOW, decision, "%d");
    TEST_END();
}

static int test_datalog_static_join_order_failure_is_fail_closed(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- Y = 1, q(X, _)."), "%d");
    maelys_datalog_fact_t facts[2];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 2, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    maelys_datalog_solve_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_solve_once_ex(&r, &edb, &result, &diag),
                      "%d");
    TEST_ASSERT_NULL(result);
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DENY_COMPARISON_TYPE_ERROR, diag.failure_reason, "%d");
    TEST_END();
}

static int test_datalog_static_join_order_greedy_no_recursion_no_heap(void) {
    TEST_BEGIN();
    static const char *const forbidden[] = {
        "malloc(",
        "calloc(",
        "realloc(",
        "strdup(",
        "asprintf(",
        "vasprintf(",
        NULL
    };
    TEST_ASSERT_FALSE(file_block_contains_any("src/core/maelys_datalog_policy.c",
                                              "static maelys_result_t build_static_join_order",
                                              "/* Recursion depth is bounded",
                                              forbidden));
    TEST_ASSERT_TRUE(MAELYS_DATALOG_MAX_BODY_LITERALS <= 64u);
    TEST_ASSERT_TRUE(MAELYS_DATALOG_MAX_RULE_VARIABLES <= 64u);
    TEST_END();
}

static int test_datalog_static_join_order_future_negated_literal_classification_documented(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_LITERAL_ATOM, MAELYS_DATALOG_LITERAL_ATOM, "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_LITERAL_COMPARISON, MAELYS_DATALOG_LITERAL_COMPARISON, "%d");
    TEST_ASSERT_EQUAL(3, MAELYS_DATALOG_LITERAL_NEGATED_ATOM, "%d");
    static const char *const forbidden[] = {"KIND_NEGATED", "negative_edge", NULL};
    TEST_ASSERT_FALSE(file_contains_any("src/core/maelys_datalog_policy.h", forbidden));
    TEST_END();
}

static int test_datalog_static_join_order_differential_source_vs_ordered(void) {
    TEST_BEGIN();
    const char *src =
        "p(X) :- q(X, _).\n"
        "p(Y) :- p(X), edge(X, Y).\n"
        "p(X) :- r(X, _), q(X, _).";
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, src), "%d");
    maelys_datalog_fact_t facts[8];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 8, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    add_int_symbol_binary(&r, &edb, "r", 1, "b");
    add_int_int_binary(&edb, "edge", 1, 2);
    add_int_int_binary(&edb, "edge", 2, 3);
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *ordered = NULL;
    maelys_datalog_solve_result_t *legacy = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &ordered), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_test_solve_once_legacy_order(&r, &edb, &legacy, NULL), "%d");
    const maelys_datalog_fact_t *ordered_facts = NULL;
    const maelys_datalog_fact_t *legacy_facts = NULL;
    size_t ordered_count = 0;
    size_t legacy_count = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_test_solve_result_idb_facts(ordered, &ordered_facts, &ordered_count),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_test_solve_result_idb_facts(legacy, &legacy_facts, &legacy_count),
                      "%d");
    TEST_ASSERT_EQUAL(legacy_count, ordered_count, "%zu");
    for (size_t i = 0; i < legacy_count; i++) {
        TEST_ASSERT_TRUE(fact_array_contains_semantic(ordered_facts, ordered_count, &legacy_facts[i]));
    }
    for (size_t i = 0; i < ordered_count; i++) {
        TEST_ASSERT_TRUE(fact_array_contains_semantic(legacy_facts, legacy_count, &ordered_facts[i]));
    }
    size_t ordered_query_hits = 0;
    size_t legacy_query_hits = 0;
    for (long long value = 1; value <= 4; value++) {
        bool ordered_present = false;
        bool legacy_present = false;
        TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(ordered, "p", value, &ordered_present), "%d");
        TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(legacy, "p", value, &legacy_present), "%d");
        TEST_ASSERT_EQUAL(legacy_present, ordered_present, "%d");
        if (ordered_present) ordered_query_hits++;
        if (legacy_present) legacy_query_hits++;
    }
    TEST_ASSERT_EQUAL(legacy_query_hits, ordered_query_hits, "%zu");
    maelys_datalog_solve_result_free(ordered);
    maelys_datalog_solve_result_free(legacy);
    TEST_END();
}

static int test_datalog_negation_simple_deny(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, \"a\"), not(r(X, \"a\"))."), "%d");
    TEST_ASSERT_TRUE(r.negation_supported);
    maelys_datalog_fact_t facts[8];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 8, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    add_int_symbol_binary(&r, &edb, "q", 2, "a");
    add_int_symbol_binary(&r, &edb, "r", 2, "a");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(result, "p", 1, &present), "%d");
    TEST_ASSERT_TRUE(present);
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(result, "p", 2, &present), "%d");
    TEST_ASSERT_FALSE(present);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_negation_positive_path_unchanged(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- q(X, _)."), "%d");
    TEST_ASSERT_FALSE(r.negation_supported);
    TEST_ASSERT_FALSE(r.negation_recursion_supported);
    TEST_ASSERT_FALSE(r.strata_assigned);
    TEST_ASSERT_EQUAL((uint32_t)0u, r.max_stratum, "%u");
    TEST_END();
}

static int test_datalog_negation_stratified_positive_subset_equivalence(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t positive;
    maelys_datalog_ruleset_t stratified;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&positive, "p(X) :- q(X, \"a\")."), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&stratified, "p(X) :- q(X, \"a\"), not(r(X, \"a\"))."), "%d");
    maelys_datalog_fact_t positive_facts_pool[8];
    maelys_datalog_fact_t stratified_facts_pool[8];
    maelys_datalog_edb_t positive_edb;
    maelys_datalog_edb_t stratified_edb;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_edb_init(&positive_edb, positive_facts_pool, 8, &positive.symbols, &positive.registry),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_edb_init(&stratified_edb, stratified_facts_pool, 8, &stratified.symbols, &stratified.registry),
                      "%d");
    add_int_symbol_binary(&positive, &positive_edb, "q", 1, "a");
    add_int_symbol_binary(&positive, &positive_edb, "q", 2, "a");
    add_int_symbol_binary(&stratified, &stratified_edb, "q", 1, "a");
    add_int_symbol_binary(&stratified, &stratified_edb, "q", 2, "a");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&positive_edb), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&stratified_edb), "%d");
    maelys_datalog_solve_result_t *positive_result = NULL;
    maelys_datalog_solve_result_t *stratified_result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&positive, &positive_edb, &positive_result), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&stratified, &stratified_edb, &stratified_result), "%d");
    const maelys_datalog_fact_t *positive_facts = NULL;
    const maelys_datalog_fact_t *stratified_facts = NULL;
    size_t positive_count = 0;
    size_t stratified_count = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_test_solve_result_idb_facts(positive_result, &positive_facts, &positive_count),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_test_solve_result_idb_facts(stratified_result, &stratified_facts, &stratified_count),
                      "%d");
    TEST_ASSERT_EQUAL(positive_count, stratified_count, "%zu");
    for (size_t i = 0; i < positive_count; i++) {
        TEST_ASSERT_TRUE(fact_array_contains_semantic(stratified_facts, stratified_count, &positive_facts[i]));
    }
    for (long long value = 1; value <= 2; value++) {
        bool positive_present = false;
        bool stratified_present = false;
        TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(positive_result, "p", value, &positive_present), "%d");
        TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(stratified_result, "p", value, &stratified_present), "%d");
        TEST_ASSERT_EQUAL(positive_present, stratified_present, "%d");
    }
    maelys_datalog_solve_result_free(positive_result);
    maelys_datalog_solve_result_free(stratified_result);
    TEST_END();
}

static int test_datalog_negation_two_strata_policy(void) {
    TEST_BEGIN();
    const char *src =
        "left(X) :- r(X, \"a\").\n"
        "right(X) :- q(X, \"a\"), not(left(X)).";
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, src), "%d");
    maelys_datalog_predicate_id_t left_id = 0;
    maelys_datalog_predicate_id_t right_id = 0;
    TEST_ASSERT_TRUE(maelys_datalog_predicate_registry_find(&r.registry, "left", 1, &left_id));
    TEST_ASSERT_TRUE(maelys_datalog_predicate_registry_find(&r.registry, "right", 1, &right_id));
    TEST_ASSERT_EQUAL((uint32_t)0u, r.strata[left_id], "%u");
    TEST_ASSERT_EQUAL((uint32_t)1u, r.strata[right_id], "%u");
    maelys_datalog_fact_t facts[8];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 8, &r.symbols, &r.registry), "%d");
    add_int_symbol_binary(&r, &edb, "q", 1, "a");
    add_int_symbol_binary(&r, &edb, "q", 2, "a");
    add_int_symbol_binary(&r, &edb, "r", 2, "a");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(result, "left", 2, &present), "%d");
    TEST_ASSERT_TRUE(present);
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(result, "right", 1, &present), "%d");
    TEST_ASSERT_TRUE(present);
    TEST_ASSERT_EQUAL(MAELYS_OK, query_solved_int_unary(result, "right", 2, &present), "%d");
    TEST_ASSERT_FALSE(present);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_negation_decision_policy_compatible(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    memset(&r, 0, sizeof(r));
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_ruleset_init(&r, "policy.decision", "decision",
                                                  "0000000000000000000000000000000000000000000000000000000000000000",
                                                  1),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_example_domains_install(), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_domain_registry_install("decision", &r.registry), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_predicate_registry_add_atom(&r.registry, "request"), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_predicate_registry_freeze(&r.registry), "%d");
    const char *src =
        "deny(R) :- blocked(R), not(safe(R)).";
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_parse_ruleset(&r, src, strlen(src)), "%d");
    maelys_datalog_fact_t facts[4];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 4, &r.symbols, &r.registry), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_add_symbol_fact(&edb, "blocked", "request"), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    bool present = false;
    maelys_datalog_term_t request = sym_term(&r, "request");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_query_solved_ground_fact(result,
                                                              "deny",
                                                              &request,
                                                              1,
                                                              &present),
                      "%d");
    TEST_ASSERT_TRUE(present);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_negation_negated_atom_not_in_bound_var_mask(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, "p(X) :- not(r(X, \"a\")), q(X, \"a\")."), "%d");
    uint8_t order[MAELYS_DATALOG_MAX_BODY_LITERALS];
    uint8_t count = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_test_build_static_join_order(&r, &r.rules[0], -1, order, &count),
                      "%d");
    TEST_ASSERT_EQUAL((uint8_t)2u, count, "%u");
    TEST_ASSERT_EQUAL((uint8_t)1u, order[0], "%u");
    TEST_ASSERT_EQUAL((uint8_t)0u, order[1], "%u");
    TEST_END();
}

static int test_datalog_proof_node_size_expected_bound(void) {
    TEST_BEGIN();
    TEST_ASSERT_TRUE(sizeof(maelys_datalog_proof_node_t) <= 112u);
    TEST_ASSERT_TRUE(sizeof(maelys_datalog_proof_tree_t) <= 8192u);
    TEST_END();
}

static int test_datalog_proof_node_has_derived_fact(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    maelys_datalog_fact_t facts[8];
    maelys_datalog_edb_t edb;
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_TRUE(solve_ancestor_chain(&r, &edb, facts, &result));
    const maelys_datalog_proof_tree_t *proof = maelys_datalog_solve_result_proof(result);
    TEST_ASSERT_NOT_NULL(proof);
    TEST_ASSERT_TRUE(proof->node_count > 0);
    for (size_t i = 0; i < proof->node_count; i++) {
        if (proof->nodes[i].deny_reason != MAELYS_DATALOG_DENY_NONE) continue;
        TEST_ASSERT_EQUAL(proof->nodes[i].predicate_id,
                          proof->nodes[i].derived_fact.predicate_id,
                          "%u");
        TEST_ASSERT_FALSE(fact_has_var(&proof->nodes[i].derived_fact));
    }
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_proof_parent_index_coherent(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    maelys_datalog_fact_t facts[8];
    maelys_datalog_edb_t edb;
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_TRUE(solve_ancestor_chain(&r, &edb, facts, &result));
    const maelys_datalog_proof_tree_t *proof = maelys_datalog_solve_result_proof(result);
    TEST_ASSERT_NOT_NULL(proof);
    int saw_parent = 0;
    for (size_t i = 0; i < proof->node_count; i++) {
        if (proof->nodes[i].parent_index == MAELYS_DATALOG_PROOF_NO_PARENT) continue;
        saw_parent = 1;
        TEST_ASSERT_TRUE(proof->nodes[i].parent_index < proof->node_count);
    }
    TEST_ASSERT_TRUE(saw_parent);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_proof_depth_consistent_with_parent(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    maelys_datalog_fact_t facts[8];
    maelys_datalog_edb_t edb;
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_TRUE(solve_ancestor_chain(&r, &edb, facts, &result));
    const maelys_datalog_proof_tree_t *proof = maelys_datalog_solve_result_proof(result);
    TEST_ASSERT_NOT_NULL(proof);
    for (size_t i = 0; i < proof->node_count; i++) {
        uint16_t parent = proof->nodes[i].parent_index;
        if (parent == MAELYS_DATALOG_PROOF_NO_PARENT) continue;
        TEST_ASSERT_TRUE(parent < proof->node_count);
        TEST_ASSERT_EQUAL(proof->nodes[parent].depth + 1u, proof->nodes[i].depth, "%zu");
    }
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_proof_extract_for_queried_fact(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    maelys_datalog_fact_t facts[8];
    maelys_datalog_edb_t edb;
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_TRUE(solve_ancestor_chain(&r, &edb, facts, &result));
    maelys_datalog_fact_t target;
    TEST_ASSERT_TRUE(make_fact2(&r, "ancestor", "alice", "dave", &target));
    maelys_datalog_proof_tree_t out;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_extract_proof_for_fact(result, &target, &out), "%d");
    TEST_ASSERT_TRUE(out.node_count >= 1);
    TEST_ASSERT_TRUE(maelys_datalog_fact_equals(&out.nodes[out.node_count - 1u].derived_fact, &target));
    for (size_t i = 0; i < out.node_count; i++) {
        if (out.nodes[i].parent_index == MAELYS_DATALOG_PROOF_NO_PARENT) continue;
        TEST_ASSERT_TRUE(out.nodes[i].parent_index < i);
    }
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_proof_extract_returns_empty_for_absent_fact(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    maelys_datalog_fact_t facts[8];
    maelys_datalog_edb_t edb;
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_TRUE(solve_ancestor_chain(&r, &edb, facts, &result));
    maelys_datalog_fact_t target;
    TEST_ASSERT_TRUE(make_fact2(&r, "ancestor", "dave", "alice", &target));
    maelys_datalog_proof_tree_t out;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_extract_proof_for_fact(result, &target, &out), "%d");
    TEST_ASSERT_EQUAL((size_t)0u, out.node_count, "%zu");
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_proof_solve_result_proof_accessor(void) {
    TEST_BEGIN();
    TEST_ASSERT_NULL(maelys_datalog_solve_result_proof(NULL));
    maelys_datalog_ruleset_t r;
    maelys_datalog_fact_t facts[8];
    maelys_datalog_edb_t edb;
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_TRUE(solve_ancestor_chain(&r, &edb, facts, &result));
    const maelys_datalog_proof_tree_t *proof = maelys_datalog_solve_result_proof(result);
    TEST_ASSERT_NOT_NULL(proof);
    TEST_ASSERT_TRUE(proof->node_count > 0);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_proof_truncation_recorded(void) {
    TEST_BEGIN();
    const char *src =
        "p(X) :- user(X).\n"
        "p_same(X) :- user(X).\n"
        "edge_seen(X) :- user(X).\n"
        "allow(X) :- user(X).\n"
        "deny(X) :- user(X).\n"
        "left(X) :- user(X).\n"
        "right(X) :- user(X).\n"
        "both(X) :- user(X).\n"
        "base_only(X) :- user(X).";
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&r, src), "%d");
    maelys_datalog_fact_t facts[16];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 16, &r.symbols, &r.registry), "%d");
    static const char *const users[] = {"alice", "bob", "carol", "dave", "r1", "r2", "a", "b", "c"};
    for (size_t i = 0; i < sizeof(users) / sizeof(users[0]); i++) {
        add_symbol_unary(&r, &edb, "user", users[i]);
    }
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    const maelys_datalog_proof_tree_t *proof = maelys_datalog_solve_result_proof(result);
    TEST_ASSERT_NOT_NULL(proof);
    TEST_ASSERT_EQUAL((size_t)MAELYS_DATALOG_MAX_PROOF_NODES, proof->node_count, "%zu");
    TEST_ASSERT_TRUE(proof->truncated);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_proof_failure_node_zero_initialized(void) {
    TEST_BEGIN();
    maelys_datalog_proof_tree_t proof;
    maelys_datalog_proof_init(&proof, "policy.test", "00", 0);
    maelys_datalog_proof_add(&proof,
                              0,
                              0,
                              NULL,
                              MAELYS_DATALOG_DENY_MAX_DEPTH,
                              1,
                              MAELYS_DATALOG_PROOF_NO_PARENT);
    TEST_ASSERT_EQUAL((size_t)1u, proof.node_count, "%zu");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DENY_MAX_DEPTH, proof.nodes[0].deny_reason, "%d");
    TEST_ASSERT_EQUAL((uint16_t)MAELYS_DATALOG_PROOF_NO_PARENT, proof.nodes[0].parent_index, "%u");
    TEST_ASSERT_EQUAL((maelys_datalog_predicate_id_t)0, proof.nodes[0].derived_fact.predicate_id, "%u");
    TEST_ASSERT_EQUAL((uint8_t)0u, proof.nodes[0].derived_fact.arity, "%u");
    TEST_END();
}

static int test_datalog_proof_idb_proof_index_survives_stratum_sort(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      make_ruleset(&r,
                                   "left(X) :- user(X).\n"
                                   "right(X) :- user(X), not(left(\"proj-1\"))."),
                      "%d");
    maelys_datalog_fact_t facts[8];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 8, &r.symbols, &r.registry), "%d");
    add_symbol_unary(&r, &edb, "user", "carol");
    add_symbol_unary(&r, &edb, "user", "alice");
    add_symbol_unary(&r, &edb, "user", "bob");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&r, &edb, &result), "%d");
    const maelys_datalog_fact_t *idb = NULL;
    const uint16_t *indices = NULL;
    size_t fact_count = 0;
    size_t index_count = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_test_solve_result_idb_facts(result, &idb, &fact_count), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_test_solve_result_idb_proof_indices(result, &indices, &index_count),
                      "%d");
    TEST_ASSERT_EQUAL(fact_count, index_count, "%zu");
    const maelys_datalog_proof_tree_t *proof = maelys_datalog_solve_result_proof(result);
    TEST_ASSERT_NOT_NULL(proof);
    for (size_t i = 0; i < fact_count; i++) {
        if (indices[i] == MAELYS_DATALOG_PROOF_NO_PARENT) continue;
        TEST_ASSERT_TRUE(indices[i] < proof->node_count);
        TEST_ASSERT_TRUE(maelys_datalog_fact_equals(&proof->nodes[indices[i]].derived_fact, &idb[i]));
    }
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_proof_extract_remaps_parent_indices(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    maelys_datalog_fact_t facts[8];
    maelys_datalog_edb_t edb;
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_TRUE(solve_ancestor_chain(&r, &edb, facts, &result));
    maelys_datalog_fact_t target;
    TEST_ASSERT_TRUE(make_fact2(&r, "ancestor", "alice", "dave", &target));
    maelys_datalog_proof_tree_t out;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_extract_proof_for_fact(result, &target, &out), "%d");
    TEST_ASSERT_TRUE(out.node_count >= 2);
    int saw_non_root = 0;
    for (size_t i = 0; i < out.node_count; i++) {
        uint16_t parent = out.nodes[i].parent_index;
        if (parent == MAELYS_DATALOG_PROOF_NO_PARENT) continue;
        saw_non_root = 1;
        TEST_ASSERT_TRUE(parent < out.node_count);
        TEST_ASSERT_TRUE(parent < i);
    }
    TEST_ASSERT_TRUE(saw_non_root);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_datalog_proof_tree_single_node_still_bounded(void) {
    TEST_BEGIN();
    TEST_ASSERT_TRUE(sizeof(maelys_datalog_proof_tree_t) <= 8192u);
    TEST_ASSERT_TRUE(sizeof(maelys_datalog_proof_node_t) <= 128u);
    TEST_END();
}

int main(int argc, char **argv) {
    test_case_t cases[] = {
        {"maelys_datalog_solver/simple_and_query", TEST_MODE_NON_BLOCKING, test_solver_simple_and_query},
        {"maelys_datalog_solver/positive_recursion_transitive_closure", TEST_MODE_NON_BLOCKING, test_solver_positive_recursion_transitive_closure},
        {"maelys_datalog_solver/duplicate_and_overflow_bounds", TEST_MODE_NON_BLOCKING, test_solver_duplicate_and_overflow_bounds},
        {"maelys_datalog_solver/query_and_conflict_helper", TEST_MODE_NON_BLOCKING, test_solver_query_and_conflict_helper},
        {"maelys_datalog_solver/query_rejects_variable_args", TEST_MODE_NON_BLOCKING, test_maelys_datalog_query_rejects_variable_args},
        {"maelys_datalog_solver/query_not_derived_sets_deny_default", TEST_MODE_NON_BLOCKING, test_maelys_datalog_query_not_derived_sets_deny_default},
        {"maelys_datalog_solver/decision_name_constants", TEST_MODE_NON_BLOCKING, test_maelys_datalog_decision_name_constants},
        {"maelys_datalog_solver/decision_from_queries_deny_conflict", TEST_MODE_NON_BLOCKING, test_maelys_datalog_decision_from_queries_deny_conflict},
        {"maelys_datalog_solver/decision_from_queries_deny_reduce_conflict", TEST_MODE_NON_BLOCKING, test_maelys_datalog_decision_from_queries_deny_reduce_conflict},
        {"maelys_datalog_solver/decision_from_queries_priority", TEST_MODE_NON_BLOCKING, test_maelys_datalog_decision_from_queries_priority},
        {"maelys_datalog_solver/solve_once_allow_reduce_without_deny_is_reduced", TEST_MODE_NON_BLOCKING, test_datalog_solve_once_allow_reduce_without_deny_is_reduced},
        {"maelys_datalog_solver/solve_once_deny_reduce_conflict_is_deny_conflict", TEST_MODE_NON_BLOCKING, test_datalog_solve_once_deny_reduce_conflict_is_deny_conflict},
        {"maelys_datalog_solver/decision_from_queries_default_deny", TEST_MODE_NON_BLOCKING, test_maelys_datalog_decision_from_queries_default_deny},
        {"maelys_datalog_solver/query_no_domain_leak", TEST_MODE_NON_BLOCKING, test_maelys_datalog_query_no_domain_leak},
        {"maelys_datalog_solver/duplicate_derivation_does_not_keep_changed_true", TEST_MODE_NON_BLOCKING, test_maelys_datalog_duplicate_derivation_does_not_keep_changed_true},
        {"maelys_datalog_solver/join_uses_predicate_range", TEST_MODE_NON_BLOCKING, test_maelys_datalog_join_uses_predicate_range},
        {"maelys_datalog_solver/cartesian_product_within_bounds", TEST_MODE_NON_BLOCKING, test_maelys_datalog_cartesian_product_within_bounds},
        {"maelys_datalog_solver/cartesian_product_overflow_fails_closed", TEST_MODE_NON_BLOCKING, test_maelys_datalog_cartesian_product_overflow_fails_closed},
        {"maelys_datalog_solver/reads_policy_fact_from_ruleset", TEST_MODE_NON_BLOCKING, test_datalog_solver_reads_policy_fact_from_ruleset},
        {"maelys_datalog_solver/policy_fact_idb_join", TEST_MODE_NON_BLOCKING, test_datalog_solver_policy_fact_idb_join},
        {"maelys_datalog_solver/policy_fact_runtime_symbol_table_not_mutated", TEST_MODE_NON_BLOCKING, test_datalog_policy_fact_runtime_symbol_table_not_mutated},
        {"maelys_datalog_solver/allowed_backend_tuple_direct_policy_fact_accepted", TEST_MODE_NON_BLOCKING, test_datalog_allowed_backend_tuple_direct_policy_fact_accepted},
        {"maelys_datalog_solver/allowed_backend_tuple_runtime_edb_rejected", TEST_MODE_NON_BLOCKING, test_datalog_allowed_backend_tuple_runtime_edb_rejected},
        {"maelys_datalog_solver/allowed_backend_tuple_rule_head_rejected", TEST_MODE_NON_BLOCKING, test_datalog_allowed_backend_tuple_rule_head_rejected},
        {"maelys_datalog_solver/allowed_backend_tuple_arity_mismatch_rejected", TEST_MODE_NON_BLOCKING, test_datalog_allowed_backend_tuple_arity_mismatch_rejected},
        {"maelys_datalog_solver/allowed_backend_tuple_non_ground_direct_fact_rejected", TEST_MODE_NON_BLOCKING, test_datalog_allowed_backend_tuple_non_ground_direct_fact_rejected},
        {"maelys_datalog_solver/backend_runtime_facts_remain_edb", TEST_MODE_NON_BLOCKING, test_datalog_backend_runtime_facts_remain_edb},
        {"maelys_datalog_solver/allowed_backend_tuple_symbol_interning_exact_match", TEST_MODE_NON_BLOCKING, test_datalog_allowed_backend_tuple_symbol_interning_exact_match},
        {"maelys_datalog_solver/allowed_backend_tuple_case_variant_denied", TEST_MODE_NON_BLOCKING, test_datalog_allowed_backend_tuple_case_variant_denied},
        {"maelys_datalog_solver/allowed_backend_tuple_whitespace_variant_denied", TEST_MODE_NON_BLOCKING, test_datalog_allowed_backend_tuple_whitespace_variant_denied},
        {"maelys_datalog_solver/allowed_backend_tuple_format_variant_denied", TEST_MODE_NON_BLOCKING, test_datalog_allowed_backend_tuple_format_variant_denied},
        {"maelys_datalog_solver/allowed_backend_tuple_no_cross_product_authorization", TEST_MODE_NON_BLOCKING, test_datalog_allowed_backend_tuple_no_cross_product_authorization},
        {"maelys_datalog_solver/wildcard_distinct_semantics", TEST_MODE_NON_BLOCKING, test_datalog_wildcard_distinct_semantics},
        {"maelys_datalog_solver/wildcard_multiple_matches_deduplicate_result", TEST_MODE_NON_BLOCKING, test_datalog_wildcard_multiple_matches_deduplicate_result},
        {"maelys_datalog_solver/wildcard_edge_two_positions_not_reflexive_only", TEST_MODE_NON_BLOCKING, test_datalog_wildcard_edge_two_positions_not_reflexive_only},
        {"maelys_datalog_solver/wildcard_solver_bindings_support_anonymous_capacity", TEST_MODE_NON_BLOCKING, test_datalog_wildcard_solver_bindings_support_anonymous_capacity},
        {"maelys_datalog_solver/solve_once_success", TEST_MODE_NON_BLOCKING, test_datalog_solve_once_success},
        {"maelys_datalog_solver/solve_once_double_solve_rejected", TEST_MODE_NON_BLOCKING, test_datalog_solve_once_double_solve_rejected},
        {"maelys_datalog_solver/query_before_solve_rejected", TEST_MODE_NON_BLOCKING, test_datalog_query_before_solve_rejected},
        {"maelys_datalog_solver/edb_mutation_after_solve_rejected", TEST_MODE_NON_BLOCKING, test_datalog_edb_mutation_after_solve_rejected},
        {"maelys_datalog_solver/ruleset_not_mutated_by_solve_once", TEST_MODE_NON_BLOCKING, test_datalog_ruleset_not_mutated_by_solve_once},
        {"maelys_datalog_solver/solve_result_lifetime_contract_enforced", TEST_MODE_NON_BLOCKING, test_datalog_solve_result_lifetime_contract_enforced},
        {"maelys_datalog_solver/solve_result_no_dangling_edb_pointer", TEST_MODE_NON_BLOCKING, test_datalog_solve_result_no_dangling_edb_pointer},
        {"maelys_datalog_solver/ruleset_shared_across_two_solves", TEST_MODE_NON_BLOCKING, test_datalog_ruleset_shared_across_two_solves},
        {"maelys_datalog_solver/query_ground_fact_absent", TEST_MODE_NON_BLOCKING, test_datalog_query_ground_fact_absent},
        {"maelys_datalog_solver/query_unknown_predicate_rejected", TEST_MODE_NON_BLOCKING, test_datalog_query_unknown_predicate_rejected},
        {"maelys_datalog_solver/query_arity_mismatch_rejected", TEST_MODE_NON_BLOCKING, test_datalog_query_arity_mismatch_rejected},
        {"maelys_datalog_solver/query_arity_above_max_rejected", TEST_MODE_NON_BLOCKING, test_datalog_query_arity_above_max_rejected},
        {"maelys_datalog_solver/query_non_ground_rejected", TEST_MODE_NON_BLOCKING, test_datalog_query_non_ground_rejected},
        {"maelys_datalog_solver/query_does_not_sort_or_mutate", TEST_MODE_NON_BLOCKING, test_datalog_query_does_not_sort_or_mutate},
        {"maelys_datalog_solver/solve_once_no_per_insert_memmove_required", TEST_MODE_NON_BLOCKING, test_datalog_solve_once_no_per_insert_memmove_required},
        {"maelys_datalog_solver/solve_once_preserves_policy_fact_boundary", TEST_MODE_NON_BLOCKING, test_datalog_solve_once_preserves_policy_fact_boundary},
        {"maelys_datalog_solver/solve_once_failed_solve_cleanup", TEST_MODE_NON_BLOCKING, test_datalog_solve_once_failed_solve_cleanup},
        {"maelys_datalog_solver/solve_once_capacity_overflow_no_leak", TEST_MODE_NON_BLOCKING, test_datalog_solve_once_capacity_overflow_no_leak},
        {"maelys_datalog_solver/solve_once_repeated_solve_query_free_no_leak", TEST_MODE_NON_BLOCKING, test_datalog_solve_once_repeated_solve_query_free_no_leak},
        {"maelys_datalog_solver/solve_once_delta_bounds_invariants", TEST_MODE_NON_BLOCKING, test_datalog_solve_once_delta_bounds_invariants},
        {"maelys_datalog_solver/solve_once_max_depth_post_loop_failure_path", TEST_MODE_NON_BLOCKING, test_datalog_solve_once_max_depth_post_loop_failure_path},
        {"maelys_datalog_solver/comparison_parser_ground_matrix_54", TEST_MODE_NON_BLOCKING, test_comparison_parser_ground_matrix_54},
        {"maelys_datalog_solver/comparison_solver_ground_truth_values", TEST_MODE_NON_BLOCKING, test_comparison_solver_ground_truth_values},
        {"maelys_datalog_solver/comparison_solver_runtime_variables", TEST_MODE_NON_BLOCKING, test_comparison_solver_runtime_variables},
        {"maelys_datalog_solver/comparison_runtime_cross_type_deny", TEST_MODE_NON_BLOCKING, test_comparison_runtime_cross_type_deny},
        {"maelys_datalog_solver/comparison_symbol_table_immutable_at_runtime", TEST_MODE_NON_BLOCKING, test_comparison_symbol_table_immutable_at_runtime},
        {"maelys_datalog_solver/symbol_table_immutable_at_query_time", TEST_MODE_NON_BLOCKING, test_symbol_table_immutable_at_query_time},
        {"maelys_datalog_solver/solve_once_eq_uses_term_equal", TEST_MODE_NON_BLOCKING, test_datalog_solve_once_eq_uses_term_equal},
        {"maelys_datalog_solver/solve_once_symbol_eq_uses_symbol_id", TEST_MODE_NON_BLOCKING, test_datalog_solve_once_symbol_eq_uses_symbol_id},
        {"maelys_datalog_solver/solve_once_int_ordinals", TEST_MODE_NON_BLOCKING, test_datalog_solve_once_int_ordinals},
        {"maelys_datalog_solver/solve_once_non_int_ordinal_classified", TEST_MODE_NON_BLOCKING, test_datalog_solve_once_non_int_ordinal_classified},
        {"maelys_datalog_solver/solve_once_unbound_comparison_variable_classified", TEST_MODE_NON_BLOCKING, test_datalog_solve_once_unbound_comparison_variable_classified},
        {"maelys_datalog_solver/solve_once_comparison_skip_does_not_allow", TEST_MODE_NON_BLOCKING, test_datalog_solve_once_comparison_skip_does_not_allow},
        {"maelys_datalog_solver/solve_once_non_int_ordinal_fails_closed", TEST_MODE_NON_BLOCKING, test_datalog_solve_once_non_int_ordinal_fails_closed},
        {"maelys_datalog_solver/solve_once_comparison_failure_reason_is_comparison_type_error", TEST_MODE_NON_BLOCKING, test_datalog_solve_once_comparison_failure_reason_is_comparison_type_error},
        {"maelys_datalog_solver/solve_once_unknown_operator_classified", TEST_MODE_NON_BLOCKING, test_datalog_solve_once_unknown_operator_classified},
        {"maelys_datalog_solver/solve_once_unknown_term_kind_classified", TEST_MODE_NON_BLOCKING, test_datalog_solve_once_unknown_term_kind_classified},
        {"maelys_datalog_solver/solve_once_diag_none_on_success", TEST_MODE_NON_BLOCKING, test_solve_once_diag_none_on_success},
        {"maelys_datalog_solver/solve_once_diag_max_depth", TEST_MODE_NON_BLOCKING, test_solve_once_diag_max_depth},
        {"maelys_datalog_solver/solve_once_diag_idb_overflow", TEST_MODE_NON_BLOCKING, test_solve_once_diag_idb_overflow},
        {"maelys_datalog_solver/solve_once_diag_comparison_type_error", TEST_MODE_NON_BLOCKING, test_solve_once_diag_comparison_type_error},
        {"maelys_datalog_solver/solve_once_diag_malformed_fact", TEST_MODE_NON_BLOCKING, test_solve_once_diag_malformed_fact},
        {"maelys_datalog_solver/solve_once_diag_malformed_edb", TEST_MODE_NON_BLOCKING, test_solve_once_diag_malformed_edb},
        {"maelys_datalog_solver/solve_once_diag_invalid_state", TEST_MODE_NON_BLOCKING, test_solve_once_diag_invalid_state},
        {"maelys_datalog_solver/solve_once_diag_null_arg", TEST_MODE_NON_BLOCKING, test_solve_once_diag_null_arg},
        {"maelys_datalog_solver/solve_once_diag_available_when_result_null", TEST_MODE_NON_BLOCKING, test_solve_once_diag_available_when_result_null},
        {"maelys_datalog_solver/solve_once_diag_cleared_on_entry", TEST_MODE_NON_BLOCKING, test_solve_once_diag_cleared_on_entry},
        {"maelys_datalog_solver/solve_once_diag_first_failure_preserved", TEST_MODE_NON_BLOCKING, test_solve_once_diag_first_failure_preserved},
        {"maelys_datalog_solver/solve_once_diag_no_raw_payload", TEST_MODE_NON_BLOCKING, test_solve_once_diag_no_raw_payload},
        {"maelys_datalog_solver/solve_once_diag_does_not_change_decision", TEST_MODE_NON_BLOCKING, test_solve_once_diag_does_not_change_decision},
        {"maelys_datalog_solver/solve_once_compat_wrapper", TEST_MODE_NON_BLOCKING, test_solve_once_compat_wrapper},
        {"maelys_datalog_solver/solve_once_null_diag_accepted", TEST_MODE_NON_BLOCKING, test_solve_once_null_diag_accepted},
        {"maelys_datalog_solver/fact_equals_matches_fact_set_contains", TEST_MODE_NON_BLOCKING, test_datalog_fact_equals_matches_fact_set_contains},
        {"maelys_datalog_solver/query_solved_ground_fact_uses_canonical_fact", TEST_MODE_NON_BLOCKING, test_datalog_query_solved_ground_fact_uses_canonical_fact},
        {"maelys_datalog_solver/factstore_rejects_structural_arity_mismatch", TEST_MODE_NON_BLOCKING, test_datalog_factstore_rejects_structural_arity_mismatch},
        {"maelys_datalog_solver/factstore_rejects_unknown_term_kind", TEST_MODE_NON_BLOCKING, test_datalog_factstore_rejects_unknown_term_kind},
        {"maelys_datalog_solver/factstore_rejects_var_in_ground_fact", TEST_MODE_NON_BLOCKING, test_datalog_factstore_rejects_var_in_ground_fact},
        {"maelys_datalog_solver/wildcard_never_interned", TEST_MODE_NON_BLOCKING, test_datalog_wildcard_never_interned},
        {"maelys_datalog_solver/wildcard_not_query_wildcard", TEST_MODE_NON_BLOCKING, test_datalog_wildcard_not_query_wildcard},
        {"maelys_datalog_solver/wildcard_not_comparison_operand", TEST_MODE_NON_BLOCKING, test_datalog_wildcard_not_comparison_operand},
        {"maelys_datalog_solver/semi_naive_same_result_as_naive_recursive", TEST_MODE_NON_BLOCKING, test_datalog_semi_naive_same_result_as_naive_recursive},
        {"maelys_datalog_solver/semi_naive_same_result_as_naive_multi_literal", TEST_MODE_NON_BLOCKING, test_datalog_semi_naive_same_result_as_naive_multi_literal},
        {"maelys_datalog_solver/semi_naive_same_result_as_naive_with_wildcard", TEST_MODE_NON_BLOCKING, test_datalog_semi_naive_same_result_as_naive_with_wildcard},
        {"maelys_datalog_solver/semi_naive_base_only_first_iteration", TEST_MODE_NON_BLOCKING, test_datalog_semi_naive_base_only_first_iteration},
        {"maelys_datalog_solver/semi_naive_delta_empty_terminates", TEST_MODE_NON_BLOCKING, test_datalog_semi_naive_delta_empty_terminates},
        {"maelys_datalog_solver/semi_naive_delta_contains_only_new_facts", TEST_MODE_NON_BLOCKING, test_datalog_semi_naive_delta_contains_only_new_facts},
        {"maelys_datalog_solver/semi_naive_new_facts_not_visible_until_next_iteration", TEST_MODE_NON_BLOCKING, test_datalog_semi_naive_new_facts_not_visible_until_next_iteration},
        {"maelys_datalog_solver/semi_naive_scans_each_idb_literal_as_delta", TEST_MODE_NON_BLOCKING, test_datalog_semi_naive_scans_each_idb_literal_as_delta},
        {"maelys_datalog_solver/semi_naive_delta_variants_bounded_by_body_literals", TEST_MODE_NON_BLOCKING, test_datalog_semi_naive_delta_variants_bounded_by_body_literals},
        {"maelys_datalog_solver/semi_naive_positive_only_no_negation", TEST_MODE_NON_BLOCKING, test_datalog_semi_naive_positive_only_no_negation},
        {"maelys_datalog_solver/static_join_order_preserves_simple_join", TEST_MODE_NON_BLOCKING, test_datalog_static_join_order_preserves_simple_join},
        {"maelys_datalog_solver/static_join_order_preserves_recursive_rule", TEST_MODE_NON_BLOCKING, test_datalog_static_join_order_preserves_recursive_rule},
        {"maelys_datalog_solver/static_join_order_delta_literal_is_root", TEST_MODE_NON_BLOCKING, test_datalog_static_join_order_delta_literal_is_root},
        {"maelys_datalog_solver/static_join_order_preserves_semi_naive_delta_literal", TEST_MODE_NON_BLOCKING, test_datalog_static_join_order_preserves_semi_naive_delta_literal},
        {"maelys_datalog_solver/static_join_order_comparison_after_binding", TEST_MODE_NON_BLOCKING, test_datalog_static_join_order_comparison_after_binding},
        {"maelys_datalog_solver/static_join_order_orphan_comparison_fails_closed", TEST_MODE_NON_BLOCKING, test_datalog_static_join_order_orphan_comparison_fails_closed},
        {"maelys_datalog_solver/static_join_order_deterministic_for_same_rule", TEST_MODE_NON_BLOCKING, test_datalog_static_join_order_deterministic_for_same_rule},
        {"maelys_datalog_solver/static_join_order_tie_breaks_by_original_body_index", TEST_MODE_NON_BLOCKING, test_datalog_static_join_order_tie_breaks_by_original_body_index},
        {"maelys_datalog_solver/static_join_order_no_runtime_stats_dependency", TEST_MODE_NON_BLOCKING, test_datalog_static_join_order_no_runtime_stats_dependency},
        {"maelys_datalog_solver/static_join_order_preserves_deny_reduce_allow_precedence", TEST_MODE_NON_BLOCKING, test_datalog_static_join_order_preserves_deny_reduce_allow_precedence},
        {"maelys_datalog_solver/static_join_order_failure_is_fail_closed", TEST_MODE_NON_BLOCKING, test_datalog_static_join_order_failure_is_fail_closed},
        {"maelys_datalog_solver/static_join_order_greedy_no_recursion_no_heap", TEST_MODE_NON_BLOCKING, test_datalog_static_join_order_greedy_no_recursion_no_heap},
        {"maelys_datalog_solver/static_join_order_future_negated_literal_classification_documented", TEST_MODE_NON_BLOCKING, test_datalog_static_join_order_future_negated_literal_classification_documented},
        {"maelys_datalog_solver/static_join_order_differential_source_vs_ordered", TEST_MODE_NON_BLOCKING, test_datalog_static_join_order_differential_source_vs_ordered},
        {"maelys_datalog_solver/negation_simple_deny", TEST_MODE_NON_BLOCKING, test_datalog_negation_simple_deny},
        {"maelys_datalog_solver/negation_positive_path_unchanged", TEST_MODE_NON_BLOCKING, test_datalog_negation_positive_path_unchanged},
        {"maelys_datalog_solver/negation_stratified_positive_subset_equivalence", TEST_MODE_NON_BLOCKING, test_datalog_negation_stratified_positive_subset_equivalence},
        {"maelys_datalog_solver/negation_two_strata_policy", TEST_MODE_NON_BLOCKING, test_datalog_negation_two_strata_policy},
        {"maelys_datalog_solver/negation_decision_policy_compatible", TEST_MODE_NON_BLOCKING, test_datalog_negation_decision_policy_compatible},
        {"maelys_datalog_solver/negation_negated_atom_not_in_bound_var_mask", TEST_MODE_NON_BLOCKING, test_datalog_negation_negated_atom_not_in_bound_var_mask},
        {"maelys_datalog_solver/proof_node_size_expected_bound", TEST_MODE_NON_BLOCKING, test_datalog_proof_node_size_expected_bound},
        {"maelys_datalog_solver/proof_node_has_derived_fact", TEST_MODE_NON_BLOCKING, test_datalog_proof_node_has_derived_fact},
        {"maelys_datalog_solver/proof_parent_index_coherent", TEST_MODE_NON_BLOCKING, test_datalog_proof_parent_index_coherent},
        {"maelys_datalog_solver/proof_depth_consistent_with_parent", TEST_MODE_NON_BLOCKING, test_datalog_proof_depth_consistent_with_parent},
        {"maelys_datalog_solver/proof_extract_for_queried_fact", TEST_MODE_NON_BLOCKING, test_datalog_proof_extract_for_queried_fact},
        {"maelys_datalog_solver/proof_extract_returns_empty_for_absent_fact", TEST_MODE_NON_BLOCKING, test_datalog_proof_extract_returns_empty_for_absent_fact},
        {"maelys_datalog_solver/proof_solve_result_proof_accessor", TEST_MODE_NON_BLOCKING, test_datalog_proof_solve_result_proof_accessor},
        {"maelys_datalog_solver/proof_truncation_recorded", TEST_MODE_NON_BLOCKING, test_datalog_proof_truncation_recorded},
        {"maelys_datalog_solver/proof_failure_node_zero_initialized", TEST_MODE_NON_BLOCKING, test_datalog_proof_failure_node_zero_initialized},
        {"maelys_datalog_solver/proof_idb_proof_index_survives_stratum_sort", TEST_MODE_NON_BLOCKING, test_datalog_proof_idb_proof_index_survives_stratum_sort},
        {"maelys_datalog_solver/proof_extract_remaps_parent_indices", TEST_MODE_NON_BLOCKING, test_datalog_proof_extract_remaps_parent_indices},
        {"maelys_datalog_solver/proof_tree_single_node_still_bounded", TEST_MODE_NON_BLOCKING, test_datalog_proof_tree_single_node_still_bounded},
    };
    return test_main("maelys_datalog_solver", cases, (int)(sizeof(cases) / sizeof(cases[0])), argc, argv);
}
