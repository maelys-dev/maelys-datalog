#include "src/core/maelys_datalog_symbol_table.h"

#include <string.h>

static uint32_t fnv1a(const char *s, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= (unsigned char)s[i];
        h *= 16777619u;
    }
    return h;
}

void maelys_datalog_symbol_table_init(maelys_datalog_symbol_table_t *table) {
    if (!table) return;
    memset(table, 0, sizeof(*table));
}

maelys_result_t maelys_datalog_symbol_intern(maelys_datalog_symbol_table_t *table,
                                             const char *text,
                                             size_t len,
                                             maelys_datalog_symbol_id_t *out_id) {
    if (!table || !text || !out_id || len > MAELYS_DATALOG_MAX_STRING_BYTES) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    uint32_t h = fnv1a(text, len);
    size_t bucket = (size_t)h & (MAELYS_DATALOG_SYMBOL_INDEX_BUCKETS - 1u);
    size_t probes = 0;
    while (table->index[bucket] != 0u) {
        size_t i = (size_t)table->index[bucket] - 1u;
        if (i >= table->count) return MAELYS_ERR_INVALID_STATE;
        if (table->entries[i].hash == h && table->entries[i].len == len &&
            memcmp(table->storage + table->entries[i].offset, text, len) == 0) {
            *out_id = (maelys_datalog_symbol_id_t)(i + 1u);
            return MAELYS_OK;
        }
        bucket = (bucket + 1u) & (MAELYS_DATALOG_SYMBOL_INDEX_BUCKETS - 1u);
        if (++probes >= MAELYS_DATALOG_SYMBOL_INDEX_BUCKETS) {
            return MAELYS_ERR_INVALID_STATE;
        }
    }
    if (table->count >= MAELYS_DATALOG_MAX_SYMBOLS) return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    if (len + 1u > sizeof(table->storage) - table->used) return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    size_t offset = table->used;
    size_t entry_index = table->count;
    memcpy(table->storage + offset, text, len);
    table->storage[offset + len] = '\0';
    table->entries[entry_index].hash = h;
    table->entries[entry_index].offset = (uint32_t)offset;
    table->entries[entry_index].len = (uint16_t)len;
    table->used += len + 1u;
    table->index[bucket] = (uint16_t)(entry_index + 1u);
    table->count++;
    *out_id = (maelys_datalog_symbol_id_t)table->count;
    return MAELYS_OK;
}

maelys_result_t maelys_datalog_symbol_lookup_readonly(
    const maelys_datalog_symbol_table_t *table,
    const char *text,
    size_t len,
    maelys_datalog_symbol_id_t *out_id,
    int *out_found) {
    if (!out_id || !out_found) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    *out_id = MAELYS_DATALOG_SYMBOL_ID_INVALID;
    *out_found = 0;
    if (!table || !text) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    if (len > MAELYS_DATALOG_MAX_STRING_BYTES) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    for (size_t i = 0u; i < table->count; i++) {
        const size_t offset = (size_t)table->entries[i].offset;
        if ((size_t)table->entries[i].len == len &&
            memcmp(table->storage + offset, text, len) == 0) {
            *out_id = (maelys_datalog_symbol_id_t)(i + 1u);
            *out_found = 1;
            return MAELYS_OK;
        }
    }
    return MAELYS_OK;
}

const char *maelys_datalog_symbol_text(const maelys_datalog_symbol_table_t *table,
                                       maelys_datalog_symbol_id_t id) {
    if (!maelys_datalog_symbol_id_is_valid(table, id)) return NULL;
    return table->storage + table->entries[id - 1u].offset;
}

int maelys_datalog_symbol_id_is_valid(const maelys_datalog_symbol_table_t *table,
                                      maelys_datalog_symbol_id_t id) {
    return table && id != MAELYS_DATALOG_SYMBOL_ID_INVALID && id <= table->count;
}
