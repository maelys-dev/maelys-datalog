#include "src/core/maelys_datalog_domain_registry.h"
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

/* ====================================================================
 * P4-C66 — Why-true text boundary.
 *
 * The domain is registered through the public domain registry (rather than the
 * WASM builder, which accepts a single committed domain per instance) so these
 * cases own their vocabulary, including the atom needed by a POLICY_FACT.
 * ==================================================================== */

static const char k_explain_domain[] = "wasm_explain_test_domain";
static const char k_explain_policy_id[] = "wasm-explain-policy";
static const char k_explain_src[] =
    "mode(\"strict\").\n"
    "allow(X) :- safe(X).\n"
    "internal(X) :- safe(X).\n"
    "path(X, Y) :- edge(X, Y).\n"
    "path(X, Z) :- path(X, Y), edge(Y, Z).\n";
/* Public truncation construction: the 9-node transitive closure derives
 * 36 path + 36 reach = 72 facts, beyond MAELYS_DATALOG_MAX_PROOF_NODES (64). */
static const char k_explain_truncated_src[] =
    "path(X, Y) :- edge(X, Y).\n"
    "path(X, Z) :- path(X, Y), edge(Y, Z).\n"
    "reach(X, Y) :- path(X, Y).\n";

static const char k_explain_allow_alice[] =
    "MAELYS-DATALOG-WHY-TRUE-TEXT-v1\n"
    "status=complete\n"
    "steps=1 premises=1\n"
    "step=0 rule=1 fact=\"allow\"(\"alice\")\n"
    "premise=0 body=0 kind=positive origin=edb fact=\"safe\"(\"alice\") parent=-\n"
    "result-step=0\n";
static const char k_explain_path_ab[] =
    "MAELYS-DATALOG-WHY-TRUE-TEXT-v1\n"
    "status=complete\n"
    "steps=1 premises=1\n"
    "step=0 rule=3 fact=\"path\"(\"a\",\"b\")\n"
    "premise=0 body=0 kind=positive origin=edb fact=\"edge\"(\"a\",\"b\") parent=-\n"
    "result-step=0\n";
static const char k_explain_truncated_text[] =
    "MAELYS-DATALOG-WHY-TRUE-TEXT-v1\n"
    "status=truncated\n"
    "steps=0 premises=0\n";

static char g_explain_text[8192];
static char g_explain_text_b[8192];
static maelys_datalog_ruleset_t g_explain_ruleset_snapshot;

static maelys_result_t explain_install_predicates(
    maelys_datalog_predicate_registry_t *registry) {
    static const maelys_datalog_predicate_def_t defs[] = {
        {"safe", 1u, MAELYS_DATALOG_PRED_KIND_EDB},
        {"edge", 2u, MAELYS_DATALOG_PRED_KIND_EDB},
        {"observed", 1u, MAELYS_DATALOG_PRED_KIND_EDB | MAELYS_DATALOG_PRED_KIND_QUERY},
        {"mode", 1u, MAELYS_DATALOG_PRED_KIND_POLICY_FACT | MAELYS_DATALOG_PRED_KIND_QUERY},
        {"allow", 1u, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
        {"internal", 1u, MAELYS_DATALOG_PRED_KIND_IDB},
        {"path", 2u, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
        {"reach", 2u, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
    };
    for (size_t i = 0u; i < sizeof(defs) / sizeof(defs[0]); i++) {
        maelys_result_t rc = maelys_datalog_predicate_registry_add_domain(registry,
                                                                          defs[i].name,
                                                                          defs[i].arity,
                                                                          defs[i].kind_flags);
        if (rc != MAELYS_OK) return rc;
    }
    return maelys_datalog_predicate_registry_add_atom(registry, "strict");
}

static maelys_result_t explain_load(const char *src) {
    maelys_datalog_domain_def_t def = {
        .domain_name = k_explain_domain,
        .predicates = NULL,
        .predicate_count = 0u,
        .description = "WASM Why-true text test domain",
        .install_predicates = explain_install_predicates,
    };
    maelys_result_t rc = maelys_datalog_domain_registry_register(&def);
    if (rc != MAELYS_OK) return rc;
    return maelys_datalog_wasm_load_ruleset(k_explain_domain,
                                            k_explain_policy_id,
                                            src,
                                            strlen(src));
}

/* safe(alice), observed(alice), edge(a,b), edge(b,c). */
static maelys_result_t explain_setup_simple(void) {
    maelys_result_t rc = explain_load(k_explain_src);
    if (rc != MAELYS_OK) return rc;
    rc = maelys_datalog_wasm_edb_begin();
    if (rc != MAELYS_OK) return rc;
    rc = maelys_datalog_wasm_edb_add_symbol("safe", "alice");
    if (rc != MAELYS_OK) return rc;
    rc = maelys_datalog_wasm_edb_add_symbol("observed", "alice");
    if (rc != MAELYS_OK) return rc;
    rc = maelys_datalog_wasm_edb_add_symbol2("edge", "a", "b");
    if (rc != MAELYS_OK) return rc;
    rc = maelys_datalog_wasm_edb_add_symbol2("edge", "b", "c");
    if (rc != MAELYS_OK) return rc;
    return maelys_datalog_wasm_solve();
}

static maelys_result_t explain_setup_truncated(void) {
    maelys_result_t rc = explain_load(k_explain_truncated_src);
    if (rc != MAELYS_OK) return rc;
    rc = maelys_datalog_wasm_edb_begin();
    if (rc != MAELYS_OK) return rc;
    for (int i = 0; i < 8; i++) {
        char from[8];
        char to[8];
        snprintf(from, sizeof(from), "n%d", i);
        snprintf(to, sizeof(to), "n%d", i + 1);
        rc = maelys_datalog_wasm_edb_add_symbol2("edge", from, to);
        if (rc != MAELYS_OK) return rc;
    }
    return maelys_datalog_wasm_solve();
}

/* Count-only then exact write into a caller buffer of required + 1 bytes. */
static int explain_expect_text(const char *predicate,
                               const char *arg0,
                               const char *arg1,
                               char *buffer,
                               size_t buffer_size,
                               const char *expected) {
    int32_t required = -1;
    int32_t found = -1;
    maelys_result_t rc =
        arg1 ? maelys_datalog_wasm_explain_symbol2_fact_text(predicate, arg0, arg1,
                                                             NULL, 0, &required, &found)
             : maelys_datalog_wasm_explain_symbol_fact_text(predicate, arg0,
                                                            NULL, 0, &required, &found);
    if (rc != MAELYS_OK || found != 1) {
        fprintf(stderr, "explain count-only rc=%d found=%d\n", (int)rc, found);
        return 0;
    }
    if (required != (int32_t)strlen(expected)) {
        fprintf(stderr, "explain size %d, expected %zu\n", required, strlen(expected));
        return 0;
    }
    if ((size_t)required + 1u > buffer_size) {
        fprintf(stderr, "explain buffer too small for %d bytes\n", required);
        return 0;
    }
    memset(buffer, 0x7f, buffer_size);
    int32_t write_required = -1;
    int32_t write_found = -1;
    rc = arg1 ? maelys_datalog_wasm_explain_symbol2_fact_text(predicate, arg0, arg1,
                                                              buffer, required + 1,
                                                              &write_required, &write_found)
              : maelys_datalog_wasm_explain_symbol_fact_text(predicate, arg0,
                                                             buffer, required + 1,
                                                             &write_required, &write_found);
    if (rc != MAELYS_OK || write_found != 1 || write_required != required) {
        fprintf(stderr, "explain write rc=%d found=%d size=%d\n",
                (int)rc, write_found, write_required);
        return 0;
    }
    if (strcmp(buffer, expected) != 0) {
        fprintf(stderr, "explain text mismatch.\n--- expected ---\n%s--- got ---\n%s",
                expected, buffer);
        return 0;
    }
    return 1;
}

static int test_wasm_explain_before_solve(void) {
    TEST_BEGIN();
    maelys_datalog_wasm_solve_result_free();
    int32_t required = 7;
    int32_t found = 7;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_STATE,
                      maelys_datalog_wasm_explain_symbol_fact_text("allow", "alice",
                                                                   NULL, 0,
                                                                   &required, &found),
                      "%d");
    TEST_ASSERT_EQUAL(0, required, "%d");
    TEST_ASSERT_EQUAL(0, found, "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_STATE,
                      maelys_datalog_wasm_explain_symbol2_fact_text("path", "a", "b",
                                                                    NULL, 0,
                                                                    &required, &found),
                      "%d");
    TEST_END();
}

static int test_wasm_explain_null_arguments(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, explain_setup_simple(), "%d");
    int32_t required = 5;
    int32_t found = 5;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_wasm_explain_symbol_fact_text("allow", "alice",
                                                                   NULL, 0, NULL, &found),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_wasm_explain_symbol_fact_text("allow", "alice",
                                                                   NULL, 0, &required, NULL),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_wasm_explain_symbol_fact_text(NULL, "alice",
                                                                   NULL, 0,
                                                                   &required, &found),
                      "%d");
    TEST_ASSERT_EQUAL(0, required, "%d");
    TEST_ASSERT_EQUAL(0, found, "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_wasm_explain_symbol_fact_text("allow", NULL,
                                                                   NULL, 0,
                                                                   &required, &found),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_wasm_explain_symbol2_fact_text("path", "a", NULL,
                                                                    NULL, 0,
                                                                    &required, &found),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_wasm_explain_symbol2_fact_text("path", NULL, "b",
                                                                    NULL, 0,
                                                                    &required, &found),
                      "%d");
    TEST_END();
}

static int test_wasm_explain_negative_capacity(void) {
    TEST_BEGIN();
    int32_t required = 3;
    int32_t found = 3;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_wasm_explain_symbol_fact_text("allow", "alice",
                                                                   g_explain_text, -1,
                                                                   &required, &found),
                      "%d");
    TEST_ASSERT_EQUAL(0, required, "%d");
    TEST_ASSERT_EQUAL(0, found, "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_wasm_explain_symbol_fact_text("allow", "alice",
                                                                   NULL, -8,
                                                                   &required, &found),
                      "%d");
    TEST_END();
}

static int test_wasm_explain_inconsistent_buffer_capacity(void) {
    TEST_BEGIN();
    int32_t required = 3;
    int32_t found = 3;
    /* Buffer without capacity. */
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_wasm_explain_symbol_fact_text("allow", "alice",
                                                                   g_explain_text, 0,
                                                                   &required, &found),
                      "%d");
    TEST_ASSERT_EQUAL(0, required, "%d");
    TEST_ASSERT_EQUAL(0, found, "%d");
    /* Capacity without buffer. */
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_wasm_explain_symbol_fact_text("allow", "alice",
                                                                   NULL, 16,
                                                                   &required, &found),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_wasm_explain_symbol2_fact_text("path", "a", "b",
                                                                    NULL, 16,
                                                                    &required, &found),
                      "%d");
    TEST_END();
}

static int test_wasm_explain_predicate_error_beats_unknown_symbol(void) {
    TEST_BEGIN();
    int32_t required = 4;
    int32_t found = 4;
    /* Absent predicate together with a never-interned term. */
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_wasm_explain_symbol_fact_text("missing", "ghost",
                                                                   NULL, 0,
                                                                   &required, &found),
                      "%d");
    TEST_ASSERT_EQUAL(0, required, "%d");
    TEST_ASSERT_EQUAL(0, found, "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_wasm_explain_symbol2_fact_text("missing", "ghost",
                                                                    "phantom", NULL, 0,
                                                                    &required, &found),
                      "%d");
    TEST_END();
}

static int test_wasm_explain_wrong_arity(void) {
    TEST_BEGIN();
    int32_t required = 4;
    int32_t found = 4;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_wasm_explain_symbol_fact_text("path", "a",
                                                                   NULL, 0,
                                                                   &required, &found),
                      "%d");
    TEST_ASSERT_EQUAL(0, required, "%d");
    TEST_ASSERT_EQUAL(0, found, "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_wasm_explain_symbol2_fact_text("allow", "alice", "bob",
                                                                    NULL, 0,
                                                                    &required, &found),
                      "%d");
    TEST_END();
}

static int test_wasm_explain_non_query_predicate(void) {
    TEST_BEGIN();
    int32_t required = 4;
    int32_t found = 4;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_wasm_explain_symbol_fact_text("internal", "alice",
                                                                   NULL, 0,
                                                                   &required, &found),
                      "%d");
    TEST_ASSERT_EQUAL(0, required, "%d");
    TEST_ASSERT_EQUAL(0, found, "%d");
    TEST_END();
}

static int test_wasm_explain_unknown_symbol_is_absent_and_readonly(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t *ruleset =
        (maelys_datalog_ruleset_t *)maelys_datalog_wasm_ruleset_ptr();
    TEST_ASSERT_NOT_NULL(ruleset);
    const size_t symbols_before = ruleset->symbols.count;

    int32_t required = 9;
    int32_t found = 9;
    memset(g_explain_text, 0x7f, sizeof(g_explain_text));
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_wasm_explain_symbol_fact_text("allow", "ghost",
                                                                   g_explain_text,
                                                                   (int32_t)sizeof(g_explain_text),
                                                                   &required, &found),
                      "%d");
    TEST_ASSERT_EQUAL(0, found, "%d");
    TEST_ASSERT_EQUAL(0, required, "%d");
    TEST_ASSERT_EQUAL('\0', g_explain_text[0], "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_wasm_explain_symbol2_fact_text("path", "a", "ghost",
                                                                    NULL, 0,
                                                                    &required, &found),
                      "%d");
    TEST_ASSERT_EQUAL(0, found, "%d");
    TEST_ASSERT_EQUAL(0, required, "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_wasm_explain_symbol2_fact_text("path", "ghost", "b",
                                                                    NULL, 0,
                                                                    &required, &found),
                      "%d");
    TEST_ASSERT_EQUAL(0, found, "%d");
    TEST_ASSERT_EQUAL(symbols_before, ruleset->symbols.count, "%zu");
    TEST_END();
}

static int test_wasm_explain_absent_idb_fact(void) {
    TEST_BEGIN();
    int32_t required = 9;
    int32_t found = 9;
    /* Both terms are interned, but path("c","a") was never derived. */
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_wasm_explain_symbol2_fact_text("path", "c", "a",
                                                                    NULL, 0,
                                                                    &required, &found),
                      "%d");
    TEST_ASSERT_EQUAL(0, found, "%d");
    TEST_ASSERT_EQUAL(0, required, "%d");
    TEST_END();
}

static int test_wasm_explain_complete_fact_count_then_write(void) {
    TEST_BEGIN();
    TEST_ASSERT_TRUE(explain_expect_text("allow", "alice", NULL,
                                         g_explain_text, sizeof(g_explain_text),
                                         k_explain_allow_alice));
    TEST_ASSERT_TRUE(explain_expect_text("path", "a", "b",
                                         g_explain_text, sizeof(g_explain_text),
                                         k_explain_path_ab));
    TEST_END();
}

static int test_wasm_explain_capacity_without_nul_is_too_large(void) {
    TEST_BEGIN();
    const int32_t exact = (int32_t)strlen(k_explain_allow_alice);
    int32_t required = 0;
    int32_t found = 0;
    memset(g_explain_text, 0x7f, sizeof(g_explain_text));
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE,
                      maelys_datalog_wasm_explain_symbol_fact_text("allow", "alice",
                                                                   g_explain_text, exact,
                                                                   &required, &found),
                      "%d");
    TEST_ASSERT_EQUAL(1, found, "%d");
    TEST_ASSERT_EQUAL(exact, required, "%d");
    TEST_ASSERT_EQUAL('\0', g_explain_text[0], "%d");
    /* No prefix was left behind beyond the terminator. */
    TEST_ASSERT_EQUAL(0x7f, (int)(unsigned char)g_explain_text[1], "%d");

    memset(g_explain_text, 0x7f, sizeof(g_explain_text));
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE,
                      maelys_datalog_wasm_explain_symbol_fact_text("allow", "alice",
                                                                   g_explain_text, 1,
                                                                   &required, &found),
                      "%d");
    TEST_ASSERT_EQUAL(1, found, "%d");
    TEST_ASSERT_EQUAL(exact, required, "%d");
    TEST_ASSERT_EQUAL('\0', g_explain_text[0], "%d");
    TEST_END();
}

static int test_wasm_explain_capacity_required_plus_one_succeeds(void) {
    TEST_BEGIN();
    const int32_t exact = (int32_t)strlen(k_explain_allow_alice);
    int32_t required = 0;
    int32_t found = 0;
    memset(g_explain_text, 0x7f, sizeof(g_explain_text));
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_wasm_explain_symbol_fact_text("allow", "alice",
                                                                   g_explain_text, exact + 1,
                                                                   &required, &found),
                      "%d");
    TEST_ASSERT_EQUAL(1, found, "%d");
    TEST_ASSERT_EQUAL(exact, required, "%d");
    TEST_ASSERT_EQUAL_STRING(k_explain_allow_alice, g_explain_text);
    /* Exactly required + 1 bytes were written. */
    TEST_ASSERT_EQUAL(0x7f, (int)(unsigned char)g_explain_text[exact + 1], "%d");
    TEST_END();
}

static int test_wasm_explain_repeated_call_is_byte_identical(void) {
    TEST_BEGIN();
    TEST_ASSERT_TRUE(explain_expect_text("path", "a", "b",
                                         g_explain_text, sizeof(g_explain_text),
                                         k_explain_path_ab));
    TEST_ASSERT_TRUE(explain_expect_text("path", "a", "b",
                                         g_explain_text_b, sizeof(g_explain_text_b),
                                         k_explain_path_ab));
    TEST_ASSERT_EQUAL(0, memcmp(g_explain_text, g_explain_text_b,
                                strlen(k_explain_path_ab) + 1u), "%d");
    TEST_END();
}

static int test_wasm_explain_policy_and_edb_facts_are_absent(void) {
    TEST_BEGIN();
    int32_t required = 9;
    int32_t found = 9;
    /* Present as a POLICY_FACT, not as a derived IDB fact. */
    TEST_ASSERT_EQUAL(1, maelys_datalog_wasm_query_symbol("mode", "strict"), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_wasm_explain_symbol_fact_text("mode", "strict",
                                                                   NULL, 0,
                                                                   &required, &found),
                      "%d");
    TEST_ASSERT_EQUAL(0, found, "%d");
    TEST_ASSERT_EQUAL(0, required, "%d");
    /* Present in the EDB only. */
    TEST_ASSERT_EQUAL(1, maelys_datalog_wasm_query_symbol("observed", "alice"), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_wasm_explain_symbol_fact_text("observed", "alice",
                                                                   NULL, 0,
                                                                   &required, &found),
                      "%d");
    TEST_ASSERT_EQUAL(0, found, "%d");
    TEST_ASSERT_EQUAL(0, required, "%d");
    TEST_END();
}

static int test_wasm_explain_scalars_zeroed_on_error(void) {
    TEST_BEGIN();
    static const char *const predicates[] = {"missing", "internal", "path"};
    for (size_t i = 0u; i < sizeof(predicates) / sizeof(predicates[0]); i++) {
        int32_t required = 4242;
        int32_t found = 4242;
        maelys_result_t rc =
            maelys_datalog_wasm_explain_symbol_fact_text(predicates[i], "alice",
                                                          NULL, 0, &required, &found);
        TEST_ASSERT_TRUE(rc != MAELYS_OK);
        TEST_ASSERT_EQUAL(0, required, "%d");
        TEST_ASSERT_EQUAL(0, found, "%d");
    }
    /* An error after a successful call still resets both scalars. */
    int32_t required = 0;
    int32_t found = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_wasm_explain_symbol_fact_text("allow", "alice",
                                                                   NULL, 0,
                                                                   &required, &found),
                      "%d");
    TEST_ASSERT_EQUAL(1, found, "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_wasm_explain_symbol_fact_text("internal", "alice",
                                                                   NULL, 0,
                                                                   &required, &found),
                      "%d");
    TEST_ASSERT_EQUAL(0, required, "%d");
    TEST_ASSERT_EQUAL(0, found, "%d");
    TEST_END();
}

static int test_wasm_explain_does_not_mutate_state(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t *ruleset =
        (maelys_datalog_ruleset_t *)maelys_datalog_wasm_ruleset_ptr();
    TEST_ASSERT_NOT_NULL(ruleset);
    const int32_t derived_before = maelys_datalog_wasm_derived_fact_count();
    TEST_ASSERT_TRUE(derived_before > 0);
    memcpy(&g_explain_ruleset_snapshot, ruleset, sizeof(g_explain_ruleset_snapshot));

    int32_t required = 0;
    int32_t found = 0;
    TEST_ASSERT_TRUE(explain_expect_text("allow", "alice", NULL,
                                         g_explain_text, sizeof(g_explain_text),
                                         k_explain_allow_alice));
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_wasm_explain_symbol_fact_text("allow", "ghost",
                                                                   NULL, 0,
                                                                   &required, &found),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_wasm_explain_symbol_fact_text("internal", "alice",
                                                                   NULL, 0,
                                                                   &required, &found),
                      "%d");

    /* The whole ruleset — symbol table, predicate registry, rules and policy
     * facts — is byte-identical, and the solve result still answers the same. */
    TEST_ASSERT_EQUAL(0, memcmp(&g_explain_ruleset_snapshot, ruleset,
                                sizeof(g_explain_ruleset_snapshot)), "%d");
    TEST_ASSERT_EQUAL(derived_before, maelys_datalog_wasm_derived_fact_count(), "%d");
    TEST_ASSERT_EQUAL(1, maelys_datalog_wasm_query_symbol("allow", "alice"), "%d");
    TEST_ASSERT_EQUAL(1, maelys_datalog_wasm_query_symbol2("path", "a", "b"), "%d");
    TEST_END();
}

static int test_wasm_explain_truncated_scenario_from_public_apis(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, explain_setup_truncated(), "%d");
    const int32_t derived = maelys_datalog_wasm_derived_fact_count();
    /* 36 path + 36 reach facts, beyond the 64 proof nodes the solver keeps. */
    TEST_ASSERT_EQUAL(72, derived, "%d");
    TEST_ASSERT_TRUE(derived > (int32_t)MAELYS_DATALOG_MAX_PROOF_NODES);
    TEST_ASSERT_EQUAL(1, maelys_datalog_wasm_query_symbol2("path", "n0", "n8"), "%d");
    TEST_ASSERT_TRUE(explain_expect_text("path", "n0", "n8",
                                         g_explain_text, sizeof(g_explain_text),
                                         k_explain_truncated_text));
    /* A truncated explanation is never confused with an absence. */
    int32_t required = 9;
    int32_t found = 9;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_wasm_explain_symbol2_fact_text("path", "n8", "n0",
                                                                    NULL, 0,
                                                                    &required, &found),
                      "%d");
    TEST_ASSERT_EQUAL(0, found, "%d");
    TEST_ASSERT_EQUAL(0, required, "%d");
    maelys_datalog_wasm_solve_result_free();
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
        {"maelys_datalog_wasm_explain/before_solve", TEST_MODE_NON_BLOCKING, test_wasm_explain_before_solve},
        {"maelys_datalog_wasm_explain/null_arguments", TEST_MODE_NON_BLOCKING, test_wasm_explain_null_arguments},
        {"maelys_datalog_wasm_explain/negative_capacity", TEST_MODE_NON_BLOCKING, test_wasm_explain_negative_capacity},
        {"maelys_datalog_wasm_explain/inconsistent_buffer_capacity", TEST_MODE_NON_BLOCKING, test_wasm_explain_inconsistent_buffer_capacity},
        {"maelys_datalog_wasm_explain/predicate_error_beats_unknown_symbol", TEST_MODE_NON_BLOCKING, test_wasm_explain_predicate_error_beats_unknown_symbol},
        {"maelys_datalog_wasm_explain/wrong_arity", TEST_MODE_NON_BLOCKING, test_wasm_explain_wrong_arity},
        {"maelys_datalog_wasm_explain/non_query_predicate", TEST_MODE_NON_BLOCKING, test_wasm_explain_non_query_predicate},
        {"maelys_datalog_wasm_explain/unknown_symbol_is_absent_and_readonly", TEST_MODE_NON_BLOCKING, test_wasm_explain_unknown_symbol_is_absent_and_readonly},
        {"maelys_datalog_wasm_explain/absent_idb_fact", TEST_MODE_NON_BLOCKING, test_wasm_explain_absent_idb_fact},
        {"maelys_datalog_wasm_explain/complete_fact_count_then_write", TEST_MODE_NON_BLOCKING, test_wasm_explain_complete_fact_count_then_write},
        {"maelys_datalog_wasm_explain/capacity_without_nul_is_too_large", TEST_MODE_NON_BLOCKING, test_wasm_explain_capacity_without_nul_is_too_large},
        {"maelys_datalog_wasm_explain/capacity_required_plus_one_succeeds", TEST_MODE_NON_BLOCKING, test_wasm_explain_capacity_required_plus_one_succeeds},
        {"maelys_datalog_wasm_explain/repeated_call_is_byte_identical", TEST_MODE_NON_BLOCKING, test_wasm_explain_repeated_call_is_byte_identical},
        {"maelys_datalog_wasm_explain/policy_and_edb_facts_are_absent", TEST_MODE_NON_BLOCKING, test_wasm_explain_policy_and_edb_facts_are_absent},
        {"maelys_datalog_wasm_explain/scalars_zeroed_on_error", TEST_MODE_NON_BLOCKING, test_wasm_explain_scalars_zeroed_on_error},
        {"maelys_datalog_wasm_explain/does_not_mutate_state", TEST_MODE_NON_BLOCKING, test_wasm_explain_does_not_mutate_state},
        {"maelys_datalog_wasm_explain/truncated_scenario_from_public_apis", TEST_MODE_NON_BLOCKING, test_wasm_explain_truncated_scenario_from_public_apis},
    };
    return test_main("maelys_datalog_wasm_builder", cases, (int)(sizeof(cases) / sizeof(cases[0])), argc, argv);
}
