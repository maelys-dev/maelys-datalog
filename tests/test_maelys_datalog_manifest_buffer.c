#include "common/maelys_sha256.h"
#include "examples/domains/maelys_datalog_example_domains.h"
#include "src/core/maelys_datalog_domain_registry.h"
#include "src/core/maelys_datalog_edb.h"
#include "src/core/maelys_datalog_symbol_table.h"
#include "src/manifest/maelys_datalog_manifest.h"
#include "tests/helpers/test_framework.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static const char k_policy_src[] =
    "allow(X) :- safe(X).\n";
static const char k_whitelist_domain[] = "whitelist_test";
static const char k_whitelist_policy_id[] = "whitelist_policy";
static const maelys_datalog_predicate_def_t k_whitelist_domain_table[] = {
    {.name = "safe", .arity = 1, .kind_flags = MAELYS_DATALOG_PRED_KIND_EDB},
    {.name = "allow",
     .arity = 1,
     .kind_flags = MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
    {.name = "deny",
     .arity = 1,
     .kind_flags = MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
    {.name = "debug_trace",
     .arity = 1,
     .kind_flags = MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
    {.name = "helper", .arity = 1, .kind_flags = MAELYS_DATALOG_PRED_KIND_IDB},
};

static void sha_bytes(const char *src, size_t len, char out[65]) {
    (void)maelys_sha256_hex((const unsigned char *)src, len, out);
}

static void build_manifest_one(char *out,
                               size_t out_cap,
                               const char *policy_id,
                               const char *sha,
                               const char *mode) {
    snprintf(out,
             out_cap,
             "{\"policy_set_id\":\"x\",\"policy_set_version\":\"1\",\"manifest_version\":\"1\","
             "\"policies\":[{\"policy_id\":\"%s\",\"domain\":\"decision\","
             "\"file\":\"ignored.dl\",\"sha256\":\"%s\",\"mode\":\"%s\","
             "\"enabled\":true,\"description\":\"x\"}],"
             "\"capabilities\":[],\"default_profile\":\"MAELYS-DATALOG-TEXT-v1\","
             "\"strict_loading\":true,\"fail_closed\":true,\"created_for\":\"test\"}",
             policy_id,
             sha,
             mode);
}

static void build_manifest_two(char *out,
                               size_t out_cap,
                               const char *sha1,
                               const char *sha2) {
    snprintf(out,
             out_cap,
             "{\"policy_set_id\":\"x\",\"policy_set_version\":\"1\",\"manifest_version\":\"1\","
             "\"policies\":["
             "{\"policy_id\":\"p1\",\"domain\":\"decision\","
             "\"file\":\"first.dl\",\"sha256\":\"%s\",\"mode\":\"shadow\","
             "\"enabled\":true,\"description\":\"x\"},"
             "{\"policy_id\":\"p2\",\"domain\":\"decision\","
             "\"file\":\"second.dl\",\"sha256\":\"%s\",\"mode\":\"shadow\","
             "\"enabled\":true,\"description\":\"x\"}],"
             "\"capabilities\":[],\"default_profile\":\"MAELYS-DATALOG-TEXT-v1\","
             "\"strict_loading\":true,\"fail_closed\":true,\"created_for\":\"test\"}",
             sha1,
             sha2);
}

static maelys_datalog_policy_bundle_entry_t bundle_entry(const char *policy_id,
                                                         const char *src,
                                                         size_t src_len) {
    maelys_datalog_policy_bundle_entry_t entry = {
        .policy_id = policy_id,
        .src = src,
        .src_len = src_len,
    };
    return entry;
}

static maelys_result_t register_whitelist_domain(void) {
    static const maelys_datalog_domain_def_t def = {
        .domain_name = k_whitelist_domain,
        .predicates = k_whitelist_domain_table,
        .predicate_count = sizeof(k_whitelist_domain_table) / sizeof(k_whitelist_domain_table[0]),
        .description = "Manifest Public Query Whitelist test domain",
        .install_predicates = NULL,
    };
    return maelys_datalog_domain_registry_register(&def);
}

static maelys_datalog_term_t symbol_term(maelys_datalog_ruleset_t *ruleset, const char *text) {
    maelys_datalog_symbol_id_t id = 0;
    (void)maelys_datalog_symbol_intern(&ruleset->symbols, text, strlen(text), &id);
    maelys_datalog_term_t term = {.kind = MAELYS_DATALOG_TERM_SYMBOL};
    term.as.symbol = id;
    return term;
}

static void build_whitelist_manifest(char *out,
                                     size_t out_cap,
                                     const char *src,
                                     const char *queries_field,
                                     const char *idb_field) {
    char sha[65];
    sha_bytes(src, strlen(src), sha);
    snprintf(out,
             out_cap,
             "{\"policy_set_id\":\"whitelist.set\",\"policy_set_version\":\"1\","
             "\"manifest_version\":\"1\","
             "\"policies\":[{\"policy_id\":\"%s\",\"domain\":\"%s\","
             "\"file\":\"ignored.dl\",\"sha256\":\"%s\",\"mode\":\"shadow\","
             "\"enabled\":true,\"description\":\"whitelist\"%s%s}],"
             "\"capabilities\":[],\"default_profile\":\"MAELYS-DATALOG-TEXT-v1\","
             "\"strict_loading\":true,\"fail_closed\":true,\"created_for\":\"test\"}",
             k_whitelist_policy_id,
             k_whitelist_domain,
             sha,
             idb_field ? idb_field : "",
             queries_field ? queries_field : "");
}

static maelys_result_t load_whitelist_policy(const char *src,
                                             const char *queries_field,
                                             const char *idb_field,
                                             maelys_datalog_policy_set_t *set,
                                             maelys_datalog_diagnostic_t *diag) {
    maelys_result_t rc = register_whitelist_domain();
    if (rc != MAELYS_OK) return rc;
    char manifest[8192];
    build_whitelist_manifest(manifest, sizeof(manifest), src, queries_field, idb_field);
    maelys_datalog_policy_bundle_entry_t bundle =
        bundle_entry(k_whitelist_policy_id, src, strlen(src));
    return maelys_datalog_manifest_load_from_text(manifest,
                                                  strlen(manifest),
                                                  &bundle,
                                                  1u,
                                                  0,
                                                  set,
                                                  diag);
}

static maelys_result_t solve_and_query(maelys_datalog_policy_set_t *set,
                                       const char *predicate,
                                       maelys_result_t *out_query_rc,
                                       bool *out_present) {
    maelys_datalog_fact_t facts[4];
    maelys_datalog_edb_t edb;
    maelys_result_t rc = maelys_datalog_edb_init(&edb,
                                                 facts,
                                                 sizeof(facts) / sizeof(facts[0]),
                                                 &set->policies[0].symbols,
                                                 &set->policies[0].registry);
    if (rc != MAELYS_OK) return rc;
    maelys_datalog_term_t alice = symbol_term(&set->policies[0], "alice");
    rc = maelys_datalog_edb_add_fact(&edb, "safe", &alice, 1);
    if (rc != MAELYS_OK) return rc;
    rc = maelys_datalog_edb_finalize(&edb);
    if (rc != MAELYS_OK) return rc;
    maelys_datalog_solve_result_t *result = NULL;
    rc = maelys_datalog_solve_once(&set->policies[0], &edb, &result);
    if (rc != MAELYS_OK) return rc;
    *out_present = false;
    *out_query_rc = maelys_datalog_query_solved_ground_fact(result,
                                                            predicate,
                                                            &alice,
                                                            1,
                                                            out_present);
    maelys_datalog_solve_result_free(result);
    return MAELYS_OK;
}

static int load_basic_policy(maelys_datalog_policy_set_t *set,
                             maelys_datalog_diagnostic_t *diag) {
    char sha[65], manifest[2048];
    sha_bytes(k_policy_src, strlen(k_policy_src), sha);
    build_manifest_one(manifest, sizeof(manifest), "p", sha, "shadow");
    maelys_datalog_policy_bundle_entry_t bundle =
        bundle_entry("p", k_policy_src, strlen(k_policy_src));
    return maelys_datalog_manifest_load_from_text(manifest,
                                                  strlen(manifest),
                                                  &bundle,
                                                  1u,
                                                  0,
                                                  set,
                                                  diag);
}

static int test_datalog_wasm_manifest_load_from_text_basic(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_example_domains_install(), "%d");
    maelys_datalog_policy_set_t set;
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_OK, load_basic_policy(&set, &diag), "%d");
    TEST_ASSERT_EQUAL((size_t)1u, set.policy_count, "%zu");
    TEST_ASSERT_EQUAL_STRING("p", set.policies[0].policy_id);
    maelys_datalog_policy_set_clear(&set);
    TEST_END();
}

static int test_datalog_wasm_manifest_load_from_text_sha_mismatch(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_example_domains_install(), "%d");
    char manifest[2048];
    build_manifest_one(manifest,
                       sizeof(manifest),
                       "p",
                       "0000000000000000000000000000000000000000000000000000000000000000",
                       "shadow");
    maelys_datalog_policy_bundle_entry_t bundle =
        bundle_entry("p", k_policy_src, strlen(k_policy_src));
    maelys_datalog_policy_set_t set;
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_manifest_load_from_text(manifest,
                                                             strlen(manifest),
                                                             &bundle,
                                                             1u,
                                                             0,
                                                             &set,
                                                             &diag),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_MANIFEST_SHA_MISMATCH, diag.code, "%d");
    TEST_END();
}

static int test_datalog_wasm_manifest_load_from_text_missing_bundle(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_example_domains_install(), "%d");
    char sha[65], manifest[2048];
    sha_bytes(k_policy_src, strlen(k_policy_src), sha);
    build_manifest_one(manifest, sizeof(manifest), "missing", sha, "shadow");
    maelys_datalog_policy_set_t set;
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_NOT_FOUND,
                      maelys_datalog_manifest_load_from_text(manifest,
                                                             strlen(manifest),
                                                             NULL,
                                                             0u,
                                                             0,
                                                             &set,
                                                             &diag),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_MANIFEST_POLICY_NOT_FOUND, diag.code, "%d");
    TEST_END();
}

static int test_datalog_wasm_manifest_load_from_text_invalid_json(void) {
    TEST_BEGIN();
    maelys_datalog_policy_set_t set;
    maelys_datalog_diagnostic_t diag;
    const char manifest[] = "{\"policy_set_id\":\"x\",";
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_manifest_load_from_text(manifest,
                                                             sizeof(manifest) - 1u,
                                                             NULL,
                                                             0u,
                                                             0,
                                                             &set,
                                                             &diag),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_MANIFEST_INVALID_JSON, diag.code, "%d");
    TEST_END();
}

static int test_datalog_wasm_manifest_load_from_text_utf8_invalid(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_example_domains_install(), "%d");
    const char invalid_src[] = {'p', 'r', 'o', 'j', 'e', 'c', 't', 'i', 'o', 'n',
                                '(', '"', (char)0xff, '"', ')', '.', '\n'};
    char sha[65], manifest[2048];
    sha_bytes(invalid_src, sizeof(invalid_src), sha);
    build_manifest_one(manifest, sizeof(manifest), "p", sha, "shadow");
    maelys_datalog_policy_bundle_entry_t bundle = bundle_entry("p", invalid_src, sizeof(invalid_src));
    maelys_datalog_policy_set_t set;
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_manifest_load_from_text(manifest,
                                                             strlen(manifest),
                                                             &bundle,
                                                             1u,
                                                             0,
                                                             &set,
                                                             &diag),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_LEXER_INVALID_UTF8, diag.code, "%d");
    TEST_END();
}

static int test_datalog_wasm_manifest_load_from_text_multiple_policies(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_example_domains_install(), "%d");
    const char src2[] =
        "deny(X) :- blocked(X).\n";
    char sha1[65], sha2[65], manifest[3072];
    sha_bytes(k_policy_src, strlen(k_policy_src), sha1);
    sha_bytes(src2, strlen(src2), sha2);
    build_manifest_two(manifest, sizeof(manifest), sha1, sha2);
    maelys_datalog_policy_bundle_entry_t bundle[] = {
        bundle_entry("p1", k_policy_src, strlen(k_policy_src)),
        bundle_entry("p2", src2, strlen(src2)),
    };
    maelys_datalog_policy_set_t set;
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_manifest_load_from_text(manifest,
                                                             strlen(manifest),
                                                             bundle,
                                                             sizeof(bundle) / sizeof(bundle[0]),
                                                             0,
                                                             &set,
                                                             &diag),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)2u, set.policy_count, "%zu");
    maelys_datalog_policy_set_clear(&set);
    TEST_END();
}

static int test_datalog_wasm_manifest_load_from_text_test_only_rejected(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_example_domains_install(), "%d");
    char sha[65], manifest[2048];
    sha_bytes(k_policy_src, strlen(k_policy_src), sha);
    build_manifest_one(manifest, sizeof(manifest), "p", sha, "test_only");
    maelys_datalog_policy_bundle_entry_t bundle =
        bundle_entry("p", k_policy_src, strlen(k_policy_src));
    maelys_datalog_policy_set_t set;
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_FORBIDDEN,
                      maelys_datalog_manifest_load_from_text(manifest,
                                                             strlen(manifest),
                                                             &bundle,
                                                             1u,
                                                             0,
                                                             &set,
                                                             &diag),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_MANIFEST_TEST_ONLY_REJECTED, diag.code, "%d");
    TEST_END();
}

static int test_datalog_wasm_manifest_load_from_text_non_nul_terminated_manifest(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_example_domains_install(), "%d");
    char sha[65], manifest[2048], raw[2048];
    sha_bytes(k_policy_src, strlen(k_policy_src), sha);
    build_manifest_one(manifest, sizeof(manifest), "p", sha, "shadow");
    size_t manifest_len = strlen(manifest);
    memcpy(raw, manifest, manifest_len);
    memcpy(raw + manifest_len, "TRAILING", 8u);
    maelys_datalog_policy_bundle_entry_t bundle =
        bundle_entry("p", k_policy_src, strlen(k_policy_src));
    maelys_datalog_policy_set_t set;
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_manifest_load_from_text(raw,
                                                             manifest_len,
                                                             &bundle,
                                                             1u,
                                                             0,
                                                             &set,
                                                             &diag),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)1u, set.policy_count, "%zu");
    maelys_datalog_policy_set_clear(&set);
    TEST_END();
}

static int test_query_whitelist_allows_whitelisted_query(void) {
    TEST_BEGIN();
    const char src[] =
        "allow(X) :- safe(X).\n"
        "debug_trace(X) :- safe(X).\n";
    maelys_datalog_policy_set_t set;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      load_whitelist_policy(src,
                                            ",\"queries\":[{\"name\":\"allow\",\"arity\":1}]",
                                            NULL,
                                            &set,
                                            NULL),
                      "%d");
    TEST_ASSERT_EQUAL(1, set.enforces_query_whitelist, "%d");
    TEST_ASSERT_EQUAL((size_t)1u, set.query_whitelist_count, "%zu");
    maelys_result_t query_rc = MAELYS_OK;
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK, solve_and_query(&set, "allow", &query_rc, &present), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, query_rc, "%d");
    TEST_ASSERT_TRUE(present);
    maelys_datalog_policy_set_clear(&set);
    TEST_END();
}

static int test_query_whitelist_rejects_non_whitelisted_query(void) {
    TEST_BEGIN();
    const char src[] =
        "allow(X) :- safe(X).\n"
        "debug_trace(X) :- safe(X).\n";
    maelys_datalog_policy_set_t set;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      load_whitelist_policy(src,
                                            ",\"queries\":[{\"name\":\"allow\",\"arity\":1}]",
                                            NULL,
                                            &set,
                                            NULL),
                      "%d");
    maelys_result_t query_rc = MAELYS_OK;
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK, solve_and_query(&set, "debug_trace", &query_rc, &present), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_FORBIDDEN, query_rc, "%d");
    TEST_ASSERT_FALSE(present);
    maelys_datalog_policy_set_clear(&set);
    TEST_END();
}

static int test_query_whitelist_manifest_empty_queries_blocks_all_queries(void) {
    TEST_BEGIN();
    maelys_datalog_policy_set_t set;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      load_whitelist_policy("allow(X) :- safe(X).\n", ",\"queries\":[]", NULL, &set, NULL),
                      "%d");
    TEST_ASSERT_EQUAL(1, set.enforces_query_whitelist, "%d");
    TEST_ASSERT_EQUAL((size_t)0u, set.query_whitelist_count, "%zu");
    maelys_result_t query_rc = MAELYS_OK;
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK, solve_and_query(&set, "allow", &query_rc, &present), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_FORBIDDEN, query_rc, "%d");
    TEST_ASSERT_FALSE(present);
    maelys_datalog_policy_set_clear(&set);
    TEST_END();
}

static int test_query_whitelist_manifest_without_queries_field_blocks_all_queries(void) {
    TEST_BEGIN();
    maelys_datalog_policy_set_t set;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      load_whitelist_policy("allow(X) :- safe(X).\n", NULL, NULL, &set, NULL),
                      "%d");
    TEST_ASSERT_EQUAL(1, set.enforces_query_whitelist, "%d");
    TEST_ASSERT_EQUAL((size_t)0u, set.query_whitelist_count, "%zu");
    maelys_result_t query_rc = MAELYS_OK;
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK, solve_and_query(&set, "allow", &query_rc, &present), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_FORBIDDEN, query_rc, "%d");
    TEST_ASSERT_FALSE(present);
    maelys_datalog_policy_set_clear(&set);
    TEST_END();
}

static int test_query_whitelist_internal_predicate_still_computed(void) {
    TEST_BEGIN();
    const char src[] =
        "debug_trace(X) :- safe(X).\n"
        "allow(X) :- debug_trace(X).\n";
    maelys_datalog_policy_set_t set;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      load_whitelist_policy(src,
                                            ",\"queries\":[{\"name\":\"allow\",\"arity\":1}]",
                                            NULL,
                                            &set,
                                            NULL),
                      "%d");
    maelys_result_t query_rc = MAELYS_OK;
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK, solve_and_query(&set, "allow", &query_rc, &present), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, query_rc, "%d");
    TEST_ASSERT_TRUE(present);
    TEST_ASSERT_EQUAL(MAELYS_OK, solve_and_query(&set, "debug_trace", &query_rc, &present), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_FORBIDDEN, query_rc, "%d");
    TEST_ASSERT_FALSE(present);
    maelys_datalog_policy_set_clear(&set);
    TEST_END();
}

static int test_query_whitelist_rejects_unknown_query_predicate(void) {
    TEST_BEGIN();
    maelys_datalog_policy_set_t set;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      load_whitelist_policy("allow(X) :- safe(X).\n",
                                            ",\"queries\":[{\"name\":\"unknown\",\"arity\":1}]",
                                            NULL,
                                            &set,
                                            NULL),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, set.policy_count, "%zu");
    TEST_END();
}

static int test_query_whitelist_rejects_wrong_query_arity(void) {
    TEST_BEGIN();
    maelys_datalog_policy_set_t set;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      load_whitelist_policy("allow(X) :- safe(X).\n",
                                            ",\"queries\":[{\"name\":\"allow\",\"arity\":2}]",
                                            NULL,
                                            &set,
                                            NULL),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, set.policy_count, "%zu");
    TEST_END();
}

static int test_query_whitelist_rejects_non_query_capable_predicate(void) {
    TEST_BEGIN();
    maelys_datalog_policy_set_t set;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      load_whitelist_policy("helper(X) :- safe(X).\n",
                                            ",\"queries\":[{\"name\":\"helper\",\"arity\":1}]",
                                            NULL,
                                            &set,
                                            NULL),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, set.policy_count, "%zu");
    TEST_END();
}

static int test_query_whitelist_too_many_queries(void) {
    TEST_BEGIN();
    char queries[4096];
    size_t off = 0u;
    off += (size_t)snprintf(queries + off, sizeof(queries) - off, ",\"queries\":[");
    for (size_t i = 0u; i < MAELYS_DATALOG_MAX_QUERY_WHITELIST + 1u; i++) {
        off += (size_t)snprintf(queries + off,
                                sizeof(queries) - off,
                                "%s{\"name\":\"allow\",\"arity\":1}",
                                i == 0u ? "" : ",");
    }
    snprintf(queries + off, sizeof(queries) - off, "]");
    maelys_datalog_policy_set_t set;
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE,
                      load_whitelist_policy("allow(X) :- safe(X).\n", queries, NULL, &set, NULL),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, set.policy_count, "%zu");
    TEST_END();
}

static int test_query_whitelist_idb_predicates_no_longer_create_vocabulary(void) {
    TEST_BEGIN();
    const char src[] =
        "invented(X) :- safe(X).\n"
        "allow(X) :- invented(X).\n";
    maelys_datalog_policy_set_t set;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      load_whitelist_policy(src,
                                            ",\"queries\":[{\"name\":\"allow\",\"arity\":1}]",
                                            ",\"idb_predicates\":[{\"name\":\"invented\",\"arity\":1}]",
                                            &set,
                                            NULL),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, set.policy_count, "%zu");
    TEST_END();
}

static int test_query_whitelist_idb_predicates_compat_noop(void) {
    TEST_BEGIN();
    const char src[] =
        "helper(X) :- safe(X).\n"
        "allow(X) :- helper(X).\n";
    maelys_datalog_policy_set_t set;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      load_whitelist_policy(src,
                                            ",\"queries\":[{\"name\":\"allow\",\"arity\":1}]",
                                            ",\"idb_predicates\":[{\"name\":\"helper\",\"arity\":1}]",
                                            &set,
                                            NULL),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)1u, set.policy_count, "%zu");
    maelys_datalog_policy_set_clear(&set);
    TEST_END();
}

static int test_query_whitelist_out_set_cleared_on_failure(void) {
    TEST_BEGIN();
    maelys_datalog_policy_set_t set;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      load_whitelist_policy("allow(X) :- safe(X).\n",
                                            ",\"queries\":[{\"name\":\"allow\",\"arity\":1}]",
                                            NULL,
                                            &set,
                                            NULL),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)1u, set.policy_count, "%zu");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      load_whitelist_policy("allow(X) :- safe(X).\n",
                                            ",\"queries\":[{\"name\":\"unknown\",\"arity\":1}]",
                                            NULL,
                                            &set,
                                            NULL),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, set.policy_count, "%zu");
    TEST_ASSERT_EQUAL(0, set.enforces_query_whitelist, "%d");
    TEST_ASSERT_EQUAL((size_t)0u, set.query_whitelist_count, "%zu");
    TEST_END();
}

static int test_query_whitelist_enforces_flag_invariant(void) {
    TEST_BEGIN();
    maelys_datalog_policy_set_t set;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      load_whitelist_policy("allow(X) :- safe(X).\n",
                                            ",\"queries\":[{\"name\":\"allow\",\"arity\":1}]",
                                            NULL,
                                            &set,
                                            NULL),
                      "%d");
    TEST_ASSERT_EQUAL(1, set.enforces_query_whitelist, "%d");
    TEST_ASSERT_TRUE(!(set.enforces_query_whitelist == 0 && set.query_whitelist_count > 0u));
    maelys_datalog_policy_set_clear(&set);
    TEST_END();
}

int main(int argc, char **argv) {
    test_case_t cases[] = {
        {"maelys_datalog_manifest_buffer/basic", TEST_MODE_NON_BLOCKING, test_datalog_wasm_manifest_load_from_text_basic},
        {"maelys_datalog_manifest_buffer/sha_mismatch", TEST_MODE_NON_BLOCKING, test_datalog_wasm_manifest_load_from_text_sha_mismatch},
        {"maelys_datalog_manifest_buffer/missing_bundle", TEST_MODE_NON_BLOCKING, test_datalog_wasm_manifest_load_from_text_missing_bundle},
        {"maelys_datalog_manifest_buffer/invalid_json", TEST_MODE_NON_BLOCKING, test_datalog_wasm_manifest_load_from_text_invalid_json},
        {"maelys_datalog_manifest_buffer/utf8_invalid", TEST_MODE_NON_BLOCKING, test_datalog_wasm_manifest_load_from_text_utf8_invalid},
        {"maelys_datalog_manifest_buffer/multiple_policies", TEST_MODE_NON_BLOCKING, test_datalog_wasm_manifest_load_from_text_multiple_policies},
        {"maelys_datalog_manifest_buffer/test_only_rejected", TEST_MODE_NON_BLOCKING, test_datalog_wasm_manifest_load_from_text_test_only_rejected},
        {"maelys_datalog_manifest_buffer/non_nul_terminated_manifest", TEST_MODE_NON_BLOCKING, test_datalog_wasm_manifest_load_from_text_non_nul_terminated_manifest},
        {"query_whitelist/allows_whitelisted_query", TEST_MODE_NON_BLOCKING, test_query_whitelist_allows_whitelisted_query},
        {"query_whitelist/rejects_non_whitelisted_query", TEST_MODE_NON_BLOCKING, test_query_whitelist_rejects_non_whitelisted_query},
        {"query_whitelist/manifest_empty_queries_blocks_all_queries", TEST_MODE_NON_BLOCKING, test_query_whitelist_manifest_empty_queries_blocks_all_queries},
        {"query_whitelist/manifest_without_queries_field_blocks_all_queries", TEST_MODE_NON_BLOCKING, test_query_whitelist_manifest_without_queries_field_blocks_all_queries},
        {"query_whitelist/internal_predicate_still_computed", TEST_MODE_NON_BLOCKING, test_query_whitelist_internal_predicate_still_computed},
        {"query_whitelist/rejects_unknown_query_predicate", TEST_MODE_NON_BLOCKING, test_query_whitelist_rejects_unknown_query_predicate},
        {"query_whitelist/rejects_wrong_query_arity", TEST_MODE_NON_BLOCKING, test_query_whitelist_rejects_wrong_query_arity},
        {"query_whitelist/rejects_non_query_capable_predicate", TEST_MODE_NON_BLOCKING, test_query_whitelist_rejects_non_query_capable_predicate},
        {"query_whitelist/too_many_queries", TEST_MODE_NON_BLOCKING, test_query_whitelist_too_many_queries},
        {"query_whitelist/idb_predicates_no_longer_create_vocabulary", TEST_MODE_NON_BLOCKING, test_query_whitelist_idb_predicates_no_longer_create_vocabulary},
        {"query_whitelist/idb_predicates_compat_noop", TEST_MODE_NON_BLOCKING, test_query_whitelist_idb_predicates_compat_noop},
        {"query_whitelist/out_set_cleared_on_failure", TEST_MODE_NON_BLOCKING, test_query_whitelist_out_set_cleared_on_failure},
        {"query_whitelist/enforces_flag_invariant", TEST_MODE_NON_BLOCKING, test_query_whitelist_enforces_flag_invariant},
    };
    return test_main("maelys_datalog_manifest_buffer", cases, (int)(sizeof(cases) / sizeof(cases[0])), argc, argv);
}
