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
    uint16_t *index, *ordinals, *pending;
    uint8_t *generations;
    uint8_t generation;
    size_t index_slots;
    uint32_t recent[2]; /* Slots + 1, never borrowed input pointers. */
    int owned;
};

/* Separate offset+1 and pending ordinal+1 arrays: neither steals a value bit.
 * A bounded undo journal restores every index byte on error.
 * No input pointer survives a successful append. At most half the slots fill. */
#define INPUT_STRINGS_PER_FACT (MAELYS_DATALOG_MAX_TERMS + 1u)
_Static_assert(MAELYS_DATALOG_INPUT_EDB_TEXT_BYTES <= UINT16_MAX,
               "text offsets plus one must fit uint16_t");
_Static_assert(MAELYS_DATALOG_MAX_EDB_FACTS * INPUT_STRINGS_PER_FACT <= UINT16_MAX / 2u,
               "batch ordinals plus one and power-of-two table slots must fit uint16_t");

static size_t input_distinct_bound(size_t capacity, size_t text_capacity) {
    size_t positions = capacity * INPUT_STRINGS_PER_FACT;
    size_t strings = text_capacity / 2u + text_capacity % 2u;
    return positions < strings ? positions : strings;
}

static size_t input_index_slots(size_t capacity, size_t text_capacity) {
    size_t slots = 1u;
    while (slots < 2u * input_distinct_bound(capacity, text_capacity)) slots *= 2u;
    return slots;
}

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
    size_t index_bytes = (2u * input_index_slots(fact_capacity, text_capacity) +
                         input_distinct_bound(fact_capacity, text_capacity)) * sizeof(uint16_t);
    index_bytes += input_index_slots(fact_capacity, text_capacity) * sizeof(uint8_t);
    if (index_bytes > SIZE_MAX - base) return MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE;
    base += index_bytes;
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
    edb->index_slots = input_index_slots(fact_capacity, text_capacity);
    edb->index = (void *)(edb->facts + fact_capacity);
    edb->ordinals = edb->index + edb->index_slots;
    edb->pending = edb->ordinals + edb->index_slots;
    edb->generations = (void *)(edb->pending + input_distinct_bound(fact_capacity, text_capacity));
    edb->generation = 1u;
    edb->text = (char *)(edb->generations + edb->index_slots);
    memset(edb->index, 0, (size_t)(edb->text - (char *)edb->index));
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

static const char *entry_text(const maelys_datalog_input_edb_t *edb, size_t slot,
                              const maelys_datalog_public_fact_t *facts) {
    if (!edb->ordinals[slot]) return edb->text + edb->index[slot] - 1u;
    size_t ordinal = edb->ordinals[slot] - 1u;
    size_t fact = ordinal / INPUT_STRINGS_PER_FACT;
    size_t term = ordinal % INPUT_STRINGS_PER_FACT;
    return term ? facts[fact].terms[term - 1u].as.symbol : facts[fact].predicate;
}

static int slot_occupied(const maelys_datalog_input_edb_t *edb, size_t slot) {
    return edb->ordinals[slot] ||
        (edb->generations[slot] == edb->generation && edb->index[slot]);
}

static size_t text_slot(const maelys_datalog_input_edb_t *edb, const char *text,
                        const maelys_datalog_public_fact_t *facts) {
    uint32_t hash = UINT32_C(2166136261);
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
        hash = (hash ^ *p) * UINT32_C(16777619);
    size_t slot = hash & (edb->index_slots - 1u);
    while (slot_occupied(edb, slot) && strcmp(entry_text(edb, slot, facts), text))
        slot = (slot + 1u) & (edb->index_slots - 1u);
    return slot;
}

/* Per-call cache for the common repeated predicate/value pair. Successful
 * appends save only slot numbers, so the next call starts with owned strings.
 * Failed appends leave the saved cache and every other arena byte unchanged. */
typedef struct {
    const char *keys[2];
    size_t slots[2], next;
} input_lookup_t;

static size_t cached_slot(const maelys_datalog_input_edb_t *edb, const char *text,
                           const maelys_datalog_public_fact_t *facts, input_lookup_t *cache) {
    for (size_t i = 0; i < 2u; ++i)
        if (cache->keys[i] && (cache->keys[i] == text || !strcmp(cache->keys[i], text)))
            return cache->slots[i];
    size_t slot = text_slot(edb, text, facts);
    size_t i = cache->next;
    cache->keys[i] = text; cache->slots[i] = slot; cache->next ^= 1u;
    return slot;
}

static void discard_pending(maelys_datalog_input_edb_t *edb, size_t count) {
    while (count) {
        --count;
        edb->ordinals[edb->pending[count]] = 0u;
        edb->pending[count] = 0u;
    }
}

static maelys_datalog_status_t measure_text(
    maelys_datalog_input_edb_t *edb, const maelys_datalog_public_fact_t *facts,
    size_t index, size_t term_index, const char *text, size_t *remaining,
    size_t *pending_count, input_lookup_t *cache, const char **reason) {
    if (!text) { *reason = "NULL string"; return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT; }
    size_t size = strnlen(text, MAELYS_DATALOG_MAX_STRING_BYTES + 1u);
    if (size > MAELYS_DATALOG_MAX_STRING_BYTES) {
        *reason = "string exceeds MAX_STRING_BYTES";
        return MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE;
    }
    size_t slot = cached_slot(edb, text, facts, cache);
    if (slot_occupied(edb, slot)) return MAELYS_DATALOG_STATUS_OK;
    if (size + 1u > *remaining) {
        *reason = "input EDB text capacity exhausted (including NUL terminators)";
        return MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE;
    }
    *remaining -= size + 1u;
    size_t ordinal = index * INPUT_STRINGS_PER_FACT + (term_index == SIZE_MAX ? 0u : term_index + 1u);
    edb->ordinals[slot] = (uint16_t)(ordinal + 1u);
    edb->pending[(*pending_count)++] = (uint16_t)slot;
    return MAELYS_DATALOG_STATUS_OK;
}

static const char *stored_text(const maelys_datalog_input_edb_t *edb, const char *text,
                               input_lookup_t *cache) {
    return entry_text(edb, cached_slot(edb, text, NULL, cache), NULL);
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
    size_t pending_count = 0u;
    input_lookup_t cache = {0};
    for (size_t i = 0; i < 2u; ++i) if (edb->recent[i]) {
        cache.slots[i] = edb->recent[i] - 1u;
        cache.keys[i] = entry_text(edb, cache.slots[i], NULL);
    }
    const char *reason = "unsupported value kind";
    maelys_datalog_status_t rc = MAELYS_DATALOG_STATUS_OK;
    for (size_t i = 0; i < count; ++i) {
        if (facts[i].arity > MAELYS_DATALOG_MAX_TERMS) {
            rc = input_error(diag, MAELYS_DATALOG_STATUS_INVALID_ARGUMENT,
                             "Fact %zu: arity %zu exceeds limit %u.", i, facts[i].arity,
                             MAELYS_DATALOG_MAX_TERMS);
            discard_pending(edb, pending_count);
            return rc;
        }
        rc = measure_text(edb, facts, i, SIZE_MAX, facts[i].predicate, &remaining, &pending_count, &cache, &reason);
        if (rc) {
            input_error(diag, rc, "Fact %zu: predicate rejected: %s.", i, reason);
            discard_pending(edb, pending_count);
            return rc;
        }
        for (size_t j = 0; j < facts[i].arity; ++j) {
            const maelys_datalog_public_value_t *in = &facts[i].terms[j];
            switch (in->kind) {
            case MAELYS_DATALOG_VALUE_SYMBOL:
                rc = measure_text(edb, facts, i, j, in->as.symbol, &remaining, &pending_count, &cache, &reason);
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
                discard_pending(edb, pending_count);
                return rc;
            }
        }
    }
    /* Validation is complete. Copy each distinct string once in encounter
     * order, then publish facts. No operation from here can fail. */
    for (size_t i = 0; i < pending_count; ++i) {
        size_t slot = edb->pending[i];
        const char *text = entry_text(edb, slot, facts);
        size_t bytes = strlen(text) + 1u;
        memcpy(edb->text + edb->text_used, text, bytes);
        edb->index[slot] = (uint16_t)(edb->text_used + 1u);
        edb->generations[slot] = edb->generation;
        edb->ordinals[slot] = 0u;
        edb->text_used += bytes;
        edb->pending[i] = 0u;
    }
    for (size_t i = 0; i < count; ++i) {
        maelys_datalog_public_fact_t *dest = &edb->facts[edb->count + i];
        *dest = (maelys_datalog_public_fact_t){0};
        dest->predicate = stored_text(edb, facts[i].predicate, &cache);
        dest->arity = facts[i].arity;
        for (size_t j = 0; j < facts[i].arity; ++j) {
            dest->terms[j] = facts[i].terms[j];
            if (dest->terms[j].kind == MAELYS_DATALOG_VALUE_SYMBOL)
                dest->terms[j].as.symbol = stored_text(edb, facts[i].terms[j].as.symbol, &cache);
            else if (dest->terms[j].kind == MAELYS_DATALOG_VALUE_BOOLEAN)
                dest->terms[j].as.boolean = !!dest->terms[j].as.boolean;
        }
    }
    edb->count += count;
    for (size_t i = 0; i < 2u; ++i)
        edb->recent[i] = cache.keys[i] ? (uint32_t)cache.slots[i] + 1u : 0u;
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
    edb->recent[0] = edb->recent[1] = 0u;
    /* Failed preflight only touches ordinals/journal, not stale offsets or
     * generations: rollback remains byte-exact even after a clear. */
    if (edb->generation == UINT8_MAX) {
        memset(edb->generations, 0, edb->index_slots * sizeof(uint8_t));
        edb->generation = 1u;
    } else ++edb->generation;
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
