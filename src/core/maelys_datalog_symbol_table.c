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
    for (size_t i = 0; i < table->count; i++) {
        if (table->entries[i].hash == h && table->entries[i].len == len &&
            memcmp(table->storage + table->entries[i].offset, text, len) == 0) {
            *out_id = (maelys_datalog_symbol_id_t)(i + 1u);
            return MAELYS_OK;
        }
    }
    if (table->count >= MAELYS_DATALOG_MAX_SYMBOLS) return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    if (len + 1u > sizeof(table->storage) - table->used) return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    size_t offset = table->used;
    memcpy(table->storage + offset, text, len);
    table->storage[offset + len] = '\0';
    table->entries[table->count].hash = h;
    table->entries[table->count].offset = (uint32_t)offset;
    table->entries[table->count].len = (uint16_t)len;
    table->used += len + 1u;
    table->count++;
    *out_id = (maelys_datalog_symbol_id_t)table->count;
    return MAELYS_OK;
}

const char *maelys_datalog_symbol_text(const maelys_datalog_symbol_table_t *table,
                                       maelys_datalog_symbol_id_t id) {
    if (!table || id == 0 || id > table->count) return NULL;
    return table->storage + table->entries[id - 1u].offset;
}
