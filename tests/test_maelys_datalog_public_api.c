#include <maelys/datalog.h>
#include "tests/helpers/test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const maelys_datalog_public_predicate_t k_predicates[] = {
    {"observed", 1u, MAELYS_DATALOG_PREDICATE_EDB},
    {"count", 1u, MAELYS_DATALOG_PREDICATE_EDB},
    {"enabled", 1u, MAELYS_DATALOG_PREDICATE_EDB},
    {"policy_value", 1u, MAELYS_DATALOG_PREDICATE_POLICY_FACT},
    {"quad", 4u, MAELYS_DATALOG_PREDICATE_POLICY_FACT},
    {"allow", 1u, MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY},
};

static maelys_datalog_status_t register_domain_with_atoms(
    const char *name,
    const char *const *atoms,
    size_t atom_count) {
    const maelys_datalog_public_domain_t domain = {
        .name = name,
        .predicates = k_predicates,
        .predicate_count = sizeof(k_predicates) / sizeof(k_predicates[0]),
        .atoms = atoms,
        .atom_count = atom_count,
    };
    return maelys_datalog_domain_register(&domain);
}

static maelys_datalog_status_t register_domain(const char *name) {
    return register_domain_with_atoms(name, NULL, 0u);
}

static maelys_datalog_public_value_t symbol_value(const char *text) {
    maelys_datalog_public_value_t value;
    memset(&value, 0, sizeof(value));
    value.kind = MAELYS_DATALOG_VALUE_SYMBOL;
    value.as.symbol = text;
    return value;
}

static maelys_datalog_public_fact_t fact(
    const char *predicate,
    maelys_datalog_public_value_t value) {
    maelys_datalog_public_fact_t item;
    memset(&item, 0, sizeof(item));
    item.predicate = predicate;
    item.arity = 1u;
    item.terms[0] = value;
    return item;
}

static int all_lower_hex(const char fingerprint[65]) {
    if (fingerprint[64] != '\0') return 0;
    for (size_t i = 0u; i < 64u; i++) {
        const char c = fingerprint[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return 0;
    }
    return 1;
}

static int test_public_api_complete_lifecycle(void) {
    TEST_BEGIN();
    const char *domain_name = "public_api_lifecycle";
    const char source[] =
        "allow(X) :- observed(X), count(7), enabled(true).\n";
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK, register_domain(domain_name), "%d");

    maelys_datalog_policy_t *policy = NULL;
    maelys_datalog_public_diagnostic_t diagnostic;
    TEST_ASSERT_EQUAL(
        MAELYS_DATALOG_STATUS_OK,
        maelys_datalog_policy_load_inline(
            domain_name, "public-api", source, strlen(source), &policy, &diagnostic),
        "%d");
    TEST_ASSERT_NOT_NULL(policy);
    if (!policy) TEST_END();

    size_t policy_count = 0u;
    char policy_fingerprint[65];
    memset(policy_fingerprint, 'x', sizeof(policy_fingerprint));
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                      maelys_datalog_policy_count(policy, &policy_count), "%d");
    TEST_ASSERT_EQUAL((size_t)1u, policy_count, "%zu");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                      maelys_datalog_policy_fingerprint(policy, policy_fingerprint), "%d");
    TEST_ASSERT_TRUE(all_lower_hex(policy_fingerprint));

    maelys_datalog_session_t *session = NULL;
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                      maelys_datalog_session_create(policy, 0u, &session), "%d");
    TEST_ASSERT_NOT_NULL(session);
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                      maelys_datalog_policy_free(policy), "%d");
    policy = NULL;
    if (!session) TEST_END();

    char session_fingerprint[65];
    memset(session_fingerprint, 'x', sizeof(session_fingerprint));
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                      maelys_datalog_session_fingerprint(session, session_fingerprint), "%d");
    TEST_ASSERT_TRUE(all_lower_hex(session_fingerprint));

    maelys_datalog_public_value_t integer;
    memset(&integer, 0, sizeof(integer));
    integer.kind = MAELYS_DATALOG_VALUE_INTEGER;
    integer.as.integer = 7;
    maelys_datalog_public_value_t boolean;
    memset(&boolean, 0, sizeof(boolean));
    boolean.kind = MAELYS_DATALOG_VALUE_BOOLEAN;
    boolean.as.boolean = 1;
    maelys_datalog_public_fact_t facts[] = {
        fact("observed", symbol_value("alice")),
        fact("count", integer),
        fact("enabled", boolean),
    };
    maelys_datalog_result_t *result = NULL;
    TEST_ASSERT_EQUAL(
        MAELYS_DATALOG_STATUS_OK,
        maelys_datalog_session_solve(
            session, facts, sizeof(facts) / sizeof(facts[0]), &result, &diagnostic),
        "%d");
    TEST_ASSERT_NOT_NULL(result);
    if (!result) {
        (void)maelys_datalog_session_free(session);
        TEST_END();
    }
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_INVALID_STATE,
                      maelys_datalog_session_free(session), "%d");

    maelys_datalog_public_value_t query = symbol_value("alice");
    int present = -1;
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                      maelys_datalog_result_query(result, "allow", &query, 1u, &present), "%d");
    TEST_ASSERT_EQUAL(1, present, "%d");
    query = symbol_value("unknown");
    present = -1;
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                      maelys_datalog_result_query(result, "allow", &query, 1u, &present), "%d");
    TEST_ASSERT_EQUAL(0, present, "%d");

    size_t total = 0u;
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                      maelys_datalog_result_enumerate(
                          result, "allow", 1u, NULL, 0u, &total), "%d");
    TEST_ASSERT_EQUAL((size_t)1u, total, "%zu");
    maelys_datalog_public_fact_view_t view;
    memset(&view, 0, sizeof(view));
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                      maelys_datalog_result_enumerate(
                          result, "allow", 1u, &view, 1u, &total), "%d");
    TEST_ASSERT_EQUAL((size_t)1u, total, "%zu");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_VALUE_SYMBOL, view.terms[0].kind, "%d");
    const char *rendered = NULL;
    size_t rendered_length = 0u;
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                      maelys_datalog_result_symbol_text(
                          result, view.terms[0].as.symbol_id, &rendered, &rendered_length),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)5u, rendered_length, "%zu");
    TEST_ASSERT_EQUAL_STRING("alice", rendered);

    query = symbol_value("alice");
    size_t required = 0u;
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                      maelys_datalog_result_explain_true_text(
                          result, "allow", &query, 1u, NULL, 0u, &required), "%d");
    TEST_ASSERT_TRUE(required > 0u);
    char *text = calloc(required + 1u, 1u);
    TEST_ASSERT_NOT_NULL(text);
    if (text) {
        size_t written = 0u;
        TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                          maelys_datalog_result_explain_true_text(
                              result, "allow", &query, 1u, text, required + 1u, &written),
                          "%d");
        TEST_ASSERT_EQUAL(required, written, "%zu");
        TEST_ASSERT_NOT_NULL(strstr(text, "document=why-true"));
        TEST_ASSERT_NOT_NULL(strstr(text, "status=complete"));
        TEST_ASSERT_NOT_NULL(strstr(text, "\"alice\""));
        free(text);
    }

    TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                      maelys_datalog_result_free(result), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                      maelys_datalog_session_free(session), "%d");
    TEST_END();
}

static int test_public_api_domain_is_copied_and_divergence_refused(void) {
    TEST_BEGIN();
    char domain_name[64] = "public_api_copied_domain";
    char predicate_name[64] = "input";
    char atom[64] = "trusted";
    maelys_datalog_public_predicate_t predicates[] = {
        {predicate_name, 1u, MAELYS_DATALOG_PREDICATE_EDB},
        {"trusted", 1u, MAELYS_DATALOG_PREDICATE_POLICY_FACT},
        {"allow", 1u, MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY},
    };
    const char *atoms[] = {atom};
    maelys_datalog_public_domain_t domain = {
        domain_name, predicates, 3u, atoms, 1u,
    };
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                      maelys_datalog_domain_register(&domain), "%d");
    memset(domain_name, 'z', strlen(domain_name));
    memset(predicate_name, 'z', strlen(predicate_name));
    memset(atom, 'z', strlen(atom));

    const char source[] =
        "trusted(\"trusted\").\n"
        "allow(X) :- input(X), trusted(\"trusted\").\n";
    maelys_datalog_policy_t *policy = NULL;
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                      maelys_datalog_policy_load_inline(
                          "public_api_copied_domain", "copied", source, strlen(source),
                          &policy, NULL), "%d");
    TEST_ASSERT_NOT_NULL(policy);
    if (policy) TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                                  maelys_datalog_policy_free(policy), "%d");

    const maelys_datalog_public_predicate_t divergent[] = {
        {"other", 1u, MAELYS_DATALOG_PREDICATE_EDB},
    };
    const maelys_datalog_public_domain_t conflict = {
        "public_api_copied_domain", divergent, 1u, NULL, 0u,
    };
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_INVALID_FIELD,
                      maelys_datalog_domain_register(&conflict), "%d");
    TEST_END();
}

static int write_bytes(const char *path, const char *bytes, size_t length) {
    FILE *stream = fopen(path, "wb");
    if (!stream) return 0;
    const size_t written = fwrite(bytes, 1u, length, stream);
    const int failed = ferror(stream);
    const int closed = fclose(stream);
    return written == length && !failed && closed == 0;
}

static maelys_datalog_status_t load_manifest_source(
    const char *domain,
    const char *source,
    const char *source_sha256,
    unsigned flags,
    maelys_datalog_policy_t **out_policy,
    maelys_datalog_public_diagnostic_t *out_diagnostic) {
    char directory[] = "/tmp/maelys-public-atoms-XXXXXX";
    if (!mkdtemp(directory)) return MAELYS_DATALOG_STATUS_IO;
    char source_path[256];
    char manifest_path[256];
    (void)snprintf(source_path, sizeof(source_path), "%s/policy.dl", directory);
    (void)snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.json", directory);
    char manifest[2048];
    const int manifest_length = snprintf(
        manifest,
        sizeof(manifest),
        "{\"policy_set_id\":\"public.atoms\",\"policy_set_version\":\"1\","
        "\"manifest_version\":\"1\",\"default_profile\":\"enforce\","
        "\"created_for\":\"test\",\"strict_loading\":true,\"fail_closed\":true,"
        "\"capabilities\":[],\"policies\":[{"
        "\"policy_id\":\"policy\",\"domain\":\"%s\",\"file\":\"policy.dl\","
        "\"sha256\":\"%s\",\"mode\":\"enforce\",\"enabled\":true,"
        "\"description\":\"policy atoms\","
        "\"queries\":[{\"name\":\"allow\",\"arity\":1}]}]}",
        domain,
        source_sha256);
    maelys_datalog_status_t status = MAELYS_DATALOG_STATUS_IO;
    if (manifest_length > 0 && (size_t)manifest_length < sizeof(manifest) &&
        write_bytes(source_path, source, strlen(source)) &&
        write_bytes(manifest_path, manifest, (size_t)manifest_length)) {
        status = maelys_datalog_policy_load_manifest(
            manifest_path, flags, out_policy, out_diagnostic);
    } else if (out_policy) {
        *out_policy = NULL;
    }
    (void)unlink(source_path);
    (void)unlink(manifest_path);
    (void)rmdir(directory);
    return status;
}

static int policy_allows(maelys_datalog_policy_t *policy, const char *symbol) {
    maelys_datalog_session_t *session = NULL;
    maelys_datalog_result_t *result = NULL;
    int present = 0;
    if (maelys_datalog_session_create(policy, 0u, &session) !=
        MAELYS_DATALOG_STATUS_OK) {
        return 0;
    }
    if (maelys_datalog_session_solve(session, NULL, 0u, &result, NULL) !=
        MAELYS_DATALOG_STATUS_OK) {
        (void)maelys_datalog_session_free(session);
        return 0;
    }
    const maelys_datalog_public_value_t value = symbol_value(symbol);
    const int ok = maelys_datalog_result_query(
                       result, "allow", &value, 1u, &present) ==
                       MAELYS_DATALOG_STATUS_OK &&
                   present == 1;
    (void)maelys_datalog_result_free(result);
    (void)maelys_datalog_session_free(session);
    return ok;
}

static int test_public_api_policy_atom_modes(void) {
    TEST_BEGIN();
    const char *domain = "public_api_policy_atom_modes";
    const char source[] =
        "policy_value(\"alpha\").\n"
        "allow(X) :- policy_value(X), starts_with(\"alpha\", \"a\").\n";
    const char sha[] = "a5bc926c79c5017cc4e7c67541f456c505e41ccf09f4e3cdcd28db1feb3906e1";
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK, register_domain(domain), "%d");

    maelys_datalog_policy_t *policy = (maelys_datalog_policy_t *)(uintptr_t)1u;
    maelys_datalog_public_diagnostic_t diagnostic;
    TEST_ASSERT_EQUAL(
        MAELYS_DATALOG_STATUS_INVALID_FIELD,
        load_manifest_source(domain, source, sha, 0u, &policy, &diagnostic),
        "%d");
    TEST_ASSERT_NULL(policy);
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAGNOSTIC_LOAD, diagnostic.source, "%d");
    TEST_ASSERT_EQUAL_STRING("unknown atom", diagnostic.message);

    policy = (maelys_datalog_policy_t *)(uintptr_t)1u;
    TEST_ASSERT_EQUAL(
        MAELYS_DATALOG_STATUS_INVALID_ARGUMENT,
        load_manifest_source(domain, source, sha, 1u << 31, &policy, &diagnostic),
        "%d");
    TEST_ASSERT_NULL(policy);

    TEST_ASSERT_EQUAL(
        MAELYS_DATALOG_STATUS_OK,
        load_manifest_source(
            domain,
            source,
            sha,
            MAELYS_DATALOG_PUBLIC_ALLOW_UNDECLARED_POLICY_ATOMS,
            &policy,
            &diagnostic),
        "%d");
    TEST_ASSERT_NOT_NULL(policy);
    if (policy) {
        TEST_ASSERT_TRUE(policy_allows(policy, "alpha"));
        TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                          maelys_datalog_policy_free(policy), "%d");
    }

    policy = (maelys_datalog_policy_t *)(uintptr_t)1u;
    TEST_ASSERT_EQUAL(
        MAELYS_DATALOG_STATUS_INVALID_FIELD,
        maelys_datalog_policy_load_inline(
            domain, "inline-closed", source, strlen(source), &policy, &diagnostic),
        "%d");
    TEST_ASSERT_NULL(policy);
    TEST_END();
}

static int test_public_api_policy_atom_load_isolation(void) {
    TEST_BEGIN();
    const char *domain = "public_api_policy_atom_isolation";
    const char source_a[] =
        "policy_value(\"alpha\").\n"
        "allow(X) :- policy_value(X).\n";
    const char source_b[] =
        "policy_value(\"beta\").\n"
        "allow(X) :- policy_value(X).\n";
    const char sha_a[] = "41ad802575655840e11ca1a1bdba31d638588fc5c39d09b7b5e74ef5acba7477";
    const char sha_b[] = "ae6be842375e9adc03836f9dd9e01eba6857870e89e75b6ffc289f075217257d";
    const unsigned flag = MAELYS_DATALOG_PUBLIC_ALLOW_UNDECLARED_POLICY_ATOMS;
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK, register_domain(domain), "%d");

    maelys_datalog_policy_t *a = NULL;
    maelys_datalog_policy_t *b = NULL;
    maelys_datalog_policy_t *again = NULL;
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                      load_manifest_source(domain, source_a, sha_a, flag, &a, NULL), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                      load_manifest_source(domain, source_b, sha_b, flag, &b, NULL), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                      load_manifest_source(domain, source_a, sha_a, flag, &again, NULL), "%d");
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_NOT_NULL(again);
    if (a && b && again) {
        char fingerprint_a[65];
        char fingerprint_b[65];
        char fingerprint_again[65];
        TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                          maelys_datalog_policy_fingerprint(a, fingerprint_a), "%d");
        TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                          maelys_datalog_policy_fingerprint(b, fingerprint_b), "%d");
        TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                          maelys_datalog_policy_fingerprint(again, fingerprint_again), "%d");
        TEST_ASSERT_EQUAL_STRING(fingerprint_a, fingerprint_again);
        TEST_ASSERT_TRUE(strcmp(fingerprint_a, fingerprint_b) != 0);
        TEST_ASSERT_TRUE(policy_allows(a, "alpha"));
        TEST_ASSERT_TRUE(policy_allows(b, "beta"));
        TEST_ASSERT_FALSE(policy_allows(a, "beta"));
        TEST_ASSERT_FALSE(policy_allows(b, "alpha"));
    }
    if (a) (void)maelys_datalog_policy_free(a);
    if (b) (void)maelys_datalog_policy_free(b);
    if (again) (void)maelys_datalog_policy_free(again);
    TEST_END();
}

static size_t append_quad_facts(char *source, size_t capacity, size_t atom_count) {
    size_t used = 0u;
    const size_t full_groups = atom_count / 4u;
    for (size_t group = 0u; group < full_groups; group++) {
        const size_t i = group * 4u;
        const int written = snprintf(
            source + used,
            capacity - used,
            "quad(\"v%03zu\",\"v%03zu\",\"v%03zu\",\"v%03zu\").\n",
            i,
            i + 1u,
            i + 2u,
            i + 3u);
        if (written <= 0 || (size_t)written >= capacity - used) return 0u;
        used += (size_t)written;
    }
    if (atom_count % 4u != 0u) {
        const size_t i = full_groups * 4u;
        const int written = snprintf(
            source + used,
            capacity - used,
            "quad(\"v%03zu\",\"v000\",\"v001\",\"v002\").\n",
            i);
        if (written <= 0 || (size_t)written >= capacity - used) return 0u;
        used += (size_t)written;
    }
    return used;
}

static int test_public_api_policy_atom_limits_and_filter_separation(void) {
    TEST_BEGIN();
    const char *domain = "public_api_policy_atom_limits";
    const unsigned flag = MAELYS_DATALOG_PUBLIC_ALLOW_UNDECLARED_POLICY_ATOMS;
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK, register_domain(domain), "%d");

    char atom63[96];
    char atom64[96];
    char value63[64];
    char value64[65];
    memset(value63, 'a', sizeof(value63) - 1u);
    value63[sizeof(value63) - 1u] = '\0';
    memset(value64, 'a', sizeof(value64) - 1u);
    value64[sizeof(value64) - 1u] = '\0';
    (void)snprintf(atom63, sizeof(atom63), "policy_value(\"%s\").\n", value63);
    (void)snprintf(atom64, sizeof(atom64), "policy_value(\"%s\").\n", value64);
    maelys_datalog_policy_t *policy = NULL;
    TEST_ASSERT_EQUAL(
        MAELYS_DATALOG_STATUS_OK,
        load_manifest_source(
            domain,
            atom63,
            "bc023635d484a16d45b825859b717351ffd3a1a4c541a165e21743dbee608c92",
            flag,
            &policy,
            NULL),
        "%d");
    if (policy) (void)maelys_datalog_policy_free(policy);
    policy = (maelys_datalog_policy_t *)(uintptr_t)1u;
    maelys_datalog_public_diagnostic_t diagnostic;
    TEST_ASSERT_EQUAL(
        MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE,
        load_manifest_source(
            domain,
            atom64,
            "dd80db0bb8c7b8d54c49fd40f91aecdd20d19149af4e4ccac094a892d0ecadcd",
            flag,
            &policy,
            &diagnostic),
        "%d");
    TEST_ASSERT_NULL(policy);
    TEST_ASSERT_NOT_NULL(strstr(diagnostic.message, "capacity"));

    char source[4096];
    size_t used = append_quad_facts(source, sizeof(source), 256u);
    TEST_ASSERT_TRUE(used > 0u);
    TEST_ASSERT_EQUAL(
        MAELYS_DATALOG_STATUS_OK,
        load_manifest_source(
            domain,
            source,
            "b5add256a65ae808a7b32893cd36c2731074f491ecac1b70c961631ba5c423ba",
            flag,
            &policy,
            NULL),
        "%d");
    if (policy) (void)maelys_datalog_policy_free(policy);

    used = append_quad_facts(source, sizeof(source), 257u);
    TEST_ASSERT_TRUE(used > 0u);
    policy = (maelys_datalog_policy_t *)(uintptr_t)1u;
    TEST_ASSERT_EQUAL(
        MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE,
        load_manifest_source(
            domain,
            source,
            "b1ef27ab531cb66ef85ec9405104ecbf58f50da19b0a72022ee6c15960f074df",
            flag,
            &policy,
            &diagnostic),
        "%d");
    TEST_ASSERT_NULL(policy);

    used = append_quad_facts(source, sizeof(source), 256u);
    const int filter_written = snprintf(
        source + used,
        sizeof(source) - used,
        "allow(X) :- observed(X), starts_with(X, "
        "\"refs/heads/release/xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\").\n");
    TEST_ASSERT_TRUE(filter_written > 0);
    TEST_ASSERT_EQUAL(
        MAELYS_DATALOG_STATUS_OK,
        load_manifest_source(
            domain,
            source,
            "fd38a529d870e5200bdc30eb0cd5a23ce95fb5d4a6c61ccff892137f7bc60f68",
            flag,
            &policy,
            NULL),
        "%d");
    if (policy) (void)maelys_datalog_policy_free(policy);

    source[0] = '\0';
    used = 0u;
    for (size_t i = 0u; i < 64u; i++) {
        const int written = snprintf(
            source + used, sizeof(source) - used, "policy_value(\"same\").\n");
        TEST_ASSERT_TRUE(written > 0 && (size_t)written < sizeof(source) - used);
        used += (size_t)written;
    }
    TEST_ASSERT_EQUAL(
        MAELYS_DATALOG_STATUS_OK,
        load_manifest_source(
            domain,
            source,
            "b46e071ab865a83625d013c0cded46af0304f95df06a0f3ac00f8be5c8e2194f",
            flag,
            &policy,
            NULL),
        "%d");
    if (policy) (void)maelys_datalog_policy_free(policy);
    TEST_END();
}

static int test_public_api_policy_atom_fingerprint_semantics(void) {
    TEST_BEGIN();
    const char *domain = "public_api_policy_atom_fingerprint";
    const char *atoms[] = {"alpha"};
    const char source_a[] =
        "policy_value(\"alpha\").\n"
        "allow(X) :- policy_value(X).\n";
    const char source_b[] =
        "policy_value(\"beta\").\n"
        "allow(X) :- policy_value(X).\n";
    const char sha_a[] = "41ad802575655840e11ca1a1bdba31d638588fc5c39d09b7b5e74ef5acba7477";
    const char sha_b[] = "ae6be842375e9adc03836f9dd9e01eba6857870e89e75b6ffc289f075217257d";
    TEST_ASSERT_EQUAL(
        MAELYS_DATALOG_STATUS_OK,
        register_domain_with_atoms(domain, atoms, 1u),
        "%d");
    maelys_datalog_policy_t *closed = NULL;
    maelys_datalog_policy_t *open_same = NULL;
    maelys_datalog_policy_t *open_other = NULL;
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                      load_manifest_source(domain, source_a, sha_a, 0u, &closed, NULL), "%d");
    TEST_ASSERT_EQUAL(
        MAELYS_DATALOG_STATUS_OK,
        load_manifest_source(
            domain,
            source_a,
            sha_a,
            MAELYS_DATALOG_PUBLIC_ALLOW_UNDECLARED_POLICY_ATOMS,
            &open_same,
            NULL),
        "%d");
    TEST_ASSERT_EQUAL(
        MAELYS_DATALOG_STATUS_OK,
        load_manifest_source(
            domain,
            source_b,
            sha_b,
            MAELYS_DATALOG_PUBLIC_ALLOW_UNDECLARED_POLICY_ATOMS,
            &open_other,
            NULL),
        "%d");
    if (closed && open_same && open_other) {
        char closed_fingerprint[65];
        char same_fingerprint[65];
        char other_fingerprint[65];
        TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                          maelys_datalog_policy_fingerprint(closed, closed_fingerprint), "%d");
        TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                          maelys_datalog_policy_fingerprint(open_same, same_fingerprint), "%d");
        TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                          maelys_datalog_policy_fingerprint(open_other, other_fingerprint), "%d");
        TEST_ASSERT_EQUAL_STRING(closed_fingerprint, same_fingerprint);
        TEST_ASSERT_TRUE(strcmp(closed_fingerprint, other_fingerprint) != 0);
    }
    if (closed) (void)maelys_datalog_policy_free(closed);
    if (open_same) (void)maelys_datalog_policy_free(open_same);
    if (open_other) (void)maelys_datalog_policy_free(open_other);
    TEST_END();
}

static int test_public_api_manifest_selects_nonzero_policy(void) {
    TEST_BEGIN();
    const char *domain = "public_api_manifest";
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK, register_domain(domain), "%d");
    char directory[] = "/tmp/maelys-public-api-XXXXXX";
    TEST_ASSERT_NOT_NULL(mkdtemp(directory));
    char first_path[256];
    char second_path[256];
    char manifest_path[256];
    (void)snprintf(first_path, sizeof(first_path), "%s/first.dl", directory);
    (void)snprintf(second_path, sizeof(second_path), "%s/second.dl", directory);
    (void)snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.json", directory);
    const char source[] = "allow(X) :- observed(X).\n";
    TEST_ASSERT_TRUE(write_bytes(first_path, source, strlen(source)));
    TEST_ASSERT_TRUE(write_bytes(second_path, source, strlen(source)));
    const char manifest[] =
        "{\"policy_set_id\":\"public.set\",\"policy_set_version\":\"1\","
        "\"manifest_version\":\"1\",\"default_profile\":\"enforce\","
        "\"created_for\":\"test\",\"strict_loading\":true,\"fail_closed\":true,"
        "\"capabilities\":[],\"policies\":["
        "{\"policy_id\":\"first\",\"domain\":\"public_api_manifest\","
        "\"file\":\"first.dl\",\"sha256\":\"bac45b7b2ed8778f13f7f857595868d5bd38bd9381708d2dba3c4c999da42630\","
        "\"mode\":\"enforce\",\"enabled\":true,\"description\":\"first\","
        "\"queries\":[{\"name\":\"allow\",\"arity\":1}]},"
        "{\"policy_id\":\"second\",\"domain\":\"public_api_manifest\","
        "\"file\":\"second.dl\",\"sha256\":\"bac45b7b2ed8778f13f7f857595868d5bd38bd9381708d2dba3c4c999da42630\","
        "\"mode\":\"enforce\",\"enabled\":true,\"description\":\"second\","
        "\"queries\":[{\"name\":\"allow\",\"arity\":1}]}]}";
    TEST_ASSERT_TRUE(write_bytes(manifest_path, manifest, strlen(manifest)));

    maelys_datalog_policy_t *policy = NULL;
    maelys_datalog_public_diagnostic_t diagnostic;
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                      maelys_datalog_policy_load_manifest(
                          manifest_path, 0u, &policy, &diagnostic), "%d");
    TEST_ASSERT_NOT_NULL(policy);
    if (policy) {
        size_t count = 0u;
        TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                          maelys_datalog_policy_count(policy, &count), "%d");
        TEST_ASSERT_EQUAL((size_t)2u, count, "%zu");
        maelys_datalog_session_t *session = NULL;
        TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                          maelys_datalog_session_create(policy, 1u, &session), "%d");
        TEST_ASSERT_NOT_NULL(session);
        if (session) TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                                       maelys_datalog_session_free(session), "%d");
        TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                          maelys_datalog_policy_free(policy), "%d");
    }
    (void)unlink(first_path);
    (void)unlink(second_path);
    (void)unlink(manifest_path);
    (void)rmdir(directory);
    TEST_END();
}

static int test_public_api_errors_are_atomic_and_diagnostic(void) {
    TEST_BEGIN();
    const char *domain = "public_api_errors";
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK, register_domain(domain), "%d");
    const char invalid_source[] = "allow(X) :- missing(X).\n";
    maelys_datalog_policy_t *policy = (maelys_datalog_policy_t *)(uintptr_t)1u;
    maelys_datalog_public_diagnostic_t diagnostic;
    memset(&diagnostic, 0x7f, sizeof(diagnostic));
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_INVALID_FIELD,
                      maelys_datalog_policy_load_inline(
                          domain, "invalid", invalid_source, strlen(invalid_source),
                          &policy, &diagnostic), "%d");
    TEST_ASSERT_NULL(policy);
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAGNOSTIC_LOAD, diagnostic.source, "%d");
    TEST_ASSERT_TRUE(diagnostic.code != 0);
    TEST_ASSERT_TRUE(diagnostic.message[0] != '\0');

    const char valid_source[] = "allow(X) :- observed(X).\n";
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                      maelys_datalog_policy_load_inline(
                          domain, "valid", valid_source, strlen(valid_source),
                          &policy, &diagnostic), "%d");
    maelys_datalog_session_t *session = NULL;
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                      maelys_datalog_session_create(policy, 0u, &session), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                      maelys_datalog_policy_free(policy), "%d");
    maelys_datalog_result_t *result = NULL;
    maelys_datalog_public_fact_t bad = fact("missing", symbol_value("alice"));
    TEST_ASSERT_TRUE(maelys_datalog_session_solve(
                         session, &bad, 1u, &result, &diagnostic) !=
                     MAELYS_DATALOG_STATUS_OK);
    TEST_ASSERT_NULL(result);
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAGNOSTIC_SOLVE, diagnostic.source, "%d");

    maelys_datalog_public_fact_t good = fact("observed", symbol_value("alice"));
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                      maelys_datalog_session_solve(
                          session, &good, 1u, &result, &diagnostic), "%d");
    const char *text = (const char *)(uintptr_t)1u;
    size_t length = 999u;
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_INVALID_STATE,
                      maelys_datalog_result_symbol_text(result, UINT32_MAX, &text, &length),
                      "%d");
    TEST_ASSERT_TRUE(text == (const char *)(uintptr_t)1u);
    TEST_ASSERT_EQUAL((size_t)999u, length, "%zu");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                      maelys_datalog_result_free(result), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                      maelys_datalog_session_free(session), "%d");
    TEST_END();
}

int main(int argc, char **argv) {
    const test_case_t cases[] = {
        {"public_api/complete_lifecycle", TEST_MODE_NON_BLOCKING,
         test_public_api_complete_lifecycle},
        {"public_api/domain_is_copied_and_divergence_refused", TEST_MODE_NON_BLOCKING,
         test_public_api_domain_is_copied_and_divergence_refused},
        {"public_api/manifest_selects_nonzero_policy", TEST_MODE_NON_BLOCKING,
         test_public_api_manifest_selects_nonzero_policy},
        {"public_api/errors_are_atomic_and_diagnostic", TEST_MODE_NON_BLOCKING,
         test_public_api_errors_are_atomic_and_diagnostic},
        {"public_api/policy_atom_modes", TEST_MODE_NON_BLOCKING,
         test_public_api_policy_atom_modes},
        {"public_api/policy_atom_load_isolation", TEST_MODE_NON_BLOCKING,
         test_public_api_policy_atom_load_isolation},
        {"public_api/policy_atom_limits_and_filter_separation", TEST_MODE_NON_BLOCKING,
         test_public_api_policy_atom_limits_and_filter_separation},
        {"public_api/policy_atom_fingerprint_semantics", TEST_MODE_NON_BLOCKING,
         test_public_api_policy_atom_fingerprint_semantics},
    };
    return test_main("maelys_datalog_public_api",
                     cases,
                     (int)(sizeof(cases) / sizeof(cases[0])),
                     argc,
                     argv);
}
