#include "src/core/maelys_datalog_diagnostic.h"
#include "src/core/maelys_datalog_edb.h"
#include "src/core/maelys_datalog_parser.h"
#include "src/core/maelys_datalog_predicate_registry.h"
#include "src/core/maelys_datalog_solver.h"
#include "src/core/maelys_datalog_symbol_table.h"
#include "tests/helpers/test_framework.h"

#include <stdbool.h>
#include <string.h>

static const char k_zero_sha[] =
    "0000000000000000000000000000000000000000000000000000000000000000";

static maelys_result_t init_base_ruleset(maelys_datalog_ruleset_t *ruleset,
                                         const char *policy_id) {
    memset(ruleset, 0, sizeof(*ruleset));
    return maelys_datalog_ruleset_init(ruleset, policy_id, "atom_vocab", k_zero_sha, 1);
}

static maelys_result_t add_predicate(maelys_datalog_ruleset_t *ruleset,
                                     const char *name,
                                     size_t arity,
                                     unsigned kind_flags) {
    return maelys_datalog_predicate_registry_add_domain(&ruleset->registry,
                                                        name,
                                                        arity,
                                                        kind_flags);
}

static maelys_result_t init_policy_fact_ruleset(maelys_datalog_ruleset_t *ruleset,
                                                int add_alice_atom) {
    maelys_result_t rc = init_base_ruleset(ruleset, "atom_vocab.policy_fact");
    if (rc != MAELYS_OK) return rc;
    rc = add_predicate(ruleset, "safe", 1u, MAELYS_DATALOG_PRED_KIND_POLICY_FACT);
    if (rc != MAELYS_OK) return rc;
    rc = add_predicate(ruleset,
                       "allow",
                       1u,
                       MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY);
    if (rc != MAELYS_OK) return rc;
    if (add_alice_atom) {
        rc = maelys_datalog_predicate_registry_add_atom(&ruleset->registry, "alice");
        if (rc != MAELYS_OK) return rc;
    }
    return maelys_datalog_predicate_registry_freeze(&ruleset->registry);
}

static maelys_result_t init_runtime_ruleset(maelys_datalog_ruleset_t *ruleset,
                                            int add_alice_atom) {
    maelys_result_t rc = init_base_ruleset(ruleset, "atom_vocab.runtime");
    if (rc != MAELYS_OK) return rc;
    rc = add_predicate(ruleset, "safe", 1u, MAELYS_DATALOG_PRED_KIND_EDB);
    if (rc != MAELYS_OK) return rc;
    rc = add_predicate(ruleset,
                       "allow",
                       1u,
                       MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY);
    if (rc != MAELYS_OK) return rc;
    if (add_alice_atom) {
        rc = maelys_datalog_predicate_registry_add_atom(&ruleset->registry, "alice");
        if (rc != MAELYS_OK) return rc;
    }
    return maelys_datalog_predicate_registry_freeze(&ruleset->registry);
}

static maelys_result_t parse_source(maelys_datalog_ruleset_t *ruleset,
                                    const char *source,
                                    maelys_datalog_diagnostic_t *diag) {
    return maelys_datalog_parse_ruleset_ex(ruleset,
                                           source,
                                           strlen(source),
                                           "atom_vocabulary.dl",
                                           diag);
}

static maelys_datalog_term_t symbol_term(maelys_datalog_ruleset_t *ruleset,
                                         const char *text) {
    maelys_datalog_symbol_id_t sid = 0;
    (void)maelys_datalog_symbol_intern(&ruleset->symbols, text, strlen(text), &sid);
    maelys_datalog_term_t term = {.kind = MAELYS_DATALOG_TERM_SYMBOL};
    term.as.symbol = sid;
    return term;
}

static int test_parser_rejects_string_constant_without_atom(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
    maelys_datalog_diagnostic_t diag = {0};
    TEST_ASSERT_EQUAL(MAELYS_OK, init_policy_fact_ruleset(&ruleset, 0), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, parse_source(&ruleset, "safe(\"alice\").", &diag), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_PARSER_UNKNOWN_ATOM, diag.code, "%d");
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

static int test_parser_accepts_string_constant_after_add_atom(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
    maelys_datalog_diagnostic_t diag = {0};
    TEST_ASSERT_EQUAL(MAELYS_OK, init_policy_fact_ruleset(&ruleset, 1), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, parse_source(&ruleset, "safe(\"alice\").", &diag), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_NONE, diag.code, "%d");
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

static int test_parser_accepts_variable_only_rule_without_atoms(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
    maelys_datalog_diagnostic_t diag = {0};
    TEST_ASSERT_EQUAL(MAELYS_OK, init_runtime_ruleset(&ruleset, 0), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, parse_source(&ruleset, "allow(X) :- safe(X).", &diag), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_NONE, diag.code, "%d");
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

static int test_edb_add_atom_fact_rejects_when_vocabulary_empty(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
    maelys_datalog_fact_t facts[2];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_runtime_ruleset(&ruleset, 0), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, parse_source(&ruleset, "allow(X) :- safe(X).", NULL), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 2u, &ruleset.symbols, &ruleset.registry), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_FORBIDDEN,
                      maelys_datalog_edb_add_atom_fact(&edb, "safe", "alice"),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, edb.fact_count, "%zu");
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

static int test_edb_add_atom_fact_accepts_after_add_atom(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
    maelys_datalog_fact_t facts[2];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_runtime_ruleset(&ruleset, 1), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, parse_source(&ruleset, "allow(X) :- safe(X).", NULL), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 2u, &ruleset.symbols, &ruleset.registry), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_add_atom_fact(&edb, "safe", "alice"), "%d");
    TEST_ASSERT_EQUAL((size_t)1u, edb.fact_count, "%zu");
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

static int test_edb_add_runtime_symbol_fact_accepts_without_atom(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
    maelys_datalog_fact_t facts[2];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_runtime_ruleset(&ruleset, 0), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, parse_source(&ruleset, "allow(X) :- safe(X).", NULL), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 2u, &ruleset.symbols, &ruleset.registry), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_edb_add_runtime_symbol_fact(&edb, "safe", "alice"),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)1u, edb.fact_count, "%zu");
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

static int test_edb_add_runtime_symbol_fact_solve_semantics(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
    maelys_datalog_fact_t facts[2];
    maelys_datalog_edb_t edb;
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_runtime_ruleset(&ruleset, 0), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, parse_source(&ruleset, "allow(X) :- safe(X).", NULL), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, facts, 2u, &ruleset.symbols, &ruleset.registry), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_edb_add_runtime_symbol_fact(&edb, "safe", "alice"),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&ruleset, &edb, &result), "%d");
    maelys_datalog_term_t alice = symbol_term(&ruleset, "alice");
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_query_solved_ground_fact(result, "allow", &alice, 1u, &present),
                      "%d");
    TEST_ASSERT_TRUE(present);
    maelys_datalog_solve_result_free(result);
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

int main(int argc, char **argv) {
    const test_case_t cases[] = {
        {"maelys_datalog_atom_vocabulary/parser_rejects_string_constant_without_atom",
         TEST_MODE_NON_BLOCKING,
         test_parser_rejects_string_constant_without_atom},
        {"maelys_datalog_atom_vocabulary/parser_accepts_string_constant_after_add_atom",
         TEST_MODE_NON_BLOCKING,
         test_parser_accepts_string_constant_after_add_atom},
        {"maelys_datalog_atom_vocabulary/parser_accepts_variable_only_rule_without_atoms",
         TEST_MODE_NON_BLOCKING,
         test_parser_accepts_variable_only_rule_without_atoms},
        {"maelys_datalog_atom_vocabulary/edb_add_atom_fact_rejects_when_vocabulary_empty",
         TEST_MODE_NON_BLOCKING,
         test_edb_add_atom_fact_rejects_when_vocabulary_empty},
        {"maelys_datalog_atom_vocabulary/edb_add_atom_fact_accepts_after_add_atom",
         TEST_MODE_NON_BLOCKING,
         test_edb_add_atom_fact_accepts_after_add_atom},
        {"maelys_datalog_atom_vocabulary/edb_add_runtime_symbol_fact_accepts_without_atom",
         TEST_MODE_NON_BLOCKING,
         test_edb_add_runtime_symbol_fact_accepts_without_atom},
        {"maelys_datalog_atom_vocabulary/edb_add_runtime_symbol_fact_solve_semantics",
         TEST_MODE_NON_BLOCKING,
         test_edb_add_runtime_symbol_fact_solve_semantics},
    };
    return test_main("maelys_datalog_atom_vocabulary",
                     cases,
                     (int)(sizeof(cases) / sizeof(cases[0])),
                     argc,
                     argv);
}
