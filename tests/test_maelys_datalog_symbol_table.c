#include "src/core/maelys_datalog_symbol_table.h"
#include "tests/helpers/test_framework.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t test_fnv1a(const char *s, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= (unsigned char)s[i];
        h *= 16777619u;
    }
    return h;
}

static size_t test_symbol_bucket(const char *s, size_t len) {
    return (size_t)test_fnv1a(s, len) & (MAELYS_DATALOG_SYMBOL_INDEX_BUCKETS - 1u);
}

static size_t test_symbol_insert_bucket(const maelys_datalog_symbol_table_t *table,
                                        const char *s,
                                        size_t len) {
    size_t bucket = test_symbol_bucket(s, len);
    for (size_t probes = 0; probes < MAELYS_DATALOG_SYMBOL_INDEX_BUCKETS; probes++) {
        if (table->index[bucket] == 0u) return bucket;
        bucket = (bucket + 1u) & (MAELYS_DATALOG_SYMBOL_INDEX_BUCKETS - 1u);
    }
    return MAELYS_DATALOG_SYMBOL_INDEX_BUCKETS;
}

static int find_bucket_collisions(char out[3][32]) {
    const char *anchor = "collision-anchor";
    size_t target = test_symbol_bucket(anchor, strlen(anchor));
    size_t found = 0;
    snprintf(out[found++], 32, "%s", anchor);
    for (size_t i = 0; i < 200000u && found < 3u; i++) {
        char candidate[32];
        int n = snprintf(candidate, sizeof(candidate), "collision-%zu", i);
        if (n < 0 || (size_t)n >= sizeof(candidate)) continue;
        if (strcmp(candidate, anchor) == 0) continue;
        if (test_symbol_bucket(candidate, strlen(candidate)) == target) {
            snprintf(out[found++], 32, "%s", candidate);
        }
    }
    return found == 3u;
}

static void make_long_symbol(char *buf, size_t i) {
    memset(buf, 'x', MAELYS_DATALOG_MAX_STRING_BYTES - 1u);
    buf[0] = (char)('A' + (char)(i % 26u));
    buf[1] = (char)('0' + (char)((i / 26u) % 10u));
}

static int test_symbol_table_interns_and_dedupes(void) {
    TEST_BEGIN();
    maelys_datalog_symbol_table_t t;
    maelys_datalog_symbol_table_init(&t);
    maelys_datalog_symbol_id_t a, b, c;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_symbol_intern(&t, "alpha", 5, &a), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_symbol_intern(&t, "alpha", 5, &b), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_symbol_intern(&t, "beta", 4, &c), "%d");
    TEST_ASSERT_EQUAL(a, b, "%u");
    TEST_ASSERT_TRUE(a != c);
    TEST_ASSERT_EQUAL_STRING("alpha", maelys_datalog_symbol_text(&t, a));
    TEST_END();
}

static int test_symbol_table_ids_are_deterministic(void) {
    TEST_BEGIN();
    maelys_datalog_symbol_table_t t;
    maelys_datalog_symbol_table_init(&t);
    maelys_datalog_symbol_id_t alice, bob, carol, alice_again;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_symbol_intern(&t, "alice", 5, &alice), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_symbol_intern(&t, "bob", 3, &bob), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_symbol_intern(&t, "carol", 5, &carol), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_symbol_intern(&t, "alice", 5, &alice_again),
                      "%d");
    TEST_ASSERT_EQUAL((maelys_datalog_symbol_id_t)1u, alice, "%u");
    TEST_ASSERT_EQUAL((maelys_datalog_symbol_id_t)2u, bob, "%u");
    TEST_ASSERT_EQUAL((maelys_datalog_symbol_id_t)3u, carol, "%u");
    TEST_ASSERT_EQUAL(alice, alice_again, "%u");
    TEST_ASSERT_EQUAL((size_t)3u, t.count, "%zu");
    TEST_END();
}

static int test_symbol_table_collision_probing(void) {
    TEST_BEGIN();
    char values[3][32];
    TEST_ASSERT_TRUE(find_bucket_collisions(values));
    TEST_ASSERT_EQUAL(test_symbol_bucket(values[0], strlen(values[0])),
                      test_symbol_bucket(values[1], strlen(values[1])),
                      "%zu");
    TEST_ASSERT_EQUAL(test_symbol_bucket(values[0], strlen(values[0])),
                      test_symbol_bucket(values[2], strlen(values[2])),
                      "%zu");

    maelys_datalog_symbol_table_t t;
    maelys_datalog_symbol_table_init(&t);
    maelys_datalog_symbol_id_t ids[3], again;
    for (size_t i = 0; i < 3u; i++) {
        TEST_ASSERT_EQUAL(MAELYS_OK,
                          maelys_datalog_symbol_intern(&t, values[i], strlen(values[i]),
                                                       &ids[i]),
                          "%d");
        TEST_ASSERT_EQUAL((maelys_datalog_symbol_id_t)(i + 1u), ids[i], "%u");
    }
    for (size_t i = 0; i < 3u; i++) {
        TEST_ASSERT_EQUAL(MAELYS_OK,
                          maelys_datalog_symbol_intern(&t, values[i], strlen(values[i]),
                                                       &again),
                          "%d");
        TEST_ASSERT_EQUAL(ids[i], again, "%u");
    }
    TEST_ASSERT_EQUAL((size_t)3u, t.count, "%zu");
    TEST_END();
}

static int test_symbol_table_overflow_fails(void) {
    TEST_BEGIN();
    maelys_datalog_symbol_table_t t;
    maelys_datalog_symbol_table_init(&t);
    maelys_datalog_symbol_id_t id;
    char buf[32];
    maelys_result_t last = MAELYS_OK;
    for (size_t i = 0; i <= MAELYS_DATALOG_MAX_SYMBOLS; i++) {
        snprintf(buf, sizeof(buf), "sym%zu", i);
        last = maelys_datalog_symbol_intern(&t, buf, strlen(buf), &id);
        if (last != MAELYS_OK) break;
    }
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE, last, "%d");
    size_t count_before = t.count;
    size_t used_before = t.used;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_symbol_intern(&t, "sym0", 4, &id), "%d");
    TEST_ASSERT_EQUAL((maelys_datalog_symbol_id_t)1u, id, "%u");
    TEST_ASSERT_EQUAL(count_before, t.count, "%zu");
    TEST_ASSERT_EQUAL(used_before, t.used, "%zu");
    TEST_END();
}

static int test_symbol_table_fresh_index_and_text_readonly(void) {
    TEST_BEGIN();
    maelys_datalog_symbol_table_t t;
    maelys_datalog_symbol_table_init(&t);
    for (size_t i = 0; i < MAELYS_DATALOG_SYMBOL_INDEX_BUCKETS; i++) {
        TEST_ASSERT_EQUAL((uint16_t)0u, t.index[i], "%u");
    }
    maelys_datalog_symbol_id_t id;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_symbol_intern(&t, "first", 5, &id), "%d");
    TEST_ASSERT_EQUAL((maelys_datalog_symbol_id_t)1u, id, "%u");
    TEST_ASSERT_EQUAL((uint16_t)1u, t.index[test_symbol_bucket("first", 5)], "%u");
    TEST_ASSERT_TRUE(maelys_datalog_symbol_id_is_valid(&t, id));
    TEST_ASSERT_EQUAL_STRING("first", maelys_datalog_symbol_text(&t, id));
    TEST_END();
}

static int test_symbol_table_pool_refusal_does_not_write_bucket(void) {
    TEST_BEGIN();
    maelys_datalog_symbol_table_t t;
    maelys_datalog_symbol_table_init(&t);
    maelys_datalog_symbol_id_t id;
    char long_symbol[MAELYS_DATALOG_MAX_STRING_BYTES];
    for (size_t i = 0; i < 32u; i++) {
        make_long_symbol(long_symbol, i);
        TEST_ASSERT_EQUAL(MAELYS_OK,
                          maelys_datalog_symbol_intern(&t, long_symbol,
                                                       MAELYS_DATALOG_MAX_STRING_BYTES - 1u,
                                                       &id),
                          "%d");
    }
    TEST_ASSERT_EQUAL(sizeof(t.storage), t.used, "%zu");

    const char *rejected = "rejected-after-pool-full";
    size_t rejected_len = strlen(rejected);
    size_t rejected_bucket = test_symbol_insert_bucket(&t, rejected, rejected_len);
    TEST_ASSERT_TRUE(rejected_bucket < MAELYS_DATALOG_SYMBOL_INDEX_BUCKETS);
    TEST_ASSERT_EQUAL((uint16_t)0u, t.index[rejected_bucket], "%u");
    size_t count_before = t.count;
    size_t used_before = t.used;
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE,
                      maelys_datalog_symbol_intern(&t, rejected, rejected_len, &id),
                      "%d");
    TEST_ASSERT_EQUAL(count_before, t.count, "%zu");
    TEST_ASSERT_EQUAL(used_before, t.used, "%zu");
    TEST_ASSERT_EQUAL((uint16_t)0u, t.index[rejected_bucket], "%u");
    TEST_END();
}

static int test_symbol_table_copy_preserves_index(void) {
    TEST_BEGIN();
    maelys_datalog_symbol_table_t a;
    maelys_datalog_symbol_table_init(&a);
    maelys_datalog_symbol_id_t alice, bob, again;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_symbol_intern(&a, "alice", 5, &alice), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_symbol_intern(&a, "bob", 3, &bob), "%d");
    maelys_datalog_symbol_table_t b = a;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_symbol_intern(&b, "alice", 5, &again),
                      "%d");
    TEST_ASSERT_EQUAL(alice, again, "%u");
    TEST_ASSERT_EQUAL(a.count, b.count, "%zu");
    TEST_ASSERT_EQUAL_STRING("bob", maelys_datalog_symbol_text(&b, bob));
    TEST_END();
}

static int test_symbol_table_probe_guard_returns_invalid_state(void) {
    TEST_BEGIN();
    maelys_datalog_symbol_table_t t;
    maelys_datalog_symbol_table_init(&t);
    maelys_datalog_symbol_id_t id;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_symbol_intern(&t, "seed", 4, &id), "%d");
    for (size_t i = 0; i < MAELYS_DATALOG_SYMBOL_INDEX_BUCKETS; i++) {
        t.index[i] = 1u;
    }
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_STATE,
                      maelys_datalog_symbol_intern(&t, "missing", 7, &id),
                      "%d");
    TEST_END();
}

int main(int argc, char **argv) {
    test_case_t cases[] = {
        {"maelys_datalog_symbol_table/interns_and_dedupes", TEST_MODE_NON_BLOCKING, test_symbol_table_interns_and_dedupes},
        {"maelys_datalog_symbol_table/ids_are_deterministic", TEST_MODE_NON_BLOCKING, test_symbol_table_ids_are_deterministic},
        {"maelys_datalog_symbol_table/collision_probing", TEST_MODE_NON_BLOCKING, test_symbol_table_collision_probing},
        {"maelys_datalog_symbol_table/overflow_fails", TEST_MODE_NON_BLOCKING, test_symbol_table_overflow_fails},
        {"maelys_datalog_symbol_table/fresh_index_and_text_readonly", TEST_MODE_NON_BLOCKING, test_symbol_table_fresh_index_and_text_readonly},
        {"maelys_datalog_symbol_table/pool_refusal_does_not_write_bucket", TEST_MODE_NON_BLOCKING, test_symbol_table_pool_refusal_does_not_write_bucket},
        {"maelys_datalog_symbol_table/copy_preserves_index", TEST_MODE_NON_BLOCKING, test_symbol_table_copy_preserves_index},
        {"maelys_datalog_symbol_table/probe_guard_returns_invalid_state", TEST_MODE_NON_BLOCKING, test_symbol_table_probe_guard_returns_invalid_state},
    };
    return test_main("maelys_datalog_symbol_table", cases, (int)(sizeof(cases) / sizeof(cases[0])), argc, argv);
}
