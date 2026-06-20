#pragma once
#ifndef MAELYS_DATALOG_SYMBOL_TABLE_H
#define MAELYS_DATALOG_SYMBOL_TABLE_H

#include <stddef.h>
#include <stdint.h>

#include "common/maelys_errors.h"
#include "src/core/maelys_datalog_types.h"

typedef struct {
    char storage[MAELYS_DATALOG_STRING_POOL_BYTES];
    size_t used;
    struct {
        uint32_t hash;
        uint32_t offset;
        uint16_t len;
    } entries[MAELYS_DATALOG_MAX_SYMBOLS];
    size_t count;
    uint16_t index[MAELYS_DATALOG_SYMBOL_INDEX_BUCKETS];
} maelys_datalog_symbol_table_t;

_Static_assert((MAELYS_DATALOG_SYMBOL_INDEX_BUCKETS &
                (MAELYS_DATALOG_SYMBOL_INDEX_BUCKETS - 1u)) == 0u,
               "symbol index buckets must be a power of two");
_Static_assert(MAELYS_DATALOG_SYMBOL_INDEX_BUCKETS >=
                   2u * MAELYS_DATALOG_MAX_SYMBOLS,
               "symbol index load factor must stay <= 0.5");
_Static_assert(MAELYS_DATALOG_MAX_SYMBOLS < UINT16_MAX,
               "symbol index bucket values store entry_index + 1");

void maelys_datalog_symbol_table_init(maelys_datalog_symbol_table_t *table);
maelys_result_t maelys_datalog_symbol_intern(maelys_datalog_symbol_table_t *table,
                                             const char *text,
                                             size_t len,
                                             maelys_datalog_symbol_id_t *out_id);
int maelys_datalog_symbol_id_is_valid(const maelys_datalog_symbol_table_t *table,
                                      maelys_datalog_symbol_id_t id);
const char *maelys_datalog_symbol_text(const maelys_datalog_symbol_table_t *table,
                                       maelys_datalog_symbol_id_t id);

#endif
