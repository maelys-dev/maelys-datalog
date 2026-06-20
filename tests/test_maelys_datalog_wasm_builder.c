#include "src/core/maelys_datalog_edb.h"
#include "src/core/maelys_datalog_solver.h"
#include "src/core/maelys_datalog_symbol_table.h"
#include "src/manifest/maelys_datalog_manifest.h"
#include "src/wasm/maelys_datalog_wasm.h"
#include "tests/helpers/test_framework.h"

#include <stdbool.h>
#include <sys/wait.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char k_domain[] = "wasm_builder_test_domain";
static const char k_policy_id[] = "wasm-builder-policy";
static const char k_policy_src[] = "allow(X) :- safe(X).\n";
static const char *g_self_path = NULL;

static char g_long_domain[MAELYS_DATALOG_INLINE_MAX_DOMAIN_LEN + 2u];
static char g_long_predicate[64u + 1u];

static void init_long_names(void) {
    memset(g_long_domain, 'd', sizeof(g_long_domain) - 1u);
    g_long_domain[sizeof(g_long_domain) - 1u] = '\0';
    memset(g_long_predicate, 'p', sizeof(g_long_predicate) - 1u);
    g_long_predicate[sizeof(g_long_predicate) - 1u] = '\0';
}

static int run_edb_begin_without_policy_selftest(void) {
    return maelys_datalog_wasm_edb_begin() == MAELYS_ERR_INVALID_STATE ? 0 : 1;
}

static int spawn_edb_begin_without_policy_selftest(const char *path) {
    if (!path || !*path) return 0;

    pid_t pid = fork();
    if (pid < 0) return 0;
    if (pid == 0) {
        (void)setenv("MAELYS_WASM_EDB_BEGIN_SELFTEST", "1", 1);
        char *const child_argv[] = {(char *)path, NULL};
        execvp(path, child_argv);
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return 0;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static maelys_datalog_term_t symbol_term(maelys_datalog_ruleset_t *ruleset, const char *text) {
    maelys_datalog_symbol_id_t id = 0;
    (void)maelys_datalog_symbol_intern(&ruleset->symbols, text, strlen(text), &id);
    maelys_datalog_term_t term = {.kind = MAELYS_DATALOG_TERM_SYMBOL};
    term.as.symbol = id;
    return term;
}

static int test_wasm_builder_double_begin(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_wasm_domain_begin("wasm_pre_double_begin"), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_STATE,
                      maelys_datalog_wasm_domain_begin("wasm_pre_double_begin_second"),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_wasm_domain_abort(), "%d");
    TEST_END();
}

static int test_wasm_builder_commit_empty_predicates(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_wasm_domain_begin("wasm_pre_empty_commit"), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT, maelys_datalog_wasm_domain_commit(), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_STATE,
                      maelys_datalog_wasm_domain_add_predicate("safe",
                                                               1,
                                                               MAELYS_DATALOG_PRED_KIND_EDB),
                      "%d");
    TEST_END();
}

static int test_wasm_builder_add_without_begin(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_STATE,
                      maelys_datalog_wasm_domain_add_predicate("safe",
                                                               1,
                                                               MAELYS_DATALOG_PRED_KIND_EDB),
                      "%d");
    TEST_END();
}

static int test_wasm_builder_commit_without_begin(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_STATE, maelys_datalog_wasm_domain_commit(), "%d");
    TEST_END();
}

static int test_wasm_builder_abort_resets(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_wasm_domain_begin("wasm_pre_abort_reset"), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_wasm_domain_add_predicate("safe",
                                                               1,
                                                               MAELYS_DATALOG_PRED_KIND_EDB),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_wasm_domain_abort(), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_wasm_domain_begin("wasm_pre_abort_reset_again"), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_wasm_domain_add_predicate("safe",
                                                               1,
                                                               MAELYS_DATALOG_PRED_KIND_EDB),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_wasm_domain_abort(), "%d");
    TEST_END();
}

static int test_wasm_builder_domain_name_too_long(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE,
                      maelys_datalog_wasm_domain_begin(g_long_domain),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_STATE,
                      maelys_datalog_wasm_domain_add_predicate("safe",
                                                               1,
                                                               MAELYS_DATALOG_PRED_KIND_EDB),
                      "%d");
    TEST_END();
}

static int test_wasm_builder_predicate_name_too_long(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_wasm_domain_begin("wasm_pre_predicate_too_long"), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE,
                      maelys_datalog_wasm_domain_add_predicate(g_long_predicate,
                                                               1,
                                                               MAELYS_DATALOG_PRED_KIND_EDB),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_wasm_domain_abort(), "%d");
    TEST_END();
}

static int test_wasm_builder_kind_flags_zero(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_wasm_domain_begin("wasm_pre_kind_zero"), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_wasm_domain_add_predicate("safe", 1, 0u),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_wasm_domain_abort(), "%d");
    TEST_END();
}

static int test_wasm_builder_kind_flags_unknown_bits(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_wasm_domain_begin("wasm_pre_kind_unknown"), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_wasm_domain_add_predicate("safe", 1, 0x100u),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_wasm_domain_abort(), "%d");
    TEST_END();
}

static int test_wasm_builder_max_predicates(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_wasm_domain_begin("wasm_pre_max_predicates"), "%d");
    char name[32];
    for (size_t i = 0; i < MAELYS_DATALOG_MAX_PREDICATES; i++) {
        snprintf(name, sizeof(name), "p%zu", i);
        TEST_ASSERT_EQUAL(MAELYS_OK,
                          maelys_datalog_wasm_domain_add_predicate(name,
                                                                   1,
                                                                   MAELYS_DATALOG_PRED_KIND_EDB),
                          "%d");
    }
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE,
                      maelys_datalog_wasm_domain_add_predicate("overflow",
                                                               1,
                                                               MAELYS_DATALOG_PRED_KIND_EDB),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_wasm_domain_abort(), "%d");
    TEST_END();
}

static int test_wasm_builder_basic(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_wasm_domain_begin(k_domain), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_wasm_domain_add_predicate("safe",
                                                               1,
                                                               MAELYS_DATALOG_PRED_KIND_EDB),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_wasm_domain_add_predicate("allow",
                                                               1,
                                                               MAELYS_DATALOG_PRED_KIND_IDB |
                                                                   MAELYS_DATALOG_PRED_KIND_QUERY),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_wasm_domain_commit(), "%d");
    TEST_ASSERT_EQUAL((uintptr_t)0u, maelys_datalog_wasm_ruleset_ptr(), "%lu");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_wasm_load_ruleset(k_domain,
                                                      k_policy_id,
                                                      k_policy_src,
                                                      strlen(k_policy_src)),
                      "%d");
    TEST_ASSERT_TRUE(maelys_datalog_wasm_ruleset_ptr() != (uintptr_t)0u);
    TEST_END();
}

static int test_wasm_builder_edb_begin_without_policy(void) {
    TEST_BEGIN();
    TEST_ASSERT_TRUE(spawn_edb_begin_without_policy_selftest(g_self_path));
    TEST_END();
}

static int test_wasm_builder_edb_and_solve(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_wasm_edb_begin(), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_wasm_edb_add_symbol("safe", "alice"),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_wasm_solve(), "%d");
    TEST_ASSERT_EQUAL(1, maelys_datalog_wasm_query_symbol("allow", "alice"), "%d");
    TEST_ASSERT_EQUAL(0, maelys_datalog_wasm_query_symbol("allow", "bob"), "%d");
    maelys_datalog_wasm_solve_result_free();
    TEST_ASSERT_EQUAL(-1, maelys_datalog_wasm_query_symbol("allow", "alice"), "%d");
    TEST_END();
}

static int test_wasm_builder_edb_two_evaluations(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_wasm_edb_begin(), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_wasm_edb_add_symbol("safe", "alice"),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_wasm_solve(), "%d");
    TEST_ASSERT_EQUAL(1, maelys_datalog_wasm_query_symbol("allow", "alice"), "%d");

    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_wasm_edb_begin(), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_wasm_edb_add_symbol("safe", "bob"),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_wasm_solve(), "%d");
    TEST_ASSERT_EQUAL(1, maelys_datalog_wasm_query_symbol("allow", "bob"), "%d");
    TEST_ASSERT_EQUAL(0, maelys_datalog_wasm_query_symbol("allow", "alice"), "%d");
    maelys_datalog_wasm_solve_result_free();
    TEST_END();
}

static int test_wasm_builder_query_without_solve(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(-1, maelys_datalog_wasm_query_symbol("allow", "alice"), "%d");
    TEST_END();
}

static int test_wasm_builder_query_unknown_symbol_is_readonly(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_wasm_edb_begin(), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_wasm_solve(), "%d");
    char unknown[32];
    for (size_t i = 0u; i < MAELYS_DATALOG_MAX_SYMBOLS + 8u; i++) {
        snprintf(unknown, sizeof(unknown), "ghost_%04zu", i);
        TEST_ASSERT_EQUAL(0, maelys_datalog_wasm_query_symbol("allow", unknown), "%d");
    }
    maelys_datalog_wasm_solve_result_free();
    TEST_END();
}

static int test_wasm_builder_no_begin_after_commit(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_STATE,
                      maelys_datalog_wasm_domain_begin("wasm_after_commit"),
                      "%d");
    TEST_END();
}

static int test_wasm_builder_abort_after_commit_does_not_reset_commit_guard(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_wasm_domain_abort(), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_STATE,
                      maelys_datalog_wasm_domain_begin("wasm_after_abort_commit"),
                      "%d");
    TEST_END();
}

static int test_wasm_builder_ruleset_ptr(void) {
    TEST_BEGIN();
    TEST_ASSERT_TRUE(maelys_datalog_wasm_ruleset_ptr() != (uintptr_t)0u);
    TEST_END();
}

static int test_wasm_builder_solve_query(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t *ruleset = (maelys_datalog_ruleset_t *)maelys_datalog_wasm_ruleset_ptr();
    TEST_ASSERT_NOT_NULL(ruleset);

    maelys_datalog_fact_t facts[4];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_edb_init(&edb,
                                              facts,
                                              sizeof(facts) / sizeof(facts[0]),
                                              &ruleset->symbols,
                                              &ruleset->registry),
                      "%d");
    maelys_datalog_term_t alice = symbol_term(ruleset, "alice");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_add_fact(&edb, "safe", &alice, 1), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");

    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(ruleset, &edb, &result), "%d");
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_query_solved_ground_fact(result, "allow", &alice, 1, &present),
                      "%d");
    TEST_ASSERT_TRUE(present);
    maelys_datalog_solve_result_free(result);
    TEST_END();
}

static int test_wasm_builder_load_failure_clears_ruleset_ptr(void) {
    TEST_BEGIN();
    const char invalid_src[] = "allow(X) :- safe(X)\n";
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_wasm_load_ruleset(k_domain,
                                                      k_policy_id,
                                                      invalid_src,
                                                      strlen(invalid_src)),
                      "%d");
    TEST_ASSERT_EQUAL((uintptr_t)0u, maelys_datalog_wasm_ruleset_ptr(), "%lu");
    TEST_END();
}

static int test_wasm_builder_diag_on_invalid_policy(void) {
    TEST_BEGIN();
    const char invalid_src[] = "allow(X) :- safe(X)\n";
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_wasm_load_ruleset(k_domain,
                                                      k_policy_id,
                                                      invalid_src,
                                                      strlen(invalid_src)),
                      "%d");
    TEST_ASSERT_TRUE(maelys_datalog_wasm_last_diag_code() != 0);
    TEST_ASSERT_TRUE(maelys_datalog_wasm_last_diag_message()[0] != '\0');
    TEST_END();
}

int main(int argc, char **argv) {
    if (getenv("MAELYS_WASM_EDB_BEGIN_SELFTEST")) {
        return run_edb_begin_without_policy_selftest();
    }
    g_self_path = (argc > 0 && argv && argv[0]) ? argv[0] : NULL;
    init_long_names();
    test_case_t cases[] = {
        {"maelys_datalog_wasm_builder/double_begin", TEST_MODE_NON_BLOCKING, test_wasm_builder_double_begin},
        {"maelys_datalog_wasm_builder/commit_empty_predicates", TEST_MODE_NON_BLOCKING, test_wasm_builder_commit_empty_predicates},
        {"maelys_datalog_wasm_builder/add_without_begin", TEST_MODE_NON_BLOCKING, test_wasm_builder_add_without_begin},
        {"maelys_datalog_wasm_builder/commit_without_begin", TEST_MODE_NON_BLOCKING, test_wasm_builder_commit_without_begin},
        {"maelys_datalog_wasm_builder/abort_resets", TEST_MODE_NON_BLOCKING, test_wasm_builder_abort_resets},
        {"maelys_datalog_wasm_builder/domain_name_too_long", TEST_MODE_NON_BLOCKING, test_wasm_builder_domain_name_too_long},
        {"maelys_datalog_wasm_builder/predicate_name_too_long", TEST_MODE_NON_BLOCKING, test_wasm_builder_predicate_name_too_long},
        {"maelys_datalog_wasm_builder/kind_flags_zero", TEST_MODE_NON_BLOCKING, test_wasm_builder_kind_flags_zero},
        {"maelys_datalog_wasm_builder/kind_flags_unknown_bits", TEST_MODE_NON_BLOCKING, test_wasm_builder_kind_flags_unknown_bits},
        {"maelys_datalog_wasm_builder/max_predicates", TEST_MODE_NON_BLOCKING, test_wasm_builder_max_predicates},
        {"maelys_datalog_wasm_builder/basic", TEST_MODE_NON_BLOCKING, test_wasm_builder_basic},
        {"maelys_datalog_wasm_builder/edb_begin_without_policy", TEST_MODE_NON_BLOCKING, test_wasm_builder_edb_begin_without_policy},
        {"maelys_datalog_wasm_builder/edb_and_solve", TEST_MODE_NON_BLOCKING, test_wasm_builder_edb_and_solve},
        {"maelys_datalog_wasm_builder/edb_two_evaluations", TEST_MODE_NON_BLOCKING, test_wasm_builder_edb_two_evaluations},
        {"maelys_datalog_wasm_builder/query_without_solve", TEST_MODE_NON_BLOCKING, test_wasm_builder_query_without_solve},
        {"maelys_datalog_wasm_builder/query_unknown_symbol_is_readonly", TEST_MODE_NON_BLOCKING, test_wasm_builder_query_unknown_symbol_is_readonly},
        {"maelys_datalog_wasm_builder/no_begin_after_commit", TEST_MODE_NON_BLOCKING, test_wasm_builder_no_begin_after_commit},
        {"maelys_datalog_wasm_builder/abort_after_commit_does_not_reset_commit_guard", TEST_MODE_NON_BLOCKING, test_wasm_builder_abort_after_commit_does_not_reset_commit_guard},
        {"maelys_datalog_wasm_builder/ruleset_ptr", TEST_MODE_NON_BLOCKING, test_wasm_builder_ruleset_ptr},
        {"maelys_datalog_wasm_builder/solve_query", TEST_MODE_NON_BLOCKING, test_wasm_builder_solve_query},
        {"maelys_datalog_wasm_builder/load_failure_clears_ruleset_ptr", TEST_MODE_NON_BLOCKING, test_wasm_builder_load_failure_clears_ruleset_ptr},
        {"maelys_datalog_wasm_builder/diag_on_invalid_policy", TEST_MODE_NON_BLOCKING, test_wasm_builder_diag_on_invalid_policy},
    };
    return test_main("maelys_datalog_wasm_builder", cases, (int)(sizeof(cases) / sizeof(cases[0])), argc, argv);
}
