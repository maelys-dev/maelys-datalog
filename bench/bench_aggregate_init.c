/* SPDX-License-Identifier: MPL-2.0 */
/* Public-API aggregate diagnostic: checked successful solve_edb only. */
#define _POSIX_C_SOURCE 200809L
#include <maelys/datalog.h>
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef AGGREGATE_COUNT
#include <valgrind/callgrind.h>
#endif
#define OK(x) do { int rc = (x); if (rc) { fprintf(stderr, "%s:%d: %s => %d\n", __FILE__, __LINE__, #x, rc); abort(); } } while (0)
#ifdef AGGREGATE_COUNT
enum { WARMUP = 50, SAMPLES = 1 };
#else
enum { WARMUP = 50, SAMPLES = 301 };
static double now_us(void) {
    struct timespec t; assert(!clock_gettime(CLOCK_MONOTONIC, &t));
    return (double)t.tv_sec * 1000000.0 + (double)t.tv_nsec / 1000.0;
}
#endif
int main(int argc, char **argv) {
    assert(argc == 3);
    FILE *out = fopen(argv[1], "w"), *raw = fopen(argv[2], "w"); assert(out && raw);
    const maelys_datalog_predicate_t predicates[] = {
        {"seed", 1, MAELYS_DATALOG_PREDICATE_EDB},
        {"allow", 1, MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY},
    };
    const maelys_datalog_domain_t domain = {"aggregate_probe", predicates, 2, NULL, 0};
    OK(maelys_datalog_domain_register(&domain));
    const char *ops[] = {"count", "min", "max", "sum"};
    const char *orders[] = {"sorted", "reverse", "permuted", "duplicate", "strided"};
    maelys_datalog_policy_t *policies[4]; maelys_datalog_session_t *sessions[4];
    for (unsigned op = 0; op < 4; ++op) {
        char source[100]; snprintf(source, sizeof(source), "allow(N) :- %s(I,seed(I),N).", ops[op]);
        OK(maelys_datalog_policy_load_inline(domain.name, "aggregate", source, strlen(source), &policies[op], NULL));
        OK(maelys_datalog_session_create(policies[op], 0, &sessions[op]));
    }
    size_t cap; OK(maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_FACTS_PER_PRED, &cap));
    assert(cap >= 32);
    size_t sizes[] = {0, 1, 8, 32, cap};
    maelys_datalog_fact_t *facts = calloc(cap, sizeof(*facts)); assert(facts);
    maelys_datalog_input_edb_t *edb; OK(maelys_datalog_input_edb_create(&edb));
    fprintf(out, "op,order,size,min_us,median_us,p95_us,derived,expected\n");
    fprintf(raw, "op,order,size,sample,us\n");
    for (unsigned order = 0; order < 5; ++order) for (unsigned si = 0; si < 5; ++si) {
        size_t n = sizes[si], unique = order == 3 && n ? 1 : n;
        int64_t stride = order == 4 ? 16 : 1;
        for (size_t i = 0; i < n; ++i) {
            size_t ordinal = order == 1 ? n - 1 - i : order == 2 ? (i * 13) % n : order == 3 ? 0 : i;
            facts[i].predicate = "seed"; facts[i].arity = 1;
            facts[i].terms[0].kind = MAELYS_DATALOG_VALUE_INTEGER;
            facts[i].terms[0].as.integer = (int64_t)ordinal * stride;
        }
        OK(maelys_datalog_input_edb_clear(edb));
        OK(maelys_datalog_input_edb_add_facts(edb, facts, n, NULL));
        for (unsigned op = 0; op < 4; ++op) {
            int64_t expected = op == 0 ? (int64_t)unique : op == 1 ? 0 : op == 2 ? (unique ? (int64_t)(unique - 1) * stride : 0) : (int64_t)unique * (unique ? (int64_t)(unique - 1) : 0) / 2 * stride;
            assert(expected <= INT32_MAX);
            size_t expected_derived = !n && (op == 1 || op == 2) ? 0 : 1;
            double samples[SAMPLES];
#ifdef AGGREGATE_COUNT
            char count_label[100];
            snprintf(count_label, sizeof(count_label), "%s/%s/%zu", ops[op], orders[order], n);
#endif
            for (unsigned s = 0; s < WARMUP + SAMPLES; ++s) {
                maelys_datalog_result_t *result = NULL;
                maelys_datalog_diagnostic_t diagnostic = MAELYS_DATALOG_DIAGNOSTIC_INIT;
#ifdef AGGREGATE_COUNT
                if (s == WARMUP) { CALLGRIND_TOGGLE_COLLECT; }
                int status = maelys_datalog_session_solve_edb(sessions[op], edb, &result, &diagnostic);
                if (s == WARMUP) {
                    CALLGRIND_TOGGLE_COLLECT;
                    CALLGRIND_DUMP_STATS_AT(count_label);
                    CALLGRIND_ZERO_STATS;
                }
                double elapsed = 0;
#else
                double begin = now_us();
                int status = maelys_datalog_session_solve_edb(sessions[op], edb, &result, &diagnostic);
                double elapsed = now_us() - begin;
#endif
                if (status) { fprintf(stderr, "%s/%s/%zu: %s\n", ops[op], orders[order], n, diagnostic.message); abort(); }
                size_t count; OK(maelys_datalog_result_derived_fact_count(result, &count)); assert(count == expected_derived);
                maelys_datalog_value_t value = {.kind = MAELYS_DATALOG_VALUE_INTEGER, .as.integer = expected};
                int present; OK(maelys_datalog_result_query(result, "allow", &value, 1, &present)); assert(present == (int)expected_derived);
                ++value.as.integer; OK(maelys_datalog_result_query(result, "allow", &value, 1, &present)); assert(!present);
                assert(diagnostic.present == 0);
                OK(maelys_datalog_result_free(result));
                if (s >= WARMUP) samples[s - WARMUP] = elapsed;
            }
            for (unsigned s = 0; s < SAMPLES; ++s) fprintf(raw, "%s,%s,%zu,%u,%.6f\n", ops[op], orders[order], n, s, samples[s]);
            for (unsigned i = 1; i < SAMPLES; ++i) {
                double v = samples[i]; unsigned j = i;
                while (j && samples[j - 1] > v) { samples[j] = samples[j - 1]; --j; } samples[j] = v;
            }
            fprintf(out, "%s,%s,%zu,%.6f,%.6f,%.6f,%zu,%" PRId64 "\n", ops[op], orders[order], n, samples[0], samples[SAMPLES / 2], samples[SAMPLES * 95 / 100], expected_derived, expected);
        }
    }
    OK(maelys_datalog_input_edb_free(edb)); free(facts);
    for (unsigned op = 0; op < 4; ++op) { OK(maelys_datalog_session_free(sessions[op])); OK(maelys_datalog_policy_free(policies[op])); }
    assert(!fclose(out)); assert(!fclose(raw));
    return 0;
}
