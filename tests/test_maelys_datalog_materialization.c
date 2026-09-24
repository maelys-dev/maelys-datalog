/* SPDX-License-Identifier: MPL-2.0 */
#include "src/core/maelys_datalog_prepared_session_internal.h"
#include "src/core/maelys_datalog_parser.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OK(call) assert((call) == MAELYS_OK)
static maelys_datalog_internal_ruleset_t rules;
static char names[32][12];
static maelys_datalog_internal_fact_t pools[2][MAELYS_DATALOG_MAX_EDB_FACTS];
static maelys_datalog_internal_edb_t edbs[2];
static maelys_datalog_edb_insert_index_t index_state;

static void reset(void) {
    memset(pools, 0, sizeof(pools));
    memset(&index_state, 0, sizeof(index_state));
    for (unsigned i = 0; i < 2; ++i)
        OK(maelys_datalog_edb_init(&edbs[i], pools[i], MAELYS_DATALOG_MAX_EDB_FACTS,
                                  &rules.symbols, &rules.registry));
}

static maelys_result_t insert(const char *name, const maelys_datalog_internal_term_t *terms, size_t arity) {
    maelys_datalog_edb_insert_index_t before = index_state;
    maelys_result_t a = maelys_datalog_edb_add_fact(&edbs[0], name, terms, arity);
    maelys_result_t b = maelys_datalog_edb_add_fact_indexed(&edbs[1], name, terms, arity, &index_state);
    assert(a == b);
    assert(edbs[0].fact_count == edbs[1].fact_count);
    assert(!memcmp(edbs[0].facts_per_pred, edbs[1].facts_per_pred, sizeof(edbs[0].facts_per_pred)));
    assert(!memcmp(pools[0], pools[1], sizeof(pools[0])));
    if (b != MAELYS_OK) assert(!memcmp(&before, &index_state, sizeof(before)));
    return b;
}

static void native_parity(void) {
    const size_t cap = MAELYS_DATALOG_MAX_FACTS_PER_PRED;
    for (unsigned order = 0; order < 4; ++order) {
        reset();
        for (size_t i = 0; i < MAELYS_DATALOG_MAX_EDB_FACTS; ++i) {
            size_t v = order == 0 ? i : order == 1 ? MAELYS_DATALOG_MAX_EDB_FACTS - 1 - i :
                       order == 2 ? (i * 13u) % MAELYS_DATALOG_MAX_EDB_FACTS : 0;
            maelys_datalog_internal_term_t t = {.kind = MAELYS_DATALOG_TERM_INT, .as.integer = (long long)(v % cap)};
            OK(insert(names[v / cap], &t, 1));
        }
        maelys_datalog_internal_term_t t = {.kind = MAELYS_DATALOG_TERM_INT, .as.integer = 0};
        assert(insert(names[0], &t, 1) == (order == 3 ? MAELYS_OK : MAELYS_ERR_PAYLOAD_TOO_LARGE));
        assert(insert("missing", &t, 1) == MAELYS_ERR_INVALID_FIELD);
        assert(insert("out", &t, 1) == MAELYS_ERR_INVALID_FIELD);
        assert(insert("fixed", &t, 1) == MAELYS_ERR_FORBIDDEN);
        for (unsigned j = 0; j < 2; ++j) OK(maelys_datalog_edb_finalize(&edbs[j]));
        assert(!memcmp(pools[0], pools[1], sizeof(pools[0])));
    }
    for (unsigned indexed = 0; indexed < 2; ++indexed) {
        reset();
        const size_t prefix = indexed ? MAELYS_DATALOG_EDB_INSERT_SCAN_LIMIT : 0;
        for (size_t i = 0; i < prefix; ++i) {
            maelys_datalog_internal_term_t t = {.kind=MAELYS_DATALOG_TERM_INT, .as.integer=(long long)i};
            OK(insert(names[1], &t, 1));
        }
        /* Same numeric payload in different kinds, unused bytes intentionally dirty. */
        const maelys_datalog_internal_term_kind_t kinds[] = {MAELYS_DATALOG_TERM_SYMBOL, MAELYS_DATALOG_TERM_INT,
                                                   MAELYS_DATALOG_TERM_BOOL, MAELYS_DATALOG_TERM_VAR};
        for (unsigned k = 0; k < 4; ++k) {
            maelys_datalog_internal_term_t t;
            memset(&t, 0xa5, sizeof(t));
            t.kind = kinds[k];
            if (k == 0) t.as.symbol = 1;
            if (k == 1) t.as.integer = 1;
            if (k == 2) t.as.boolean = 1;
            if (k == 3) t.as.variable = 1;
            OK(insert(names[0], &t, 1));
            memset(&t, 0, sizeof(t)); t.kind = kinds[k];
            if (k == 0) t.as.symbol = 1;
            if (k == 1) t.as.integer = 1;
            if (k == 2) t.as.boolean = 1;
            if (k == 3) t.as.variable = 1;
            OK(insert(names[0], &t, 1));
            assert(edbs[1].fact_count == prefix + k + 1u);
        }
        maelys_datalog_internal_term_t pair[2] = {{.kind=MAELYS_DATALOG_TERM_INT, .as.integer=-1},
                                        {.kind=MAELYS_DATALOG_TERM_INT, .as.integer=1}};
        OK(insert("pair", pair, 2));
        pair[1].as.integer = -1; OK(insert("pair", pair, 2));
        assert(edbs[1].fact_count == prefix + 6);
        assert(insert("pair", pair, 1) == MAELYS_ERR_INVALID_FIELD);
    }
}

static void activation_boundary(void) {
    reset();
    const maelys_datalog_edb_insert_index_t empty = {0};
    const size_t limit = MAELYS_DATALOG_EDB_INSERT_SCAN_LIMIT;
    for (size_t i = 0; i < limit; ++i) {
        maelys_datalog_internal_term_t t = {.kind=MAELYS_DATALOG_TERM_INT, .as.integer=(long long)i};
        OK(insert(names[0], &t, 1));
        assert(!memcmp(&empty, &index_state, sizeof(empty)));
        t.as.integer = 0; /* Includes nonconsecutive duplicates at 31 and 32. */
        OK(insert(names[0], &t, 1));
        assert(edbs[1].fact_count == i + 1u);
        assert(!memcmp(&empty, &index_state, sizeof(empty)));
    }
    maelys_datalog_internal_term_t t = {.kind=MAELYS_DATALOG_TERM_INT, .as.integer=(long long)limit};
    assert(insert("missing", &t, 1) == MAELYS_ERR_INVALID_FIELD);
    for (unsigned i = 0; i < 2; ++i) edbs[i].fact_capacity = limit;
    assert(insert(names[0], &t, 1) == MAELYS_ERR_PAYLOAD_TOO_LARGE);
    assert(!memcmp(&empty, &index_state, sizeof(empty)));
    for (unsigned i = 0; i < 2; ++i) edbs[i].fact_capacity = MAELYS_DATALOG_MAX_EDB_FACTS;
    OK(insert(names[0], &t, 1));
    size_t occupied = 0;
    for (size_t i = 0; i < MAELYS_DATALOG_EDB_INSERT_SLOTS; ++i)
        occupied += index_state.slots[i] != 0;
    assert(occupied == limit + 1u);
    for (size_t i = 0; i <= limit; ++i) {
        t.as.integer = (long long)i;
        OK(insert(names[0], &t, 1));
        assert(edbs[1].fact_count == limit + 1u); /* Backfill finds every old fact. */
    }
    /* A fresh construction on reused storage must start in scan mode again. */
    reset();
    OK(insert(names[0], &t, 1));
    assert(!memcmp(&empty, &index_state, sizeof(empty)));
}

static void collisions(void) {
    reset();
    maelys_datalog_predicate_id_t pid;
    assert(maelys_datalog_predicate_registry_find(&rules.registry, names[0], 1, &pid));
    maelys_datalog_internal_fact_t fact = {.predicate_id=pid, .arity=1};
    fact.terms[0].kind = MAELYS_DATALOG_TERM_INT;
    size_t found = 0;
    long long first = -1;
    for (long long v = 0; found < 40; ++v) {
        assert(v < 1000000);
        fact.terms[0].as.integer = v;
        if (maelys_datalog_test_edb_insert_bucket(&fact) != MAELYS_DATALOG_EDB_INSERT_SLOTS - 1u) continue;
        if (!found) first = v;
        OK(insert(names[0], fact.terms, 1));
        OK(insert(names[0], fact.terms, 1));
        ++found;
        assert(edbs[1].fact_count == found);
    }
    /* This deliberately wraps a chain across the end of the table. */
    assert(index_state.slots[0] && index_state.slots[38]);
    /* A nonconsecutive duplicate must traverse the index, not the last fact. */
    fact.terms[0].as.integer = first;
    OK(insert(names[0], fact.terms, 1));
    assert(edbs[1].fact_count == found);
}

static void transaction_rollback(void) {
    maelys_datalog_internal_prepared_session_t *session;
    OK(maelys_datalog_prepared_session_create(&rules, &session));
    OK(maelys_datalog_prepared_session_materialize_inputs(session, NULL, 0));
    /* A rejection resets to an empty mutable transaction, not a finalized one. */
    maelys_datalog_edb_clear(&session->edb);
    maelys_datalog_internal_prepared_session_t *snapshot = malloc(sizeof(*snapshot));
    assert(snapshot); memcpy(snapshot, session, sizeof(*snapshot));
    static maelys_datalog_fact_t facts[MAELYS_DATALOG_MAX_EDB_FACTS + 1];
    const size_t cap = MAELYS_DATALOG_MAX_FACTS_PER_PRED;
    for (size_t i = 0; i <= cap; ++i) {
        facts[i].predicate = names[0]; facts[i].arity = 1;
        facts[i].terms[0].kind = MAELYS_DATALOG_VALUE_INTEGER;
        facts[i].terms[0].as.integer = (long long)i;
    }
    char message[256], expected[64];
    snprintf(expected, sizeof(expected), "index %zu", cap);
    for (unsigned duplicate = 0; duplicate < 2; ++duplicate) {
        facts[cap].terms[0].as.integer = duplicate ? 0 : (long long)cap;
        assert(maelys_datalog_prepared_session_materialize_inputs_diagnosed(session, facts, cap + 1,
                   message, sizeof(message)) == MAELYS_ERR_PAYLOAD_TOO_LARGE);
        assert(strstr(message, expected) && strstr(message, "per-predicate"));
        assert(!memcmp(snapshot, session, sizeof(*snapshot)));
    }
    /* A duplicate below the bound consumes no distinct-fact capacity. */
    facts[cap - 1].terms[0].as.integer = 0;
    facts[cap].terms[0].as.integer = (long long)cap;
    OK(maelys_datalog_prepared_session_materialize_inputs(session, facts, cap + 1));
    assert(session->edb.fact_count == cap);
    facts[cap].predicate = "missing";
    assert(maelys_datalog_prepared_session_materialize_inputs_diagnosed(session, facts, cap + 1,
               message, sizeof(message)) == MAELYS_ERR_INVALID_FIELD);
    assert(strstr(message, expected) && strstr(message, "unknown predicate"));
    assert(!memcmp(snapshot, session, sizeof(*snapshot)));
    facts[0].predicate = names[0]; facts[0].terms[0].kind = MAELYS_DATALOG_VALUE_SYMBOL;
    facts[0].terms[0].as.symbol = "orphan-after-rejection";
    facts[1].predicate = "missing";
    assert(maelys_datalog_prepared_session_materialize_inputs(session, facts, 2) == MAELYS_ERR_INVALID_FIELD);
    assert(!memcmp(snapshot, session, sizeof(*snapshot)));
    facts[1].predicate = names[1]; facts[1].terms[0].kind = MAELYS_DATALOG_VALUE_SYMBOL;
    facts[1].terms[0].as.symbol = "new-valid-symbol";
    OK(maelys_datalog_prepared_session_materialize_inputs(session, facts, 2));
    maelys_datalog_internal_solve_result_t *result;
    OK(maelys_datalog_prepared_session_solve_materialized_ex(session, &result, NULL, NULL));
    maelys_datalog_solve_result_free(result);
    free(snapshot);
    OK(maelys_datalog_prepared_session_destroy(session));
}

int main(void) {
    OK(maelys_datalog_ruleset_init(&rules, "index", "test",
       "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", 1));
    for (unsigned i = 0; i < 32; ++i) {
        snprintf(names[i], sizeof(names[i]), "p%u", i);
        OK(maelys_datalog_predicate_registry_add_domain(&rules.registry, names[i], 1, MAELYS_DATALOG_PRED_KIND_EDB));
    }
    OK(maelys_datalog_predicate_registry_add_domain(&rules.registry, "pair", 2, MAELYS_DATALOG_PRED_KIND_EDB));
    OK(maelys_datalog_predicate_registry_add_domain(&rules.registry, "out", 1, MAELYS_DATALOG_PRED_KIND_IDB));
    OK(maelys_datalog_predicate_registry_add_domain(&rules.registry, "fixed", 1, MAELYS_DATALOG_PRED_KIND_POLICY_FACT));
    OK(maelys_datalog_predicate_registry_freeze(&rules.registry));
    const char *source = "out(X) :- p0(X), p1(X).";
    OK(maelys_datalog_parse_ruleset(&rules, source, strlen(source)));
    native_parity(); activation_boundary(); collisions(); transaction_rollback();
    printf("materialization: legacy parity, capacity precedence, collisions, byte-exact rollback; session=%zu index=%zu bytes\n",
           sizeof(maelys_datalog_internal_prepared_session_t), sizeof(index_state));
    return 0;
}
