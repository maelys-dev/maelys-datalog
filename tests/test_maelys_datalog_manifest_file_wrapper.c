#include "common/maelys_sha256.h"
#include "src/core/maelys_datalog_domain_registry.h"
#include "src/core/maelys_datalog_edb.h"
#include "src/core/maelys_datalog_solver.h"
#include "src/core/maelys_datalog_symbol_table.h"
#include "src/manifest/maelys_datalog_manifest.h"
#include "tests/helpers/test_framework.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char k_domain[] = "file_wrapper_test";
static const char k_policy_id[] = "file_wrapper_test.main";
static const char k_policy_src[] = "allow(X) :- safe(X).\n";
static const char k_zero_sha[] =
    "0000000000000000000000000000000000000000000000000000000000000000";

typedef struct {
    char dir[256];
    char manifest_path[512];
    char policy_path[512];
    int active;
} file_fixture_t;

static maelys_result_t install_file_wrapper_predicates(maelys_datalog_predicate_registry_t *reg) {
    maelys_result_t rc = maelys_datalog_predicate_registry_add_domain(
        reg, "safe", 1, MAELYS_DATALOG_PRED_KIND_EDB);
    if (rc != MAELYS_OK) return rc;
    return maelys_datalog_predicate_registry_add_domain(
        reg, "allow", 1, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY);
}

static maelys_result_t register_file_wrapper_domain(void) {
    static const maelys_datalog_domain_def_t def = {
        .domain_name = k_domain,
        .predicates = NULL,
        .predicate_count = 0,
        .description = "Manifest file wrapper regression domain",
        .install_predicates = install_file_wrapper_predicates,
    };
    return maelys_datalog_domain_registry_register(&def);
}

static int init_fixture(file_fixture_t *fx) {
    if (!fx) return 0;
    memset(fx, 0, sizeof(*fx));
    char templ[] = "/tmp/maelys_datalog_filewrap_XXXXXX";
    char *dir = mkdtemp(templ);
    if (!dir) return 0;
    snprintf(fx->dir, sizeof(fx->dir), "%s", dir);
    snprintf(fx->manifest_path, sizeof(fx->manifest_path), "%s/manifest.json", fx->dir);
    snprintf(fx->policy_path, sizeof(fx->policy_path), "%s/policy.dl", fx->dir);
    fx->active = 1;
    return 1;
}

static void cleanup_fixture(file_fixture_t *fx) {
    if (!fx || !fx->active) return;
    (void)unlink(fx->manifest_path);
    (void)unlink(fx->policy_path);
    (void)rmdir(fx->dir);
    fx->active = 0;
}

static int write_file_text(const char *path, const char *src, size_t src_len) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    size_t n = fwrite(src, 1u, src_len, f);
    int err = ferror(f);
    int close_rc = fclose(f);
    return !err && close_rc == 0 && n == src_len;
}

static void sha_bytes(const char *src, size_t len, char out[65]) {
    (void)maelys_sha256_hex((const unsigned char *)src, len, out);
}

static void build_manifest(char *out,
                           size_t out_cap,
                           const char *domain,
                           const char *file,
                           const char *sha,
                           const char *mode,
                           int enabled) {
    snprintf(out,
             out_cap,
             "{\"policy_set_id\":\"file_wrapper_test.set\","
             "\"policy_set_version\":\"1\","
             "\"manifest_version\":\"1\","
             "\"default_profile\":\"enforce\","
             "\"created_for\":\"test\","
             "\"strict_loading\":true,"
             "\"fail_closed\":true,"
             "\"capabilities\":[],"
             "\"policies\":[{"
             "\"policy_id\":\"%s\","
             "\"domain\":\"%s\","
             "\"file\":\"%s\","
             "\"sha256\":\"%s\","
             "\"mode\":\"%s\","
             "\"enabled\":%s,"
             "\"description\":\"file wrapper regression policy\","
             "\"queries\":[{\"name\":\"allow\",\"arity\":1}]"
             "}]}",
             k_policy_id,
             domain,
             file,
             sha,
             mode,
             enabled ? "true" : "false");
}

static int write_manifest(file_fixture_t *fx,
                          const char *domain,
                          const char *file,
                          const char *sha,
                          const char *mode,
                          int enabled) {
    char manifest[2048];
    build_manifest(manifest, sizeof(manifest), domain, file, sha, mode, enabled);
    return write_file_text(fx->manifest_path, manifest, strlen(manifest));
}

static int write_valid_policy_and_manifest(file_fixture_t *fx, const char *mode, int enabled) {
    char sha[65];
    sha_bytes(k_policy_src, strlen(k_policy_src), sha);
    if (enabled && !write_file_text(fx->policy_path, k_policy_src, strlen(k_policy_src))) return 0;
    return write_manifest(fx, k_domain, "policy.dl", sha, mode, enabled);
}

static maelys_datalog_term_t symbol_term(maelys_datalog_ruleset_t *ruleset, const char *text) {
    maelys_datalog_symbol_id_t id = 0;
    (void)maelys_datalog_symbol_intern(&ruleset->symbols, text, strlen(text), &id);
    maelys_datalog_term_t term = {.kind = MAELYS_DATALOG_TERM_SYMBOL};
    term.as.symbol = id;
    return term;
}

static int test_manifest_file_wrapper_load_ex_basic(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, register_file_wrapper_domain(), "%d");
    file_fixture_t fx;
    TEST_ASSERT_TRUE(init_fixture(&fx));
    TEST_ASSERT_TRUE(write_valid_policy_and_manifest(&fx, "enforce", 1));
    maelys_datalog_policy_set_t set;
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_manifest_load_ex(fx.manifest_path, 0, &set, &diag),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)1u, set.policy_count, "%zu");
    TEST_ASSERT_EQUAL_STRING(k_policy_id, set.policies[0].policy_id);
    TEST_ASSERT_EQUAL_STRING(k_domain, set.policies[0].domain);
    maelys_datalog_policy_set_clear(&set);
    cleanup_fixture(&fx);
    TEST_END();
}

static int test_manifest_file_wrapper_load_simple_matches_ex_success(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, register_file_wrapper_domain(), "%d");
    file_fixture_t fx;
    TEST_ASSERT_TRUE(init_fixture(&fx));
    TEST_ASSERT_TRUE(write_valid_policy_and_manifest(&fx, "enforce", 1));
    maelys_datalog_policy_set_t simple_set;
    maelys_datalog_policy_set_t ex_set;
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_manifest_load_ex(fx.manifest_path, 0, &ex_set, &diag),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_manifest_load(fx.manifest_path, 0, &simple_set),
                      "%d");
    TEST_ASSERT_EQUAL(ex_set.policy_count, simple_set.policy_count, "%zu");
    TEST_ASSERT_EQUAL_STRING(ex_set.policies[0].policy_id, simple_set.policies[0].policy_id);
    TEST_ASSERT_EQUAL_STRING(ex_set.policies[0].domain, simple_set.policies[0].domain);
    maelys_datalog_policy_set_clear(&simple_set);
    maelys_datalog_policy_set_clear(&ex_set);
    cleanup_fixture(&fx);
    TEST_END();
}

static int test_manifest_file_wrapper_load_ex_null_diag(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, register_file_wrapper_domain(), "%d");
    file_fixture_t fx;
    TEST_ASSERT_TRUE(init_fixture(&fx));
    TEST_ASSERT_TRUE(write_valid_policy_and_manifest(&fx, "enforce", 1));
    maelys_datalog_policy_set_t set;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_manifest_load_ex(fx.manifest_path, 0, &set, NULL),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)1u, set.policy_count, "%zu");
    maelys_datalog_policy_set_clear(&set);
    cleanup_fixture(&fx);
    TEST_END();
}

static int test_manifest_file_wrapper_missing_manifest(void) {
    TEST_BEGIN();
    file_fixture_t fx;
    TEST_ASSERT_TRUE(init_fixture(&fx));
    maelys_datalog_policy_set_t set;
    set.policy_count = 99u;
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_NOT_FOUND,
                      maelys_datalog_manifest_load_ex(fx.manifest_path, 0, &set, &diag),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, set.policy_count, "%zu");
    cleanup_fixture(&fx);
    TEST_END();
}

static int test_manifest_file_wrapper_missing_policy_file(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, register_file_wrapper_domain(), "%d");
    file_fixture_t fx;
    TEST_ASSERT_TRUE(init_fixture(&fx));
    char sha[65];
    sha_bytes(k_policy_src, strlen(k_policy_src), sha);
    TEST_ASSERT_TRUE(write_manifest(&fx, k_domain, "policy.dl", sha, "enforce", 1));
    maelys_datalog_policy_set_t set;
    set.policy_count = 99u;
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_NOT_FOUND,
                      maelys_datalog_manifest_load_ex(fx.manifest_path, 0, &set, &diag),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, set.policy_count, "%zu");
    cleanup_fixture(&fx);
    TEST_END();
}

static int test_manifest_file_wrapper_sha_mismatch(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, register_file_wrapper_domain(), "%d");
    file_fixture_t fx;
    TEST_ASSERT_TRUE(init_fixture(&fx));
    TEST_ASSERT_TRUE(write_file_text(fx.policy_path, k_policy_src, strlen(k_policy_src)));
    TEST_ASSERT_TRUE(write_manifest(&fx, k_domain, "policy.dl", k_zero_sha, "enforce", 1));
    maelys_datalog_policy_set_t set;
    set.policy_count = 99u;
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_manifest_load_ex(fx.manifest_path, 0, &set, &diag),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, set.policy_count, "%zu");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_MANIFEST_SHA_MISMATCH, diag.code, "%d");
    cleanup_fixture(&fx);
    TEST_END();
}

static int test_manifest_file_wrapper_test_only_rejected(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, register_file_wrapper_domain(), "%d");
    file_fixture_t fx;
    TEST_ASSERT_TRUE(init_fixture(&fx));
    TEST_ASSERT_TRUE(write_valid_policy_and_manifest(&fx, "test_only", 1));
    maelys_datalog_policy_set_t set;
    set.policy_count = 99u;
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_FORBIDDEN,
                      maelys_datalog_manifest_load_ex(fx.manifest_path, 0, &set, &diag),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, set.policy_count, "%zu");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_MANIFEST_TEST_ONLY_REJECTED, diag.code, "%d");
    cleanup_fixture(&fx);
    TEST_END();
}

static int test_manifest_file_wrapper_test_only_allowed_with_flag(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, register_file_wrapper_domain(), "%d");
    file_fixture_t fx;
    TEST_ASSERT_TRUE(init_fixture(&fx));
    TEST_ASSERT_TRUE(write_valid_policy_and_manifest(&fx, "test_only", 1));
    maelys_datalog_policy_set_t set;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_manifest_load_ex(fx.manifest_path,
                                                      MAELYS_DATALOG_MANIFEST_ALLOW_TEST_ONLY,
                                                      &set,
                                                      NULL),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)1u, set.policy_count, "%zu");
    TEST_ASSERT_EQUAL(1, set.policies[0].test_only, "%d");
    maelys_datalog_policy_set_clear(&set);
    cleanup_fixture(&fx);
    TEST_END();
}

static int test_manifest_file_wrapper_unknown_domain(void) {
    TEST_BEGIN();
    file_fixture_t fx;
    TEST_ASSERT_TRUE(init_fixture(&fx));
    char sha[65];
    sha_bytes(k_policy_src, strlen(k_policy_src), sha);
    TEST_ASSERT_TRUE(write_file_text(fx.policy_path, k_policy_src, strlen(k_policy_src)));
    TEST_ASSERT_TRUE(write_manifest(&fx, "missing_file_wrapper_domain", "policy.dl", sha, "enforce", 1));
    maelys_datalog_policy_set_t set;
    set.policy_count = 99u;
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_UNSUPPORTED,
                      maelys_datalog_manifest_load_ex(fx.manifest_path, 0, &set, &diag),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, set.policy_count, "%zu");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_MANIFEST_UNKNOWN_DOMAIN, diag.code, "%d");
    cleanup_fixture(&fx);
    TEST_END();
}

static int test_manifest_file_wrapper_disabled_policy_skipped(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, register_file_wrapper_domain(), "%d");
    file_fixture_t fx;
    TEST_ASSERT_TRUE(init_fixture(&fx));
    TEST_ASSERT_TRUE(write_manifest(&fx, k_domain, "missing-disabled-policy.dl", k_zero_sha, "enforce", 0));
    maelys_datalog_policy_set_t set;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_manifest_load_ex(fx.manifest_path, 0, &set, NULL),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, set.policy_count, "%zu");
    cleanup_fixture(&fx);
    TEST_END();
}

static int test_manifest_file_wrapper_out_set_cleared_on_failure(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, register_file_wrapper_domain(), "%d");
    file_fixture_t fx;
    TEST_ASSERT_TRUE(init_fixture(&fx));
    TEST_ASSERT_TRUE(write_valid_policy_and_manifest(&fx, "enforce", 1));
    maelys_datalog_policy_set_t set;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_manifest_load_ex(fx.manifest_path, 0, &set, NULL),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)1u, set.policy_count, "%zu");
    maelys_datalog_policy_set_clear(&set);
    TEST_ASSERT_TRUE(write_manifest(&fx, k_domain, "policy.dl", k_zero_sha, "enforce", 1));
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_manifest_load_ex(fx.manifest_path, 0, &set, NULL),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, set.policy_count, "%zu");
    cleanup_fixture(&fx);
    TEST_END();
}

static int test_manifest_file_wrapper_solve_query(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, register_file_wrapper_domain(), "%d");
    file_fixture_t fx;
    TEST_ASSERT_TRUE(init_fixture(&fx));
    TEST_ASSERT_TRUE(write_valid_policy_and_manifest(&fx, "enforce", 1));
    maelys_datalog_policy_set_t set;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_manifest_load_ex(fx.manifest_path, 0, &set, NULL),
                      "%d");
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
    cleanup_fixture(&fx);
    TEST_END();
}

static int test_manifest_file_wrapper_reject_absolute_policy_path(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, register_file_wrapper_domain(), "%d");
    file_fixture_t fx;
    TEST_ASSERT_TRUE(init_fixture(&fx));
    char sha[65];
    sha_bytes(k_policy_src, strlen(k_policy_src), sha);
    TEST_ASSERT_TRUE(write_manifest(&fx, k_domain, fx.policy_path, sha, "enforce", 1));
    maelys_datalog_policy_set_t set;
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_manifest_load_ex(fx.manifest_path, 0, &set, &diag),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, set.policy_count, "%zu");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_MANIFEST_INVALID_FIELD, diag.code, "%d");
    cleanup_fixture(&fx);
    TEST_END();
}

static int test_manifest_file_wrapper_reject_parent_directory_policy_path(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, register_file_wrapper_domain(), "%d");
    file_fixture_t fx;
    TEST_ASSERT_TRUE(init_fixture(&fx));
    char sha[65];
    sha_bytes(k_policy_src, strlen(k_policy_src), sha);
    TEST_ASSERT_TRUE(write_manifest(&fx, k_domain, "../policy.dl", sha, "enforce", 1));
    maelys_datalog_policy_set_t set;
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_manifest_load_ex(fx.manifest_path, 0, &set, &diag),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, set.policy_count, "%zu");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_MANIFEST_INVALID_FIELD, diag.code, "%d");
    cleanup_fixture(&fx);
    TEST_END();
}

int main(int argc, char **argv) {
    test_case_t cases[] = {
        {"manifest_file_wrapper/load_ex_basic", TEST_MODE_NON_BLOCKING, test_manifest_file_wrapper_load_ex_basic},
        {"manifest_file_wrapper/load_simple_matches_ex_success", TEST_MODE_NON_BLOCKING, test_manifest_file_wrapper_load_simple_matches_ex_success},
        {"manifest_file_wrapper/load_ex_null_diag", TEST_MODE_NON_BLOCKING, test_manifest_file_wrapper_load_ex_null_diag},
        {"manifest_file_wrapper/missing_manifest", TEST_MODE_NON_BLOCKING, test_manifest_file_wrapper_missing_manifest},
        {"manifest_file_wrapper/missing_policy_file", TEST_MODE_NON_BLOCKING, test_manifest_file_wrapper_missing_policy_file},
        {"manifest_file_wrapper/sha_mismatch", TEST_MODE_NON_BLOCKING, test_manifest_file_wrapper_sha_mismatch},
        {"manifest_file_wrapper/test_only_rejected", TEST_MODE_NON_BLOCKING, test_manifest_file_wrapper_test_only_rejected},
        {"manifest_file_wrapper/test_only_allowed_with_flag", TEST_MODE_NON_BLOCKING, test_manifest_file_wrapper_test_only_allowed_with_flag},
        {"manifest_file_wrapper/unknown_domain", TEST_MODE_NON_BLOCKING, test_manifest_file_wrapper_unknown_domain},
        {"manifest_file_wrapper/disabled_policy_skipped", TEST_MODE_NON_BLOCKING, test_manifest_file_wrapper_disabled_policy_skipped},
        {"manifest_file_wrapper/out_set_cleared_on_failure", TEST_MODE_NON_BLOCKING, test_manifest_file_wrapper_out_set_cleared_on_failure},
        {"manifest_file_wrapper/solve_query", TEST_MODE_NON_BLOCKING, test_manifest_file_wrapper_solve_query},
        {"manifest_file_wrapper/reject_absolute_policy_path", TEST_MODE_NON_BLOCKING, test_manifest_file_wrapper_reject_absolute_policy_path},
        {"manifest_file_wrapper/reject_parent_directory_policy_path", TEST_MODE_NON_BLOCKING, test_manifest_file_wrapper_reject_parent_directory_policy_path},
    };
    return test_main("maelys_datalog_manifest_file_wrapper",
                     cases,
                     (int)(sizeof(cases) / sizeof(cases[0])),
                     argc,
                     argv);
}
