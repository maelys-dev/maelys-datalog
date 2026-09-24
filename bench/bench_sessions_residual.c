#include "types_compat.h" /* Driver-owned diagnostic protocol on both revisions. */
/* SPDX-License-Identifier: MPL-2.0 */
/* Public facade only. Time solve_edb, excluding append, query/check and release.
 * Inert-policy cost is a common-cost control, not an isolated materialization timer. */
#define _POSIX_C_SOURCE 200809L
#include <maelys/datalog.h>
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <valgrind/callgrind.h>

#define OK(call) do { maelys_datalog_status_t rc_ = (call); if (rc_) { \
    fprintf(stderr, "%s:%d: %s -> %s\n", __FILE__, __LINE__, #call, maelys_datalog_status_name(rc_)); abort(); } } while (0)
enum { SAMPLES = 301, WARMUP = 50, PREDS = 32 };
static char names[PREDS][12];
static const char *orders[] = {"sorted", "reverse", "permuted", "duplicate", "strided"};
static const char *sizes[] = {"8", "16", "31", "32", "33", "64", "128", "256", "402", "maximum"};
static double now_us(void) {
    struct timespec t; assert(!clock_gettime(CLOCK_MONOTONIC, &t));
    return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}
static uint64_t mix(uint64_t h, uint64_t value) {
    return (h ^ value) * UINT64_C(1099511628211);
}

static uint64_t verify(maelys_datalog_result_t *result, maelys_bench_fact_view_t *views,
                       size_t cap, size_t entries, size_t lanes, unsigned order, unsigned symbolic,
                       unsigned deriving, char (*texts)[32]) {
    uint64_t hash = UINT64_C(14695981039346656037);
    size_t expected_out = order == 3 ? 1 : (entries + lanes - 1) / lanes;
    size_t derived;
    OK(maelys_datalog_result_derived_fact_count(result, &derived));
    assert(derived == (deriving ? expected_out : 0));
    for (size_t p = 0; p <= lanes; ++p) {
        size_t count;
        const char *name = p == lanes ? "out" : names[p];
        size_t expected = p == lanes ? (deriving ? expected_out : 0) :
                          order == 3 ? (p == 0) : (entries + lanes - 1 - p) / lanes;
        if (p != lanes) {
            /* enumerate exposes derived facts only. Check EDB membership and
             * an absent boundary value through query, outside the timer. */
            for (size_t i = 0; i <= expected; ++i) {
                maelys_bench_value_t term = {0};
                term.kind = symbolic ? MAELYS_DATALOG_VALUE_SYMBOL : MAELYS_DATALOG_VALUE_INTEGER;
                if (symbolic) term.as.symbol = texts[i];
                else term.as.integer = (int64_t)(i * (order == 4 ? 4096u : 1u));
                int present;
                OK(maelys_datalog_result_query(result, name, &term, 1, &present));
                assert(present == (i < expected));
            }
            hash = mix(mix(hash, p), expected);
            continue;
        }
        OK(maelys_datalog_result_enumerate(result, name, 1, views, cap, &count));
        assert(count == expected);
        hash = mix(mix(hash, p), count);
        for (size_t i = 0; i < count; ++i) {
            assert(views[i].arity == 1);
            const maelys_bench_term_view_t *term = &views[i].terms[0];
            hash = mix(hash, term->kind);
            size_t value = i * (order == 4 ? 4096u : 1u);
            if (symbolic) {
                assert(term->kind == MAELYS_DATALOG_VALUE_SYMBOL);
                const char *text; size_t length;
                OK(maelys_datalog_result_symbol_text(result, term->as.symbol_id, &text, &length));
                assert(length == strlen(texts[i]) && !memcmp(text, texts[i], length));
                hash = mix(hash, term->as.symbol_id);
                for (size_t j = 0; j < length; ++j) hash = mix(hash, (unsigned char)text[j]);
            } else {
                assert(term->kind == MAELYS_DATALOG_VALUE_INTEGER && term->as.integer == (int64_t)value);
                hash = mix(hash, (uint64_t)term->as.integer);
            }
        }
    }
    return hash;
}

int main(int argc, char **argv) {
    if (argc != 8 || (strcmp(argv[7], "count") && strcmp(argv[7], "time"))) {
        fprintf(stderr, "usage: %s SUMMARY.csv SAMPLES.csv POLICY ORDER VALUES SIZE time|count\n", argv[0]); return 2;
    }
    const unsigned measured = !strcmp(argv[7], "count") ? 1u : SAMPLES;
    const int counting = measured == 1u;
    unsigned selected = 0;
    FILE *summary = fopen(argv[1], "w"), *raw = fopen(argv[2], "w"); assert(summary && raw);
    size_t cap, edb_limit;
    OK(maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_FACTS_PER_PRED, &cap));
    OK(maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_EDB_FACTS, &edb_limit));
    assert((edb_limit + cap - 1) / cap < PREDS);
    maelys_bench_predicate_t predicates[PREDS + 1];
    for (unsigned i = 0; i < PREDS; ++i) {
        snprintf(names[i], sizeof(names[i]), "p%02u", i);
        predicates[i] = (maelys_bench_predicate_t){names[i], 1,
            MAELYS_DATALOG_PREDICATE_EDB | MAELYS_DATALOG_PREDICATE_QUERY};
    }
    predicates[PREDS] = (maelys_bench_predicate_t){"out", 1,
        MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY};
    const maelys_bench_domain_t domain = {"session_bench", predicates, PREDS + 1, NULL, 0};
    OK(maelys_datalog_domain_register(&domain));
    const char *sources[] = {"out(X) :- p00(X), p31(X).", "out(X) :- p00(X)."};
    maelys_datalog_policy_t *policies[2]; maelys_datalog_session_t *sessions[2];
    for (unsigned i = 0; i < 2; ++i) {
        OK(maelys_datalog_policy_load_inline(domain.name, "bench", sources[i], strlen(sources[i]), &policies[i], NULL));
        OK(maelys_datalog_session_create(policies[i], 0, &sessions[i]));
    }
    maelys_datalog_input_edb_t *edb; OK(maelys_datalog_input_edb_create(&edb));
    maelys_bench_fact_t *facts = calloc(edb_limit, sizeof(*facts));
    maelys_bench_fact_view_t *views = calloc(cap, sizeof(*views));
    char (*texts)[32] = calloc(cap + 1, sizeof(*texts));
    assert(facts && views && texts);
    /* Observe this process outside timing; opaque internals remain unobserved. */
    const char *layout_path = getenv("MAELYS_RESIDUAL_LAYOUT");
    if (layout_path) {
        FILE *layout = fopen(layout_path, "w"); assert(layout);
        fprintf(layout, "key,value\n");
#define OBS_PTR(name, ptr) fprintf(layout, "address." name ",%" PRIuPTR "\nmod64." name ",%" PRIuPTR "\n", (uintptr_t)(ptr), (uintptr_t)(ptr) % 64u)
#define OBS_TYPE(name, type) fprintf(layout, "sizeof." name ",%zu\nalignof." name ",%zu\n", sizeof(type), _Alignof(type))
        OBS_PTR("edb", edb); OBS_PTR("session.inert", sessions[0]); OBS_PTR("session.derive", sessions[1]);
        OBS_PTR("facts", facts); OBS_PTR("facts.terms", &facts[0].terms);
        OBS_PTR("views", views); OBS_PTR("texts", texts);
        OBS_TYPE("fact", maelys_bench_fact_t); OBS_TYPE("value", maelys_bench_value_t);
        OBS_TYPE("fact_view", maelys_bench_fact_view_t);
        fprintf(layout, "offsetof.fact.terms,%zu\n", offsetof(maelys_bench_fact_t, terms));
        assert(!fclose(layout));
#undef OBS_PTR
#undef OBS_TYPE
    }
    fprintf(summary, "policy,order,values,size,entries,edb_limit,samples,min_us,median_us,p95_us,result_digest,commit,profile,compiler,cflags,opt_level\n");
    fprintf(raw, "policy,order,values,size,sample,elapsed_us\n");
    for (unsigned kind = 0; kind < 2; ++kind) for (unsigned order = 0; order < 5; ++order)
    for (unsigned size = 0; size < sizeof(sizes) / sizeof(sizes[0]); ++size) {
        if (strcmp(argv[4], orders[order]) || strcmp(argv[5], kind ? "symbol" : "integer") ||
            strcmp(argv[6], sizes[size])) continue;
        size_t entries = !strcmp(sizes[size], "maximum") ? edb_limit : (size_t)strtoul(sizes[size], NULL, 10);
        size_t lanes = (entries + cap - 1) / cap;
        for (size_t i = 0; i <= cap; ++i)
            snprintf(texts[i], sizeof(texts[i]), "value-%08zu", i * (order == 4 ? 4096u : 1u));
        for (size_t i = 0; i < entries; ++i) {
            /* 13 is coprime to every case size (including 402). Mapping each
             * ordinal to a predicate-sized run gives an ordered first case. */
            size_t ordinal = order == 1 ? entries - 1 - i : order == 2 ? (i * 13u) % entries : order == 3 ? 0 : i;
            size_t p = 0, value = ordinal;
            while (value >= (entries + lanes - 1 - p) / lanes) {
                value -= (entries + lanes - 1 - p) / lanes; ++p;
            }
            facts[i].predicate = names[p]; facts[i].arity = 1;
            facts[i].terms[0].kind = kind ? MAELYS_DATALOG_VALUE_SYMBOL : MAELYS_DATALOG_VALUE_INTEGER;
            if (kind) facts[i].terms[0].as.symbol = texts[value];
            else facts[i].terms[0].as.integer = (int64_t)(value * (order == 4 ? 4096u : 1u));
        }
        OK(maelys_datalog_input_edb_clear(edb));
        OK(maelys_datalog_input_edb_add_facts(edb, facts, entries, NULL));
        for (unsigned policy = 0; policy < 2; ++policy) {
            const char *policy_name = policy ? "derive" : "inert", *value_name = kind ? "symbol" : "integer";
            if (strcmp(argv[3], policy_name)) continue;
            ++selected;
            double samples[SAMPLES]; uint64_t digest = 0;
            for (unsigned sample = 0; sample < measured + WARMUP; ++sample) {
                maelys_datalog_result_t *result; maelys_bench_diagnostic_t diagnostic = MAELYS_BENCH_DIAGNOSTIC_INIT;
                maelys_datalog_status_t status;
                double elapsed = 0;
                if (counting) {
                    if (sample == WARMUP) { CALLGRIND_ZERO_STATS; CALLGRIND_TOGGLE_COLLECT; }
                    status = maelys_datalog_session_solve_edb(sessions[policy], edb, &result, &diagnostic);
                    if (sample == WARMUP) { CALLGRIND_TOGGLE_COLLECT; }
                } else {
                    double start = now_us();
                    status = maelys_datalog_session_solve_edb(sessions[policy], edb, &result, &diagnostic);
                    elapsed = now_us() - start;
                }
                if (status) { fprintf(stderr, "%s/%s/%s/%zu: %s\n", policy_name, orders[order], value_name, entries, diagnostic.message); abort(); }
                uint64_t actual = verify(result, views, cap, entries, lanes, order, kind, policy, texts);
                if (!sample) digest = actual; else assert(digest == actual);
                OK(maelys_datalog_result_free(result));
                if (sample >= WARMUP) {
                    samples[sample - WARMUP] = elapsed;
                    fprintf(raw, "%s,%s,%s,%s,%u,%.6f\n", policy_name, orders[order], value_name, sizes[size], sample - WARMUP, elapsed);
                }
            }
            for (unsigned i = 1; i < measured; ++i) {
                double v = samples[i]; unsigned j = i;
                while (j && samples[j - 1] > v) { samples[j] = samples[j - 1]; --j; } samples[j] = v;
            }
            fprintf(summary, "%s,%s,%s,%s,%zu,%zu,%u,%.6f,%.6f,%.6f,%016" PRIx64 ",%s,%s,%s,%s,-O2\n",
                policy_name, orders[order], value_name, sizes[size], entries, edb_limit, measured,
                samples[0], samples[measured / 2], samples[measured * 95u / 100u], digest, BENCH_COMMIT, BENCH_PROFILE, BENCH_COMPILER, BENCH_CFLAGS);
        }
    }
    free(facts); free(views); free(texts);
    OK(maelys_datalog_input_edb_free(edb));
    for (unsigned i = 0; i < 2; ++i) { OK(maelys_datalog_session_free(sessions[i])); OK(maelys_datalog_policy_free(policies[i])); }
    assert(!fclose(summary)); assert(!fclose(raw));
    if (selected != 1) { fprintf(stderr, "expected one valid diagnostic case\n"); return 2; }
    return 0;
}
