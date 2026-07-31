#include "tests/helpers/test_framework.h"

#include "src/core/maelys_datalog_explanation_format.h"
#include "src/core/maelys_datalog_parser.h"
#include "src/core/maelys_datalog_predicate_registry.h"
#include "src/core/maelys_datalog_ruleset.h"
#include "src/core/maelys_datalog_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int add_predicate(maelys_datalog_ruleset_t *ruleset,
                         const char *name,
                         uint8_t arity,
                         uint8_t kind) {
    return maelys_datalog_predicate_registry_add_domain(
        &ruleset->registry, name, arity, kind);
}

static int init_ruleset(maelys_datalog_ruleset_t *ruleset) {
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

static int parse_source(const char *source, maelys_datalog_diagnostic_t *diag) {
    maelys_datalog_ruleset_t ruleset;
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
    maelys_datalog_diagnostic_t diag = {0};
    TEST_ASSERT_EQUAL(MAELYS_OK, parse_source(source, &diag), "%d");

    TEST_ASSERT_TRUE(parse_source("allow(_) :- user(_).\n", &diag) != MAELYS_OK);
    TEST_ASSERT_TRUE(parse_source("allow(X) :- user(X), not blocked(X).\n", &diag) != MAELYS_OK);
    TEST_ASSERT_TRUE(parse_source("path(X,Y) :- edge(X,Y) OR owns(X,Y).\n", &diag) != MAELYS_OK);
    TEST_ASSERT_TRUE(parse_source("safe(\"line\\nbreak\").\n", &diag) != MAELYS_OK);
    TEST_END();
}

static int test_contextual_or_and_atomic_rejection(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
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
    maelys_datalog_ruleset_t first;
    maelys_datalog_ruleset_t second;
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

static int test_v2_why_true_envelope_states(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
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

int main(int argc, char **argv) {
    test_case_t cases[] = {
        {"maelys_datalog_v2/profile_and_source", TEST_MODE_NON_BLOCKING, test_v2_profile_and_source_contract},
        {"maelys_datalog_v2/contextual_or_atomic", TEST_MODE_NON_BLOCKING, test_contextual_or_and_atomic_rejection},
        {"maelys_datalog_v2/canonical_hash", TEST_MODE_NON_BLOCKING, test_v2_canonical_hash_stability},
        {"maelys_datalog_v2/why_true_envelope", TEST_MODE_NON_BLOCKING, test_v2_why_true_envelope_states},
    };
    return test_main("maelys_datalog_language_spec",
                     cases,
                     (int)(sizeof(cases) / sizeof(cases[0])),
                     argc,
                     argv);
}
