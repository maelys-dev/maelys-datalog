#include "common/maelys_sha256.h"
#include "examples/domains/maelys_datalog_example_domains.h"
#include "src/manifest/maelys_datalog_manifest.h"
#include "tests/helpers/test_framework.h"

#include <stdio.h>
#include <string.h>

static const char k_policy_src[] =
    "allow(X) :- safe(X).\n";

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
    };
    return test_main("maelys_datalog_manifest_buffer", cases, (int)(sizeof(cases) / sizeof(cases[0])), argc, argv);
}
