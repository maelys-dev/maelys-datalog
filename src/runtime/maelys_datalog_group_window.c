/* SPDX-License-Identifier: MPL-2.0 */
/* The adapter consumes only the installed public facade. */
#include <maelys/datalog_group_window.h>
#include "src/runtime/maelys_datalog_transaction_internal.h"
#include <stdio.h>
#include <string.h>

struct maelys_datalog_group_window {
    maelys_datalog_session_t *sessions[2];
    maelys_datalog_input_edb_t *inputs[2];
    maelys_datalog_event_group_t *groups[2];
    maelys_datalog_fact_t *facts[2];
    maelys_datalog_result_t *result;
    maelys_datalog_group_window_capacities_t capacities;
    size_t group_count, unique_count;
    uint64_t next;
    unsigned active;
    int busy;
};

typedef struct {
    size_t bytes, alignment, start, stride, input, groups, facts, input_bytes;
} group_layout;

static maelys_datalog_status_t group_error(maelys_datalog_diagnostic_t *d,
    maelys_datalog_status_t rc, const char *message) {
    if (d) {
        { maelys_datalog_status_t ds = maelys_datalog_diagnostic_clear(d); if (ds) return ds; }
        d->source = MAELYS_DATALOG_DIAGNOSTIC_SOLVE;
        d->status = rc;
        d->code = MAELYS_DATALOG_DIAG_OPERATION_REJECTED;
        snprintf(d->phase, sizeof(d->phase), "group_window");
        snprintf(d->message, sizeof(d->message), "%s", message);
        snprintf(d->hint, sizeof(d->hint), "No group transaction was published; the group cursor did not advance.");
    }
    return rc;
}

static int reserve_region(size_t *end, size_t count, size_t item, size_t alignment,
    size_t *offset) {
    if (*end > SIZE_MAX - (alignment - 1u)) return 0;
    size_t start = (*end + alignment - 1u) / alignment * alignment;
    if (count > (SIZE_MAX - start) / item) return 0;
    *offset = start;
    *end = start + count * item;
    return 1;
}

static maelys_datalog_status_t group_storage_layout(
    const maelys_datalog_group_window_capacities_t *c, group_layout *l) {
    if (!c || !c->groups || (uint64_t)c->groups > (uint64_t)INT32_MAX + 1u ||
        !c->unique_facts || c->unique_facts > c->contributions)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    size_t alignment, input_bytes;
    maelys_datalog_status_t rc = maelys_datalog_input_edb_storage_requirements(
        c->contributions, c->text_bytes, &input_bytes, &alignment);
    if (rc) return rc;
    if (alignment < _Alignof(max_align_t)) alignment = _Alignof(max_align_t);
    group_layout v = {.alignment = alignment, .input_bytes = input_bytes};
    size_t end = 0, ignored;
    if (!reserve_region(&end, 1, input_bytes, alignment, &v.input) ||
        !reserve_region(&end, c->groups, sizeof(maelys_datalog_event_group_t), alignment, &v.groups) ||
        !reserve_region(&end, c->contributions, sizeof(maelys_datalog_fact_t), alignment, &v.facts) ||
        !reserve_region(&end, 0, 1, alignment, &ignored))
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    v.stride = end;
    end = sizeof(maelys_datalog_group_window_t);
    if (!reserve_region(&end, 2, v.stride, alignment, &v.start))
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    v.bytes = end;
    *l = v;
    return MAELYS_DATALOG_STATUS_OK;
}

maelys_datalog_status_t maelys_datalog_group_window_storage_requirements(
    const maelys_datalog_group_window_capacities_t *c, size_t *bytes, size_t *alignment) {
    if (!bytes || !alignment) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    group_layout l;
    maelys_datalog_status_t rc = group_storage_layout(c, &l);
    if (!rc) { *bytes = l.bytes; *alignment = l.alignment; }
    return rc;
}

maelys_datalog_status_t maelys_datalog_group_window_init(void *storage, size_t storage_bytes,
    const maelys_datalog_group_window_capacities_t *c, uint32_t first,
    maelys_datalog_session_t *a, maelys_datalog_session_t *b,
    maelys_datalog_group_window_t **out, maelys_datalog_diagnostic_t *diag) {
    if (out) *out = NULL;
    { maelys_datalog_status_t ds = maelys_datalog_diagnostic_clear(diag); if (ds) return ds; }
    if (!out || !storage || !a || !b || a == b || first > INT32_MAX)
        return group_error(diag, MAELYS_DATALOG_STATUS_INVALID_ARGUMENT, "Two distinct sessions, storage and a nonnegative int32 group ID are required.");
    group_layout l;
    maelys_datalog_status_t rc = group_storage_layout(c, &l);
    if (rc) return group_error(diag, rc, "Invalid group window capacities.");
    if ((uintptr_t)storage % l.alignment || storage_bytes > UINTPTR_MAX - (uintptr_t)storage)
        return group_error(diag, MAELYS_DATALOG_STATUS_INVALID_ARGUMENT, "Window storage is misaligned or its range overflows.");
    if (storage_bytes < l.bytes)
        return group_error(diag, MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL, "Window storage is smaller than its queried requirement.");
    char fa[MAELYS_DATALOG_PUBLIC_FINGERPRINT_BYTES], fb[MAELYS_DATALOG_PUBLIC_FINGERPRINT_BYTES];
    rc = maelys_datalog_session_execution_fingerprint(a, fa);
    if (!rc) rc = maelys_datalog_session_execution_fingerprint(b, fb);
    if (rc) return rc;
    if (strcmp(fa, fb))
        return group_error(diag, MAELYS_DATALOG_STATUS_INVALID_ARGUMENT, "Window sessions have different execution fingerprints.");
    maelys_datalog_group_window_t *w = storage;
    *w = (maelys_datalog_group_window_t){.sessions = {a, b}, .capacities = *c, .next = first};
    for (unsigned i = 0; i < 2; ++i) {
        unsigned char *bank = (unsigned char *)storage + l.start + i * l.stride;
        w->groups[i] = (void *)(bank + l.groups);
        w->facts[i] = (void *)(bank + l.facts);
        rc = maelys_datalog_input_edb_init(bank + l.input, l.input_bytes,
            c->contributions, c->text_bytes, &w->inputs[i]);
        if (rc) return rc;
    }
    maelys_datalog_result_t *probe = NULL;
    rc = maelys_datalog_session_solve_candidate(b, NULL, 0, &probe, diag);
    if (rc) return rc;
    rc = maelys_datalog_result_free(probe);
    if (rc) return rc;
    rc = maelys_datalog_session_solve_candidate(a, NULL, 0, &w->result, diag);
    if (rc) return rc;
    maelys_datalog_result_commit(w->result);
    *out = w;
    return MAELYS_DATALOG_STATUS_OK;
}

/* All strings/values have already been validated and copied by input_edb.
 * Ignore padding/inactive terms; nonzero booleans have already become one. */
static int fact_order(const maelys_datalog_fact_t *a,
    const maelys_datalog_fact_t *b) {
    int d = strcmp(a->predicate, b->predicate);
    if (d) return d;
    if (a->arity != b->arity) return a->arity < b->arity ? -1 : 1;
    for (size_t i = 0; i < a->arity; ++i) {
        const maelys_datalog_value_t *x = &a->terms[i], *y = &b->terms[i];
        if (x->kind != y->kind) return x->kind < y->kind ? -1 : 1;
        if (x->kind == MAELYS_DATALOG_VALUE_SYMBOL) d = strcmp(x->as.symbol, y->as.symbol);
        else if (x->kind == MAELYS_DATALOG_VALUE_INTEGER)
            d = (x->as.integer > y->as.integer) - (x->as.integer < y->as.integer);
        else d = (x->as.boolean > y->as.boolean) - (x->as.boolean < y->as.boolean);
        if (d) return d;
    }
    return 0;
}
static void swap_fact(maelys_datalog_fact_t *a, maelys_datalog_fact_t *b) {
    maelys_datalog_fact_t tmp = *a; *a = *b; *b = tmp;
}
static void sift(maelys_datalog_fact_t *a, size_t n, size_t root) {
    while (root < n / 2u) {
        size_t child = 2u * root + 1u;
        if (child + 1u < n && fact_order(&a[child], &a[child + 1u]) < 0) ++child;
        if (fact_order(&a[root], &a[child]) >= 0) break;
        swap_fact(&a[root], &a[child]); root = child;
    }
}
/* In-place heapsort: O(C log C) comparisons, constant stack, no libc qsort.
 * Text comparisons are bounded by the SDK's per-string length limit. */
static size_t make_union(maelys_datalog_fact_t *a, size_t n) {
    for (size_t i = n / 2u; i; --i) sift(a, n, i - 1u);
    for (size_t i = n; i > 1u; --i) { swap_fact(a, a + i - 1u); sift(a, i - 1u, 0); }
    size_t unique = 0;
    for (size_t i = 0; i < n; ++i)
        if (!unique || fact_order(&a[unique - 1u], &a[i])) a[unique++] = a[i];
    return unique;
}

maelys_datalog_status_t maelys_datalog_group_window_push(maelys_datalog_group_window_t *w,
    const maelys_datalog_fact_t *facts, size_t count, uint32_t *id,
    maelys_datalog_diagnostic_t *diag) {
    { maelys_datalog_status_t ds = maelys_datalog_diagnostic_clear(diag); if (ds) return ds; }
    if (!w || (!facts && count))
        return group_error(diag, MAELYS_DATALOG_STATUS_INVALID_ARGUMENT, "A nonempty group needs a fact array.");
    if (!w->result) return group_error(diag, MAELYS_DATALOG_STATUS_INVALID_STATE, "Window is closed.");
    if (w->busy) return group_error(diag, MAELYS_DATALOG_STATUS_INVALID_STATE, "Window operation reentry is forbidden.");
    if (w->next > INT32_MAX)
        return group_error(diag, MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE, "The window group ID space is exhausted.");
    unsigned active = w->active, candidate = 1u - active;
    size_t skip_group = w->group_count == w->capacities.groups ? 1u : 0u;
    size_t skip_fact = skip_group ? w->groups[active][0].fact_count : 0;
    const maelys_datalog_fact_t *old = NULL;
    size_t old_count = 0;
    w->busy = 1;
    maelys_datalog_status_t rc = maelys_datalog_input_edb_view(w->inputs[active], &old, &old_count);
    size_t retained = old_count - skip_fact;
    if (!rc && count > w->capacities.contributions - retained)
        rc = group_error(diag, MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE, "Retained raw contributions exceed the window contribution capacity.");
    if (!rc) rc = maelys_datalog_input_edb_clear(w->inputs[candidate]);
    if (!rc) rc = maelys_datalog_input_edb_add_facts(w->inputs[candidate], old + skip_fact, retained, diag);
    if (!rc) rc = maelys_datalog_input_edb_add_facts(w->inputs[candidate], facts, count, diag);
    const maelys_datalog_fact_t *raw = NULL;
    size_t total = 0, unique = 0;
    if (!rc) rc = maelys_datalog_input_edb_view(w->inputs[candidate], &raw, &total);
    if (!rc) {
        memcpy(w->facts[candidate], raw, total * sizeof(*raw));
        unique = make_union(w->facts[candidate], total);
        if (unique > w->capacities.unique_facts)
            rc = group_error(diag, MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE, "Runtime union exceeds the window unique-fact capacity.");
    }
    size_t groups = w->group_count - skip_group;
    if (!rc) {
        for (size_t i = 0; i < groups; ++i) {
            w->groups[candidate][i] = w->groups[active][i + skip_group];
            w->groups[candidate][i].fact_offset -= skip_fact;
        }
        w->groups[candidate][groups] = (maelys_datalog_event_group_t){(uint32_t)w->next, retained, count};
    }
    maelys_datalog_result_t *result = NULL;
    if (!rc) rc = maelys_datalog_session_solve_candidate(w->sessions[candidate], w->facts[candidate], unique, &result, diag);
    if (!rc) {
        rc = maelys_datalog_result_free(w->result);
        if (rc) {
            (void)maelys_datalog_result_free(result);
            group_error(diag, rc, "Current window result could not be released; candidate discarded.");
        } else {
            /* After releasing the old lease, only infallible publication remains. */
            w->result = result;
            w->active = candidate;
            w->group_count = groups + 1u;
            w->unique_count = unique;
            if (id) *id = (uint32_t)w->next;
            ++w->next;
            maelys_datalog_result_commit(result);
        }
    }
    w->busy = 0;
    if (rc && diag && (diag->source == MAELYS_DATALOG_DIAGNOSTIC_NONE || !diag->code))
        group_error(diag, rc, "Candidate group could not be committed.");
    return rc;
}

static maelys_datalog_status_t readable(const maelys_datalog_group_window_t *w) {
    if (!w) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    return !w->result || w->busy ? MAELYS_DATALOG_STATUS_INVALID_STATE : MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t maelys_datalog_group_window_state(const maelys_datalog_group_window_t *w,
    maelys_datalog_group_window_usage_t *out) {
    if (!out) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    maelys_datalog_status_t rc = readable(w);
    if (rc) return rc;
    maelys_datalog_group_window_usage_t usage = {0};
    size_t capacity;
    rc = maelys_datalog_input_edb_count(w->inputs[w->active], &usage.contributions);
    if (!rc) rc = maelys_datalog_input_edb_text_usage(w->inputs[w->active], &usage.text_bytes, &capacity);
    if (rc) return rc;
    usage.groups = w->group_count; usage.unique_facts = w->unique_count; usage.next_group = w->next;
    *out = usage;
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t maelys_datalog_group_window_groups(const maelys_datalog_group_window_t *w,
    const maelys_datalog_event_group_t **out, size_t *count) {
    if (!out || !count) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    maelys_datalog_status_t rc = readable(w);
    if (rc) return rc;
    *out = w->groups[w->active]; *count = w->group_count;
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t maelys_datalog_group_window_contributions(const maelys_datalog_group_window_t *w,
    const maelys_datalog_fact_t **out, size_t *count) {
    if (!out || !count) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    maelys_datalog_status_t rc = readable(w);
    return rc ? rc : maelys_datalog_input_edb_view(w->inputs[w->active], out, count);
}
maelys_datalog_status_t maelys_datalog_group_window_facts(const maelys_datalog_group_window_t *w,
    const maelys_datalog_fact_t **out, size_t *count) {
    if (!out || !count) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    maelys_datalog_status_t rc = readable(w);
    if (rc) return rc;
    *out = w->facts[w->active]; *count = w->unique_count;
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t maelys_datalog_group_window_result(const maelys_datalog_group_window_t *w,
    maelys_datalog_result_t **out) {
    if (!out) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    maelys_datalog_status_t rc = readable(w);
    if (rc) return rc;
    *out = w->result;
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t maelys_datalog_group_window_free(maelys_datalog_group_window_t *w) {
    maelys_datalog_status_t rc = readable(w);
    if (rc) return rc;
    w->busy = 1;
    rc = maelys_datalog_result_free(w->result);
    if (!rc) {
        w->result = NULL;
        for (unsigned i = 0; i < 2; ++i) {
            w->sessions[i] = NULL; w->inputs[i] = NULL;
            w->groups[i] = NULL; w->facts[i] = NULL;
        }
    }
    w->busy = 0;
    return rc;
}
