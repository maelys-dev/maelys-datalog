/* SPDX-License-Identifier: MPL-2.0 */
/* Instrument input storage only, not the independently linked solver. */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>
#include <limits.h>
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

static unsigned argument_calls[7];
static maelys_datalog_input_edb_t *counted_edb(maelys_datalog_input_edb_t *edb) {
    ++argument_calls[0]; return edb;
}
static maelys_datalog_public_diagnostic_t *counted_diagnostic(maelys_datalog_public_diagnostic_t *d) {
    ++argument_calls[1]; return d;
}
static const char *counted_predicate(void) { ++argument_calls[2]; return "mixed"; }
static const char *counted_symbol(void) { ++argument_calls[3]; return "alice"; }
static int counted_integer(void) { ++argument_calls[4]; return -7; }
static unsigned long long counted_unsigned(void) { ++argument_calls[5]; return 9; }
static int counted_boolean(void) { ++argument_calls[6]; return 3; }

static void test_c11_fact_builders(void) {
    union { max_align_t align; unsigned char bytes[8192]; } arena;
    unsigned char snapshot[sizeof(arena.bytes)];
    memset(arena.bytes, 0xA5, sizeof(arena.bytes));
    maelys_datalog_input_edb_t *edb = NULL;
    maelys_datalog_public_diagnostic_t diag;
    forbid_allocations = 1;
    assert(maelys_datalog_input_edb_init(arena.bytes, sizeof(arena.bytes), 8u, 256u, &edb) == 0);
    assert(MAELYS_DATALOG_ADD_FACT(edb, &diag, "ready") == 0);
    assert(edb->facts[0].arity == 0u);
    assert(MAELYS_DATALOG_ADD_FACT(edb, &diag, "user", "alice") == 0);
    assert(edb->facts[1].arity == 1u);
    assert(edb->facts[1].terms[0].kind == MAELYS_DATALOG_VALUE_SYMBOL);
    char name[] = "owns", user[] = "bob";
    const char *const document = "roadmap.pdf";
    assert(MAELYS_DATALOG_ADD_FACT(edb, &diag, name, user, document) == 0);
    assert(edb->facts[2].arity == 2u);
    user[0] = 'x'; name[0] = 'x';
    assert(strcmp(edb->facts[2].predicate, "owns") == 0);
    assert(strcmp(edb->facts[2].terms[0].as.symbol, "bob") == 0);
    assert(strcmp(edb->facts[2].terms[1].as.symbol, "roadmap.pdf") == 0);

    /* Every standard integer rank, typedefs, qualifiers, enum and bool. */
    const volatile int number = -8;
    assert(MAELYS_DATALOG_ADD_FACT(edb, NULL, "i", (signed char)-1, (short)-2, number, -4L) == 0);
    for (size_t i = 0; i < 4; ++i) assert(edb->facts[3].terms[i].kind == MAELYS_DATALOG_VALUE_INTEGER);
    assert(edb->facts[3].terms[0].as.integer == -1 && edb->facts[3].terms[2].as.integer == -8);
    assert(MAELYS_DATALOG_ADD_FACT(edb, NULL, "u", (unsigned char)1, (unsigned short)2, 3u, 4ul) == 0);
    for (size_t i = 0; i < 4; ++i) assert(edb->facts[4].terms[i].as.integer == (int64_t)(i + 1));
    assert(MAELYS_DATALOG_ADD_FACT(edb, NULL, "bounds", INT64_MIN, INT64_MAX, (uint64_t)INT64_MAX, 7ull) == 0);
    assert(edb->facts[5].terms[0].as.integer == INT64_MIN);
    assert(edb->facts[5].terms[1].as.integer == INT64_MAX);
    assert(edb->facts[5].terms[2].as.integer == INT64_MAX);
    enum { enumeration = 6 };
    const bool enabled = true;
    assert(MAELYS_DATALOG_ADD_FACT(edb, NULL, "types", (char)5, enumeration, enabled) == 0);
    assert(edb->facts[6].arity == 3u);
    assert(edb->facts[6].terms[0].kind == MAELYS_DATALOG_VALUE_INTEGER);
    assert(edb->facts[6].terms[1].as.integer == 6);
    assert(edb->facts[6].terms[2].kind == MAELYS_DATALOG_VALUE_BOOLEAN);
    assert(edb->facts[6].terms[2].as.boolean == 1);
    assert(MAELYS_DATALOG_ADD_FACT(edb, NULL, "bools", true, 1 < 2,
                                 MAELYS_DATALOG_BOOL(true), MAELYS_DATALOG_BOOL(0)) == 0);
    assert(edb->facts[7].terms[0].kind == MAELYS_DATALOG_VALUE_INTEGER);
    assert(edb->facts[7].terms[1].kind == MAELYS_DATALOG_VALUE_INTEGER);
    assert(edb->facts[7].terms[2].kind == MAELYS_DATALOG_VALUE_BOOLEAN && edb->facts[7].terms[2].as.boolean == 1);
    assert(edb->facts[7].terms[3].kind == MAELYS_DATALOG_VALUE_BOOLEAN && edb->facts[7].terms[3].as.boolean == 0);
    memcpy(snapshot, arena.bytes, sizeof(snapshot));
    assert(MAELYS_DATALOG_ADD_FACT(edb, &diag, "full", 1) == MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
    assert(memcmp(snapshot, arena.bytes, sizeof(snapshot)) == 0);
    assert(maelys_datalog_input_edb_clear(edb) == 0);

    assert(MAELYS_DATALOG_ADD_FACT(counted_edb(edb), counted_diagnostic(&diag), counted_predicate(),
        counted_symbol(), counted_integer(), counted_unsigned(), MAELYS_DATALOG_BOOL(counted_boolean())) == 0);
    for (size_t i = 0; i < 7; ++i) assert(argument_calls[i] == 1u);
    assert(edb->facts[0].terms[1].as.integer == -7);
    assert(edb->facts[0].terms[2].as.integer == 9);
    assert(edb->facts[0].terms[3].kind == MAELYS_DATALOG_VALUE_BOOLEAN);
    assert(edb->facts[0].terms[3].as.boolean == 1);

    /* Range rejection precedes insertion, including writes into unused bytes. */
    memcpy(snapshot, arena.bytes, sizeof(snapshot));
    assert(MAELYS_DATALOG_ADD_FACT(edb, &diag, "bad", (uint64_t)INT64_MAX + 1u) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    assert(strstr(diag.message, "term 0") && strstr(diag.message, "INT64_MIN..INT64_MAX"));
    assert(strcmp(diag.phase, "input") == 0 && diag.code == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    assert(diag.line == 0 && diag.column == 0);
    assert(memcmp(snapshot, arena.bytes, sizeof(snapshot)) == 0);
    assert(MAELYS_DATALOG_ADD_FACT(edb, &diag, "bad", "unused", UINT64_MAX) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    assert(strstr(diag.message, "term 1"));
    assert(memcmp(snapshot, arena.bytes, sizeof(snapshot)) == 0);
    assert(MAELYS_DATALOG_ADD_FACT(edb, &diag, "bad", 1, 2, UINTMAX_MAX) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    assert(strstr(diag.message, "term 2"));
    assert(memcmp(snapshot, arena.bytes, sizeof(snapshot)) == 0);
    assert(MAELYS_DATALOG_ADD_FACT(edb, &diag, "bad", 1, 2, 3, ULLONG_MAX) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    assert(strstr(diag.message, "term 3"));
    assert(memcmp(snapshot, arena.bytes, sizeof(snapshot)) == 0);
    assert(MAELYS_DATALOG_ADD_FACT(edb, NULL, "bad", UINT64_MAX) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    assert(memcmp(snapshot, arena.bytes, sizeof(snapshot)) == 0);
    assert(MAELYS_DATALOG_ADD_FACT(edb, &diag, "bad", (const char *)NULL) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    assert(strstr(diag.message, "NULL string"));
    assert(memcmp(snapshot, arena.bytes, sizeof(snapshot)) == 0);
    assert(MAELYS_DATALOG_ADD_FACT(NULL, &diag, "bad", 1) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    assert(MAELYS_DATALOG_ADD_FACT(edb, &diag, "good", 42) == 0 && diag.code == 0);
    assert(maelys_datalog_input_edb_free(edb) == 0);
    assert(allocations == 0u && releases == 0u);
    puts("C11 fact builders: inferred types, arities 0..4, single evaluation, checked ranges, atomic rejection, no allocations");
}

static void test_c11_batch_builders(void) {
    union { max_align_t align; unsigned char bytes[8192]; } arena;
    unsigned char snapshot[sizeof(arena.bytes)];
    maelys_datalog_input_edb_t *edb = NULL;
    maelys_datalog_public_diagnostic_t diag;
    forbid_allocations = 1;
    memset(arena.bytes, 0xA5, sizeof(arena.bytes));
    assert(maelys_datalog_input_edb_init(arena.bytes, sizeof(arena.bytes), 8u, 256u, &edb) == 0);
    assert(MAELYS_DATALOG_ADD_FACT(edb, NULL, "existing", "preserved") == 0);
    memcpy(snapshot, arena.bytes, sizeof(snapshot));
    /* Building alone never inserts. Descriptors borrow until batch submission. */
    (void)MAELYS_DATALOG_FACT("user", "alice");
    assert(memcmp(snapshot, arena.bytes, sizeof(snapshot)) == 0);
    memset(argument_calls, 0, sizeof(argument_calls));
    char predicate[] = "owns", owner[] = "bob";
    assert(MAELYS_DATALOG_ADD_FACTS(counted_edb(edb), counted_diagnostic(&diag),
        MAELYS_DATALOG_FACT("ready"),
        MAELYS_DATALOG_FACT("user", "alice"),
        MAELYS_DATALOG_FACT(predicate, owner, "roadmap.pdf"),
        MAELYS_DATALOG_FACT("bounds", INT64_MIN, (uint64_t)INT64_MAX, MAELYS_DATALOG_BOOL(0)),
        MAELYS_DATALOG_FACT(counted_predicate(), counted_symbol(), counted_integer(),
            counted_unsigned(), MAELYS_DATALOG_BOOL(counted_boolean()))) == 0);
    for (size_t i = 0u; i < 7u; ++i) assert(argument_calls[i] == 1u);
    size_t count = 0u;
    assert(maelys_datalog_input_edb_count(edb, &count) == 0 && count == 6u && diag.code == 0);
    assert(edb->facts[1].arity == 0u);
    assert(edb->facts[2].arity == 1u);
    assert(edb->facts[3].arity == 2u);
    assert(edb->facts[4].arity == 3u);
    assert(edb->facts[5].arity == 4u);
    assert(edb->facts[4].terms[0].as.integer == INT64_MIN);
    assert(edb->facts[4].terms[1].as.integer == INT64_MAX);
    assert(edb->facts[4].terms[2].kind == MAELYS_DATALOG_VALUE_BOOLEAN);
    assert(edb->facts[4].terms[2].as.boolean == 0);
    assert(edb->facts[5].terms[1].as.integer == -7);
    assert(edb->facts[5].terms[2].as.integer == 9);
    assert(edb->facts[5].terms[3].kind == MAELYS_DATALOG_VALUE_BOOLEAN);
    assert(edb->facts[5].terms[3].as.boolean == 1);
    predicate[0] = 'x'; owner[0] = 'x';
    assert(strcmp(edb->facts[3].predicate, "owns") == 0);
    assert(strcmp(edb->facts[3].terms[0].as.symbol, "bob") == 0);
    memcpy(snapshot, arena.bytes, sizeof(snapshot));
    /* A late failure cannot append earlier valid facts, even unused bytes. */
    assert(MAELYS_DATALOG_ADD_FACTS(edb, &diag,
        MAELYS_DATALOG_FACT("valid", 1),
        MAELYS_DATALOG_FACT("bad", "unused", UINT64_MAX)) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    assert(strstr(diag.message, "Fact 1, term 1:"));
    assert(memcmp(snapshot, arena.bytes, sizeof(snapshot)) == 0);
    assert(MAELYS_DATALOG_ADD_FACTS(edb, NULL,
        MAELYS_DATALOG_FACT("valid", 1),
        MAELYS_DATALOG_FACT("bad", UINT64_MAX)) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    assert(memcmp(snapshot, arena.bytes, sizeof(snapshot)) == 0);
    assert(MAELYS_DATALOG_ADD_FACTS(edb, &diag,
        MAELYS_DATALOG_FACT("valid", 1),
        MAELYS_DATALOG_FACT("bad", (const char *)NULL)) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    assert(strstr(diag.message, "Fact 1") && strstr(diag.message, "NULL string"));
    assert(memcmp(snapshot, arena.bytes, sizeof(snapshot)) == 0);
    assert(MAELYS_DATALOG_ADD_FACTS(edb, &diag,
        MAELYS_DATALOG_FACT("valid", 1),
        MAELYS_DATALOG_FACT((const char *)NULL, 2)) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    assert(memcmp(snapshot, arena.bytes, sizeof(snapshot)) == 0);
    assert(MAELYS_DATALOG_ADD_FACTS(edb, &diag,
        MAELYS_DATALOG_FACT("fits", 1), MAELYS_DATALOG_FACT("fits", 2),
        MAELYS_DATALOG_FACT("overflow", 3)) == MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
    assert(memcmp(snapshot, arena.bytes, sizeof(snapshot)) == 0);
    /* Range errors are checked before the native capacity check. */
    assert(MAELYS_DATALOG_ADD_FACTS(edb, &diag,
        MAELYS_DATALOG_FACT("ok"), MAELYS_DATALOG_FACT("ok"),
        MAELYS_DATALOG_FACT("ok"), MAELYS_DATALOG_FACT("ok"),
        MAELYS_DATALOG_FACT("ok"), MAELYS_DATALOG_FACT("ok"),
        MAELYS_DATALOG_FACT("ok"), MAELYS_DATALOG_FACT("ok"),
        MAELYS_DATALOG_FACT("ok"), MAELYS_DATALOG_FACT("ok"),
        MAELYS_DATALOG_FACT("bad", 1, 2, 3, UINT64_MAX)) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    assert(strstr(diag.message, "Fact 10, term 3:"));
    assert(memcmp(snapshot, arena.bytes, sizeof(snapshot)) == 0);
    assert(MAELYS_DATALOG_ADD_FACTS(NULL, &diag,
        MAELYS_DATALOG_FACT("user", "alice")) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    assert(maelys_datalog_input_edb_clear(edb) == 0);
    assert(MAELYS_DATALOG_ADD_FACTS(edb, &diag, MAELYS_DATALOG_FACT("user", "alice")) == 0);
    assert(diag.code == 0 && maelys_datalog_input_edb_count(edb, &count) == 0 && count == 1u);
    assert(maelys_datalog_input_edb_free(edb) == 0);

    /* A valid leading fact followed by text exhaustion is also atomic. */
    assert(maelys_datalog_input_edb_init(arena.bytes, sizeof(arena.bytes), 8u, 16u, &edb) == 0);
    memcpy(snapshot, arena.bytes, sizeof(snapshot));
    assert(MAELYS_DATALOG_ADD_FACTS(edb, &diag,
        MAELYS_DATALOG_FACT("x", "a"),
        MAELYS_DATALOG_FACT("y", "this string exceeds the text capacity")) == MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
    assert(memcmp(snapshot, arena.bytes, sizeof(snapshot)) == 0);
    assert(maelys_datalog_input_edb_free(edb) == 0);
    assert(allocations == 0u && releases == 0u);
    puts("C11 batch builders: arities 0..4, exactly-once arguments, copied strings, indexed diagnostics, atomic capacity/type rejection, no allocations");
}

static void check_regime(size_t text_capacity, int indexed) {
    union { max_align_t align; unsigned char bytes[8192]; } arena;
    unsigned char snapshot[sizeof(arena.bytes)];
    memset(arena.bytes, 0xA5, sizeof(arena.bytes));
    maelys_datalog_input_edb_t *edb;
    assert(!maelys_datalog_input_edb_init(arena.bytes, sizeof(arena.bytes), 8u, text_capacity, &edb));
    assert(!!edb->index_slots == indexed);
    assert(!!edb->index == indexed && !!edb->pending == indexed && !!edb->generations == indexed);
    maelys_datalog_fact_t batch[2] = {0};
    batch[0].predicate = "p"; batch[0].arity = 1u;
    batch[0].terms[0].kind = MAELYS_DATALOG_VALUE_SYMBOL;
    batch[0].terms[0].as.symbol = "p";
    batch[1] = batch[0]; batch[1].predicate = "x";
    for (size_t round = 0u; round < 3u; ++round) {
        assert(!maelys_datalog_input_edb_add_facts(edb, batch, 2u, NULL));
        assert(edb->text_used == 4u);
        assert(edb->facts[0].predicate == edb->facts[1].terms[0].as.symbol);
        maelys_datalog_fact_t bad[2] = {batch[0], batch[1]};
        bad[0].predicate = "new";
        bad[1].terms[0].as.symbol = NULL;
        memcpy(snapshot, arena.bytes, sizeof(snapshot));
        assert(maelys_datalog_input_edb_add_facts(edb, bad, 2u, NULL) != 0);
        assert(!memcmp(snapshot, arena.bytes, sizeof(snapshot)));
        bad[1].terms[0].as.symbol = "more-than-the-entire-linear-arena";
        if (!indexed) {
            assert(maelys_datalog_input_edb_add_facts(edb, bad, 2u, NULL) == MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
            assert(!memcmp(snapshot, arena.bytes, sizeof(snapshot)));
        }
        assert(!maelys_datalog_input_edb_add_facts(edb, batch, 2u, NULL));
        assert(edb->text_used == 4u && edb->count == 4u);
        assert(!maelys_datalog_input_edb_clear(edb));
    }
    assert(!maelys_datalog_input_edb_free(edb));
}

static void check_rollback_prefixes(void) {
    union { max_align_t align; unsigned char bytes[8192]; } arena;
    unsigned char snapshot[sizeof(arena.bytes)];
    memset(arena.bytes, 0xA5, sizeof(arena.bytes));
    maelys_datalog_input_edb_t *edb;
    assert(!maelys_datalog_input_edb_init(arena.bytes, sizeof(arena.bytes), 16u, 512u, &edb));
    maelys_datalog_fact_t batch[8] = {0}, bad[8];
    const char *names[] = {"a", "b", "c", "d", "e", "f", "g", "h"};
    for (size_t i = 0; i < 8u; ++i) {
        batch[i].predicate = names[i]; batch[i].arity = 4u;
        batch[i].terms[0].kind = MAELYS_DATALOG_VALUE_SYMBOL;
        batch[i].terms[0].as.symbol = names[(i + 1u) % 8u];
        batch[i].terms[1].kind = MAELYS_DATALOG_VALUE_INTEGER;
        batch[i].terms[1].as.integer = 17;
        batch[i].terms[2].kind = MAELYS_DATALOG_VALUE_BOOLEAN;
        batch[i].terms[2].as.boolean = 1;
        batch[i].terms[3].kind = MAELYS_DATALOG_VALUE_SYMBOL;
        batch[i].terms[3].as.symbol = "shared";
    }
    for (size_t round = 0; round < 3u; ++round) {
        assert(!maelys_datalog_input_edb_add_fact(edb, "kept", NULL, 0u, NULL));
        for (size_t i = 0; i < 8u; ++i) for (size_t field = 0; field < 6u; ++field) {
            memcpy(bad, batch, sizeof(bad));
            if (!field) bad[i].predicate = NULL;
            else if (field == 5u) bad[i].arity = MAELYS_DATALOG_MAX_TERMS + 1u;
            else bad[i].terms[field - 1u].kind = (maelys_datalog_value_kind_t)99;
            memcpy(snapshot, arena.bytes, sizeof(snapshot));
            assert(maelys_datalog_input_edb_add_facts(edb, bad, 8u, NULL) != 0);
            assert(!memcmp(snapshot, arena.bytes, sizeof(snapshot)));
        }
        assert(!maelys_datalog_input_edb_add_facts(edb, batch, 8u, NULL));
        assert(edb->facts[1].terms[0].as.symbol == edb->facts[2].predicate);
        assert(!maelys_datalog_input_edb_clear(edb));
    }
    assert(!maelys_datalog_input_edb_free(edb));
}

int main(void) {
    test_c11_fact_builders();
    test_c11_batch_builders();
    forbid_allocations = 1;
    assert(INPUT_INDEX_THRESHOLD == 16u);
    assert(input_index_slots(8u, 30u) == 0u); /* D=15 */
    assert(input_index_slots(8u, 31u) != 0u); /* D=16, ceil for empty string */
    check_regime(16u, 0);
    check_regime(128u, 1);
    check_rollback_prefixes();
    assert(allocations == 0u && releases == 0u);
    assert(input_distinct_bound(8u, 1u) == 1u);
    assert(input_distinct_bound(8u, 3u) == 2u);
    assert(input_distinct_bound(8u, 256u) == 40u);
    assert(input_distinct_bound(MAELYS_DATALOG_MAX_EDB_FACTS, 128u) == 64u);
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
    assert(bytes == facts_offset() + 8u * sizeof(maelys_datalog_fact_t) +
        3u * edb->index_slots + 2u * input_distinct_bound(8u, 256u) + 256u);
    char predicate[] = "seen", symbol[] = "alice";
    maelys_datalog_value_t value = {.kind = MAELYS_DATALOG_VALUE_SYMBOL, .as.symbol = symbol};
    assert(maelys_datalog_input_edb_add_fact(edb, predicate, &value, 1u, NULL) == 0);
    predicate[0] = 'x'; symbol[0] = 'x';
    assert(strcmp(edb->facts[0].predicate, "seen") == 0);
    assert(strcmp(edb->facts[0].terms[0].as.symbol, "alice") == 0);
    maelys_datalog_fact_t batch[2] = {0};
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

    /* The empty byte string costs one byte. With three text bytes, two
     * distinct strings fit: a text_capacity/2 bound would undercount them.
     * Predicate/domain semantics remain the solver's responsibility. */
    assert(maelys_datalog_input_edb_init(arena.bytes, sizeof(arena.bytes), 2u, 1u, &edb) == 0);
    assert(maelys_datalog_input_edb_add_fact(edb, "", NULL, 0u, NULL) == 0);
    assert(edb->text_used == 1u);
    assert(maelys_datalog_input_edb_free(edb) == 0);
    assert(maelys_datalog_input_edb_init(arena.bytes, sizeof(arena.bytes), 2u, 3u, &edb) == 0);
    value.as.symbol = "";
    assert(maelys_datalog_input_edb_add_fact(edb, "p", &value, 1u, NULL) == 0);
    assert(edb->text_used == 3u);
    memcpy(snapshot, arena.bytes, sizeof(snapshot));
    value.as.symbol = "q";
    assert(maelys_datalog_input_edb_add_fact(edb, "p", &value, 1u, &diag) != 0);
    assert(memcmp(snapshot, arena.bytes, sizeof(snapshot)) == 0);
    assert(maelys_datalog_input_edb_free(edb) == 0);
    assert(allocations == 0u && releases == 0u);
    value.as.symbol = "x";

    /* Exactly D=16 strings in 31 bytes: empty plus fifteen one-byte names.
     * This is indexed, fills its entire D-entry journal and its text arena. */
    assert(!maelys_datalog_input_edb_init(arena.bytes, sizeof(arena.bytes), 8u, 31u, &edb));
    char tiny_names[16][2] = {{0}};
    maelys_datalog_fact_t tiny_batch[4] = {0};
    for (size_t i = 1u; i < 16u; ++i) tiny_names[i][0] = (char)('a' + i - 1u);
    for (size_t i = 0u; i < 4u; ++i) {
        tiny_batch[i].predicate = tiny_names[4u * i];
        tiny_batch[i].arity = 3u;
        for (size_t j = 0u; j < 3u; ++j) {
            tiny_batch[i].terms[j].kind = MAELYS_DATALOG_VALUE_SYMBOL;
            tiny_batch[i].terms[j].as.symbol = tiny_names[4u * i + j + 1u];
        }
    }
    assert(!maelys_datalog_input_edb_add_facts(edb, tiny_batch, 4u, NULL));
    assert(edb->text_used == 31u && edb->index_slots == 32u);
    memcpy(snapshot, arena.bytes, sizeof(snapshot));
    assert(maelys_datalog_input_edb_add_fact(edb, "z", NULL, 0u, NULL) != 0);
    assert(!memcmp(snapshot, arena.bytes, sizeof(snapshot)));
    assert(!maelys_datalog_input_edb_free(edb));

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

    /* Deliberate hash collisions, including rollback in the middle of an
     * existing probe chain. Every arena byte (index and journal included)
     * must be restored, and surviving entries must remain discoverable. */
    assert(maelys_datalog_input_edb_init(arena.bytes, sizeof(arena.bytes), 8u, 256u, &edb) == 0);
    char colliding[7][24];
    size_t found = 0u;
    for (size_t candidate = 0; found < 7u; ++candidate) {
        char name[24]; snprintf(name, sizeof(name), "collision-%zu", candidate);
        /* Empty index: text_slot returns the initial bucket. */
        if (text_slot(edb, name, NULL) == edb->index_slots - 1u)
            strcpy(colliding[found++], name);
    }
    value.as.symbol = colliding[1];
    assert(maelys_datalog_input_edb_add_fact(edb, colliding[0], &value, 1u, NULL) == 0);
    batch[0].predicate = colliding[2]; batch[0].arity = 1;
    batch[0].terms[0] = value; batch[0].terms[0].as.symbol = colliding[3];
    batch[1] = batch[0]; batch[1].predicate = colliding[4];
    batch[1].terms[0].as.symbol = NULL;
    memcpy(snapshot, arena.bytes, sizeof(snapshot));
    assert(maelys_datalog_input_edb_add_facts(edb, batch, 2u, &diag) != 0);
    assert(memcmp(snapshot, arena.bytes, sizeof(snapshot)) == 0);
    batch[1].arity = MAELYS_DATALOG_MAX_TERMS + 1u;
    assert(maelys_datalog_input_edb_add_facts(edb, batch, 2u, &diag) != 0);
    assert(memcmp(snapshot, arena.bytes, sizeof(snapshot)) == 0);
    batch[1].arity = 1u; batch[1].terms[0].as.symbol = colliding[5];
    assert(maelys_datalog_input_edb_add_facts(edb, batch, 2u, &diag) == 0);
    assert(!strcmp(edb->facts[0].predicate, colliding[0]));
    assert(!strcmp(edb->facts[1].predicate, colliding[2]));
    assert(!strcmp(edb->facts[2].terms[0].as.symbol, colliding[5]));
    value.as.symbol = colliding[1];
    assert(maelys_datalog_input_edb_add_fact(edb, colliding[0], &value, 1u, NULL) == 0);
    assert(edb->facts[0].predicate == edb->facts[3].predicate);
    assert(edb->facts[0].terms[0].as.symbol == edb->facts[3].terms[0].as.symbol);
    /* Stale colliding slots and a real generation wrap: rejected batches must
     * restore even obsolete metadata, not just the logical contents. */
    for (size_t round = 0; round < 256u; ++round) {
        uint8_t previous = edb->generation;
        assert(maelys_datalog_input_edb_clear(edb) == 0);
        assert(edb->generation == (previous == UINT8_MAX ? 1u : previous + 1u));
        batch[1].terms[0].as.symbol = NULL;
        memcpy(snapshot, arena.bytes, sizeof(snapshot));
        assert(maelys_datalog_input_edb_add_facts(edb, batch, 2u, &diag) != 0);
        assert(memcmp(snapshot, arena.bytes, sizeof(snapshot)) == 0);
        batch[1].terms[0].as.symbol = colliding[5];
        assert(maelys_datalog_input_edb_add_facts(edb, batch, 2u, NULL) == 0);
        assert(edb->count == 2u && !strcmp(edb->facts[0].predicate, colliding[2]));
    }
    assert(maelys_datalog_input_edb_free(edb) == 0);

    /* The committed offset must use all 16 bits, not a 15-bit payload with
     * a pending flag. Keep the allocator disabled even at the profile limit. */
    static union { max_align_t align; unsigned char bytes[512000]; } full;
    assert(maelys_datalog_input_edb_init(full.bytes, sizeof(full.bytes),
        MAELYS_DATALOG_MAX_EDB_FACTS, MAELYS_DATALOG_INPUT_EDB_TEXT_BYTES, &edb) == 0);
    size_t default_bound = input_distinct_bound(MAELYS_DATALOG_MAX_EDB_FACTS, MAELYS_DATALOG_INPUT_EDB_TEXT_BYTES);
    size_t table_bytes = 3u * edb->index_slots + 2u * default_bound;
    assert(table_bytes == (MAELYS_DATALOG_MAX_EDB_FACTS == 1024u ? 58u : 116u) * 1024u);
    assert(table_bytes < (edb->index_slots + MAELYS_DATALOG_MAX_EDB_FACTS * INPUT_STRINGS_PER_FACT) * sizeof(uint32_t));
    for (size_t i = 0; i < 800u; ++i) {
        char name[48];
        snprintf(name, sizeof(name), "symbol-%04zu-abcdefghijklmnopqrstuvwxyz-012345678", i);
        assert(maelys_datalog_input_edb_add_fact(edb, name, NULL, 0u, NULL) == 0);
    }
    assert(edb->text_used > 32768u);
    const char *last = edb->facts[799].predicate;
    char last_copy[48]; strcpy(last_copy, last);
    assert(maelys_datalog_input_edb_add_fact(edb, last_copy, NULL, 0u, NULL) == 0);
    assert(edb->facts[800].predicate == last);
    assert(!maelys_datalog_input_edb_clear(edb));
    /* Highest pending ordinal in a maximum-sized batch, not merely a small
     * ordinal in a default-sized buffer. A failed preflight must undo stale
     * committed entries before the successful retry. */
    static maelys_datalog_fact_t maximum[MAELYS_DATALOG_MAX_EDB_FACTS];
    for (size_t i = 0u; i < MAELYS_DATALOG_MAX_EDB_FACTS; ++i) maximum[i].predicate = "p";
    maelys_datalog_fact_t *tail = &maximum[MAELYS_DATALOG_MAX_EDB_FACTS - 1u];
    tail->arity = MAELYS_DATALOG_MAX_TERMS;
    for (size_t j = 0u; j < tail->arity; ++j) tail->terms[j].kind = MAELYS_DATALOG_VALUE_INTEGER;
    tail->terms[tail->arity - 1u].kind = MAELYS_DATALOG_VALUE_SYMBOL;
    tail->terms[tail->arity - 1u].as.symbol = NULL;
    static unsigned char full_snapshot[sizeof(full.bytes)];
    memcpy(full_snapshot, full.bytes, sizeof(full.bytes));
    assert(maelys_datalog_input_edb_add_facts(edb, maximum, MAELYS_DATALOG_MAX_EDB_FACTS, NULL) != 0);
    assert(!memcmp(full_snapshot, full.bytes, sizeof(full.bytes)));
    tail->terms[tail->arity - 1u].as.symbol = "last-ordinal";
    assert(!maelys_datalog_input_edb_add_facts(edb, maximum, MAELYS_DATALOG_MAX_EDB_FACTS, NULL));
    assert(!strcmp(edb->facts[MAELYS_DATALOG_MAX_EDB_FACTS - 1u].terms[tail->arity - 1u].as.symbol, "last-ordinal"));
    assert(maelys_datalog_input_edb_free(edb) == 0);
    assert(allocations == 0u && releases == 0u);

    /* One construction allocation, none during append/clear, one release. */
    batch[0].predicate = "p"; batch[0].arity = 1; value.as.symbol = "x";
    batch[0].terms[0] = value; batch[1] = batch[0];
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
