/* SPDX-License-Identifier: MPL-2.0 */
/* Same engine binary, legacy versus configured direct-text explanations.
 * Only measure+write is timed; setup, solve, release and text checks are not. */
#define _POSIX_C_SOURCE 200809L
#include <maelys/datalog.h>
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define OK(call) assert((call) == MAELYS_DATALOG_STATUS_OK)
enum { SAMPLES = 301, WARMUP = 50, TEXT_BYTES = 16384 };
typedef maelys_datalog_status_t (*explain_fn)(const maelys_datalog_result_t *,
    const char *, const maelys_datalog_public_value_t *, size_t, char *, size_t, size_t *);
static const explain_fn explain[] = {
    maelys_datalog_result_explain_true_text, maelys_datalog_result_explain_false_text
};
static const char *names[2][2] = {{"alice", "carol"}, {"bob", "dave"}};
static char oracle[2][2][TEXT_BYTES];
static size_t lengths[2][2];

static double now_us(void) {
    struct timespec t;
    assert(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}

static size_t render(maelys_datalog_result_t *result, unsigned kind,
                     maelys_datalog_public_value_t *term, char *text) {
    size_t required, written;
    OK(explain[kind](result, "allow", term, 1, NULL, 0, &required));
    assert(required < TEXT_BYTES);
    OK(explain[kind](result, "allow", term, 1, text, TEXT_BYTES, &written));
    assert(written == required);
    return written;
}

static uint64_t digest(unsigned kind) {
    uint64_t value = UINT64_C(14695981039346656037);
    for (unsigned query = 0; query < 2; ++query)
        for (size_t i = 0; i <= lengths[kind][query]; ++i) {
            value ^= (unsigned char)oracle[kind][query][i];
            value *= UINT64_C(1099511628211);
        }
    return value;
}

static void verify_text(maelys_datalog_session_t *sessions[2], maelys_datalog_input_edb_t *edb) {
    for (unsigned mode = 0; mode < 2; ++mode) {
        maelys_datalog_result_t *result;
        OK(maelys_datalog_session_solve_edb(sessions[mode], edb, &result, NULL));
        for (unsigned kind = 0; kind < 2; ++kind) for (unsigned query = 0; query < 2; ++query) {
            maelys_datalog_public_value_t term = MAELYS_DATALOG_SYMBOL(names[kind][query]);
            int present;
            OK(maelys_datalog_result_query(result, "allow", &term, 1, &present));
            assert(present == (kind == 0));
            char text[TEXT_BYTES];
            size_t length = render(result, kind, &term, text);
            assert(strlen(text) == length && strstr(text, "status=complete"));
            if (!mode) {
                lengths[kind][query] = length;
                memcpy(oracle[kind][query], text, length + 1);
            } else {
                assert(length == lengths[kind][query]);
                assert(!memcmp(text, oracle[kind][query], length + 1));
            }
        }
        OK(maelys_datalog_result_free(result));
    }
}

static void run_case(maelys_datalog_session_t *session, maelys_datalog_input_edb_t *edb,
                     const char *mode, size_t reserved, unsigned kind, unsigned scenario,
                     FILE *summary, FILE *raw) {
    const char *scenarios[] = {"fresh-result", "alternating-query", "cache-hit"};
    const char *kind_name = kind ? "false" : "true";
    double samples[SAMPLES];
    maelys_datalog_result_t *result = NULL;
    if (scenario) OK(maelys_datalog_session_solve_edb(session, edb, &result, NULL));
    for (size_t i = 0; i < SAMPLES + WARMUP; ++i) {
        unsigned query = scenario == 1 ? i % 2 : 0;
        maelys_datalog_public_value_t term = MAELYS_DATALOG_SYMBOL(names[kind][query]);
        char text[TEXT_BYTES];
        if (!scenario) OK(maelys_datalog_session_solve_edb(session, edb, &result, NULL));
        double start = now_us();
        size_t length = render(result, kind, &term, text);
        double elapsed = now_us() - start;
        assert(length == lengths[kind][query]);
        assert(!memcmp(text, oracle[kind][query], length + 1));
        if (!scenario) OK(maelys_datalog_result_free(result));
        if (i >= WARMUP) {
            size_t sample = i - WARMUP;
            samples[sample] = elapsed;
            fprintf(raw, "%s,%s,%s,%zu,%.6f\n", scenarios[scenario], kind_name, mode, sample, elapsed);
        }
    }
    if (scenario) OK(maelys_datalog_result_free(result));
    /* Sorting/reporting is outside timing, with no allocation needed. */
    for (size_t i = 1; i < SAMPLES; ++i) {
        double value = samples[i];
        size_t j = i;
        while (j && samples[j - 1] > value) { samples[j] = samples[j - 1]; --j; }
        samples[j] = value;
    }
    fprintf(summary, "%s,%s,%s,%u,%.6f,%.6f,%.6f,%zu,%016" PRIx64 ",%s,%s,%s,%s,-O2\n",
        scenarios[scenario], kind_name, mode, SAMPLES, samples[0], samples[SAMPLES / 2], samples[285],
        reserved, digest(kind), BENCH_COMMIT, BENCH_PROFILE, BENCH_COMPILER, BENCH_CFLAGS);
}

int main(int argc, char **argv) {
    if (argc != 4 || (strcmp(argv[1], "legacy") && strcmp(argv[1], "workspace"))) {
        fprintf(stderr, "usage: %s legacy|workspace SUMMARY.csv SAMPLES.csv\n", argv[0]);
        return 2;
    }
    unsigned mode = !strcmp(argv[1], "workspace");
    FILE *summary = fopen(argv[2], "w"), *raw = fopen(argv[3], "w");
    assert(summary && raw);
    const maelys_datalog_public_predicate_t predicates[] = {
        MAELYS_DATALOG_EDB("seed", 1), MAELYS_DATALOG_EDB("blocked", 1),
        MAELYS_DATALOG_IDB_QUERY("allow", 1)
    };
    const maelys_datalog_public_domain_t domain = {"explanation_bench", predicates, 3, NULL, 0};
    OK(maelys_datalog_domain_register(&domain));
    const char *source = "allow(X) :- seed(X), not(blocked(X)).";
    maelys_datalog_policy_t *policy;
    OK(maelys_datalog_policy_load_inline(domain.name, "bench", source, strlen(source), &policy, NULL));
    maelys_datalog_input_edb_t *edb;
    OK(maelys_datalog_input_edb_create(&edb));
    OK(MAELYS_DATALOG_ADD_FACTS(edb, NULL,
        MAELYS_DATALOG_FACT("seed", "alice"), MAELYS_DATALOG_FACT("seed", "carol"),
        MAELYS_DATALOG_FACT("seed", "bob"), MAELYS_DATALOG_FACT("seed", "dave"),
        MAELYS_DATALOG_FACT("blocked", "bob"), MAELYS_DATALOG_FACT("blocked", "dave")));
    maelys_datalog_session_config_t *config;
    maelys_datalog_session_t *sessions[2];
    OK(maelys_datalog_session_config_create(&config));
    OK(maelys_datalog_session_create_configured(policy, 0, config, &sessions[0]));
    OK(maelys_datalog_session_config_set_explanation_workspace(config,
        MAELYS_DATALOG_EXPLAIN_TRUE | MAELYS_DATALOG_EXPLAIN_FALSE));
    OK(maelys_datalog_session_create_configured(policy, 0, config, &sessions[1]));
    OK(maelys_datalog_session_config_free(config));
    size_t reserved = 0;
    for (unsigned kind = 1; kind <= 2; ++kind) {
        size_t bytes, alignment;
        OK(maelys_datalog_session_explanation_storage_bound(sessions[1],
            (maelys_datalog_explanation_kind_t)kind, &bytes, &alignment));
        if (bytes > reserved) reserved = bytes;
    }
    verify_text(sessions, edb);
    fprintf(summary, "scenario,kind,mode,samples,min_us,median_us,p95_us,workspace_bytes,text_digest,commit,profile,compiler,cflags,opt_level\n");
    fprintf(raw, "scenario,kind,mode,sample,elapsed_us\n");
    for (unsigned kind = 0; kind < 2; ++kind) for (unsigned scenario = 0; scenario < 3; ++scenario)
        run_case(sessions[mode], edb, argv[1], mode ? reserved : 0, kind, scenario, summary, raw);
    for (unsigned i = 0; i < 2; ++i) OK(maelys_datalog_session_free(sessions[i]));
    OK(maelys_datalog_input_edb_free(edb));
    OK(maelys_datalog_policy_free(policy));
    assert(!fclose(summary));
    assert(!fclose(raw));
    return 0;
}
