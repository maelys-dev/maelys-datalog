/* SPDX-License-Identifier: MPL-2.0 */
#include "src/core/maelys_datalog_sort_internal.h"
#include "src/core/maelys_datalog_edb.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t comparisons;
static int integer_cmp(const int *a, const int *b) {
    ++comparisons;
    return (*a > *b) - (*a < *b);
}
MAELYS_DEFINE_SORT(sort_integers, int, integer_cmp)
static int reference_cmp(const void *a, const void *b) {
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}
static int reference_fact_cmp(const void *a, const void *b) {
    return maelys_datalog_fact_cmp(a, b);
}
static int values[MAELYS_DATALOG_MAX_EDB_FACTS], expected[MAELYS_DATALOG_MAX_EDB_FACTS];
static maelys_datalog_internal_fact_t facts[MAELYS_DATALOG_MAX_EDB_FACTS];
static maelys_datalog_internal_fact_t expected_facts[MAELYS_DATALOG_MAX_EDB_FACTS];
static uint32_t random_state = 1u;
static uint32_t next_random(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}
static void check(size_t n, unsigned pattern) {
    for (size_t i = 0; i < n; ++i) {
        switch (pattern) {
        case 0: values[i] = (int)i; break;
        case 1: values[i] = (int)(n - i); break;
        case 2: values[i] = 7; break;
        case 3: values[i] = (int)(next_random() % 5u); break;
        case 4: values[i] = (int)(i < n / 2u ? i : n - i); break;
        case 5: values[i] = (int)(i % 17u); break;
        case 6: values[i] = (int)((i * 2u) % (n | 1u)); break;
        case 8: values[i] = (int)(i < 32u ? next_random() % 32u : i); break;
        case 9: values[i] = (int)(i ? i - 1u : n); break;
        case 10: values[i] = (int)(i < n / 4u ? n / 2u - i : i); break;
        default: values[i] = (int)(next_random() & 0x7fffffff); break;
        }
    }
    memcpy(expected, values, n * sizeof(*values));
    /* libc reference runs outside the engine and its allocation contract. */
    qsort(expected, n, sizeof(*expected), reference_cmp);
    comparisons = 0u;
    sort_integers(values, n);
    assert(!memcmp(values, expected, n * sizeof(*values)));
    size_t bits = 1u;
    for (size_t k = n; k > 1u; k >>= 1u) ++bits;
    assert(comparisons <= 64u * (n + 1u) * bits);
    /* Explicitly exercise the depth-exhaustion fallback, not merely sorted
     * output from random data which might never trigger it. */
    for (size_t i = 0; i < n; ++i) values[i] = expected[n - i - 1u];
    sort_integers_partition(values, n, 0u);
    assert(!memcmp(values, expected, n * sizeof(*values)));

    /* Exercise the actual engine specialization and canonical value order. */
    memset(facts, 0, n * sizeof(*facts));
    for (size_t i = 0; i < n; ++i) {
        facts[i].predicate_id = (uint16_t)(1u + next_random() % 7u);
        facts[i].arity = 1u;
        facts[i].terms[0].kind = MAELYS_DATALOG_TERM_INT;
        facts[i].terms[0].as.integer = expected[(i * 13u) % n];
    }
    memcpy(expected_facts, facts, n * sizeof(*facts));
    qsort(expected_facts, n, sizeof(*facts), reference_fact_cmp);
    maelys_datalog_fact_set_t set;
    maelys_datalog_fact_set_init(&set, facts, MAELYS_DATALOG_MAX_EDB_FACTS);
    set.count = n; set.sorted = 0;
    assert(maelys_datalog_fact_set_sort(&set) == MAELYS_OK);
    for (size_t i = 0; i < n; ++i)
        assert(maelys_datalog_fact_cmp(&facts[i], &expected_facts[i]) == 0);
}
int main(void) {
    for (unsigned pattern = 0; pattern < 11u; ++pattern) {
        for (size_t n = 0; n <= 128u; ++n) check(n, pattern);
        check(MAELYS_DATALOG_MAX_EDB_FACTS - 1u, pattern);
        check(MAELYS_DATALOG_MAX_EDB_FACTS, pattern);
    }
    puts("bounded sort: canonical order, duplicates, adversarial patterns and heap fallback passed");
    return 0;
}
