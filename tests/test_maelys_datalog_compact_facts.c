/* SPDX-License-Identifier: MPL-2.0 */
#include "src/core/maelys_datalog_types.h"
#include "src/core/maelys_datalog_edb.h"
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

/* Representation changes must not narrow values or leak stale payload bytes.
 * Compare logical tuples separately from their canonical storage image. */
static void canonical(const maelys_datalog_internal_fact_t *fact) {
    assert((uintptr_t)fact->payload % _Alignof(int64_t) == 0);
    assert(fact->reserved == 0);
    for (size_t i = fact->arity; i < MAELYS_DATALOG_MAX_TERMS; ++i) {
        const maelys_datalog_fact_payload_t zero = {0};
        assert(fact->kind[i] == 0);
        assert(!memcmp(&fact->payload[i], &zero, sizeof(zero)));
    }
}
static void roundtrip(void) {
    const int64_t integers[] = {INT64_MIN, INT64_MIN+1, -1, 0, 1,
        INT32_MAX, (int64_t)INT32_MAX+1, INT64_MAX-1, INT64_MAX};
    for (size_t arity = 0; arity <= MAELYS_DATALOG_MAX_TERMS; ++arity)
    for (size_t n = 0; n < sizeof(integers)/sizeof(*integers); ++n) {
        maelys_datalog_internal_atom_t atom = {0};
        atom.predicate_id = 17; atom.arity = (uint8_t)arity;
        for (size_t t = 0; t < arity; ++t) {
            atom.terms[t].kind = MAELYS_DATALOG_TERM_INT;
            atom.terms[t].as.integer = integers[n];
        }
        for (size_t t = arity; t < MAELYS_DATALOG_MAX_TERMS; ++t)
            memset(&atom.terms[t], 0xa5, sizeof(atom.terms[t]));
        maelys_datalog_internal_fact_t fact = maelys_datalog_atom_fact(&atom);
        canonical(&fact);
        maelys_datalog_internal_atom_t restored = maelys_datalog_fact_atom(&fact);
        assert(restored.predicate_id == atom.predicate_id);
        assert(restored.arity == arity);
        for (size_t t = 0; t < MAELYS_DATALOG_MAX_TERMS; ++t) {
            /* The compiled term has padding; compare its defined fields only. */
            assert(restored.terms[t].kind == (t < arity ? MAELYS_DATALOG_TERM_INT : 0));
            assert(restored.terms[t].as.integer == (t < arity ? integers[n] : 0));
        }
        for (size_t t = 0; t < arity; ++t) {
            assert(maelys_datalog_fact_term(&fact, t).as.integer == integers[n]);
            assert(maelys_datalog_fact_kind(&fact, t) == MAELYS_DATALOG_TERM_INT);
            assert(maelys_datalog_fact_integer(&fact, t) == integers[n]);
            maelys_datalog_internal_term_t expanded;
            memset(&expanded, 0xa5, sizeof(expanded));
            maelys_datalog_fact_copy_term(&expanded, &fact, t);
            unsigned char expected_bytes[sizeof(expanded)] = {0};
            maelys_datalog_internal_term_kind_t kind = MAELYS_DATALOG_TERM_INT;
            memcpy(expected_bytes + offsetof(maelys_datalog_internal_term_t, kind), &kind, sizeof(kind));
            memcpy(expected_bytes + offsetof(maelys_datalog_internal_term_t, as), &integers[n], sizeof(integers[n]));
            assert(!memcmp(&expanded, expected_bytes, sizeof(expanded)));
        }
    }
}
static void reused_payload(void) {
    maelys_datalog_internal_fact_t actual = {0}, expected = {0};
    actual.arity = expected.arity = 4;
    for (size_t t = 0; t < 4; ++t) {
        maelys_datalog_internal_term_t value = {.kind=MAELYS_DATALOG_TERM_INT};
        value.as.integer = INT64_MIN;
        maelys_datalog_fact_set_term(&actual, t, value);
        value.kind = (maelys_datalog_internal_term_kind_t)(t+1);
        memset(&value.as, 0, sizeof(value.as));
        switch (value.kind) {
        case MAELYS_DATALOG_TERM_SYMBOL: value.as.symbol = UINT32_MAX; break;
        case MAELYS_DATALOG_TERM_INT: value.as.integer = INT64_MAX; break;
        case MAELYS_DATALOG_TERM_BOOL: value.as.boolean = 1; break;
        case MAELYS_DATALOG_TERM_VAR: value.as.variable = 31; break;
        default: assert(0);
        }
        maelys_datalog_fact_set_term(&actual, t, value);
        maelys_datalog_fact_set_term(&expected, t, value);
    }
    assert(!memcmp(&actual, &expected, sizeof(actual)));
    assert(maelys_datalog_fact_term(&actual, 0).as.symbol == UINT32_MAX);
    assert(maelys_datalog_fact_symbol(&actual, 0) == UINT32_MAX);
    assert(maelys_datalog_fact_boolean(&actual, 2) == 1);
    assert(maelys_datalog_fact_variable(&actual, 3) == 31);
    assert(maelys_datalog_fact_term(&actual, 2).as.boolean == 1);
    assert(maelys_datalog_fact_term(&actual, 3).as.variable == 31);
    maelys_datalog_fact_set_integer(&actual, 0, INT64_MIN);
    maelys_datalog_fact_set_kind(&actual, 0, MAELYS_DATALOG_TERM_SYMBOL);
    maelys_datalog_fact_set_symbol(&actual, 0, UINT32_MAX);
    assert(!memcmp(&actual, &expected, sizeof(actual)));
    /* Clear all positions before reusing the record at a smaller arity. */
    maelys_datalog_internal_atom_t narrow = {0}; narrow.arity=1;
    narrow.terms[0]=(maelys_datalog_internal_term_t){.kind=MAELYS_DATALOG_TERM_INT,.as.integer=-1};
    actual=maelys_datalog_atom_fact(&narrow);canonical(&actual);
}
static void accessor_arguments_once(void) {
    maelys_datalog_internal_fact_t fact[1] = {{0}};
    fact[0].arity = 1;
    maelys_datalog_fact_set_kind(&fact[0], 0, MAELYS_DATALOG_TERM_INT);
    maelys_datalog_fact_set_integer(&fact[0], 0, INT64_MIN);
    size_t f = 0, t = 0;
    assert(maelys_datalog_fact_integer(&fact[f++], t++) == INT64_MIN);
    assert(f == 1 && t == 1);
    f = t = 0;
    assert(maelys_datalog_fact_kind(&fact[f++], t++) == MAELYS_DATALOG_TERM_INT);
    assert(f == 1 && t == 1);
}
static void aggregate_metadata(void) {
    maelys_datalog_explanation_premise_t p = {0};
    p.kind=MAELYS_DATALOG_EXPLANATION_PREMISE_COUNT;
    p.as.count.pattern.predicate_id=3;p.as.count.pattern.arity=4;
    p.as.count.projected_variable=25;p.as.count.value=INT32_MAX;
    for(size_t t=0;t<4;++t) {
        maelys_datalog_internal_term_t term={.kind=MAELYS_DATALOG_TERM_VAR,.as.variable=(unsigned)t};
        maelys_datalog_fact_set_term(&p.as.count.pattern,t,term);
    }
    assert(p.as.count.value==INT32_MAX && p.as.count.projected_variable==25);
    for(size_t t=0;t<4;++t) assert(maelys_datalog_fact_term(&p.as.count.pattern,t).as.variable==t);
    canonical(&p.as.count.pattern);
}
int main(void) {
    assert(sizeof(maelys_datalog_rule_t)==2016);
    assert(sizeof(maelys_datalog_literal_t)==144);
    roundtrip();reused_payload();aggregate_metadata();accessor_arguments_once();
    return 0;
}
