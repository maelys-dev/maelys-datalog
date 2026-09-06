/* SPDX-License-Identifier: MPL-2.0 */
#include <maelys/datalog_backend.h>
#include <maelys/datalog_module.h>
#include "common/maelys_sha256.h"
#include "src/core/maelys_datalog_pipeline_testing.h"
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
 * Captured on the pre-refactor tree (PR #4 merge b55f3d6) with only the
 * final WHY-FALSE-v1 formatter applied, so they still hold the single-pass
 * pipeline to the legacy identities and proof texts (SMALL and LARGE).
 * `expected` covers the implicit, inline and explicit built-in descriptor;
 * `expected_wrapped` covers a copied descriptor that wraps the standard
 * lowering, which carries the extended identity like any other frontend. */
#ifdef MAELYS_DATALOG_PROFILE_LARGE
static const char *const expected[] = {
    "fd2055959520286d8cce0d481d9c064c13c5f3b09949a6f37f482e6d3f3d0755",
    "55f5612ad3d37b5d333ba2870b37fd7ea7b56a6bcf48e3549b8c34326c491ba6",
    "9f060c2cc05990697df10d006ddbd2c08ac38a583058b7e46e394faacc7fc7c6",
    "ea445cabc17d8edd680f69bbf0159da06aa7fc8afdb207e3136bbdce7856917f",
    "09c164e9f75280961273b9cdcd84a381e4043d7800f2c84a7016d02ef0a34105",
    "d339de290deb5f702eccca4924336838d0784a0b0744f4d09b9e9b1a9de8fd8a"};
static const char *const expected_wrapped[] = {
    "372ad882766190bf0d902e13b30f26acb2cedc63d5984e0f782eee9567e67ee5",
    "12f6ae02b2f3d1044dbfbc9cd5fcd0c2447f6d9d2d2ed999e3d132e23c67c5de",
    "9414d4e5e4aac5d7b65d97671187d39ee78822b6b89e1c315379a0f54ab7d911",
    "aa3fcdb969b4f1c722d5348feb7145e32a7059554f635ec6745e4350d07cb004",
    "975bef30684c1451e685b563b843ffb5157b2164433fb607a4e91f2fc8570bb1"};
#else
static const char *const expected[] = {
    "e4ad61f97773f694dfc29c4884e641ff00fa2b7da0c703062d57aa7b48251710",
    "acf55233edfd870f61e7c06ade32681373ec23423dcc89bcdeffd5066f4d4d4c",
    "080de07a8450264b086e3164d6841e032b9637e09e463dd919e10fbbe5abf626",
    "6f0614551ea91d4ae480420abb1400b1e647a5e5b042c2349b0be11377de2a99",
    "4803cc3e2e209755c2a0291ddd38a1ca08cfaa0168809b89dd2e55321d6b567c",
    "5cdaefda4995878fc11cc8d47dcce9adfe2b966f9187878ed9d71eeb440631c5"};
static const char *const expected_wrapped[] = {
    "d973ae3428576b0a33b2431d8da9b760c40263f884fc9ac4be74b9e3e442e6fe",
    "7f8fcbe346f0b0ae393e4e70ad86d93ae680e1dc919a44345fea65d4f13957b0",
    "c57ac50c6f6f8e683f1aca7ad76dd7333c9e6405eeee031503f8b40501db3e81",
    "0311a25cf121acea6318f69383c0c283c4614f8dfca0692b76ceb1ae796b459e",
    "5e14a2e5c5c0b1be483a848cad67a2f80221059b65b2e5178592a9e423d0b56e"};
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
    const char *want = loader == 3 ? expected_wrapped[index] : expected[index];
    if (strcmp(actual, want)) {
        fprintf(stderr, "pipeline case %zu loader %d: expected %s, got %s\n", index, loader,
                want, actual);
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
            /* Reported at the end of the rejected clause, not after the pattern. */
            assert(diag.line == 1 && diag.column == 42);
        } else {
            assert(!rc);
            assert(!maelys_datalog_policy_free(policy));
        }
    }
}
static maelys_datalog_public_diagnostic_t load_failure(const char *source) {
    maelys_datalog_policy_t *policy = NULL;
    maelys_datalog_public_diagnostic_t diag;
    assert(maelys_datalog_policy_load_inline("pipeline", "order", source, strlen(source), &policy,
                                             &diag) != MAELYS_DATALOG_STATUS_OK);
    assert(!policy && diag.code);
    return diag;
}
static int same_diagnostic(const maelys_datalog_public_diagnostic_t *a,
                           const maelys_datalog_public_diagnostic_t *b) {
    return a->code == b->code && a->line == b->line && a->column == b->column &&
           !strcmp(a->message, b->message);
}
/* Validation is a single pass after parsing, yet the reported diagnostic must
 * be the one the former per-clause parser produced: a clause-local error in an
 * earlier clause wins over a later parse error, while stratification stays a
 * whole-program check that follows any parse error. */
static void diagnostic_order(void) {
    const char *unsafe = "allow(X) :- seed(Y).";
    const char *edb_head = "seed(X) :- allow(X).";
    const char *unstratified =
        "allow(X) :- seed(X), not(reach(X, X)).\nreach(X, Y) :- edge(X, Y), not(allow(X)).";
    const char *broken = "allow(X) :- seed(X";
    const maelys_datalog_public_diagnostic_t unsafe_diag = load_failure(unsafe);
    const maelys_datalog_public_diagnostic_t edb_head_diag = load_failure(edb_head);
    const maelys_datalog_public_diagnostic_t strata_diag = load_failure(unstratified);
    assert(unsafe_diag.code == MAELYS_DATALOG_DIAG_PARSER_UNSAFE_VARIABLE);
    assert(edb_head_diag.code == MAELYS_DATALOG_DIAG_PARSER_RULE_HEAD_EDB_FORBIDDEN);
    assert(strata_diag.code == MAELYS_DATALOG_DIAG_POLICY_NOT_STRATIFIABLE);
    char source[256];
    maelys_datalog_public_diagnostic_t diag;
    snprintf(source, sizeof(source), "%s\n%s", unsafe, broken);
    diag = load_failure(source);
    assert(same_diagnostic(&diag, &unsafe_diag));
    snprintf(source, sizeof(source), "%s\n%s", edb_head, broken);
    diag = load_failure(source);
    assert(same_diagnostic(&diag, &edb_head_diag));
    snprintf(source, sizeof(source), "%s\n%s", unstratified, broken);
    diag = load_failure(source);
    assert(diag.code != MAELYS_DATALOG_DIAG_POLICY_NOT_STRATIFIABLE && diag.line == 3);
    snprintf(source, sizeof(source), "%s\n%s", unsafe, unstratified);
    diag = load_failure(source);
    assert(same_diagnostic(&diag, &unsafe_diag));
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
    diagnostic_order();
    puts("pipeline: 4 diagnostic-order checks passed");
    if (argc == 2 && !strcmp(argv[1], "--bench"))
        bench();
    return 0;
}
