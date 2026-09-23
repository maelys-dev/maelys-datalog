/* SPDX-License-Identifier: MPL-2.0 */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "src/core/maelys_datalog_parser.h"
#include "src/core/maelys_datalog_edb.h"
#include "src/core/maelys_datalog_solver.h"
#include "src/core/maelys_datalog_solver_testing.h"

maelys_result_t maelys_datalog_test_solve_once_legacy_order(
    const maelys_datalog_internal_ruleset_t *, const maelys_datalog_internal_edb_t *,
    maelys_datalog_internal_solve_result_t **, maelys_datalog_internal_solve_diagnostic_t *);
maelys_result_t maelys_datalog_test_solve_once_full_scan(
    const maelys_datalog_internal_ruleset_t *, const maelys_datalog_internal_edb_t *,
    maelys_datalog_internal_solve_result_t **, maelys_datalog_internal_solve_diagnostic_t *);
maelys_result_t maelys_datalog_test_solve_result_idb_facts(
    const maelys_datalog_internal_solve_result_t *, const maelys_datalog_internal_fact_t **, size_t *);

typedef maelys_result_t (*solve_fn)(const maelys_datalog_internal_ruleset_t *,
    const maelys_datalog_internal_edb_t *, maelys_datalog_internal_solve_result_t **,
    maelys_datalog_internal_solve_diagnostic_t *);

static void init(maelys_datalog_internal_ruleset_t *r, const char *source) {
    memset(r, 0, sizeof(*r));
    assert(maelys_datalog_ruleset_init(r, "base-membership", "test", MAELYS_DATALOG_SHA256_UNSET, 0) == MAELYS_OK);
    const maelys_datalog_predicate_t defs[] = {
        {"e", 1, MAELYS_DATALOG_PRED_KIND_EDB},
        {"src", 1, MAELYS_DATALOG_PRED_KIND_EDB},
        {"pf", 1, MAELYS_DATALOG_PRED_KIND_POLICY_FACT},
        {"out", 1, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
    };
    for (size_t i = 0; i < sizeof(defs)/sizeof(defs[0]); ++i)
        assert(maelys_datalog_predicate_registry_add_domain(&r->registry, defs[i].name, defs[i].arity, defs[i].flags) == MAELYS_OK);
    assert(maelys_datalog_predicate_registry_freeze(&r->registry) == MAELYS_OK);
    assert(maelys_datalog_parse_ruleset(r, source, strlen(source)) == MAELYS_OK);
    assert(r->program_validated);
}

static maelys_datalog_predicate_id_t pid(const maelys_datalog_internal_ruleset_t *r, const char *name) {
    maelys_datalog_predicate_id_t id = 0;
    assert(maelys_datalog_predicate_registry_find(&r->registry, name, 1, &id));
    return id;
}

static void add(maelys_datalog_internal_edb_t *e, const char *name, int value) {
    const maelys_datalog_internal_term_t t = {.kind=MAELYS_DATALOG_TERM_INT, .as.integer=value};
    assert(maelys_datalog_edb_add_fact(e, name, &t, 1) == MAELYS_OK);
}

static size_t count(const maelys_datalog_internal_solve_result_t *r) {
    const maelys_datalog_internal_fact_t *facts = NULL;
    size_t n = 0;
    assert(maelys_datalog_test_solve_result_idb_facts(r, &facts, &n) == MAELYS_OK);
    return n;
}

static void equivalent(const maelys_datalog_internal_solve_result_t *a,
                        const maelys_datalog_internal_solve_result_t *b) {
    const maelys_datalog_internal_fact_t *fa = NULL, *fb = NULL;
    size_t na = 0, nb = 0;
    assert(maelys_datalog_test_solve_result_idb_facts(a, &fa, &na) == MAELYS_OK);
    assert(maelys_datalog_test_solve_result_idb_facts(b, &fb, &nb) == MAELYS_OK);
    assert(na == nb && memcmp(fa, fb, na * sizeof(*fa)) == 0);
    assert(memcmp(maelys_datalog_solve_result_proof(a), maelys_datalog_solve_result_proof(b),
                  sizeof(maelys_datalog_proof_tree_t)) == 0);
    for (size_t i = 0; i < na; ++i) {
        maelys_datalog_explanation_t ea, eb;
        assert(maelys_datalog_explain_solved_fact(a, &fa[i], &ea) == MAELYS_OK);
        assert(maelys_datalog_explain_solved_fact(b, &fb[i], &eb) == MAELYS_OK);
        assert(memcmp(&ea, &eb, sizeof(ea)) == 0);
    }
}

static void validated(solve_fn solve, const char *source, size_t expected) {
    maelys_datalog_internal_ruleset_t r;
    init(&r, source);
    maelys_datalog_internal_fact_t pool[8];
    maelys_datalog_internal_edb_t edb;
    assert(maelys_datalog_edb_init(&edb, pool, 8, &r.symbols, &r.registry) == MAELYS_OK);
    for (int i = 0; i < 4; ++i) add(&edb, "src", i);
    assert(maelys_datalog_edb_finalize(&edb) == MAELYS_OK);
    maelys_datalog_internal_solve_result_t *a = NULL, *b = NULL;
    maelys_datalog_base_lookup_counts = (maelys_datalog_base_lookup_counts_t){0};
    assert(solve(&r, &edb, &a, NULL) == MAELYS_OK);
    assert(count(a) == expected);
    assert(maelys_datalog_base_lookup_counts.rule_checks > 0);
    assert(maelys_datalog_base_lookup_counts.validated_audits > 0);
    assert(maelys_datalog_base_lookup_counts.validated_hits == 0);
    assert(maelys_datalog_base_lookup_counts.lookups == 0);
    assert(maelys_datalog_base_lookup_counts.skips == maelys_datalog_base_lookup_counts.audits);
    assert(maelys_datalog_test_solve_once_full_scan(&r, &edb, &b, NULL) == MAELYS_OK);
    equivalent(a, b);
    maelys_datalog_solve_result_free(a);
    maelys_datalog_solve_result_free(b);
    maelys_datalog_ruleset_clear(&r);
}

/* Fill the derived predicate, then encounter a head already in the base.
 * Suppression must still precede capacity failure, on both base sources and
 * both derivation traversals. Removing the base makes that same head overflow. */
static void legacy_capacity(solve_fn solve, int policy_base) {
    maelys_datalog_internal_ruleset_t r;
    char source[160];
    snprintf(source, sizeof(source), "pf(%u). out(X) :- src(X). out(%u) :- src(0).",
             MAELYS_DATALOG_MAX_FACTS_PER_PRED, MAELYS_DATALOG_MAX_FACTS_PER_PRED);
    init(&r, source);
    maelys_datalog_predicate_id_t head = pid(&r, policy_base ? "pf" : "e");
    r.rules[0].head.predicate_id = r.rules[1].head.predicate_id = head;
    r.program_validated = 0;
    for (int present = 1; present >= 0; --present) {
        maelys_datalog_internal_fact_t pool[MAELYS_DATALOG_MAX_FACTS_PER_PRED + 1u];
        maelys_datalog_internal_edb_t edb;
        assert(maelys_datalog_edb_init(&edb, pool, MAELYS_DATALOG_MAX_FACTS_PER_PRED + 1u,
                                      &r.symbols, &r.registry) == MAELYS_OK);
        for (unsigned i = 0; i < MAELYS_DATALOG_MAX_FACTS_PER_PRED; ++i) add(&edb, "src", (int)i);
        if (!policy_base && present) add(&edb, "e", MAELYS_DATALOG_MAX_FACTS_PER_PRED);
        if (!present) r.fact_count = 0;
        assert(maelys_datalog_edb_finalize(&edb) == MAELYS_OK);
        maelys_datalog_base_lookup_counts = (maelys_datalog_base_lookup_counts_t){0};
        maelys_datalog_internal_solve_result_t *result = NULL;
        maelys_datalog_internal_solve_diagnostic_t diag;
        maelys_result_t rc = solve(&r, &edb, &result, &diag);
        if (present) {
            assert(rc == MAELYS_OK && count(result) == MAELYS_DATALOG_MAX_FACTS_PER_PRED);
            assert(maelys_datalog_base_lookup_counts.hits == 1);
            assert(maelys_datalog_base_lookup_counts.audit_hits == 1);
            maelys_datalog_solve_result_free(result);
        } else {
            assert(rc == MAELYS_ERR_PAYLOAD_TOO_LARGE && result == NULL);
            assert(diag.category == MAELYS_DATALOG_SOLVE_DIAG_IDB_OVERFLOW);
            assert(diag.count_observed == MAELYS_DATALOG_MAX_FACTS_PER_PRED + 1u);
            assert(diag.capacity == MAELYS_DATALOG_MAX_FACTS_PER_PRED);
        }
    }
    maelys_datalog_ruleset_clear(&r);
}

/* Low-level arrays can even put an IDB fact in either base, independently of
 * registry flags and with a stale validated bit. Guard actual contents. */
static void manually_populated_idb(solve_fn solve, int policy_base) {
    maelys_datalog_internal_ruleset_t r;
    init(&r, "pf(1). out(X) :- src(X).");
    maelys_datalog_internal_fact_t pool[3];
    maelys_datalog_internal_edb_t edb;
    assert(maelys_datalog_edb_init(&edb, pool, 3, &r.symbols, &r.registry) == MAELYS_OK);
    add(&edb, "src", 1); add(&edb, "src", 2);
    if (policy_base) r.facts[0].predicate_id = pid(&r, "out");
    else {
        /* Finalize the hand-built array through its fact-set contract. */
        pool[edb.fact_count++] = r.facts[0];
        pool[edb.fact_count - 1u].predicate_id = pid(&r, "out");
        edb.fact_set.count = edb.fact_count;
    }
    assert(maelys_datalog_edb_finalize(&edb) == MAELYS_OK);
    maelys_datalog_base_lookup_counts = (maelys_datalog_base_lookup_counts_t){0};
    maelys_datalog_internal_solve_result_t *result = NULL;
    assert(solve(&r, &edb, &result, NULL) == MAELYS_OK);
    assert(count(result) == 1);
    assert(maelys_datalog_base_lookup_counts.hits == 1);
    assert(maelys_datalog_base_lookup_counts.validated_hits == 1);
    maelys_datalog_solve_result_free(result);
    maelys_datalog_ruleset_clear(&r);
}

int main(void) {
    const solve_fn traversals[] = {maelys_datalog_solve_once_ex, maelys_datalog_test_solve_once_legacy_order};
    for (size_t i = 0; i < 2; ++i) {
        validated(traversals[i], "pf(3). out(X) :- src(X). out(X) :- out(X).", 4);
        for (int policy = 0; policy <= 1; ++policy) {
            legacy_capacity(traversals[i], policy);
            manually_populated_idb(traversals[i], policy);
        }
    }
    validated(maelys_datalog_solve_once_ex, "pf(3). out(X) :- src(X), not(pf(X)).", 3);
    puts("base membership: skips, legacy hits, precedence and canonical proofs PASS");
    return 0;
}
