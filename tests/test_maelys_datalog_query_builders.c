/* SPDX-License-Identifier: MPL-2.0 */
#include <maelys/datalog.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const maelys_datalog_value_t literal[] = {
    MAELYS_DATALOG_SYMBOL("alice"),
};
static unsigned result_calls, output_calls, predicate_calls, term_calls;
static const maelys_datalog_result_t *result_once(const maelys_datalog_result_t *r) {
    ++result_calls; return r;
}
static int *output_once(int *p) { ++output_calls; return p; }
static const char *predicate_once(void) { ++predicate_calls; return "four"; }
static int term_once(void) { ++term_calls; return 42; }
static const char *symbol_once(void) { ++term_calls; return "alice"; }

int main(void) {
    const maelys_datalog_predicate_t predicates[] = {
        MAELYS_DATALOG_EDB("zero_in", 0), MAELYS_DATALOG_IDB_QUERY("zero", 0),
        MAELYS_DATALOG_EDB("one_in", 1), MAELYS_DATALOG_IDB_QUERY("one", 1),
        MAELYS_DATALOG_EDB("two_in", 2), MAELYS_DATALOG_IDB_QUERY("two", 2),
        MAELYS_DATALOG_EDB("three_in", 3), MAELYS_DATALOG_IDB_QUERY("three", 3),
        MAELYS_DATALOG_EDB("four_in", 4), MAELYS_DATALOG_IDB_QUERY("four", 4),
    };
    const maelys_datalog_domain_t domain = {
        "query_builders", predicates, sizeof(predicates)/sizeof(predicates[0]), NULL, 0,
    };
    const char source[] =
        "zero() :- zero_in().\n"
        "one(A) :- one_in(A).\n"
        "two(A,B) :- two_in(A,B).\n"
        "three(A,B,C) :- three_in(A,B,C).\n"
        "four(A,B,C,D) :- four_in(A,B,C,D).\n";
    maelys_datalog_policy_t *policy = NULL;
    maelys_datalog_session_t *session = NULL;
    maelys_datalog_result_t *result = NULL;
    maelys_datalog_input_edb_t *edb = NULL;
    assert(maelys_datalog_domain_register(&domain) == MAELYS_DATALOG_STATUS_OK);
    assert(maelys_datalog_policy_load_inline(domain.name, "query", source,
        strlen(source), &policy, NULL) == MAELYS_DATALOG_STATUS_OK);
    assert(maelys_datalog_session_create(policy, 0, &session) == MAELYS_DATALOG_STATUS_OK);
    assert(maelys_datalog_input_edb_create(&edb) == MAELYS_DATALOG_STATUS_OK);
    assert(MAELYS_DATALOG_ADD_FACT(edb, NULL, "zero_in") == MAELYS_DATALOG_STATUS_OK);
    assert(MAELYS_DATALOG_ADD_FACT(edb, NULL, "one_in", "alice") == MAELYS_DATALOG_STATUS_OK);
    assert(MAELYS_DATALOG_ADD_FACT(edb, NULL, "two_in", INT64_MIN, INT64_MAX) == MAELYS_DATALOG_STATUS_OK);
    assert(MAELYS_DATALOG_ADD_FACT(edb, NULL, "three_in", "alice", 42u, MAELYS_DATALOG_BOOL(1)) == MAELYS_DATALOG_STATUS_OK);
    assert(MAELYS_DATALOG_ADD_FACT(edb, NULL, "four_in", 42, 42, 42, 42) == MAELYS_DATALOG_STATUS_OK);
    assert(maelys_datalog_session_solve_edb(session, edb, &result, NULL) == MAELYS_DATALOG_STATUS_OK);

    int present = -7;
    assert(MAELYS_DATALOG_QUERY(result, &present, "zero") == MAELYS_DATALOG_STATUS_OK && present == 1);
    assert(MAELYS_DATALOG_QUERY(result, &present, "one", "alice") == MAELYS_DATALOG_STATUS_OK && present == 1);
    assert(MAELYS_DATALOG_QUERY(result, &present, "two", INT64_MIN, (uint64_t)INT64_MAX) == MAELYS_DATALOG_STATUS_OK && present == 1);
    assert(MAELYS_DATALOG_QUERY(result, &present, "three", "alice", 42, MAELYS_DATALOG_BOOL(1)) == MAELYS_DATALOG_STATUS_OK && present == 1);
    assert(MAELYS_DATALOG_QUERY(result_once(result), output_once(&present), predicate_once(),
        term_once(), term_once(), term_once(), term_once()) == MAELYS_DATALOG_STATUS_OK && present == 1);
    assert(result_calls == 1 && output_calls == 1 && predicate_calls == 1 && term_calls == 4);
    assert(MAELYS_DATALOG_QUERY(result, &present, "three", "alice", 42, 1) == MAELYS_DATALOG_STATUS_OK && present == 0);
    assert(MAELYS_DATALOG_QUERY(result, &present, "one", "unknown") == MAELYS_DATALOG_STATUS_OK && present == 0);
    assert(MAELYS_DATALOG_QUERY(result, &present, "four", 42, 42, 42, 43) == MAELYS_DATALOG_STATUS_OK && present == 0);

    /* Errors must not be confused with a successful negative answer. */
    present = -7;
    assert(MAELYS_DATALOG_QUERY(result, &present, "two", UINT64_MAX, 0) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT && present == -7);
    assert(MAELYS_DATALOG_QUERY(result, &present, "one", (const char *)NULL) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT && present == -7);
    assert(MAELYS_DATALOG_QUERY(NULL, &present, "zero") == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT && present == -7);
    assert(MAELYS_DATALOG_QUERY(result, &present, NULL, 1) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT && present == -7);
    assert(MAELYS_DATALOG_QUERY(result, NULL, "one", "alice") == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    int typed_present = -7;
    maelys_datalog_status_t typed = maelys_datalog_result_query(result, "missing", literal, 1, &typed_present);
    assert(typed != MAELYS_DATALOG_STATUS_OK);
    assert(MAELYS_DATALOG_QUERY(result, &present, "missing", "alice") == typed && present == typed_present);
    typed = maelys_datalog_result_query(result, "two", literal, 1, &typed_present);
    assert(typed != MAELYS_DATALOG_STATUS_OK);
    assert(MAELYS_DATALOG_QUERY(result, &present, "two", "alice") == typed && present == typed_present);

    const maelys_datalog_value_t dynamic[] = {MAELYS_DATALOG_SYMBOL(symbol_once())};
    assert(term_calls == 5 && dynamic[0].kind == MAELYS_DATALOG_VALUE_SYMBOL);
    assert(strcmp(dynamic[0].as.symbol, literal[0].as.symbol) == 0);
    assert(maelys_datalog_result_query(result, "one", dynamic, 1, &typed_present) == MAELYS_DATALOG_STATUS_OK && typed_present == 1);
    assert(maelys_datalog_result_free(result) == MAELYS_DATALOG_STATUS_OK);
    assert(maelys_datalog_input_edb_free(edb) == MAELYS_DATALOG_STATUS_OK);
    assert(maelys_datalog_session_free(session) == MAELYS_DATALOG_STATUS_OK);
    assert(maelys_datalog_policy_free(policy) == MAELYS_DATALOG_STATUS_OK);
    puts("query builders: arities 0-4, typed parity, checked ranges, errors vs false, single evaluation and symbol initializers PASS");
    return 0;
}
