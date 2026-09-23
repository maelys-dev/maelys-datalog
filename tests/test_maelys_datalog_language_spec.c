#include "tests/helpers/test_framework.h"

#include "src/core/maelys_datalog_explanation_format.h"
#include "src/core/maelys_datalog_parser.h"
#include "src/core/maelys_datalog_predicate_registry.h"
#include "src/core/maelys_datalog_ruleset.h"
#include "src/core/maelys_datalog_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int add_predicate(maelys_datalog_internal_ruleset_t *ruleset,
                         const char *name,
                         uint8_t arity,
                         uint8_t kind) {
    return maelys_datalog_predicate_registry_add_domain(
        &ruleset->registry, name, arity, kind);
}

static int init_ruleset(maelys_datalog_internal_ruleset_t *ruleset) {
    memset(ruleset, 0, sizeof(*ruleset));
    int rc = maelys_datalog_ruleset_init(
        ruleset, "v2.conformance", "v2", MAELYS_DATALOG_SHA256_UNSET, 1);
    if (rc != MAELYS_OK) return rc;
    struct {
        const char *name;
        uint8_t arity;
        uint8_t kind;
    } predicates[] = {
        {"user", 1u, MAELYS_DATALOG_PRED_KIND_EDB},
        {"blocked", 1u, MAELYS_DATALOG_PRED_KIND_POLICY_FACT},
        {"safe", 1u, MAELYS_DATALOG_PRED_KIND_POLICY_FACT},
        {"edge", 2u, MAELYS_DATALOG_PRED_KIND_EDB},
        {"owns", 2u, MAELYS_DATALOG_PRED_KIND_EDB},
        {"score", 2u, MAELYS_DATALOG_PRED_KIND_EDB},
        {"or", 2u, MAELYS_DATALOG_PRED_KIND_EDB},
        {"allow", 1u, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
        {"path", 2u, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
        {"deny", 2u, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
    };
    for (size_t i = 0; i < sizeof(predicates) / sizeof(predicates[0]); i++) {
        rc = add_predicate(ruleset,
                           predicates[i].name,
                           predicates[i].arity,
                           predicates[i].kind);
        if (rc != MAELYS_OK) return rc;
    }
    const char *atoms[] = {"alice", "bob", "a", "b"};
    for (size_t i = 0; i < sizeof(atoms) / sizeof(atoms[0]); i++) {
        rc = maelys_datalog_predicate_registry_add_atom(&ruleset->registry, atoms[i]);
        if (rc != MAELYS_OK) return rc;
    }
    return maelys_datalog_predicate_registry_freeze(&ruleset->registry);
}

static int parse_source(const char *source, maelys_datalog_internal_diagnostic_t *diag) {
    maelys_datalog_internal_ruleset_t ruleset;
    int rc = init_ruleset(&ruleset);
    if (rc == MAELYS_OK) {
        rc = maelys_datalog_parse_ruleset_ex(
            &ruleset, source, strlen(source), "<v2-conformance>", diag);
    }
    maelys_datalog_ruleset_clear(&ruleset);
    return rc;
}

static int test_v2_profile_and_source_contract(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL_STRING("MAELYS-DATALOG-v2", MAELYS_DATALOG_PROFILE_NAME);
    static const char source[] =
        "safe(\"alice\").\n"
        "allow(X) :- user(X), not(blocked(X)).\n"
        "path(X, Y) :- edge(X, Y) or owns(X, Y).\n"
        "path(X, Y) :- edge(X, Y), edge(X, _).\n"
        "allow(X) :- score(X, S), S * 2 + 1 >= 7.\n";
    maelys_datalog_internal_diagnostic_t diag = {0};
    TEST_ASSERT_EQUAL(MAELYS_OK, parse_source(source, &diag), "%d");

    TEST_ASSERT_TRUE(parse_source("allow(_) :- user(_).\n", &diag) != MAELYS_OK);
    TEST_ASSERT_TRUE(parse_source("allow(X) :- user(X), not blocked(X).\n", &diag) != MAELYS_OK);
    TEST_ASSERT_TRUE(parse_source("path(X,Y) :- edge(X,Y) OR owns(X,Y).\n", &diag) != MAELYS_OK);
    TEST_ASSERT_TRUE(parse_source("safe(\"line\\nbreak\").\n", &diag) != MAELYS_OK);
    TEST_END();
}

static int test_contextual_or_and_atomic_rejection(void) {
    TEST_BEGIN();
    maelys_datalog_internal_ruleset_t ruleset;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_ruleset(&ruleset), "%d");
    const char valid[] = "path(X,Y) :- edge(X,Y) or or(X,Y).\n";
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_parse_ruleset(&ruleset, valid, strlen(valid)),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)2u, ruleset.rule_count, "%zu");
    const size_t before = ruleset.rule_count;
    const char invalid[] = "deny(X,Y) :- user(X) or user(Y).\n";
    TEST_ASSERT_TRUE(
        maelys_datalog_parse_ruleset(&ruleset, invalid, strlen(invalid)) != MAELYS_OK);
    TEST_ASSERT_EQUAL(before, ruleset.rule_count, "%zu");
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

static int test_v2_canonical_hash_stability(void) {
    TEST_BEGIN();
    const char source[] = "allow(X) :- user(X), not(blocked(X)).\n";
    maelys_datalog_internal_ruleset_t first;
    maelys_datalog_internal_ruleset_t second;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_ruleset(&first), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, init_ruleset(&second), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_parse_ruleset(&first, source, strlen(source)),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_parse_ruleset(&second, source, strlen(source)),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_ruleset_finalize_sha256(&first), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_ruleset_finalize_sha256(&second), "%d");
    TEST_ASSERT_EQUAL_STRING(first.sha256, second.sha256);
    TEST_ASSERT_TRUE(strcmp(first.sha256,
                            "e745887b8f197efa9b91c29b15dcdccd436aa44f481e9ab17f9926c521be5834") != 0);
    maelys_datalog_ruleset_clear(&first);
    maelys_datalog_ruleset_clear(&second);
    TEST_END();
}

static int test_v2_ground_filter_literals(void) {
    TEST_BEGIN();
    maelys_datalog_internal_diagnostic_t diag = {0};
    TEST_ASSERT_EQUAL(
        MAELYS_OK,
        parse_source(
            "allow(X) :- starts_with(X, \"ali\"), user(X).\n"
            "allow(X) :- user(X), ends_with(X, \"ice\").\n"
            "allow(X) :- user(X), contains(X, \"lic\").\n",
            &diag),
        "%d");
    TEST_ASSERT_TRUE(
        parse_source("allow(X) :- starts_with(X, \"ali\").\n", &diag) !=
        MAELYS_OK);
    TEST_ASSERT_TRUE(
        parse_source("allow(X) :- user(X), starts_with(1, \"a\").\n", &diag) !=
        MAELYS_OK);
    TEST_ASSERT_TRUE(
        parse_source("allow(X) :- user(X), starts_with(X, X).\n", &diag) !=
        MAELYS_OK);
    TEST_ASSERT_TRUE(
        parse_source("starts_with(\"alice\", \"a\").\n", &diag) != MAELYS_OK);
    TEST_END();
}

static int test_v2_why_true_envelope_states(void) {
    TEST_BEGIN();
    maelys_datalog_internal_ruleset_t ruleset;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_ruleset(&ruleset), "%d");
    maelys_datalog_explanation_t *explanation = calloc(1u, sizeof(*explanation));
    TEST_ASSERT_NOT_NULL(explanation);
    char text[256];
    size_t required = 0u;

    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_format_explanation_text(
                          &ruleset, explanation, text, sizeof(text), &required),
                      "%d");
    TEST_ASSERT_EQUAL_STRING(
        "MAELYS-DATALOG-v2\ndocument=why-true\nstatus=not-derived\nsteps=0 premises=0\n",
        text);

    explanation->found = 1u;
    explanation->truncated = 1u;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_format_explanation_text(
                          &ruleset, explanation, text, sizeof(text), &required),
                      "%d");
    TEST_ASSERT_EQUAL_STRING(
        "MAELYS-DATALOG-v2\ndocument=why-true\nstatus=truncated\nsteps=0 premises=0\n",
        text);
    free(explanation);
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

static int test_v2_why_false_envelope_states(void) {
    TEST_BEGIN();
    maelys_datalog_internal_ruleset_t ruleset;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_ruleset(&ruleset), "%d");
    maelys_datalog_why_false_explanation_t *explanation = calloc(1u, sizeof(*explanation));
    TEST_ASSERT_NOT_NULL(explanation);
    TEST_ASSERT_EQUAL(1, maelys_datalog_predicate_registry_find(
        &ruleset.registry, "allow", 1, &explanation->query.predicate_id), "%d");
    explanation->query.arity = 1;
    explanation->query.terms[0].kind = MAELYS_DATALOG_TERM_INT;
    explanation->query.terms[0].as.integer = 7;
    const unsigned states[] = {MAELYS_DATALOG_WHY_FALSE_STATUS_COMPLETE,
                               MAELYS_DATALOG_WHY_FALSE_STATUS_TRUNCATED,
                               MAELYS_DATALOG_WHY_FALSE_STATUS_NOT_APPLICABLE};
    const char *const statuses[] = {"complete", "truncated", "not-applicable"};
    char text[512], expected[512];
    for (size_t i = 0; i < 3; ++i) {
        explanation->status = states[i];
        explanation->query_origin = i == 2 ? MAELYS_DATALOG_EXPLANATION_ORIGIN_IDB : 0;
        explanation->limit_hits = i == 1 ? MAELYS_DATALOG_WHY_FALSE_LIMIT_DIAGNOSTICS : 0;
        snprintf(expected, sizeof(expected),
                 "MAELYS-DATALOG-v2\ndocument=why-false\nstatus=%s\n"
                 "query=\"allow\"(7) origin=%s\nsummary=none\n"
                 "limit-hits=%s candidate-rules=0 substitutions=0 diagnostics=0 filter-cost=0\n",
                 statuses[i], i == 2 ? "idb" : "none", i == 1 ? "diagnostics" : "none");
        size_t required = 0;
        TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_format_why_false_text(
            &ruleset, explanation, NULL, 0, &required), "%d");
        TEST_ASSERT_EQUAL(strlen(expected), required, "%zu");
        /* Exact size excludes NUL: no partial document on undersized writes. */
        for (size_t capacity = 0; capacity <= required; ++capacity) {
            memset(text, 'X', sizeof(text));
            size_t reported = 0;
            TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE,
                maelys_datalog_format_why_false_text(
                    &ruleset, explanation, text, capacity, &reported), "%d");
            TEST_ASSERT_EQUAL(required, reported, "%zu");
            TEST_ASSERT_EQUAL(capacity ? 0 : 'X', text[0], "%d");
            for (size_t j = 1; j < sizeof(text); ++j)
                TEST_ASSERT_EQUAL('X', text[j], "%d");
        }
        TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_format_why_false_text(
            &ruleset, explanation, text, required + 1, &required), "%d");
        TEST_ASSERT_EQUAL_STRING(expected, text);
        TEST_ASSERT_EQUAL('X', text[required + 1], "%d");
    }
    free(explanation);
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

static int test_v2_shared_explanation_envelope(void) {
    TEST_BEGIN();
    maelys_datalog_internal_ruleset_t ruleset;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_ruleset(&ruleset), "%d");
    maelys_datalog_explanation_t *why_true = calloc(1u, sizeof(*why_true));
    maelys_datalog_why_false_explanation_t *why_false = calloc(1u, sizeof(*why_false));
    TEST_ASSERT_NOT_NULL(why_true);
    TEST_ASSERT_NOT_NULL(why_false);
    if (why_true == NULL || why_false == NULL) {
        free(why_false);
        free(why_true);
        maelys_datalog_ruleset_clear(&ruleset);
        return 1;
    }
    TEST_ASSERT_EQUAL(1, maelys_datalog_predicate_registry_find(
        &ruleset.registry, "allow", 1, &why_false->query.predicate_id), "%d");
    why_false->query.arity = 1u;
    why_false->query.terms[0].kind = MAELYS_DATALOG_TERM_INT;
    why_false->query.terms[0].as.integer = 7;
    why_false->status = MAELYS_DATALOG_WHY_FALSE_STATUS_COMPLETE;
    /* The zero-initialized Why-true fixture is valid "not-derived" output;
     * sharing an envelope does not require sharing the status vocabulary. */
    char true_text[512] = {0}, false_text[512] = {0};
    size_t required = 0u;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_format_explanation_text(
        &ruleset, why_true, true_text, sizeof(true_text), &required), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_format_why_false_text(
        &ruleset, why_false, false_text, sizeof(false_text), &required), "%d");

    /* Compare both real formatters: they must share one version line while
     * preserving distinct document discriminators, not identical payloads. */
    char *true_document = strchr(true_text, '\n');
    char *false_document = strchr(false_text, '\n');
    TEST_ASSERT_NOT_NULL(true_document);
    TEST_ASSERT_NOT_NULL(false_document);
    if (true_document != NULL && false_document != NULL) {
        *true_document++ = '\0';
        *false_document++ = '\0';
        TEST_ASSERT_EQUAL_STRING("MAELYS-DATALOG-v2", true_text);
        TEST_ASSERT_EQUAL_STRING(true_text, false_text);
        TEST_ASSERT_EQUAL(0, strncmp(true_document, "document=why-true\n",
                                   sizeof("document=why-true\n") - 1u), "%d");
        TEST_ASSERT_EQUAL(0, strncmp(false_document, "document=why-false\n",
                                   sizeof("document=why-false\n") - 1u), "%d");
    }
    free(why_false);
    free(why_true);
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

int main(int argc, char **argv) {
    test_case_t cases[] = {
        {"maelys_datalog_v2/profile_and_source", TEST_MODE_NON_BLOCKING, test_v2_profile_and_source_contract},
        {"maelys_datalog_v2/contextual_or_atomic", TEST_MODE_NON_BLOCKING, test_contextual_or_and_atomic_rejection},
        {"maelys_datalog_v2/canonical_hash", TEST_MODE_NON_BLOCKING, test_v2_canonical_hash_stability},
        {"maelys_datalog_v2/filter_ground_only", TEST_MODE_NON_BLOCKING, test_v2_ground_filter_literals},
        {"maelys_datalog_v2/why_true_envelope", TEST_MODE_NON_BLOCKING, test_v2_why_true_envelope_states},
        {"maelys_datalog_v2/why_false_envelope", TEST_MODE_NON_BLOCKING, test_v2_why_false_envelope_states},
        {"maelys_datalog_v2/shared_explanation_envelope", TEST_MODE_NON_BLOCKING, test_v2_shared_explanation_envelope},
    };
    return test_main("maelys_datalog_language_spec",
                     cases,
                     (int)(sizeof(cases) / sizeof(cases[0])),
                     argc,
                     argv);
}
