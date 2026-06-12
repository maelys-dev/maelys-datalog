#include "common/maelys_sha256.h"
#include "src/core/maelys_datalog_domain_registry.h"
#include "src/core/maelys_datalog_edb.h"
#include "src/core/maelys_datalog_symbol_table.h"
#include "src/manifest/maelys_datalog_manifest.h"
#include "tests/helpers/test_framework.h"

#include <string.h>

static const char k_inline_domain[] = "inline_test";
static const char k_policy_id[] = "inline-policy";
static const char k_policy_src[] = "allow(X) :- safe(X).\n";

static maelys_result_t install_inline_test_predicates(maelys_datalog_predicate_registry_t *reg) {
    maelys_result_t rc = maelys_datalog_predicate_registry_add_domain(
        reg, "safe", 1, MAELYS_DATALOG_PRED_KIND_EDB);
    if (rc != MAELYS_OK) return rc;
    return maelys_datalog_predicate_registry_add_domain(
        reg, "allow", 1, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY);
}

static maelys_result_t register_inline_test_domain(void) {
    static const maelys_datalog_domain_def_t def = {
        .domain_name = k_inline_domain,
        .predicates = NULL,
        .predicate_count = 0,
        .description = "Inline loader test domain",
        .install_predicates = install_inline_test_predicates,
    };
    return maelys_datalog_domain_registry_register(&def);
}

static maelys_result_t load_inline_source(const char *src,
                                          size_t src_len,
                                          maelys_datalog_policy_set_t *set,
                                          maelys_datalog_diagnostic_t *diag) {
    maelys_result_t rc = register_inline_test_domain();
    if (rc != MAELYS_OK) return rc;
    return maelys_datalog_load_policy_inline(k_inline_domain,
                                             k_policy_id,
                                             src,
                                             src_len,
                                             0,
                                             set,
                                             diag);
}

static maelys_datalog_term_t symbol_term(maelys_datalog_ruleset_t *ruleset, const char *text) {
    maelys_datalog_symbol_id_t id = 0;
    (void)maelys_datalog_symbol_intern(&ruleset->symbols, text, strlen(text), &id);
    maelys_datalog_term_t term = {.kind = MAELYS_DATALOG_TERM_SYMBOL};
    term.as.symbol = id;
    return term;
}

static int test_inline_load_basic(void) {
    TEST_BEGIN();
    maelys_datalog_policy_set_t set;
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_OK, load_inline_source(k_policy_src, strlen(k_policy_src), &set, &diag), "%d");
    TEST_ASSERT_EQUAL((size_t)1u, set.policy_count, "%zu");
    TEST_ASSERT_EQUAL_STRING(k_policy_id, set.policies[0].policy_id);
    TEST_ASSERT_EQUAL_STRING(k_inline_domain, set.policies[0].domain);
    maelys_datalog_policy_set_clear(&set);
    TEST_END();
}

static int test_inline_load_null_args(void) {
    TEST_BEGIN();
    maelys_datalog_policy_set_t set;
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_load_policy_inline(k_inline_domain,
                                                        k_policy_id,
                                                        k_policy_src,
                                                        strlen(k_policy_src),
                                                        0,
                                                        NULL,
                                                        &diag),
                      "%d");
    set.policy_count = 99u;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_load_policy_inline(NULL,
                                                        k_policy_id,
                                                        k_policy_src,
                                                        strlen(k_policy_src),
                                                        0,
                                                        &set,
                                                        &diag),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, set.policy_count, "%zu");
    set.policy_count = 99u;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_load_policy_inline(k_inline_domain,
                                                        NULL,
                                                        k_policy_src,
                                                        strlen(k_policy_src),
                                                        0,
                                                        &set,
                                                        &diag),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, set.policy_count, "%zu");
    set.policy_count = 99u;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_load_policy_inline(k_inline_domain,
                                                        k_policy_id,
                                                        NULL,
                                                        strlen(k_policy_src),
                                                        0,
                                                        &set,
                                                        &diag),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, set.policy_count, "%zu");
    TEST_END();
}

static int test_inline_load_out_diag_null(void) {
    TEST_BEGIN();
    maelys_datalog_policy_set_t set;
    TEST_ASSERT_EQUAL(MAELYS_OK, load_inline_source(k_policy_src, strlen(k_policy_src), &set, NULL), "%d");
    TEST_ASSERT_EQUAL((size_t)1u, set.policy_count, "%zu");
    maelys_datalog_policy_set_clear(&set);
    TEST_END();
}

static int test_inline_load_empty_domain(void) {
    TEST_BEGIN();
    maelys_datalog_policy_set_t set;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_load_policy_inline("",
                                                        k_policy_id,
                                                        k_policy_src,
                                                        strlen(k_policy_src),
                                                        0,
                                                        &set,
                                                        NULL),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, set.policy_count, "%zu");
    TEST_END();
}

static int test_inline_load_empty_policy_id(void) {
    TEST_BEGIN();
    maelys_datalog_policy_set_t set;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_load_policy_inline(k_inline_domain,
                                                        "",
                                                        k_policy_src,
                                                        strlen(k_policy_src),
                                                        0,
                                                        &set,
                                                        NULL),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, set.policy_count, "%zu");
    TEST_END();
}

static int test_inline_load_nonzero_flags(void) {
    TEST_BEGIN();
    maelys_datalog_policy_set_t set;
    set.policy_count = 99u;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_load_policy_inline(k_inline_domain,
                                                        k_policy_id,
                                                        k_policy_src,
                                                        strlen(k_policy_src),
                                                        MAELYS_DATALOG_MANIFEST_ALLOW_TEST_ONLY,
                                                        &set,
                                                        NULL),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, set.policy_count, "%zu");
    TEST_END();
}

static int test_inline_load_zero_src_len(void) {
    TEST_BEGIN();
    maelys_datalog_policy_set_t set;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_load_policy_inline(k_inline_domain,
                                                        k_policy_id,
                                                        k_policy_src,
                                                        0u,
                                                        0,
                                                        &set,
                                                        NULL),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, set.policy_count, "%zu");
    TEST_END();
}

static int test_inline_load_unknown_domain(void) {
    TEST_BEGIN();
    maelys_datalog_policy_set_t set;
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_UNSUPPORTED,
                      maelys_datalog_load_policy_inline("missing_inline_domain",
                                                        k_policy_id,
                                                        k_policy_src,
                                                        strlen(k_policy_src),
                                                        0,
                                                        &set,
                                                        &diag),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, set.policy_count, "%zu");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_MANIFEST_UNKNOWN_DOMAIN, diag.code, "%d");
    TEST_END();
}

static int test_inline_load_lexer_error(void) {
    TEST_BEGIN();
    const char invalid_src[] = {'a', 'l', 'l', 'o', 'w', '(', '"', (char)0xff, '"', ')', '.', '\n'};
    maelys_datalog_policy_set_t set;
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      load_inline_source(invalid_src, sizeof(invalid_src), &set, &diag),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, set.policy_count, "%zu");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_LEXER_INVALID_UTF8, diag.code, "%d");
    TEST_END();
}

static int test_inline_load_syntax_error(void) {
    TEST_BEGIN();
    const char src[] = "allow(X) :- safe(X)\n";
    maelys_datalog_policy_set_t set;
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, load_inline_source(src, strlen(src), &set, &diag), "%d");
    TEST_ASSERT_EQUAL((size_t)0u, set.policy_count, "%zu");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_PARSER_EXPECTED_DOT, diag.code, "%d");
    TEST_END();
}

static int test_inline_load_unknown_predicate(void) {
    TEST_BEGIN();
    const char src[] = "unknown(X) :- safe(X).\n";
    maelys_datalog_policy_set_t set;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, load_inline_source(src, strlen(src), &set, NULL), "%d");
    TEST_ASSERT_EQUAL((size_t)0u, set.policy_count, "%zu");
    TEST_END();
}

static int test_inline_load_solve_query(void) {
    TEST_BEGIN();
    maelys_datalog_policy_set_t set;
    TEST_ASSERT_EQUAL(MAELYS_OK, load_inline_source(k_policy_src, strlen(k_policy_src), &set, NULL), "%d");
    maelys_datalog_fact_t facts[4];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_edb_init(&edb,
                                              facts,
                                              sizeof(facts) / sizeof(facts[0]),
                                              &set.policies[0].symbols,
                                              &set.policies[0].registry),
                      "%d");
    maelys_datalog_term_t alice = symbol_term(&set.policies[0], "alice");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_add_fact(&edb, "safe", &alice, 1), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&set.policies[0], &edb, &result), "%d");
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_query_solved_ground_fact(result, "allow", &alice, 1, &present),
                      "%d");
    TEST_ASSERT_TRUE(present);
    maelys_datalog_solve_result_free(result);
    maelys_datalog_policy_set_clear(&set);
    TEST_END();
}

static int test_inline_load_policy_id_special_chars(void) {
    TEST_BEGIN();
    const char policy_id[] = "quote\"slash\\id";
    maelys_datalog_policy_set_t set;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_load_policy_inline(k_inline_domain,
                                                        policy_id,
                                                        k_policy_src,
                                                        strlen(k_policy_src),
                                                        0,
                                                        &set,
                                                        NULL),
                      "%d");
    TEST_ASSERT_EQUAL_STRING(policy_id, set.policies[0].policy_id);
    maelys_datalog_policy_set_clear(&set);
    TEST_END();
}

static int test_inline_load_domain_too_long(void) {
    TEST_BEGIN();
    char domain[MAELYS_DATALOG_INLINE_MAX_DOMAIN_LEN + 2u];
    memset(domain, 'd', sizeof(domain) - 1u);
    domain[sizeof(domain) - 1u] = '\0';
    maelys_datalog_policy_set_t set;
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE,
                      maelys_datalog_load_policy_inline(domain,
                                                        k_policy_id,
                                                        k_policy_src,
                                                        strlen(k_policy_src),
                                                        0,
                                                        &set,
                                                        NULL),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, set.policy_count, "%zu");
    TEST_END();
}

static int test_inline_load_policy_id_too_long(void) {
    TEST_BEGIN();
    char policy_id[MAELYS_DATALOG_INLINE_MAX_POLICY_ID_LEN + 2u];
    memset(policy_id, 'p', sizeof(policy_id) - 1u);
    policy_id[sizeof(policy_id) - 1u] = '\0';
    maelys_datalog_policy_set_t set;
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE,
                      maelys_datalog_load_policy_inline(k_inline_domain,
                                                        policy_id,
                                                        k_policy_src,
                                                        strlen(k_policy_src),
                                                        0,
                                                        &set,
                                                        NULL),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, set.policy_count, "%zu");
    TEST_END();
}

static int test_inline_load_source_not_nul_terminated(void) {
    TEST_BEGIN();
    const char literal[] = "allow(X) :- safe(X).\n";
    size_t len = strlen(literal);
    char buf[64];
    memset(buf, '#', sizeof(buf));
    memcpy(buf, literal, len);
    maelys_datalog_policy_set_t set;
    TEST_ASSERT_EQUAL(MAELYS_OK, load_inline_source(buf, len, &set, NULL), "%d");
    TEST_ASSERT_EQUAL((size_t)1u, set.policy_count, "%zu");
    maelys_datalog_policy_set_clear(&set);
    TEST_END();
}

static int test_inline_load_source_with_embedded_nul(void) {
    TEST_BEGIN();
    const char src[] = {'a', 'l', 'l', 'o', 'w', '\0', '(', 'X', ')', '.', '\n'};
    maelys_datalog_policy_set_t set;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, load_inline_source(src, sizeof(src), &set, NULL), "%d");
    TEST_ASSERT_EQUAL((size_t)0u, set.policy_count, "%zu");
    TEST_END();
}

static int test_inline_load_existing_lexer_parser_structural_limit(void) {
    TEST_BEGIN();
    char src[MAELYS_DATALOG_MAX_TOKEN_BYTES + 16u];
    size_t n = MAELYS_DATALOG_MAX_TOKEN_BYTES + 1u;
    memset(src, 'a', n);
    memcpy(src + n, "(X).\n", 5u);
    maelys_datalog_policy_set_t set;
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE,
                      load_inline_source(src, n + 5u, &set, NULL),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, set.policy_count, "%zu");
    TEST_END();
}

static int test_inline_load_no_manifest_mode(void) {
    TEST_BEGIN();
    maelys_datalog_policy_set_t set;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_load_policy_inline(k_inline_domain,
                                                        k_policy_id,
                                                        k_policy_src,
                                                        strlen(k_policy_src),
                                                        MAELYS_DATALOG_MANIFEST_ALLOW_TEST_ONLY,
                                                        &set,
                                                        NULL),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, set.policy_count, "%zu");
    TEST_END();
}

static int test_inline_load_synthetic_metadata(void) {
    TEST_BEGIN();
    maelys_datalog_policy_set_t set;
    TEST_ASSERT_EQUAL(MAELYS_OK, load_inline_source(k_policy_src, strlen(k_policy_src), &set, NULL), "%d");
    char expected_sha[65];
    TEST_ASSERT_EQUAL(0,
                      maelys_sha256_hex((const unsigned char *)k_policy_src,
                                        strlen(k_policy_src),
                                        expected_sha),
                      "%d");
    TEST_ASSERT_EQUAL_STRING(k_policy_id, set.policies[0].policy_id);
    TEST_ASSERT_EQUAL_STRING(k_inline_domain, set.policies[0].domain);
    TEST_ASSERT_EQUAL_STRING(expected_sha, set.policies[0].sha256);
    TEST_ASSERT_EQUAL(0, set.policies[0].test_only, "%d");
    maelys_datalog_policy_set_clear(&set);
    TEST_END();
}

static int test_inline_v1_private_idb_requires_domain_declaration(void) {
    TEST_BEGIN();
    const char src[] =
        "helper(X) :- safe(X).\n"
        "allow(X) :- helper(X).\n";
    maelys_datalog_policy_set_t set;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, load_inline_source(src, strlen(src), &set, NULL), "%d");
    TEST_ASSERT_EQUAL((size_t)0u, set.policy_count, "%zu");
    TEST_END();
}

int main(int argc, char **argv) {
    test_case_t cases[] = {
        {"inline_load/basic", TEST_MODE_NON_BLOCKING, test_inline_load_basic},
        {"inline_load/null_args", TEST_MODE_NON_BLOCKING, test_inline_load_null_args},
        {"inline_load/out_diag_null", TEST_MODE_NON_BLOCKING, test_inline_load_out_diag_null},
        {"inline_load/empty_domain", TEST_MODE_NON_BLOCKING, test_inline_load_empty_domain},
        {"inline_load/empty_policy_id", TEST_MODE_NON_BLOCKING, test_inline_load_empty_policy_id},
        {"inline_load/nonzero_flags", TEST_MODE_NON_BLOCKING, test_inline_load_nonzero_flags},
        {"inline_load/zero_src_len", TEST_MODE_NON_BLOCKING, test_inline_load_zero_src_len},
        {"inline_load/unknown_domain", TEST_MODE_NON_BLOCKING, test_inline_load_unknown_domain},
        {"inline_load/lexer_error", TEST_MODE_NON_BLOCKING, test_inline_load_lexer_error},
        {"inline_load/syntax_error", TEST_MODE_NON_BLOCKING, test_inline_load_syntax_error},
        {"inline_load/unknown_predicate", TEST_MODE_NON_BLOCKING, test_inline_load_unknown_predicate},
        {"inline_load/solve_query", TEST_MODE_NON_BLOCKING, test_inline_load_solve_query},
        {"inline_load/policy_id_special_chars", TEST_MODE_NON_BLOCKING, test_inline_load_policy_id_special_chars},
        {"inline_load/domain_too_long", TEST_MODE_NON_BLOCKING, test_inline_load_domain_too_long},
        {"inline_load/policy_id_too_long", TEST_MODE_NON_BLOCKING, test_inline_load_policy_id_too_long},
        {"inline_load/source_not_nul_terminated", TEST_MODE_NON_BLOCKING, test_inline_load_source_not_nul_terminated},
        {"inline_load/source_with_embedded_nul", TEST_MODE_NON_BLOCKING, test_inline_load_source_with_embedded_nul},
        {"inline_load/existing_structural_limit", TEST_MODE_NON_BLOCKING, test_inline_load_existing_lexer_parser_structural_limit},
        {"inline_load/no_manifest_mode", TEST_MODE_NON_BLOCKING, test_inline_load_no_manifest_mode},
        {"inline_load/synthetic_metadata", TEST_MODE_NON_BLOCKING, test_inline_load_synthetic_metadata},
        {"inline_load/private_idb_requires_domain", TEST_MODE_NON_BLOCKING, test_inline_v1_private_idb_requires_domain_declaration},
    };
    return test_main("maelys_datalog_inline_load", cases, (int)(sizeof(cases) / sizeof(cases[0])), argc, argv);
}
