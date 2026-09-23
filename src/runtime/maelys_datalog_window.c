/* SPDX-License-Identifier: MPL-2.0 */
/* This adapter intentionally consumes only the installed public facade. */
#include <maelys/datalog_window.h>
#include <stdio.h>
#include <string.h>

struct maelys_datalog_window {
    maelys_datalog_session_t *sessions[2];
    maelys_datalog_input_edb_t *inputs[2];
    maelys_datalog_result_t *result; /* NULL marks a closed caller-owned handle. */
    size_t capacity;
    uint64_t next;
    unsigned active;
    int busy;
};

static maelys_datalog_status_t window_error(maelys_datalog_public_diagnostic_t *d,
    maelys_datalog_status_t rc, const char *message) {
    if (d) {
        maelys_datalog_public_diagnostic_clear(d);
        d->source = MAELYS_DATALOG_DIAGNOSTIC_SOLVE;
        d->code = rc;
        snprintf(d->phase, sizeof(d->phase), "window");
        snprintf(d->message, sizeof(d->message), "%s", message);
        snprintf(d->hint, sizeof(d->hint), "No window transaction was published; the occurrence cursor did not advance.");
    }
    return rc;
}

static maelys_datalog_status_t layout(size_t n, size_t text, size_t *bytes,
    size_t *alignment, size_t *offset, size_t *stride) {
    size_t input_bytes, input_alignment;
    maelys_datalog_status_t rc = maelys_datalog_input_edb_storage_requirements(
        n, text, &input_bytes, &input_alignment);
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
    if (!bytes || !alignment) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    size_t offset, stride;
    return layout(n, text, bytes, alignment, &offset, &stride);
}

maelys_datalog_status_t maelys_datalog_window_init(
    void *storage, size_t storage_bytes, size_t n, size_t text, uint32_t first,
    maelys_datalog_session_t *a, maelys_datalog_session_t *b,
    maelys_datalog_window_t **out, maelys_datalog_public_diagnostic_t *diag) {
    if (out) *out = NULL;
    maelys_datalog_public_diagnostic_clear(diag);
    size_t bytes, alignment, offset, stride;
    if (!out || !storage || !a || !b || a == b || first > INT32_MAX)
        return window_error(diag, MAELYS_DATALOG_STATUS_INVALID_ARGUMENT, "Two distinct sessions, storage and a nonnegative int32 occurrence ID are required.");
    maelys_datalog_status_t rc = layout(n, text, &bytes, &alignment, &offset, &stride);
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
    *w = (maelys_datalog_window_t){.sessions = {a, b}, .capacity = n, .next = first};
    for (unsigned i = 0; i < 2u; ++i) {
        rc = maelys_datalog_input_edb_init((unsigned char *)storage + offset + i * stride,
            stride, n, text, &w->inputs[i]);
        if (rc) return rc;
    }
    /* Check both sessions while leaving no candidate result leased on failure.
     * No result has escaped, so candidate release cannot have an explanation. */
    maelys_datalog_result_t *probe = NULL;
    rc = maelys_datalog_session_solve_edb(b, w->inputs[1], &probe, diag);
    if (rc) return rc;
    rc = maelys_datalog_result_free(probe);
    if (rc) return rc;
    rc = maelys_datalog_session_solve_edb(a, w->inputs[0], &w->result, diag);
    if (rc) return rc;
    *out = w;
    return MAELYS_DATALOG_STATUS_OK;
}

maelys_datalog_status_t maelys_datalog_window_push(
    maelys_datalog_window_t *w, const char *predicate,
    const maelys_datalog_value_t *values, size_t count,
    uint32_t *occurrence, maelys_datalog_public_diagnostic_t *diag) {
    maelys_datalog_public_diagnostic_clear(diag);
    if (!w || !predicate || count >= MAELYS_DATALOG_PUBLIC_MAX_TERMS || (!values && count))
        return window_error(diag, MAELYS_DATALOG_STATUS_INVALID_ARGUMENT, "An event needs a predicate and at most three payload values.");
    if (!w->result) return window_error(diag, MAELYS_DATALOG_STATUS_INVALID_STATE, "Window is closed.");
    if (w->busy) return window_error(diag, MAELYS_DATALOG_STATUS_INVALID_STATE, "Window operation reentry is forbidden.");
    if (w->next > INT32_MAX)
        return window_error(diag, MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE, "The window occurrence ID space is exhausted.");
    w->busy = 1;
    unsigned candidate = 1u - w->active;
    const maelys_datalog_fact_t *old = NULL;
    size_t old_count = 0;
    maelys_datalog_result_t *result = NULL;
    maelys_datalog_status_t rc = maelys_datalog_input_edb_view(w->inputs[w->active], &old, &old_count);
    if (!rc) rc = maelys_datalog_input_edb_clear(w->inputs[candidate]);
    size_t skip = old_count == w->capacity ? 1u : 0u;
    if (!rc) rc = maelys_datalog_input_edb_add_facts(w->inputs[candidate], old + skip, old_count - skip, diag);
    maelys_datalog_value_t terms[MAELYS_DATALOG_PUBLIC_MAX_TERMS] = {{0}};
    terms[0].kind = MAELYS_DATALOG_VALUE_INTEGER;
    terms[0].as.integer = (int64_t)w->next;
    if (count) memcpy(terms + 1, values, count * sizeof(*values));
    if (!rc) rc = maelys_datalog_input_edb_add_fact(w->inputs[candidate], predicate, terms, count + 1u, diag);
    if (!rc) rc = maelys_datalog_session_solve_edb(w->sessions[candidate], w->inputs[candidate], &result, diag);
    if (!rc) {
        rc = maelys_datalog_result_free(w->result);
        if (rc) {
            (void)maelys_datalog_result_free(result);
            window_error(diag, rc, "Current window result could not be released; candidate discarded.");
        } else {
            /* The old lease is released. Publication below cannot fail. */
            w->result = result;
            w->active = candidate;
            if (occurrence) *occurrence = (uint32_t)w->next;
            ++w->next;
        }
    }
    w->busy = 0;
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
    if (!w) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (!w->result || w->busy) return MAELYS_DATALOG_STATUS_INVALID_STATE;
    return maelys_datalog_input_edb_view(w->inputs[w->active], out, count);
}
maelys_datalog_status_t maelys_datalog_window_state(
    const maelys_datalog_window_t *w, size_t *count, uint64_t *next) {
    if (!w || !count || !next) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (!w->result || w->busy) return MAELYS_DATALOG_STATUS_INVALID_STATE;
    maelys_datalog_status_t rc = maelys_datalog_input_edb_count(w->inputs[w->active], count);
    if (!rc) *next = w->next;
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
