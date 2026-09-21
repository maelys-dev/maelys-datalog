/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include "src/core/maelys_datalog_edb.h"

#define MAELYS_DATALOG_EDB_INSERT_SLOTS (2u * MAELYS_DATALOG_MAX_EDB_FACTS)
typedef struct {
    /* Zero means empty; other values are fact-pool offsets plus one. */
    uint16_t slots[MAELYS_DATALOG_EDB_INSERT_SLOTS];
} maelys_datalog_edb_insert_index_t;

/* Private construction path: start with an empty EDB and a zeroed index.
 * Until finalization, every insertion must use this index and the pool must
 * not be reordered. Finalization invalidates it. No allocation or fallback. */
maelys_result_t maelys_datalog_edb_add_fact_indexed(
    maelys_datalog_edb_t *edb, const char *predicate,
    const maelys_datalog_term_t *terms, size_t arity,
    maelys_datalog_edb_insert_index_t *index);

#ifdef MAELYS_TESTING
size_t maelys_datalog_test_edb_insert_bucket(const maelys_datalog_fact_t *fact);
#endif
