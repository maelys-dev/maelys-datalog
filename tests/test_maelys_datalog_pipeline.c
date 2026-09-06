/* SPDX-License-Identifier: MPL-2.0 */
#include <maelys/datalog_backend.h>
#include <maelys/datalog_module.h>
#include "common/maelys_sha256.h"
#include "tests/helpers/pipeline_counts.h"
#ifdef MAELYS_TESTING
_Thread_local maelys_datalog_pipeline_counts_t maelys_datalog_pipeline_counts;
#endif
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern const maelys_datalog_frontend_t *example_arrow_frontend(void);
static size_t filter_validations;
static maelys_datalog_status_t counted_validate(const unsigned char *pattern, size_t n) {
    ++filter_validations;
    return n == 6 && !memcmp(pattern, "reject", 6) ? MAELYS_DATALOG_STATUS_INVALID_FIELD
                                                   : MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t counted_cost(size_t vn, size_t pn, size_t *out) {
    (void)vn;
    (void)pn;
    *out = 1;
    return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t counted_evaluate(const unsigned char *v, size_t vn,
                                                const unsigned char *p, size_t pn, int *out) {
    (void)v;
    (void)vn;
    (void)p;
    (void)pn;
    *out = 1;
    return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t observed_lower(const char *source, size_t size,
                                              maelys_datalog_program_builder_t *builder,
                                              maelys_datalog_public_diagnostic_t *diag) {
    maelys_datalog_status_t rc =
        maelys_datalog_frontend_datalog()->lower(source, size, builder, diag);
#ifdef MAELYS_TESTING
    assert(maelys_datalog_pipeline_counts.parses == 1);
    assert(maelys_datalog_pipeline_counts.validations == 0);
    assert(maelys_datalog_pipeline_counts.fingerprints == 0);
#endif
    return rc;
}
static void hash_text(maelys_sha256_ctx_t *hash, const char *text) {
    maelys_sha256_update(hash, (const unsigned char *)text, strlen(text) + 1);
}
/* SHA-256 of exact NUL-delimited authority/program/execution fingerprints and
 * why-true/why-false text, including a second solve with reversed inputs.
 * Captured BEFORE this refactor, at PR #4 commit 9825a6c (SMALL and LARGE). */
#ifdef MAELYS_DATALOG_PROFILE_LARGE
static const char *const expected[] = {
    "6a7c1762ee4c5e77d5452797a69cc530bc8de4f161d4789ec1eaff1d3b904e17",
    "1128e5845605ccab0a18ccf2797b91cd9eb2e7efcee2efd20c87187cb2ca1eba",
    "dbbf955986af544f73825008e75c96ed42171e016ecb754fe3dec35c22d1d339",
    "50f0fa6452ae14d3eb4b682e4cf54b3547f97529cb51fae30baf1daba0e546e2",
    "9fc4bfa7ae0cbba0de9e45fe14d50f0478cac5026c40aee02d0926822357bea7",
    "61c96f83be2aac2e0f436d67a065e2473fb5759705ab741fad41d8c60a7a94f9"};
#else
static const char *const expected[] = {
    "b8151d2ddddf62acb67b733bc7eece7c878d231ec0b46dc5380b0d6e51a19b51",
    "c6352250363127e20cd6efeedfa4c11acd13612a06c202866c1b108b179182bc",
    "49ad1babba77a0fd1951819eeebecc366bcc1a2553c2b60e3dd678b031dff26f",
    "fd9778ac6ed1a839a07144393fb94c933ef9fed4380b03828b7182c06d4367a7",
    "f850d8096754c7822e645e20b1b7ecb5d70ca06eda4c084a80889282a20764fe",
    "1b7baae272222dfaadd110485c8ead46b734c3dfd82fb6edaf5e8047ffe2ea3d"};
#endif
static void probe_case(size_t index, const char *source, int loader) {
#ifdef MAELYS_TESTING
    maelys_datalog_pipeline_counts = (maelys_datalog_pipeline_counts_t){0};
#endif
    maelys_datalog_policy_t *policy;
    maelys_datalog_frontend_t copied = *maelys_datalog_frontend_datalog();
    copied.lower = observed_lower;
    if (loader == 1)
        assert(!maelys_datalog_policy_load_inline("pipeline", "pipeline.test", source,
                                                  strlen(source), &policy, NULL));
    else
        assert(!maelys_datalog_policy_load_frontend(
            "pipeline", "pipeline.test", source, strlen(source),
            index == 5    ? example_arrow_frontend()
            : loader == 2 ? maelys_datalog_frontend_datalog()
            : loader == 3 ? &copied
                          : NULL,
            &policy, NULL));
#ifdef MAELYS_TESTING
    assert(maelys_datalog_pipeline_counts.validations == 1);
    assert(maelys_datalog_pipeline_counts.fingerprints == 1);
    assert(maelys_datalog_pipeline_counts.preparations == 0);
#endif
    maelys_datalog_session_t *session;
    const maelys_datalog_session_options_t options = {MAELYS_DATALOG_BACKEND_ABI_VERSION,
                                                      sizeof(options),
                                                      maelys_datalog_backend_reference(), 0, 0};
    if (loader)
        assert(!maelys_datalog_session_create_ex(policy, 0, &options, &session));
    else
        assert(!maelys_datalog_session_create(policy, 0, &session));
    assert(!maelys_datalog_policy_free(policy));
    char fingerprint[65];
    maelys_sha256_ctx_t hash;
    maelys_sha256_init(&hash);
    assert(!maelys_datalog_session_fingerprint(session, fingerprint));
    hash_text(&hash, fingerprint);
    const maelys_datalog_program_t *program;
    assert(!maelys_datalog_session_program(session, &program));
    assert(!maelys_datalog_program_fingerprint(program, fingerprint));
    hash_text(&hash, fingerprint);
    assert(!maelys_datalog_program_fingerprint(program, fingerprint)); /* Cached second read. */
    assert(!maelys_datalog_session_execution_fingerprint(session, fingerprint));
    hash_text(&hash, fingerprint);
    maelys_datalog_public_fact_t facts[7] = {0};
    const char *predicates[] = {"seed", "seed", "extra", "blocked", "number", "edge", "edge"};
    const char *values[] = {"alice", "bob", "bob", "bob", "unused", "alice", "bob"};
    for (size_t i = 0; i < 7; ++i) {
        facts[i].predicate = predicates[i];
        facts[i].arity = 1;
        facts[i].terms[0].kind = MAELYS_DATALOG_VALUE_SYMBOL;
        facts[i].terms[0].as.symbol = values[i];
    }
    facts[4].terms[0].kind = MAELYS_DATALOG_VALUE_INTEGER;
    facts[4].terms[0].as.integer = 2;
    for (size_t i = 5; i < 7; ++i) {
        facts[i].arity = 2;
        facts[i].terms[1].kind = MAELYS_DATALOG_VALUE_SYMBOL;
        facts[i].terms[1].as.symbol = i == 5 ? "bob" : "carol";
    }
    for (size_t repeat = 0; repeat < 2; ++repeat) {
        maelys_datalog_result_t *result;
        assert(!maelys_datalog_session_solve(session, facts, 7, &result, NULL));
        maelys_datalog_public_value_t value = {0};
        value.kind = MAELYS_DATALOG_VALUE_SYMBOL;
        for (size_t absent = 0; absent < 2; ++absent) {
            value.as.symbol = absent ? "carol" : "alice";
            size_t size;
            maelys_datalog_status_t (*explain)(const maelys_datalog_result_t *, const char *,
                                               const maelys_datalog_public_value_t *, size_t,
                                               char *, size_t, size_t *) =
                absent ? maelys_datalog_result_explain_false_text
                       : maelys_datalog_result_explain_true_text;
            assert(!explain(result, "allow", &value, 1, NULL, 0, &size));
            char *text = malloc(size + 1);
            assert(text);
            assert(!explain(result, "allow", &value, 1, text, size + 1, &size));
            hash_text(&hash, text);
            free(text);
        }
        assert(!maelys_datalog_result_free(result));
        for (size_t i = 0; i < 3; ++i) {
            maelys_datalog_public_fact_t f = facts[i];
            facts[i] = facts[6 - i];
            facts[6 - i] = f;
        }
    }
    assert(!maelys_datalog_session_free(session));
    unsigned char digest[32];
    maelys_sha256_final(&hash, digest);
    char actual[65];
    for (size_t i = 0; i < 32; ++i)
        snprintf(actual + 2 * i, 3, "%02x", digest[i]);
    if (strcmp(actual, expected[index])) {
        fprintf(stderr, "pipeline case %zu loader %d: expected %s, got %s\n", index, loader,
                expected[index], actual);
        abort();
    }
#ifdef MAELYS_TESTING
    assert(maelys_datalog_pipeline_counts.parses == (index == 5 ? 0u : 1u));
    assert(maelys_datalog_pipeline_counts.validations == 1);
    assert(maelys_datalog_pipeline_counts.fingerprints == 1);
    assert(maelys_datalog_pipeline_counts.preparations == 1);
    assert(maelys_datalog_pipeline_counts.materializations == 2);
#endif
}
static void filter_validation_once(void) {
    const char *sources[] = {"allow(X) :- seed(X) or extra(X), counted(X, \"ok\").",
                             "allow(X) :- seed(X), counted(X, \"reject\")."};
    for (size_t i = 0; i < 2; ++i) {
        filter_validations = 0;
        maelys_datalog_policy_t *policy = NULL;
        maelys_datalog_public_diagnostic_t diag;
        maelys_datalog_status_t rc = maelys_datalog_policy_load_inline(
            "pipeline", "filter", sources[i], strlen(sources[i]), &policy, &diag);
        assert(filter_validations == 1); /* Including shared OR alternatives. */
        if (i) {
            assert(rc == MAELYS_DATALOG_STATUS_INVALID_FIELD && !policy);
            assert(diag.code == MAELYS_DATALOG_DIAG_PARSER_INVALID_FILTER);
        } else {
            assert(!rc);
            assert(!maelys_datalog_policy_free(policy));
        }
    }
}
static void bench(void) {
#ifdef MAELYS_TESTING
    maelys_datalog_pipeline_counts = (maelys_datalog_pipeline_counts_t){0};
    const char *source = "allow(X) :- seed(X).";
    maelys_datalog_policy_t *policy;
    maelys_datalog_session_t *session;
    assert(!maelys_datalog_policy_load_inline("pipeline", "bench", source, strlen(source), &policy,
                                              NULL));
    assert(!maelys_datalog_session_create(policy, 0, &session));
    assert(!maelys_datalog_policy_free(policy));
    maelys_datalog_public_fact_t fact = {0};
    fact.predicate = "seed";
    fact.arity = 1;
    fact.terms[0].kind = MAELYS_DATALOG_VALUE_SYMBOL;
    fact.terms[0].as.symbol = "alice";
    const size_t solves = 2000;
    const clock_t start = clock();
    for (size_t i = 0; i < solves; ++i) {
        maelys_datalog_result_t *result;
        assert(!maelys_datalog_session_solve(session, &fact, 1, &result, NULL));
        assert(!maelys_datalog_result_free(result));
    }
    double seconds = (double)(clock() - start) / CLOCKS_PER_SEC;
    assert(maelys_datalog_pipeline_counts.materializations == solves);
    assert(maelys_datalog_pipeline_counts.preparations == 1);
    assert(maelys_datalog_pipeline_counts.validations == 1);
    assert(maelys_datalog_pipeline_counts.fingerprints == 1);
    printf("pipeline benchmark: %zu solves, %zu materializations (1/solve), %zu preparation, %.3f "
           "CPU seconds\n",
           solves, maelys_datalog_pipeline_counts.materializations,
           maelys_datalog_pipeline_counts.preparations, seconds);
    assert(!maelys_datalog_session_free(session));
#else
    puts("benchmark counters require MAELYS_TESTING (make bench-pipeline)");
#endif
}
int main(int argc, char **argv) {
    const maelys_datalog_filter_module_t module = {MAELYS_DATALOG_MODULE_ABI_VERSION,
                                                   sizeof(module),
                                                   "counted",
                                                   "test.counted.v1",
                                                   counted_validate,
                                                   counted_cost,
                                                   counted_evaluate};
    assert(!maelys_datalog_register_filter_module(&module));
    const maelys_datalog_public_predicate_t predicates[] = {
        {"seed", 1, MAELYS_DATALOG_PREDICATE_EDB},
        {"extra", 1, MAELYS_DATALOG_PREDICATE_EDB},
        {"blocked", 1, MAELYS_DATALOG_PREDICATE_EDB},
        {"number", 1, MAELYS_DATALOG_PREDICATE_EDB},
        {"edge", 2, MAELYS_DATALOG_PREDICATE_EDB},
        {"base", 1, MAELYS_DATALOG_PREDICATE_POLICY_FACT},
        {"reach", 2, MAELYS_DATALOG_PREDICATE_IDB},
        {"allow", 1, MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY}};
    const char *atoms[] = {"alice", "carol"};
    const maelys_datalog_public_domain_t domain = {"pipeline", predicates, 8, atoms, 2};
    assert(!maelys_datalog_domain_register(&domain));
    const char *sources[] = {
        "base(\"alice\"). allow(X) :- base(X). allow(X) :- seed(X) or extra(X).",
        "allow(X) :- seed(X), not(blocked(X)).",
        "allow(X) :- number(N), N + 1 > 2, seed(X).",
        "allow(X) :- seed(X), starts_with(X, \"a\").",
        ("reach(X,Y) :- edge(X,Y). reach(X,Z) :- reach(X,Y), edge(Y,Z). allow(X) :- seed(X), "
         "reach(X,\"carol\")."),
        "allow <- seed"};
    for (size_t i = 0; i < 6; ++i)
        for (int loader = 0; loader < (i == 5 ? 1 : 4); ++loader)
            probe_case(i, sources[i], loader);
    puts("pipeline: 21 legacy transcript/cache/single-pass checks passed");
    filter_validation_once();
    puts("pipeline: 2 single filter-validation checks passed");
    if (argc == 2 && !strcmp(argv[1], "--bench"))
        bench();
    return 0;
}
