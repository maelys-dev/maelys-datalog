/* SPDX-License-Identifier: MPL-2.0 */
#include "tests/fixtures/allocation_guard.h"
#undef malloc
#undef calloc
#undef realloc
#undef free
#include "maelys/datalog.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int forbidden;
static size_t attempts, hot_frees, live, total, fault_after = SIZE_MAX;
static int refuse(void) {
    ++total;
    if (forbidden) { ++attempts; return 1; }
    if (fault_after == 0u) return 1;
    if (fault_after != SIZE_MAX) --fault_after;
    return 0;
}
void *maelys_test_malloc(size_t n) {
    if (refuse()) return NULL;
    void *p = malloc(n); if (p) ++live; return p;
}
void *maelys_test_calloc(size_t n, size_t width) {
    if (refuse()) return NULL;
    void *p = calloc(n, width); if (p) ++live; return p;
}
void *maelys_test_realloc(void *p, size_t n) {
    if (refuse()) return NULL;
    int had_pointer = p != NULL;
    void *q = realloc(p, n);
    if (q && !had_pointer) ++live;
    if (!q && had_pointer && !n) --live;
    return q;
}
void maelys_test_free(void *p) {
    if (forbidden && p) ++hot_frees;
    if (p) { assert(live); --live; }
    free(p);
}
static maelys_datalog_public_value_t symbol(const char *s) {
    maelys_datalog_public_value_t v = {.kind=MAELYS_DATALOG_VALUE_SYMBOL};
    v.as.symbol = s; return v;
}
int main(void) {
    const maelys_datalog_public_predicate_t predicates[] = {
        {"seed", 1, MAELYS_DATALOG_PREDICATE_EDB},
        {"blocked", 1, MAELYS_DATALOG_PREDICATE_EDB},
        {"allow", 1, MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY},
    };
    const maelys_datalog_public_domain_t domain = {"hot_path", predicates, 3, NULL, 0};
    assert(maelys_datalog_domain_register(&domain) == 0);
    const char *source = "allow(X) :- seed(X), not(blocked(X)).";
    maelys_datalog_public_diagnostic_t diag;
    maelys_datalog_policy_t *policy = NULL;
    assert(maelys_datalog_policy_load_inline("hot_path", "hot", source, strlen(source), &policy, &diag) == 0);
    maelys_datalog_session_t *session = NULL, *second = NULL, *filtered = NULL;
    size_t before = total, baseline = live;
    assert(maelys_datalog_session_create(policy, 0, &session) == 0);
    size_t create_allocations = total - before;
    assert(create_allocations > 0u);
    assert(maelys_datalog_session_free(session) == 0);
    assert(live == baseline);
    for (size_t fail = 0; fail < create_allocations; ++fail) {
        fault_after = fail;
        session = (void *)(uintptr_t)1;
        assert(maelys_datalog_session_create(policy, 0, &session) != 0 && session == NULL);
        assert(live == baseline);
    }
    fault_after = SIZE_MAX;
    assert(maelys_datalog_session_create(policy, 0, &session) == 0);
    assert(maelys_datalog_session_create(policy, 0, &second) == 0);
    maelys_datalog_policy_t *filter_policy = NULL;
    const char *filter_source = "allow(X) :- seed(X), starts_with(X, \"docs/\").";
    assert(maelys_datalog_policy_load_inline("hot_path", "filter", filter_source,
        strlen(filter_source), &filter_policy, &diag) == 0);
    assert(maelys_datalog_session_create(filter_policy, 0, &filtered) == 0);
    maelys_datalog_input_edb_t *edb = NULL;
    assert(maelys_datalog_input_edb_create_with_capacity(64u, 4096u, &edb) == 0);
    maelys_datalog_result_t *result = NULL, *other = NULL;
    char names[32][16];
    strcpy(names[0], "alpha"); strcpy(names[31], "zeta");
    for (size_t i = 1u; i < 31u; ++i)
        snprintf(names[i], sizeof(names[i]), "name-%02zu", i);
    forbidden = 1;
    for (size_t cycle = 0; cycle < 40u; ++cycle) {
        assert(maelys_datalog_input_edb_clear(edb) == 0);
        /* Sorting, duplicate text and multi-instance isolation are exercised. */
        /* More than the insertion cutoff: exercise the partitioning path in
         * both symbol and fact sorts, not just the tiny-array fast path. */
        maelys_datalog_public_fact_t facts[33] = {0};
        for (size_t i = 0; i < 32u; ++i) {
            facts[i].predicate = "seed"; facts[i].arity = 1;
            facts[i].terms[0] = symbol(names[cycle % 2u ? 31u-i : (i * 13u) % 32u]);
        }
        facts[32].predicate = "blocked"; facts[32].arity = 1;
        facts[32].terms[0] = symbol("zeta");
        assert(maelys_datalog_input_edb_add_facts(edb, facts, 33u, &diag) == 0);
        assert(maelys_datalog_session_solve_edb(session, edb, &result, &diag) == 0);
        assert(maelys_datalog_session_solve_edb(second, edb, &other, &diag) == 0);
        assert(maelys_datalog_session_free(session) == MAELYS_DATALOG_STATUS_INVALID_STATE);
        maelys_datalog_result_t *rejected = NULL;
        assert(maelys_datalog_session_solve_edb(session, edb, &rejected, &diag) == MAELYS_DATALOG_STATUS_INVALID_STATE && !rejected);
        assert(maelys_datalog_input_edb_clear(edb) == 0);
        maelys_datalog_public_value_t alpha = symbol("alpha"), zeta = symbol("zeta");
        int present = 0;
        assert(maelys_datalog_result_query(result, "allow", &alpha, 1u, &present) == 0 && present);
        assert(maelys_datalog_result_query(result, "allow", &zeta, 1u, &present) == 0 && !present);
        assert(maelys_datalog_result_free(result) == 0);
        assert(maelys_datalog_result_query(other, "allow", &alpha, 1u, &present) == 0 && present);
        assert(maelys_datalog_result_free(other) == 0);
        /* A rejected input must not poison the reserved result or symbols. */
        assert(maelys_datalog_input_edb_add_fact(edb, "unknown", &alpha, 1u, NULL) == 0);
        assert(maelys_datalog_session_solve_edb(session, edb, &result, &diag) != 0 && !result);
        assert(maelys_datalog_input_edb_clear(edb) == 0);
        assert(maelys_datalog_session_solve_edb(session, edb, &result, &diag) == 0);
        assert(maelys_datalog_result_free(result) == 0);
    }
    /* Fail inside the solver (not just during input validation), then reuse the
     * same native result workspace. No result is published on filter errors. */
    maelys_datalog_public_value_t integer = {.kind=MAELYS_DATALOG_VALUE_INTEGER, .as.integer=7};
    assert(maelys_datalog_input_edb_add_fact(edb, "seed", &integer, 1u, NULL) == 0);
    assert(maelys_datalog_session_solve_edb(filtered, edb, &result, &diag) != 0 && !result);
    assert(maelys_datalog_input_edb_clear(edb) == 0);
    maelys_datalog_public_value_t path = symbol("docs/api");
    assert(maelys_datalog_input_edb_add_fact(edb, "seed", &path, 1u, NULL) == 0);
    assert(maelys_datalog_session_solve_edb(filtered, edb, &result, &diag) == 0);
    int present = 0;
    assert(maelys_datalog_result_query(result, "allow", &path, 1u, &present) == 0 && present);
    assert(attempts == 0u && hot_frees == 0u);
    forbidden = 0;
    /* Explanations remain deliberately outside the no-allocation contract. */
    char explanation[8192]; size_t required;
    before = total;
    assert(maelys_datalog_result_explain_true_text(result, "allow", &path, 1u,
        explanation, sizeof(explanation), &required) == 0);
    assert(total > before);
    forbidden = 1;
    assert(maelys_datalog_result_free(result) == 0);
    assert(attempts == 0u && hot_frees == 0u);
    forbidden = 0;
    assert(maelys_datalog_input_edb_free(edb) == 0);
    assert(maelys_datalog_session_free(second) == 0);
    assert(maelys_datalog_session_free(session) == 0);
    assert(maelys_datalog_session_free(filtered) == 0);
    assert(maelys_datalog_policy_free(filter_policy) == 0);
    assert(maelys_datalog_policy_free(policy) == 0);
    printf("reference hot path: 40 repeated transactions, zero allocator calls; %zu constructor failure points checked\n", create_allocations);
    return 0;
}
