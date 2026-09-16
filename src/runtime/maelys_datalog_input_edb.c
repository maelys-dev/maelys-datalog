/* SPDX-License-Identifier: MPL-2.0 */
#include "src/public/maelys_datalog_public_internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct maelys_datalog_input_edb {
    maelys_datalog_public_fact_t *facts;
    size_t count, capacity;
    char *text;
    size_t text_used, text_capacity;
    int owned;
};

static maelys_datalog_status_t input_error(
    maelys_datalog_public_diagnostic_t *diag, maelys_datalog_status_t status,
    const char *format, ...) {
    if (diag) {
        diag->source = MAELYS_DATALOG_DIAGNOSTIC_SOLVE;
        diag->code = status;
        snprintf(diag->phase, sizeof(diag->phase), "input");
        va_list args;
        va_start(args, format);
        vsnprintf(diag->message, sizeof(diag->message), format, args);
        va_end(args);
        snprintf(diag->hint, sizeof(diag->hint),
                 "No facts were appended. Batch fact/term indices are zero-based; correct the batch and retry.");
    }
    return status;
}

/* Header size is rounded for the public fact array. The arena is never grown;
 * moving it would invalidate every copied string pointer. */
static size_t facts_offset(void) {
    size_t alignment = _Alignof(maelys_datalog_public_fact_t);
    return (sizeof(maelys_datalog_input_edb_t) + alignment - 1u) / alignment * alignment;
}

maelys_datalog_status_t maelys_datalog_input_edb_storage_requirements(
    size_t fact_capacity, size_t text_capacity, size_t *bytes, size_t *alignment) {
    if (!bytes || !alignment || !fact_capacity || fact_capacity > MAELYS_DATALOG_MAX_EDB_FACTS ||
        text_capacity > MAELYS_DATALOG_INPUT_EDB_TEXT_BYTES)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    size_t offset = facts_offset();
    if (fact_capacity > (SIZE_MAX - offset) / sizeof(maelys_datalog_public_fact_t))
        return MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE;
    size_t base = offset + fact_capacity * sizeof(maelys_datalog_public_fact_t);
    if (text_capacity > SIZE_MAX - base)
        return MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE;
    *bytes = base + text_capacity;
    *alignment = _Alignof(maelys_datalog_input_edb_t) > _Alignof(maelys_datalog_public_fact_t)
        ? _Alignof(maelys_datalog_input_edb_t) : _Alignof(maelys_datalog_public_fact_t);
    return MAELYS_DATALOG_STATUS_OK;
}

maelys_datalog_status_t maelys_datalog_input_edb_init(
    void *storage, size_t storage_bytes, size_t fact_capacity, size_t text_capacity,
    maelys_datalog_input_edb_t **out) {
    if (!out) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    *out = NULL;
    size_t bytes, alignment;
    maelys_datalog_status_t rc = maelys_datalog_input_edb_storage_requirements(
        fact_capacity, text_capacity, &bytes, &alignment);
    if (rc) return rc;
    if (!storage || (uintptr_t)storage % alignment)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (storage_bytes < bytes) return MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE;
    maelys_datalog_input_edb_t *edb = storage;
    *edb = (maelys_datalog_input_edb_t){0};
    edb->facts = (void *)((unsigned char *)storage + facts_offset());
    edb->capacity = fact_capacity;
    edb->text = (char *)(edb->facts + fact_capacity);
    edb->text_capacity = text_capacity;
    *out = edb;
    return MAELYS_DATALOG_STATUS_OK;
}

maelys_datalog_status_t maelys_datalog_input_edb_create_with_capacity(
    size_t fact_capacity, size_t text_capacity, maelys_datalog_input_edb_t **out) {
    if (!out) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    *out = NULL;
    size_t bytes, alignment;
    maelys_datalog_status_t rc = maelys_datalog_input_edb_storage_requirements(
        fact_capacity, text_capacity, &bytes, &alignment);
    if (rc) return rc;
    void *storage = malloc(bytes);
    if (!storage) return MAELYS_DATALOG_STATUS_INTERNAL;
    rc = maelys_datalog_input_edb_init(storage, bytes, fact_capacity, text_capacity, out);
    if (rc) { free(storage); return rc; }
    (*out)->owned = 1;
    return MAELYS_DATALOG_STATUS_OK;
}

/* Count bytes without modifying the arena. A whole batch is validated before
 * its first write, preserving atomicity without an allocated staging buffer. */
static const char *find_text(const maelys_datalog_input_edb_t *edb, const char *text) {
    for (size_t offset = 0u; offset < edb->text_used; ) {
        const char *candidate = edb->text + offset;
        if (!strcmp(candidate, text)) return candidate;
        offset += strlen(candidate) + 1u;
    }
    return NULL;
}

/* Only the already-validated prefix is inspected. This avoids staging copies
 * and keeps failed appends byte-for-byte unchanged, including the spare arena.
 * The scan is bounded by the profile's fact/arity limits. */
static int prefix_has_text(const maelys_datalog_public_fact_t *facts,
                           size_t index, size_t term_index, const char *text) {
    for (size_t i = 0; i <= index; ++i) {
        if (i == index && term_index == SIZE_MAX) break; /* current predicate */
        if (!strcmp(facts[i].predicate, text)) return 1;
        size_t terms = i == index ? term_index : facts[i].arity;
        for (size_t j = 0; j < terms; ++j)
            if (facts[i].terms[j].kind == MAELYS_DATALOG_VALUE_SYMBOL &&
                !strcmp(facts[i].terms[j].as.symbol, text)) return 1;
    }
    return 0;
}

static maelys_datalog_status_t measure_text(
    const maelys_datalog_input_edb_t *edb, const maelys_datalog_public_fact_t *facts,
    size_t index, size_t term_index, const char *text, size_t *remaining, const char **reason) {
    if (!text) { *reason = "NULL string"; return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT; }
    size_t size = strnlen(text, MAELYS_DATALOG_MAX_STRING_BYTES + 1u);
    if (size > MAELYS_DATALOG_MAX_STRING_BYTES) {
        *reason = "string exceeds MAX_STRING_BYTES";
        return MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE;
    }
    if (find_text(edb, text) || prefix_has_text(facts, index, term_index, text))
        return MAELYS_DATALOG_STATUS_OK;
    if (size + 1u > *remaining) {
        *reason = "input EDB text capacity exhausted (including NUL terminators)";
        return MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE;
    }
    *remaining -= size + 1u;
    return MAELYS_DATALOG_STATUS_OK;
}

static const char *store_text(maelys_datalog_input_edb_t *edb, const char *text) {
    const char *existing = find_text(edb, text);
    if (existing) return existing;
    size_t bytes = strlen(text) + 1u; /* Already bounded by validation. */
    char *copy = edb->text + edb->text_used;
    memcpy(copy, text, bytes);
    edb->text_used += bytes;
    return copy;
}

maelys_datalog_status_t maelys_datalog_input_edb_create(maelys_datalog_input_edb_t **out) {
    return maelys_datalog_input_edb_create_with_capacity(
        MAELYS_DATALOG_MAX_EDB_FACTS, MAELYS_DATALOG_INPUT_EDB_TEXT_BYTES, out);
}

maelys_datalog_status_t maelys_datalog_input_edb_add_facts(
    maelys_datalog_input_edb_t *edb, const maelys_datalog_public_fact_t *facts,
    size_t count, maelys_datalog_public_diagnostic_t *diag) {
    maelys_datalog_public_diagnostic_clear(diag);
    if (!edb || (!facts && count))
        return input_error(diag, MAELYS_DATALOG_STATUS_INVALID_ARGUMENT,
                           "An input EDB and a non-NULL batch for nonzero fact_count are required.");
    if (count > edb->capacity - edb->count)
        return input_error(diag, MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE,
                           "Appending %zu entries to %zu exceeds input EDB capacity %zu (before deduplication).",
                           count, edb->count, edb->capacity);
    if (!count) return MAELYS_DATALOG_STATUS_OK;
    size_t remaining = edb->text_capacity - edb->text_used;
    const char *reason = "unsupported value kind";
    maelys_datalog_status_t rc = MAELYS_DATALOG_STATUS_OK;
    for (size_t i = 0; i < count; ++i) {
        if (facts[i].arity > MAELYS_DATALOG_MAX_TERMS) {
            rc = input_error(diag, MAELYS_DATALOG_STATUS_INVALID_ARGUMENT,
                             "Fact %zu: arity %zu exceeds limit %u.", i, facts[i].arity,
                             MAELYS_DATALOG_MAX_TERMS);
            return rc;
        }
        rc = measure_text(edb, facts, i, SIZE_MAX, facts[i].predicate, &remaining, &reason);
        if (rc) {
            input_error(diag, rc, "Fact %zu: predicate rejected: %s.", i, reason);
            return rc;
        }
        for (size_t j = 0; j < facts[i].arity; ++j) {
            const maelys_datalog_public_value_t *in = &facts[i].terms[j];
            switch (in->kind) {
            case MAELYS_DATALOG_VALUE_SYMBOL:
                rc = measure_text(edb, facts, i, j, in->as.symbol, &remaining, &reason);
                break;
            case MAELYS_DATALOG_VALUE_INTEGER:
            case MAELYS_DATALOG_VALUE_BOOLEAN:
                break;
            default:
                rc = MAELYS_DATALOG_STATUS_INVALID_FIELD;
                break;
            }
            if (rc) {
                input_error(diag, rc, "Fact %zu, term %zu: %s.", i, j, reason);
                return rc;
            }
        }
    }
    for (size_t i = 0; i < count; ++i) {
        maelys_datalog_public_fact_t *dest = &edb->facts[edb->count + i];
        *dest = (maelys_datalog_public_fact_t){0};
        dest->predicate = store_text(edb, facts[i].predicate);
        dest->arity = facts[i].arity;
        for (size_t j = 0; j < facts[i].arity; ++j) {
            dest->terms[j] = facts[i].terms[j];
            if (dest->terms[j].kind == MAELYS_DATALOG_VALUE_SYMBOL)
                dest->terms[j].as.symbol = store_text(edb, facts[i].terms[j].as.symbol);
            else if (dest->terms[j].kind == MAELYS_DATALOG_VALUE_BOOLEAN)
                dest->terms[j].as.boolean = !!dest->terms[j].as.boolean;
        }
    }
    edb->count += count;
    return MAELYS_DATALOG_STATUS_OK;
}

maelys_datalog_status_t maelys_datalog_input_edb_add_fact(
    maelys_datalog_input_edb_t *edb, const char *predicate,
    const maelys_datalog_public_value_t *terms, size_t arity,
    maelys_datalog_public_diagnostic_t *diag) {
    maelys_datalog_public_diagnostic_clear(diag);
    if (arity > MAELYS_DATALOG_MAX_TERMS || (!terms && arity))
        return input_error(diag, MAELYS_DATALOG_STATUS_INVALID_ARGUMENT,
                           "Terms are required for nonzero arity; maximum arity is %u.",
                           MAELYS_DATALOG_MAX_TERMS);
    maelys_datalog_public_fact_t fact = {0};
    fact.predicate = predicate;
    fact.arity = arity;
    if (arity) memcpy(fact.terms, terms, arity * sizeof(*terms));
    return maelys_datalog_input_edb_add_facts(edb, &fact, 1u, diag);
}

maelys_datalog_status_t maelys_datalog_input_edb_count(
    const maelys_datalog_input_edb_t *edb, size_t *out) {
    if (!edb || !out) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    *out = edb->count;
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t maelys_datalog_input_edb_clear(maelys_datalog_input_edb_t *edb) {
    if (!edb) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    edb->count = 0;
    edb->text_used = 0;
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t maelys_datalog_input_edb_free(maelys_datalog_input_edb_t *edb) {
    if (edb) {
        int owned = edb->owned;
        memset(edb, 0, sizeof(*edb));
        if (owned) free(edb);
    }
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t maelys_datalog_session_solve_edb(
    maelys_datalog_session_t *session, const maelys_datalog_input_edb_t *edb,
    maelys_datalog_result_t **out, maelys_datalog_public_diagnostic_t *diag) {
    if (!edb) {
        if (out) *out = NULL;
        maelys_datalog_public_diagnostic_clear(diag);
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    }
    return maelys_datalog_session_solve(session, edb->facts, edb->count, out, diag);
}
