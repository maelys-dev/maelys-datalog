#include "src/core/maelys_datalog_edb.h"

#include "src/core/maelys_datalog_predicate_registry.h"
#include "src/core/maelys_datalog_symbol_table.h"

#include <stdlib.h>
#include <string.h>

static int term_cmp(const maelys_datalog_term_t *a, const maelys_datalog_term_t *b) {
    if (a->kind != b->kind) return (int)a->kind - (int)b->kind;
    switch (a->kind) {
        case MAELYS_DATALOG_TERM_SYMBOL:
            if (a->as.symbol < b->as.symbol) return -1;
            if (a->as.symbol > b->as.symbol) return 1;
            return 0;
        case MAELYS_DATALOG_TERM_INT:
            if (a->as.integer < b->as.integer) return -1;
            if (a->as.integer > b->as.integer) return 1;
            return 0;
        case MAELYS_DATALOG_TERM_BOOL: return a->as.boolean - b->as.boolean;
        case MAELYS_DATALOG_TERM_VAR:
            if (a->as.variable < b->as.variable) return -1;
            if (a->as.variable > b->as.variable) return 1;
            return 0;
        default: return 0;
    }
}

int maelys_datalog_term_equal(const maelys_datalog_term_t *a,
                              const maelys_datalog_term_t *b) {
    if (!a || !b || a->kind != b->kind) return 0;
    switch (a->kind) {
        case MAELYS_DATALOG_TERM_SYMBOL: return a->as.symbol == b->as.symbol;
        case MAELYS_DATALOG_TERM_INT: return a->as.integer == b->as.integer;
        case MAELYS_DATALOG_TERM_BOOL: return a->as.boolean == b->as.boolean;
        case MAELYS_DATALOG_TERM_VAR: return 0;
        default: return 0;
    }
}

int maelys_datalog_fact_cmp(const maelys_datalog_fact_t *a,
                            const maelys_datalog_fact_t *b) {
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    if (a->predicate_id < b->predicate_id) return -1;
    if (a->predicate_id > b->predicate_id) return 1;
    if (a->arity < b->arity) return -1;
    if (a->arity > b->arity) return 1;
    for (size_t i = 0; i < a->arity; i++) {
        int cmp = term_cmp(&a->terms[i], &b->terms[i]);
        if (cmp != 0) return cmp;
    }
    return 0;
}

static int qsort_fact_cmp(const void *lhs, const void *rhs) {
    return maelys_datalog_fact_cmp((const maelys_datalog_fact_t *)lhs,
                                   (const maelys_datalog_fact_t *)rhs);
}

void maelys_datalog_fact_set_init(maelys_datalog_fact_set_t *set,
                                  maelys_datalog_fact_t *facts,
                                  size_t capacity) {
    if (!set) return;
    memset(set, 0, sizeof(*set));
    set->facts = facts;
    set->capacity = capacity;
    set->sorted = 1;
}

maelys_result_t maelys_datalog_fact_set_sort(maelys_datalog_fact_set_t *set) {
    if (!set || (!set->facts && set->count > 0) || set->count > set->capacity) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    if (set->count > 1 && !set->sorted) {
        qsort(set->facts, set->count, sizeof(set->facts[0]), qsort_fact_cmp);
    }
    set->sorted = 1;
    return MAELYS_OK;
}

maelys_result_t maelys_datalog_fact_set_dedup(maelys_datalog_fact_set_t *set) {
    if (!set || (!set->facts && set->count > 0) || !set->sorted) return MAELYS_ERR_INVALID_ARGUMENT;
    if (set->count < 2) return MAELYS_OK;
    size_t out = 1;
    for (size_t i = 1; i < set->count; i++) {
        if (maelys_datalog_fact_cmp(&set->facts[out - 1], &set->facts[i]) == 0) continue;
        if (out != i) set->facts[out] = set->facts[i];
        out++;
    }
    set->count = out;
    return MAELYS_OK;
}

int maelys_datalog_fact_equals(const maelys_datalog_fact_t *a,
                               const maelys_datalog_fact_t *b) {
    if (!a || !b) return 0;
    return maelys_datalog_fact_cmp(a, b) == 0;
}

int maelys_datalog_fact_set_contains(const maelys_datalog_fact_set_t *set,
                                     const maelys_datalog_fact_t *fact) {
    if (!set || !fact || (!set->facts && set->count > 0)) return 0;
    if (!set->sorted) {
        for (size_t i = 0; i < set->count; i++) {
            if (maelys_datalog_fact_equals(&set->facts[i], fact)) return 1;
        }
        return 0;
    }
    size_t lo = 0;
    size_t hi = set->count;
    while (lo < hi) {
        size_t mid = lo + ((hi - lo) / 2u);
        int cmp = maelys_datalog_fact_cmp(&set->facts[mid], fact);
        if (cmp < 0) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return lo < set->count && maelys_datalog_fact_cmp(&set->facts[lo], fact) == 0;
}

maelys_result_t maelys_datalog_fact_set_predicate_range(const maelys_datalog_fact_set_t *set,
                                                        maelys_datalog_predicate_id_t predicate_id,
                                                        size_t *begin,
                                                        size_t *end) {
    if (!set || !begin || !end || (!set->facts && set->count > 0) || !set->sorted) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    size_t lo = 0;
    size_t hi = set->count;
    while (lo < hi) {
        size_t mid = lo + ((hi - lo) / 2u);
        if (set->facts[mid].predicate_id < predicate_id) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    *begin = lo;
    hi = set->count;
    while (lo < hi) {
        size_t mid = lo + ((hi - lo) / 2u);
        if (set->facts[mid].predicate_id <= predicate_id) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    *end = lo;
    return MAELYS_OK;
}

maelys_result_t maelys_datalog_fact_set_merge_delta(maelys_datalog_fact_set_t *current,
                                                    const maelys_datalog_fact_set_t *delta,
                                                    maelys_datalog_fact_t *merge_buffer,
                                                    size_t merge_capacity,
                                                    size_t *inserted_count) {
    if (!current || !delta || !merge_buffer || !inserted_count || !current->sorted || !delta->sorted ||
        (!current->facts && current->count > 0) || (!delta->facts && delta->count > 0)) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    size_t per_pred[MAELYS_DATALOG_MAX_PREDICATES];
    memset(per_pred, 0, sizeof(per_pred));
    size_t i = 0;
    size_t j = 0;
    size_t out = 0;
    size_t inserted = 0;
    while (i < current->count || j < delta->count) {
        const maelys_datalog_fact_t *src = NULL;
        int from_delta = 0;
        if (i >= current->count) {
            src = &delta->facts[j++];
            from_delta = 1;
        } else if (j >= delta->count) {
            src = &current->facts[i++];
        } else {
            int cmp = maelys_datalog_fact_cmp(&current->facts[i], &delta->facts[j]);
            if (cmp == 0) {
                src = &current->facts[i++];
                j++;
            } else if (cmp < 0) {
                src = &current->facts[i++];
            } else {
                src = &delta->facts[j++];
                from_delta = 1;
            }
        }
        if (src->predicate_id >= MAELYS_DATALOG_MAX_PREDICATES) return MAELYS_ERR_INVALID_FIELD;
        if (out >= merge_capacity || out >= MAELYS_DATALOG_MAX_IDB_FACTS) return MAELYS_ERR_PAYLOAD_TOO_LARGE;
        if (per_pred[src->predicate_id] >= MAELYS_DATALOG_MAX_FACTS_PER_PRED) return MAELYS_ERR_PAYLOAD_TOO_LARGE;
        merge_buffer[out++] = *src;
        per_pred[src->predicate_id]++;
        if (from_delta) inserted++;
    }
    memcpy(current->facts, merge_buffer, out * sizeof(current->facts[0]));
    current->count = out;
    current->sorted = 1;
    *inserted_count = inserted;
    return MAELYS_OK;
}

maelys_result_t maelys_datalog_edb_init(maelys_datalog_edb_t *edb,
                                        maelys_datalog_fact_t *fact_pool,
                                        size_t fact_capacity,
                                        maelys_datalog_symbol_table_t *symbols,
                                        const maelys_datalog_predicate_registry_t *registry) {
    if (!edb || !fact_pool || fact_capacity == 0 || !symbols || !registry) return MAELYS_ERR_INVALID_ARGUMENT;
    memset(edb, 0, sizeof(*edb));
    edb->facts = fact_pool;
    edb->fact_capacity = fact_capacity;
    maelys_datalog_fact_set_init(&edb->fact_set, fact_pool, fact_capacity);
    edb->symbols = symbols;
    edb->registry = registry;
    return MAELYS_OK;
}

void maelys_datalog_edb_clear(maelys_datalog_edb_t *edb) {
    if (!edb) return;
    edb->fact_count = 0;
    edb->fact_set.count = 0;
    edb->fact_set.sorted = 1;
    edb->immutable = 0;
    memset(edb->facts_per_pred, 0, sizeof(edb->facts_per_pred));
}

int maelys_datalog_edb_contains(const maelys_datalog_edb_t *edb,
                                const maelys_datalog_fact_t *fact) {
    if (!edb || !fact) return 0;
    return maelys_datalog_fact_set_contains(&edb->fact_set, fact);
}

maelys_result_t maelys_datalog_edb_add_fact(maelys_datalog_edb_t *edb,
                                            const char *predicate,
                                            const maelys_datalog_term_t *terms,
                                            size_t arity) {
    if (!edb || !predicate || (!terms && arity > 0) || arity > MAELYS_DATALOG_MAX_ARITY) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    if (edb->immutable) return MAELYS_ERR_INVALID_STATE;
    maelys_datalog_predicate_id_t pid;
    if (!maelys_datalog_predicate_registry_find(edb->registry, predicate, arity, &pid)) {
        return MAELYS_ERR_INVALID_FIELD;
    }
    const maelys_datalog_predicate_def_t *def =
        maelys_datalog_predicate_registry_get(edb->registry, pid);
    if (!def) return MAELYS_ERR_INVALID_FIELD;
    if (def->kind_flags & MAELYS_DATALOG_PRED_KIND_POLICY_FACT) {
        return MAELYS_ERR_FORBIDDEN;
    }
    if (!(def->kind_flags & MAELYS_DATALOG_PRED_KIND_EDB)) {
        return MAELYS_ERR_INVALID_FIELD;
    }
    if (edb->fact_count >= edb->fact_capacity || edb->fact_count >= MAELYS_DATALOG_MAX_EDB_FACTS) {
        return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    }
    if (edb->facts_per_pred[pid] >= MAELYS_DATALOG_MAX_FACTS_PER_PRED) return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    maelys_datalog_fact_t fact;
    memset(&fact, 0, sizeof(fact));
    fact.predicate_id = pid;
    fact.arity = (uint8_t)arity;
    for (size_t i = 0; i < arity; i++) fact.terms[i] = terms[i];
    if (maelys_datalog_edb_contains(edb, &fact)) return MAELYS_OK;
    edb->facts[edb->fact_count++] = fact;
    edb->fact_set.count = edb->fact_count;
    edb->fact_set.sorted = 0;
    edb->facts_per_pred[pid]++;
    return MAELYS_OK;
}

static maelys_result_t validate_unary_edb_symbol_target(maelys_datalog_edb_t *edb,
                                                        const char *predicate) {
    if (!edb || !predicate) return MAELYS_ERR_INVALID_ARGUMENT;
    if (edb->immutable) return MAELYS_ERR_INVALID_STATE;
    maelys_datalog_predicate_id_t pid;
    if (!maelys_datalog_predicate_registry_find(edb->registry, predicate, 1, &pid)) {
        return MAELYS_ERR_INVALID_FIELD;
    }
    const maelys_datalog_predicate_def_t *def =
        maelys_datalog_predicate_registry_get(edb->registry, pid);
    if (!def) return MAELYS_ERR_INVALID_FIELD;
    if (def->kind_flags & MAELYS_DATALOG_PRED_KIND_POLICY_FACT) return MAELYS_ERR_FORBIDDEN;
    if (!(def->kind_flags & MAELYS_DATALOG_PRED_KIND_EDB)) return MAELYS_ERR_INVALID_FIELD;
    if (edb->fact_count >= edb->fact_capacity || edb->fact_count >= MAELYS_DATALOG_MAX_EDB_FACTS) {
        return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    }
    if (edb->facts_per_pred[pid] >= MAELYS_DATALOG_MAX_FACTS_PER_PRED) {
        return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    }
    return MAELYS_OK;
}

maelys_result_t maelys_datalog_edb_add_runtime_symbol_fact(maelys_datalog_edb_t *edb,
                                                           const char *predicate,
                                                           const char *value) {
    if (!edb || !predicate || !value) return MAELYS_ERR_INVALID_ARGUMENT;
    maelys_result_t rc = validate_unary_edb_symbol_target(edb, predicate);
    if (rc != MAELYS_OK) return rc;
    maelys_datalog_symbol_id_t sid;
    rc = maelys_datalog_symbol_intern(edb->symbols, value, strlen(value), &sid);
    if (rc != MAELYS_OK) return rc;
    maelys_datalog_term_t term = {.kind = MAELYS_DATALOG_TERM_SYMBOL};
    term.as.symbol = sid;
    return maelys_datalog_edb_add_fact(edb, predicate, &term, 1);
}

maelys_result_t maelys_datalog_edb_add_atom_fact(maelys_datalog_edb_t *edb,
                                                 const char *predicate,
                                                 const char *atom) {
    if (!edb || !predicate || !atom) return MAELYS_ERR_INVALID_ARGUMENT;
    maelys_result_t rc = validate_unary_edb_symbol_target(edb, predicate);
    if (rc != MAELYS_OK) return rc;
    if (!maelys_datalog_predicate_registry_atom_allowed(edb->registry, atom)) {
        return MAELYS_ERR_FORBIDDEN;
    }
    maelys_datalog_symbol_id_t sid;
    rc = maelys_datalog_symbol_intern(edb->symbols, atom, strlen(atom), &sid);
    if (rc != MAELYS_OK) return rc;
    maelys_datalog_term_t term = {.kind = MAELYS_DATALOG_TERM_SYMBOL};
    term.as.symbol = sid;
    return maelys_datalog_edb_add_fact(edb, predicate, &term, 1);
}

static maelys_result_t recompute_edb_counts(maelys_datalog_edb_t *edb) {
    memset(edb->facts_per_pred, 0, sizeof(edb->facts_per_pred));
    for (size_t i = 0; i < edb->fact_set.count; i++) {
        maelys_datalog_predicate_id_t pid = edb->fact_set.facts[i].predicate_id;
        if (pid >= MAELYS_DATALOG_MAX_PREDICATES) return MAELYS_ERR_INVALID_FIELD;
        if (edb->facts_per_pred[pid] >= MAELYS_DATALOG_MAX_FACTS_PER_PRED) {
            return MAELYS_ERR_PAYLOAD_TOO_LARGE;
        }
        edb->facts_per_pred[pid]++;
    }
    return MAELYS_OK;
}

maelys_result_t maelys_datalog_edb_finalize(maelys_datalog_edb_t *edb) {
    if (!edb || !edb->facts) return MAELYS_ERR_INVALID_ARGUMENT;
    edb->fact_set.count = edb->fact_count;
    maelys_result_t rc = maelys_datalog_fact_set_sort(&edb->fact_set);
    if (rc != MAELYS_OK) return rc;
    rc = maelys_datalog_fact_set_dedup(&edb->fact_set);
    if (rc != MAELYS_OK) return rc;
    edb->fact_count = edb->fact_set.count;
    rc = recompute_edb_counts(edb);
    if (rc != MAELYS_OK) return rc;
    edb->immutable = 1;
    return MAELYS_OK;
}
