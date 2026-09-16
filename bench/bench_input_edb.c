/* SPDX-License-Identifier: MPL-2.0 */
/* Common public-API consumer for both revisions. No implementation flags.
 * Allocations, input construction and reporting are outside append timing.
 * Clear is timed separately on a populated EDB (clock overhead included).
 * With two paths, retain every raw measured sample as well as case summaries. */
#define _POSIX_C_SOURCE 200809L
#include <maelys/datalog.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { SAMPLES = 301, WARMUP = 50, NAMES = 1024 };
static FILE *summary, *raw;
static char names[NAMES][24];

static double now_us(void) {
    struct timespec t;
    assert(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}
static int sample_cmp(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
static void run_case(const char *scenario, size_t capacity, size_t text_capacity,
                     size_t count, size_t distinct, int unit, int packed) {
    size_t bytes, alignment;
    assert(!maelys_datalog_input_edb_storage_requirements(capacity, text_capacity, &bytes, &alignment));
    maelys_datalog_public_fact_t *facts = calloc(count, sizeof(*facts));
    assert(facts && distinct <= NAMES);
    for (size_t i = 0; i < count; ++i) {
        facts[i].predicate = packed ? names[(i * 5u) % distinct] : "seed";
        facts[i].arity = packed ? 4u : 1u;
        for (size_t j = 0; j < facts[i].arity; ++j) {
            facts[i].terms[j].kind = MAELYS_DATALOG_VALUE_SYMBOL;
            facts[i].terms[j].as.symbol = names[(packed ? i * 5u + j + 1u : i) % distinct];
        }
    }
    maelys_datalog_input_edb_t *edb;
    assert(!maelys_datalog_input_edb_create_with_capacity(capacity, text_capacity, &edb));
    double append[SAMPLES], clear[SAMPLES];
    for (size_t r = 0; r < SAMPLES + WARMUP; ++r) {
        double start = now_us();
        if (unit) {
            for (size_t i = 0; i < count; ++i)
                assert(!maelys_datalog_input_edb_add_fact(edb, facts[i].predicate, facts[i].terms, facts[i].arity, NULL));
        } else assert(!maelys_datalog_input_edb_add_facts(edb, facts, count, NULL));
        double duration = now_us() - start;
        size_t actual;
        assert(!maelys_datalog_input_edb_count(edb, &actual) && actual == count);
        start = now_us();
        assert(!maelys_datalog_input_edb_clear(edb));
        double clear_duration = now_us() - start;
        if (r >= WARMUP) {
            size_t sample = r - WARMUP;
            append[sample] = duration;
            clear[sample] = clear_duration;
        }
    }
    size_t strings = distinct + (packed ? 0u : 1u);
    const char *mode = unit ? "unit" : "batch";
    if (raw) for (size_t r = 0; r < SAMPLES; ++r)
        fprintf(raw, "%s,%zu,%zu,%zu,%s,%zu,%zu,%.6f,%.6f\n",
                scenario, capacity, count, strings, mode, text_capacity, r, append[r], clear[r]);
    qsort(append, SAMPLES, sizeof(*append), sample_cmp);
    qsort(clear, SAMPLES, sizeof(*clear), sample_cmp);
    fprintf(summary, "%s,%zu,%zu,%zu,%s,%zu,%zu,%u,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
            scenario, capacity, count, strings, mode, text_capacity, bytes, SAMPLES,
            append[0], append[SAMPLES / 2], append[285],
            clear[0], clear[SAMPLES / 2], clear[285]);
    assert(!maelys_datalog_input_edb_free(edb));
    free(facts);
}
int main(int argc, char **argv) {
    if (argc != 1 && argc != 3) {
        fprintf(stderr, "usage: %s [SUMMARY.csv SAMPLES.csv]\n", argv[0]); return 2;
    }
    summary = argc == 3 ? fopen(argv[1], "w") : stdout;
    if (!summary) { perror(argv[1]); return 2; }
    if (argc == 3) {
        raw = fopen(argv[2], "w");
        if (!raw) { perror(argv[2]); fclose(summary); return 2; }
        fputs("scenario,capacity,entries,distinct_strings,mode,text_capacity,sample,append_us,clear_us\n", raw);
    }
    fputs("scenario,capacity,entries,distinct_strings,mode,text_capacity,reserved_bytes,samples,min_us,median_us,p95_us,clear_min_us,clear_median_us,clear_p95_us\n", summary);
    size_t capacity, text_capacity, bytes, alignment;
    assert(!maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_EDB_FACTS, &capacity));
    assert(!maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_INPUT_EDB_TEXT_BYTES, &text_capacity));
    const size_t budgets[] = {128u, text_capacity, text_capacity};
    const char *labels[] = {"128", "default", "maximum"};
    for (size_t i = 0; i < 3; ++i) {
        assert(!maelys_datalog_input_edb_storage_requirements(capacity, budgets[i], &bytes, &alignment));
        fprintf(stderr, "storage,%s,fact_capacity=%zu,text_capacity=%zu,bytes=%zu,alignment=%zu\n",
                labels[i], capacity, budgets[i], bytes, alignment);
    }
    for (size_t i = 0; i < NAMES; ++i) snprintf(names[i], sizeof(names[i]), "v%04zu", i);
    const size_t distinct[] = {64, 128, 256, 512, 1024};
    for (size_t i = 0; i < 5; ++i) for (int unit = 0; unit < 2; ++unit) {
        size_t n = distinct[i], facts = (n + 4u) / 5u;
        /* Five string positions/fact. Possible distinct count is rounded up
         * by at most four: a capacity-based threshold, not a default-size
         * index used with a tiny batch. Each name uses exactly six bytes. */
        assert(facts <= capacity && n * 6u <= text_capacity);
        run_case("crossover", facts, n * 6u, facts, n, unit, 1);
    }
    const size_t sizes[] = {64, 256, capacity};
    for (size_t c = 0; c < 3; ++c) for (int mode = 0; mode < 2; ++mode)
    for (int unit = 0; unit < 2; ++unit)
        run_case("control", capacity, text_capacity, sizes[c],
                 mode ? (sizes[c] < 512u ? sizes[c] : 512u) : 1u, unit, 0);
    int failed = ferror(summary) || (raw && ferror(raw));
    if (summary != stdout && fclose(summary)) failed = 1;
    if (raw && fclose(raw)) failed = 1;
    return failed ? 1 : 0;
}
