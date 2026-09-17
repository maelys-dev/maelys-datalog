/* SPDX-License-Identifier: MPL-2.0 */
#include <maelys/datalog.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const maelys_datalog_public_predicate_t declarations[] = {
    MAELYS_DATALOG_EDB("seed", 1),
    MAELYS_DATALOG_IDB("hidden", 1),
    MAELYS_DATALOG_IDB_QUERY("allow", 1),
    MAELYS_DATALOG_EDB_QUERY("observed", 1),
    MAELYS_DATALOG_POLICY_FACT("fixed", 1),
    MAELYS_DATALOG_POLICY_FACT_QUERY("trusted", 1),
};
static unsigned name_calls, arity_calls;
static const char *name_once(void) { ++name_calls; return "dynamic"; }
static size_t arity_once(void) { ++arity_calls; return 2; }

int main(void) {
    assert(strcmp(declarations[0].name, "seed") == 0);
    assert(declarations[0].arity == 1);
    assert(declarations[0].flags == MAELYS_DATALOG_PREDICATE_EDB);
    assert(declarations[1].flags == MAELYS_DATALOG_PREDICATE_IDB);
    assert(declarations[2].flags ==
           (MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY));
    assert(declarations[3].flags ==
           (MAELYS_DATALOG_PREDICATE_EDB | MAELYS_DATALOG_PREDICATE_QUERY));
    assert(declarations[4].flags == MAELYS_DATALOG_PREDICATE_POLICY_FACT);
    assert(declarations[5].flags ==
           (MAELYS_DATALOG_PREDICATE_POLICY_FACT | MAELYS_DATALOG_PREDICATE_QUERY));
    const maelys_datalog_public_predicate_t dynamic[] = {
        MAELYS_DATALOG_EDB(name_once(), arity_once()),
        MAELYS_DATALOG_IDB(name_once(), arity_once()),
        MAELYS_DATALOG_IDB_QUERY(name_once(), arity_once()),
        MAELYS_DATALOG_EDB_QUERY(name_once(), arity_once()),
        MAELYS_DATALOG_POLICY_FACT(name_once(), arity_once()),
        MAELYS_DATALOG_POLICY_FACT_QUERY(name_once(), arity_once()),
    };
    assert(name_calls == 6 && arity_calls == 6);
    for (size_t i = 0; i < 6; ++i) {
        assert(strcmp(dynamic[i].name, "dynamic") == 0 && dynamic[i].arity == 2);
    }
    const char *const atoms[] = {"alice"};
    const maelys_datalog_public_domain_t domain = {
        "predicate_builders", declarations, 6, atoms, 1,
    };
    assert(maelys_datalog_domain_register(&domain) == MAELYS_DATALOG_STATUS_OK);
    maelys_datalog_policy_t *policy = NULL;
    maelys_datalog_session_t *session = NULL;
    maelys_datalog_result_t *result = NULL;
    const char *source = "fixed(\"alice\"). trusted(\"alice\"). "
                         "hidden(X) :- seed(X). allow(X) :- hidden(X), fixed(X).";
    assert(!maelys_datalog_policy_load_inline(domain.name, "builders", source,
                                              strlen(source), &policy, NULL));
    assert(!maelys_datalog_session_create(policy, 0, &session));
    maelys_datalog_public_fact_t facts[2] = {0};
    facts[0].predicate = "seed";
    facts[1].predicate = "observed";
    for (size_t i = 0; i < 2; ++i) {
        facts[i].arity = 1;
        facts[i].terms[0] = (maelys_datalog_public_value_t)MAELYS_DATALOG_SYMBOL("alice");
    }
    assert(!maelys_datalog_session_solve(session, facts, 2, &result, NULL));
    for (size_t i = 0; i < 6; ++i) {
        int present = 71;
        maelys_datalog_status_t rc = maelys_datalog_result_query(
            result, declarations[i].name, facts[0].terms, 1, &present);
        if (declarations[i].flags & MAELYS_DATALOG_PREDICATE_QUERY) {
            assert(rc == MAELYS_DATALOG_STATUS_OK && present == 1);
        } else {
            assert(rc != MAELYS_DATALOG_STATUS_OK && present == 71);
        }
    }
    assert(!maelys_datalog_result_free(result));
    /* QUERY does not authorize injecting a policy fact through request inputs. */
    facts[0].predicate = "trusted";
    result = NULL;
    assert(maelys_datalog_session_solve(session, facts, 1, &result, NULL) !=
           MAELYS_DATALOG_STATUS_OK);
    assert(result == NULL);
    assert(!maelys_datalog_session_free(session));
    assert(!maelys_datalog_policy_free(policy));
    /* Initializers do not silently repair or validate declarations. */
    const maelys_datalog_public_predicate_t invalid[] = {
        MAELYS_DATALOG_EDB("bad", MAELYS_DATALOG_PUBLIC_MAX_TERMS + 1),
    };
    const maelys_datalog_public_domain_t bad_domain = {
        "predicate_builders_invalid", invalid, 1, NULL, 0,
    };
    assert(maelys_datalog_domain_register(&bad_domain) == MAELYS_DATALOG_STATUS_INVALID_FIELD);
    const maelys_datalog_public_predicate_t query_only = {
        "query_only", 1, MAELYS_DATALOG_PREDICATE_QUERY,
    };
    const maelys_datalog_public_domain_t invalid_origin = {
        "predicate_query_only", &query_only, 1, NULL, 0,
    };
    /* The existing facade stores flags at registration; policy loading validates
     * the predicate registry. Builders must not change that error boundary. */
    assert(!maelys_datalog_domain_register(&invalid_origin));
    policy = NULL;
    const char *invalid_source = "query_only(X) :- query_only(X).";
    assert(maelys_datalog_policy_load_inline(invalid_origin.name, "invalid", invalid_source,
        strlen(invalid_source), &policy, NULL) != MAELYS_DATALOG_STATUS_OK);
    assert(policy == NULL);
    puts("predicate builders: exact flags, static/runtime initializers, single evaluation, registration validation PASS");
    return 0;
}
