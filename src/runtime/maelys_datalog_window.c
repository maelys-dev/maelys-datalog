/* SPDX-License-Identifier: MPL-2.0 */
/* Public value/handle API plus the runtime-only publication boundary. */
#include <maelys/datalog_window.h>
#include "src/runtime/maelys_datalog_transaction_internal.h"
#include <stdio.h>
#include <string.h>

struct maelys_datalog_window {
    maelys_datalog_session_t *sessions[2];
    maelys_datalog_input_edb_t *inputs[2];
    maelys_datalog_result_t *result; /* NULL marks a closed caller-owned handle. */
    size_t capacity, static_capacity, static_count;
    uint64_t next;
    unsigned active;
    int busy;
};

static maelys_datalog_status_t window_error(maelys_datalog_diagnostic_t *d,
    maelys_datalog_status_t rc, const char *message) {
    if (d) {
        { maelys_datalog_status_t ds = maelys_datalog_diagnostic_clear(d); if (ds) return ds; }
        d->source = MAELYS_DATALOG_DIAGNOSTIC_SOLVE;
        d->status = rc;
        d->code = MAELYS_DATALOG_DIAG_OPERATION_REJECTED;
        snprintf(d->phase, sizeof(d->phase), "window");
        snprintf(d->message, sizeof(d->message), "%s", message);
        snprintf(d->hint, sizeof(d->hint), "No window transaction was published; the occurrence cursor did not advance.");
    }
    return rc;
}

static maelys_datalog_status_t layout(size_t n, size_t text, size_t static_capacity, size_t *bytes,
    size_t *alignment, size_t *offset, size_t *stride) {
    if (!n || static_capacity > SIZE_MAX - n) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    size_t input_bytes, input_alignment;
    maelys_datalog_status_t rc = maelys_datalog_input_edb_storage_requirements(
        n + static_capacity, text, &input_bytes, &input_alignment);
    if (rc) return rc;
    size_t a = _Alignof(max_align_t);
    if (input_alignment > a) a = input_alignment;
    if (input_bytes > SIZE_MAX - (a - 1u)) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    size_t step = (input_bytes + a - 1u) / a * a;
    size_t start = (sizeof(maelys_datalog_window_t) + a - 1u) / a * a;
    if (step > (SIZE_MAX - start) / 2u) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    *bytes = start + 2u * step;
    *alignment = a;
    *offset = start;
    *stride = step;
    return MAELYS_DATALOG_STATUS_OK;
}

maelys_datalog_status_t maelys_datalog_window_storage_requirements(
    size_t n, size_t text, size_t *bytes, size_t *alignment) {
    return maelys_datalog_window_storage_requirements_configured(n, text, NULL, bytes, alignment);
}

maelys_datalog_status_t maelys_datalog_window_storage_requirements_configured(
    size_t n, size_t text, const maelys_datalog_window_options_t *options, size_t *bytes, size_t *alignment) {
    if (options && (options->struct_size != sizeof(*options) || options->flags)) {
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    }
    size_t static_capacity = options ? options->static_fact_capacity : 0;

    if (!bytes || !alignment) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    size_t offset, stride;
    return layout(n, text, static_capacity, bytes, alignment, &offset, &stride);
}

maelys_datalog_status_t maelys_datalog_window_init(
    void *storage, size_t storage_bytes, size_t n, size_t text, uint32_t first,
    maelys_datalog_session_t *a, maelys_datalog_session_t *b,
    maelys_datalog_window_t **out, maelys_datalog_diagnostic_t *diag) {
    return maelys_datalog_window_init_configured(storage, storage_bytes, n, text, NULL,
        first, a, b, out, diag);
}

maelys_datalog_status_t maelys_datalog_window_init_configured(
    void *storage, size_t storage_bytes, size_t n, size_t text, const maelys_datalog_window_options_t *options,
    uint32_t first, maelys_datalog_session_t *a, maelys_datalog_session_t *b,
    maelys_datalog_window_t **out, maelys_datalog_diagnostic_t *diag) {

    if (out) *out = NULL;
    { maelys_datalog_status_t ds = maelys_datalog_diagnostic_clear(diag); if (ds) return ds; }
    if (options && (options->struct_size != sizeof(*options) || options->flags))
        return window_error(diag, MAELYS_DATALOG_STATUS_INVALID_ARGUMENT, "Invalid window options.");
    size_t static_capacity = options ? options->static_fact_capacity : 0;
    size_t bytes, alignment, offset, stride;
    if (!out || !storage || !a || !b || a == b || first > INT32_MAX)
        return window_error(diag, MAELYS_DATALOG_STATUS_INVALID_ARGUMENT, "Two distinct sessions, storage and a nonnegative int32 occurrence ID are required.");
    maelys_datalog_status_t rc = layout(n, text, static_capacity, &bytes, &alignment, &offset, &stride);
    if (rc) return window_error(diag, rc, "Invalid window capacities.");
    if ((uintptr_t)storage % alignment || storage_bytes > UINTPTR_MAX - (uintptr_t)storage)
        return window_error(diag, MAELYS_DATALOG_STATUS_INVALID_ARGUMENT, "Window storage is misaligned or its range overflows.");
    if (storage_bytes < bytes)
        return window_error(diag, MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL, "Window storage is smaller than its queried requirement.");
    char fa[MAELYS_DATALOG_PUBLIC_FINGERPRINT_BYTES], fb[MAELYS_DATALOG_PUBLIC_FINGERPRINT_BYTES];
    rc = maelys_datalog_session_execution_fingerprint(a, fa);
    if (!rc) rc = maelys_datalog_session_execution_fingerprint(b, fb);
    if (rc) return rc;
    if (strcmp(fa, fb))
        return window_error(diag, MAELYS_DATALOG_STATUS_INVALID_ARGUMENT, "Window sessions have different execution fingerprints.");
    maelys_datalog_window_t *w = storage;
    *w = (maelys_datalog_window_t){.sessions = {a, b}, .capacity = n, .static_capacity = static_capacity, .next = first};
    for (unsigned i = 0; i < 2u; ++i) {
        rc = maelys_datalog_input_edb_init((unsigned char *)storage + offset + i * stride,
            stride, n + static_capacity, text, &w->inputs[i]);
        if (rc) return rc;
    }
    /* Check both sessions while leaving no candidate result leased on failure.
     * No result has escaped, so candidate release cannot have an explanation. */
    maelys_datalog_result_t *probe = NULL;
    rc = maelys_datalog_session_solve_edb_candidate(b, w->inputs[1], &probe, diag);
    if (rc) return rc;
    rc = maelys_datalog_result_free(probe);
    if (rc) return rc;
    rc = maelys_datalog_session_solve_edb_candidate(a, w->inputs[0], &w->result, diag);
    if (rc) return rc;
    maelys_datalog_result_commit(w->result);
    *out = w;
    return MAELYS_DATALOG_STATUS_OK;
}

/* Build a complete candidate bank. Static facts occupy a prefix; event views
 * and occurrence IDs never include that prefix. Only committed metadata changes
 * after the old result lease has been released. */
static maelys_datalog_status_t window_update(maelys_datalog_window_t *w,
    const maelys_datalog_fact_t *facts, size_t count, int replace_static,
    uint32_t *occurrence, maelys_datalog_diagnostic_t *diag) {
    if (!w || (!facts && count))
        return window_error(diag, MAELYS_DATALOG_STATUS_INVALID_ARGUMENT, "A nonempty update needs facts.");
    if (!w->result || w->busy)
        return window_error(diag, MAELYS_DATALOG_STATUS_INVALID_STATE, "Window is closed or busy.");
    if (replace_static ? count > w->static_capacity : w->next > INT32_MAX)
        return window_error(diag, MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE, "Static capacity or occurrence ID space is exhausted.");
    w->busy = 1;
    unsigned candidate = 1u - w->active;
    const maelys_datalog_fact_t *old = NULL;
    size_t old_count = 0;
    maelys_datalog_result_t *result = NULL;
    maelys_datalog_status_t rc = maelys_datalog_input_edb_view(w->inputs[w->active], &old, &old_count);
    size_t static_count = replace_static ? count : w->static_count;
    size_t events = old_count - w->static_count;
    size_t skip = !replace_static && events == w->capacity ? 1u : 0u;
    if (!rc) rc = maelys_datalog_input_edb_clear(w->inputs[candidate]);
    if (!rc) rc = maelys_datalog_input_edb_add_facts(w->inputs[candidate],
        replace_static ? facts : old, static_count, diag);
    if (!rc) rc = maelys_datalog_input_edb_add_facts(w->inputs[candidate],
        old + w->static_count + skip, events - skip, diag);
    if (!rc && !replace_static)
        rc = maelys_datalog_input_edb_add_facts(w->inputs[candidate], facts, count, diag);
    if (!rc) rc = maelys_datalog_session_solve_edb_candidate(w->sessions[candidate], w->inputs[candidate], &result, diag);
    if (!rc) {
        rc = maelys_datalog_result_free(w->result);
        if (rc) {
            (void)maelys_datalog_result_free(result);
            window_error(diag, rc, "Current window result could not be released; candidate discarded.");
        } else {
            w->result = result;
            w->active = candidate;
            w->static_count = static_count;
            if (!replace_static) {
                if (occurrence) *occurrence = (uint32_t)w->next;
                ++w->next;
            }
            maelys_datalog_result_commit(result);
        }
    }
    w->busy = 0;
    return rc;
}

maelys_datalog_status_t maelys_datalog_window_push(
    maelys_datalog_window_t *w, const char *predicate,
    const maelys_datalog_value_t *values, size_t count,
    uint32_t *occurrence, maelys_datalog_diagnostic_t *diag) {
    { maelys_datalog_status_t ds = maelys_datalog_diagnostic_clear(diag); if (ds) return ds; }
    if (!w || !predicate || count >= MAELYS_DATALOG_PUBLIC_MAX_TERMS || (!values && count))
        return window_error(diag, MAELYS_DATALOG_STATUS_INVALID_ARGUMENT, "An event needs a predicate and at most three payload values.");
    if (!w->result || w->busy)
        return window_error(diag, MAELYS_DATALOG_STATUS_INVALID_STATE, "Window is closed or busy.");
    maelys_datalog_fact_t fact = {.predicate = predicate, .arity = count + 1u};
    fact.terms[0].kind = MAELYS_DATALOG_VALUE_INTEGER;
    fact.terms[0].as.integer = (int64_t)w->next;
    if (count) memcpy(fact.terms + 1, values, count * sizeof(*values));
    return window_update(w, &fact, 1, 0, occurrence, diag);
}

maelys_datalog_status_t maelys_datalog_window_replace_static(maelys_datalog_window_t *w,
    const maelys_datalog_fact_t *facts, size_t count, maelys_datalog_diagnostic_t *diag) {
    { maelys_datalog_status_t ds = maelys_datalog_diagnostic_clear(diag); if (ds) return ds; }
    return window_update(w, facts, count, 1, NULL, diag);
}

maelys_datalog_status_t maelys_datalog_window_static_facts(const maelys_datalog_window_t *w,
    const maelys_datalog_fact_t **out, size_t *count) {
    if (!w || !out || !count) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (!w->result || w->busy) return MAELYS_DATALOG_STATUS_INVALID_STATE;
    size_t total;
    maelys_datalog_status_t rc = maelys_datalog_input_edb_view(w->inputs[w->active], out, &total);
    if (!rc) *count = w->static_count;
    return rc;
}

maelys_datalog_status_t maelys_datalog_window_result(
    const maelys_datalog_window_t *w, maelys_datalog_result_t **out) {
    if (!w || !out) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (!w->result || w->busy) return MAELYS_DATALOG_STATUS_INVALID_STATE;
    *out = w->result;
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t maelys_datalog_window_events(
    const maelys_datalog_window_t *w, const maelys_datalog_fact_t **out, size_t *count) {
    if (!w || !out || !count) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (!w->result || w->busy) return MAELYS_DATALOG_STATUS_INVALID_STATE;
    maelys_datalog_status_t rc = maelys_datalog_input_edb_view(w->inputs[w->active], out, count);
    if (!rc) { *out += w->static_count; *count -= w->static_count; }
    return rc;
}
maelys_datalog_status_t maelys_datalog_window_state(
    const maelys_datalog_window_t *w, size_t *count, uint64_t *next) {
    if (!w || !count || !next) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (!w->result || w->busy) return MAELYS_DATALOG_STATUS_INVALID_STATE;
    maelys_datalog_status_t rc = maelys_datalog_input_edb_count(w->inputs[w->active], count);
    if (!rc) { *count -= w->static_count; *next = w->next; }
    return rc;
}
maelys_datalog_status_t maelys_datalog_window_text_usage(
    const maelys_datalog_window_t *w, size_t *used, size_t *capacity) {
    if (!w) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (!w->result || w->busy) return MAELYS_DATALOG_STATUS_INVALID_STATE;
    return maelys_datalog_input_edb_text_usage(w->inputs[w->active], used, capacity);
}
maelys_datalog_status_t maelys_datalog_window_free(maelys_datalog_window_t *w) {
    if (!w) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (!w->result || w->busy) return MAELYS_DATALOG_STATUS_INVALID_STATE;
    w->busy = 1;
    maelys_datalog_status_t rc = maelys_datalog_result_free(w->result);
    if (!rc) {
        w->result = NULL;
        w->sessions[0] = w->sessions[1] = NULL;
        w->inputs[0] = w->inputs[1] = NULL;
    }
    w->busy = 0;
    return rc;
}
