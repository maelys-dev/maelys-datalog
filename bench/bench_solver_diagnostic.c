#include "bench/types_compat.h"
/* SPDX-License-Identifier: MPL-2.0 */
/* Reuse the unchanged fixture and payload; this is a disclosed, single-case
 * diagnostic, not a replacement for the complete historical benchmark. */
#define main historical_benchmark_main
#include "bench_datalog.c"
#undef main
#include <valgrind/callgrind.h>
#include <maelys/datalog.h>

/* Observe the data addresses of this exact fixture outside every measured
 * region. Modulo 64 is a named reference, not a portable cache-line assertion. */
static void write_layout(FILE *out) {
    fprintf(out, "key,value\n");
#define TYPE_LAYOUT(name, type) \
    fprintf(out, "sizeof." name ",%zu\nalignof." name ",%zu\n", sizeof(type), _Alignof(type))
    TYPE_LAYOUT("ruleset", maelys_bench_native_ruleset_t);
    TYPE_LAYOUT("literal", maelys_datalog_literal_t);
    TYPE_LAYOUT("rule", maelys_datalog_rule_t);
    TYPE_LAYOUT("premise", maelys_datalog_explanation_premise_t);
    TYPE_LAYOUT("fact", maelys_bench_native_fact_t);
    TYPE_LAYOUT("context", bench_ctx_t);
#undef TYPE_LAYOUT
    fprintf(out, "offsetof.context.ruleset,%zu\naddress_mod64.ruleset,%" PRIuPTR "\n",
            offsetof(bench_ctx_t, ruleset), (uintptr_t)&s_bench_ctx.ruleset % 64u);
#define FIELD_LAYOUT(member) \
    fprintf(out, "offsetof.ruleset." #member ",%zu\naddress_mod64.ruleset." #member ",%" PRIuPTR "\n", \
            offsetof(maelys_bench_native_ruleset_t, member), (uintptr_t)&s_bench_ctx.ruleset.member % 64u)
    FIELD_LAYOUT(loaded);
    FIELD_LAYOUT(modules);
    FIELD_LAYOUT(policy_id);
    FIELD_LAYOUT(domain);
    FIELD_LAYOUT(sha256);
    FIELD_LAYOUT(query_whitelist);
    FIELD_LAYOUT(query_whitelist_count);
    FIELD_LAYOUT(enforces_query_whitelist);
    FIELD_LAYOUT(positive_recursion_supported);
    FIELD_LAYOUT(negation_supported);
    FIELD_LAYOUT(negation_recursion_supported);
    FIELD_LAYOUT(strata);
    FIELD_LAYOUT(max_stratum);
    FIELD_LAYOUT(strata_assigned);
    FIELD_LAYOUT(has_positive_recursion);
    FIELD_LAYOUT(test_only);
    FIELD_LAYOUT(symbols);
    FIELD_LAYOUT(registry);
    FIELD_LAYOUT(facts);
    FIELD_LAYOUT(fact_count);
    FIELD_LAYOUT(rules);
    FIELD_LAYOUT(rule_sources);
    FIELD_LAYOUT(rule_count);
    FIELD_LAYOUT(frontend_name);
    FIELD_LAYOUT(frontend_semantic_id);
    FIELD_LAYOUT(source_sha256);
    FIELD_LAYOUT(program_validated);
    FIELD_LAYOUT(compiled_fingerprint);
    FIELD_LAYOUT(filter_programs);
    FIELD_LAYOUT(filter_program_count);
    FIELD_LAYOUT(filter_pattern_pool);
    FIELD_LAYOUT(filter_pattern_pool_used);
#ifdef MAELYS_DATALOG_CAP_AGGREGATES
    FIELD_LAYOUT(aggregates_supported);
#endif
#undef FIELD_LAYOUT
}

int main(int argc, char **argv) {
    if ((argc != 3 && argc != 4) ||
        (strcmp(argv[1], "count") && strcmp(argv[1], "time") && strcmp(argv[1], "layout")) ||
        (argc == 4 && strcmp(argv[3], "1024") && strcmp(argv[3], "2048"))) {
        fprintf(stderr, "usage: %s count|time|layout RAW_CSV [1024|2048]\n", argv[0]);
        return 2;
    }
    _Static_assert(MAELYS_DATALOG_MAX_EDB_FACTS >= 2048, "LARGE diagnostic only");
    if (!strcmp(argv[1], "layout")) {
        FILE *out = fopen(argv[2], "w");
        require_true(out != NULL, "layout output");
        write_layout(out);
        require_true(fclose(out) == 0, "close layout output");
        return 0;
    }
    const size_t size = argc == 4 && !strcmp(argv[3], "1024") ? 1024u : 2048u;
    bench_case_t bench = {"solver_size_pure", "solver", "size_pure", size,
        useful_for_percent(size, 10, 100), 0, "solve_calls/sec", 1};
    bench.selectivity = (double)bench.useful_count / bench.size;
    /* Force both modes through the original function, not an inlined copy. */
    run_fn_t volatile payload = run_solver_once;
    memset(&s_bench_ctx, 0, sizeof(s_bench_ctx));
    init_registry(&s_bench_ctx);
    const bool count = !strcmp(argv[1], "count");
    for (unsigned i = 0; i < (count ? 8u : 500u); ++i) {
        prepare_solver(&s_bench_ctx, &bench);
        require_true(payload(&s_bench_ctx, &bench) == 17, "warmup result");
        cleanup_case(&s_bench_ctx, &bench);
    }
    FILE *raw = fopen(argv[2], "w");
    require_true(raw != NULL, "raw output");
    fprintf(raw, "sample,elapsed_us,result\n");
    for (unsigned i = 0; i < (count ? 1u : 1000u); ++i) {
        prepare_solver(&s_bench_ctx, &bench);
        require_true(s_bench_ctx.edb.fact_count == size, "fixture fact count");
        uint64_t value, start = 0, elapsed = 0;
        if (count) {
            /* Start with --collect-atstart=no. No preparation, clock, oracle
             * or cleanup is counted, only the historical solve payload. */
            CALLGRIND_TOGGLE_COLLECT;
            value = payload(&s_bench_ctx, &bench);
            CALLGRIND_TOGGLE_COLLECT;
        } else {
            start = now_ns();
            value = payload(&s_bench_ctx, &bench);
            elapsed = now_ns() - start;
        }
        require_true(value == 17, "measured result");
        cleanup_case(&s_bench_ctx, &bench);
        fprintf(raw, "%u,%.6f,%" PRIu64 "\n", i, (double)elapsed / 1000, value);
    }
    require_true(fclose(raw) == 0, "close raw output");
    return 0;
}
