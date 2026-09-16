/* SPDX-License-Identifier: MPL-2.0 */
/* Instrument input storage only, not the independently linked solver. */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
static size_t allocations, releases;
static int forbid_allocations;
static void *input_test_malloc(size_t size) {
    ++allocations;
    return forbid_allocations ? NULL : malloc(size);
}
static void input_test_free(void *p) { ++releases; free(p); }
#define malloc input_test_malloc
#define free input_test_free
#define calloc(...) INPUT_EDB_CALLOC_IS_FORBIDDEN
#define realloc(...) INPUT_EDB_REALLOC_IS_FORBIDDEN
#include "src/runtime/maelys_datalog_input_edb.c"
#undef malloc
#undef free
#undef calloc
#undef realloc

int main(void) {
    union { max_align_t align; unsigned char bytes[8192]; } arena;
    unsigned char snapshot[sizeof(arena.bytes)];
    memset(arena.bytes, 0xA5, sizeof(arena.bytes));
    size_t bytes = 99u, alignment = 99u;
    assert(maelys_datalog_input_edb_storage_requirements(8u, 256u, &bytes, &alignment) == 0);
    assert(bytes <= sizeof(arena.bytes) && (uintptr_t)arena.bytes % alignment == 0u);
    size_t expected_bytes = bytes, expected_alignment = alignment;
    assert(maelys_datalog_input_edb_storage_requirements(0u, 1u, &bytes, &alignment) != 0);
    assert(maelys_datalog_input_edb_storage_requirements(SIZE_MAX, 1u, &bytes, &alignment) != 0);
    assert(maelys_datalog_input_edb_storage_requirements(1u, SIZE_MAX, &bytes, &alignment) != 0);
    assert(bytes == expected_bytes && alignment == expected_alignment);
    maelys_datalog_input_edb_t *edb = NULL;
    forbid_allocations = 1;
    memcpy(snapshot, arena.bytes, sizeof(snapshot));
    assert(maelys_datalog_input_edb_init(arena.bytes + 1u, bytes, 8u, 256u, &edb) != 0 && !edb);
    assert(maelys_datalog_input_edb_init(arena.bytes, bytes - 1u, 8u, 256u, &edb) != 0 && !edb);
    assert(maelys_datalog_input_edb_init(NULL, bytes, 8u, 256u, &edb) != 0 && !edb);
    assert(memcmp(snapshot, arena.bytes, sizeof(snapshot)) == 0);
    assert(maelys_datalog_input_edb_init(arena.bytes, bytes, 8u, 256u, &edb) == 0);
    char predicate[] = "seen", symbol[] = "alice";
    maelys_datalog_public_value_t value = {.kind = MAELYS_DATALOG_VALUE_SYMBOL, .as.symbol = symbol};
    assert(maelys_datalog_input_edb_add_fact(edb, predicate, &value, 1u, NULL) == 0);
    predicate[0] = 'x'; symbol[0] = 'x';
    assert(strcmp(edb->facts[0].predicate, "seen") == 0);
    assert(strcmp(edb->facts[0].terms[0].as.symbol, "alice") == 0);
    maelys_datalog_public_fact_t batch[2] = {0};
    batch[0].predicate = "seen"; batch[0].arity = 1; batch[0].terms[0] = value;
    batch[1] = batch[0]; batch[1].terms[0].as.symbol = NULL;
    maelys_datalog_public_diagnostic_t diag;
    memcpy(snapshot, arena.bytes, sizeof(snapshot));
    assert(maelys_datalog_input_edb_add_facts(edb, batch, 2u, &diag) != 0);
    assert(strstr(diag.message, "Fact 1, term 0"));
    assert(memcmp(snapshot, arena.bytes, sizeof(snapshot)) == 0);
    batch[1].terms[0].kind = (maelys_datalog_value_kind_t)99;
    assert(maelys_datalog_input_edb_add_facts(edb, batch, 2u, &diag) != 0);
    assert(memcmp(snapshot, arena.bytes, sizeof(snapshot)) == 0);
    batch[1] = batch[0];
    assert(maelys_datalog_input_edb_add_facts(edb, batch, 2u, &diag) == 0);
    size_t count;
    assert(maelys_datalog_input_edb_count(edb, &count) == 0 && count == 3u);
    assert(maelys_datalog_input_edb_clear(edb) == 0);
    assert(edb->count == 0u && edb->text_used == 0u);
    assert(maelys_datalog_input_edb_free(edb) == 0);
    assert(allocations == 0u && releases == 0u);

    /* Exact byte fit, atomic arena exhaustion, and reuse after clear. */
    assert(maelys_datalog_input_edb_init(arena.bytes, sizeof(arena.bytes), 2u, 4u, &edb) == 0);
    value.as.symbol = "x";
    batch[0].predicate = "p"; batch[0].terms[0] = value; batch[1] = batch[0];
    batch[1].terms[0].as.symbol = "y";
    memcpy(snapshot, arena.bytes, sizeof(snapshot));
    assert(maelys_datalog_input_edb_add_facts(edb, batch, 2u, &diag) == MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
    assert(strstr(diag.message, "text capacity exhausted"));
    assert(memcmp(snapshot, arena.bytes, sizeof(snapshot)) == 0);
    for (size_t i = 0; i < 100u; ++i) {
        assert(maelys_datalog_input_edb_add_fact(edb, "p", &value, 1u, NULL) == 0);
        assert(edb->text_used == 4u);
        memcpy(snapshot, arena.bytes, sizeof(snapshot));
        assert(maelys_datalog_input_edb_add_fact(edb, "q", &value, 1u, &diag) != 0);
        assert(memcmp(snapshot, arena.bytes, sizeof(snapshot)) == 0);
        assert(maelys_datalog_input_edb_clear(edb) == 0);
    }
    assert(maelys_datalog_input_edb_free(edb) == 0);
    assert(maelys_datalog_input_edb_init(arena.bytes, sizeof(arena.bytes), 1u, 4u, &edb) == 0);
    assert(maelys_datalog_input_edb_add_fact(edb, "p", &value, 1u, NULL) == 0);
    assert(maelys_datalog_input_edb_add_fact(edb, "p", &value, 1u, &diag) != 0);
    assert(strstr(diag.message, "EDB capacity 1"));
    assert(maelys_datalog_input_edb_free(edb) == 0);
    assert(maelys_datalog_input_edb_init(arena.bytes, sizeof(arena.bytes), 1u, 0u, &edb) == 0);
    assert(maelys_datalog_input_edb_add_fact(edb, "", NULL, 0u, &diag) != 0);
    assert(maelys_datalog_input_edb_add_facts(edb, NULL, 0u, NULL) == 0);
    assert(maelys_datalog_input_edb_free(edb) == 0);
    assert(allocations == 0u && releases == 0u);

    /* Cross-fact, cross-role and intra-batch strings share one copy. Repeated
     * strings consume no bytes even when the pool is already exactly full. */
    assert(maelys_datalog_input_edb_init(arena.bytes, sizeof(arena.bytes), 8u, 4u, &edb) == 0);
    batch[1] = batch[0];
    assert(maelys_datalog_input_edb_add_facts(edb, batch, 2u, NULL) == 0);
    assert(edb->count == 2u && edb->text_used == 4u);
    assert(edb->facts[0].predicate == edb->facts[1].predicate);
    assert(edb->facts[0].terms[0].as.symbol == edb->facts[1].terms[0].as.symbol);
    assert(maelys_datalog_input_edb_add_fact(edb, "x", &value, 1u, NULL) == 0);
    assert(edb->text_used == 4u);
    assert(maelys_datalog_input_edb_free(edb) == 0);

    /* One construction allocation, none during append/clear, one release. */
    assert(maelys_datalog_input_edb_create_with_capacity(2u, 32u, &edb) == MAELYS_DATALOG_STATUS_INTERNAL && !edb);
    assert(allocations == 1u);
    forbid_allocations = 0;
    assert(maelys_datalog_input_edb_create_with_capacity(2u, 32u, &edb) == 0);
    assert(allocations == 2u);
    forbid_allocations = 1;
    assert(maelys_datalog_input_edb_add_facts(edb, batch, 2u, NULL) == 0);
    assert(maelys_datalog_input_edb_clear(edb) == 0);
    assert(maelys_datalog_input_edb_free(edb) == 0);
    assert(allocations == 2u && releases == 1u);
    assert(maelys_datalog_input_edb_free(NULL) == 0 && releases == 1u);
    puts("input EDB: no allocator calls for caller-owned lifecycle or append/clear; one allocation for owned construction");
    return 0;
}
