/* SPDX-License-Identifier: MPL-2.0 */
/* Same-generation: the recursive atom sits in the middle of the body, neither
 * of its variables reaches the head, and the two head variables come from two
 * different non-recursive atoms. Transitive closure, the only recursion the
 * other tests use, never reaches that join shape. The base rule also carries a
 * repeated head variable, which nothing else in the suite covers. */
#include <maelys/datalog.h>
#include "tests/helpers/test_framework.h"
#include <stdio.h>
#include <string.h>

#define REQUIRE(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
#define OK(c) REQUIRE((c) == MAELYS_DATALOG_STATUS_OK)

/* a is the root; b and c are its children; d,e belong to b and f,g to c. */
static const char *const k_parent[][2] = {
    {"d", "b"}, {"e", "b"}, {"f", "c"}, {"g", "c"}, {"b", "a"}, {"c", "a"}
};
/* The complete answer: the root alone, then each generation pairwise. */
static const char *const k_expected[][2] = {
    {"a","a"},
    {"b","b"}, {"b","c"}, {"c","b"}, {"c","c"},
    {"d","d"}, {"d","e"}, {"d","f"}, {"d","g"},
    {"e","d"}, {"e","e"}, {"e","f"}, {"e","g"},
    {"f","d"}, {"f","e"}, {"f","f"}, {"f","g"},
    {"g","d"}, {"g","e"}, {"g","f"}, {"g","g"}
};
#define EXPECTED_COUNT (sizeof(k_expected) / sizeof(k_expected[0]))
#define PERSON_COUNT 7u

static const char k_source[] =
    "person(X) :- parent(X, _).\n"
    "person(X) :- parent(_, X).\n"
    "same_generation(X, X) :- person(X).\n"
    "same_generation(X, Y) :- parent(X, P), same_generation(P, Q), parent(Y, Q).\n";

static maelys_datalog_public_value_t symbol(const char *s) {
    maelys_datalog_public_value_t v = {.kind = MAELYS_DATALOG_VALUE_SYMBOL};
    v.as.symbol = s;
    return v;
}

static int setup(void) {
    const maelys_datalog_public_predicate_t predicates[] = {
        {"parent", 2, MAELYS_DATALOG_PREDICATE_EDB},
        {"person", 1, MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY},
        {"same_generation", 2, MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY}
    };
    const maelys_datalog_public_domain_t domain = {
        "same_generation", predicates,
        sizeof(predicates) / sizeof(predicates[0]), NULL, 0u
    };
    OK(maelys_datalog_domain_register(&domain));
    return 0;
}

static int open_session(maelys_datalog_session_t **out) {
    maelys_datalog_public_diagnostic_t diagnostic;
    maelys_datalog_policy_t *policy = NULL;
    maelys_datalog_status_t rc = maelys_datalog_policy_load_inline(
        "same_generation", "same_generation.test",
        k_source, strlen(k_source), &policy, &diagnostic);
    if (rc != MAELYS_DATALOG_STATUS_OK) {
        fprintf(stderr, "load %d: %s\n", (int)rc, diagnostic.message);
        return 1;
    }
    OK(maelys_datalog_session_create(policy, 0, out));
    OK(maelys_datalog_policy_free(policy));
    return 0;
}

static int solve_parents(maelys_datalog_session_t *session,
                         maelys_datalog_result_t **out) {
    maelys_datalog_public_diagnostic_t diagnostic;
    maelys_datalog_public_fact_t facts[sizeof(k_parent) / sizeof(k_parent[0])];
    memset(facts, 0, sizeof(facts));
    for (size_t i = 0; i < sizeof(facts) / sizeof(facts[0]); i++) {
        facts[i].predicate = "parent";
        facts[i].arity = 2u;
        facts[i].terms[0] = symbol(k_parent[i][0]);
        facts[i].terms[1] = symbol(k_parent[i][1]);
    }
    maelys_datalog_status_t rc = maelys_datalog_session_solve(
        session, facts, sizeof(facts) / sizeof(facts[0]), out, &diagnostic);
    if (rc != MAELYS_DATALOG_STATUS_OK) {
        fprintf(stderr, "solve %d: %s\n", (int)rc, diagnostic.message);
        return 1;
    }
    return 0;
}

/* Every expected pair is derived, nothing beyond them is, and the totals of
 * both derived relations match. An incomplete fixpoint fails the first check,
 * an over-derivation the second. */
static int complete_and_exact(void) {
    maelys_datalog_session_t *session = NULL;
    REQUIRE(open_session(&session) == 0);

    /* Repeat the solve: the answer must not depend on reused session state. */
    for (int pass = 0; pass < 3; pass++) {
        maelys_datalog_result_t *result = NULL;
        REQUIRE(solve_parents(session, &result) == 0);

        for (size_t i = 0; i < EXPECTED_COUNT; i++) {
            maelys_datalog_public_value_t pair[2] = {
                symbol(k_expected[i][0]), symbol(k_expected[i][1])
            };
            int present = -1;
            OK(maelys_datalog_result_query(result, "same_generation", pair, 2u, &present));
            if (present != 1) {
                fprintf(stderr, "missing same_generation(%s,%s)\n",
                        k_expected[i][0], k_expected[i][1]);
                return 1;
            }
        }

        /* Cousins of different generations must never pair. */
        static const char *const k_absent[][2] = {
            {"a","b"}, {"b","a"}, {"d","b"}, {"b","d"}, {"a","d"}, {"d","a"}
        };
        for (size_t i = 0; i < sizeof(k_absent) / sizeof(k_absent[0]); i++) {
            maelys_datalog_public_value_t pair[2] = {
                symbol(k_absent[i][0]), symbol(k_absent[i][1])
            };
            int present = -1;
            OK(maelys_datalog_result_query(result, "same_generation", pair, 2u, &present));
            if (present != 0) {
                fprintf(stderr, "unexpected same_generation(%s,%s)\n",
                        k_absent[i][0], k_absent[i][1]);
                return 1;
            }
        }

        maelys_datalog_public_fact_view_t views[64];
        size_t pairs = 0;
        OK(maelys_datalog_result_enumerate(result, "same_generation", 2u,
                                           views, sizeof(views) / sizeof(views[0]), &pairs));
        REQUIRE(pairs == EXPECTED_COUNT);

        size_t people = 0;
        OK(maelys_datalog_result_enumerate(result, "person", 1u,
                                           views, sizeof(views) / sizeof(views[0]), &people));
        REQUIRE(people == PERSON_COUNT);

        size_t derived = 0;
        OK(maelys_datalog_result_derived_fact_count(result, &derived));
        REQUIRE(derived == EXPECTED_COUNT + PERSON_COUNT);

        OK(maelys_datalog_result_free(result));
    }
    OK(maelys_datalog_session_free(session));
    return 0;
}

int main(int argc, char **argv) {
    if (setup()) return 1;
    const test_case_t cases[] = {
        {"same_generation/complete_and_exact", TEST_MODE_NON_BLOCKING, complete_and_exact}
    };
    return test_main("same_generation", cases,
                     (int)(sizeof(cases) / sizeof(cases[0])), argc, argv);
}
