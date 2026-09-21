/* SPDX-License-Identifier: MPL-2.0 */
/* Reuse the unchanged fixture and payload; this is a disclosed, single-case
 * diagnostic, not a replacement for the complete historical benchmark. */
#define main historical_benchmark_main
#include "bench_datalog.c"
#undef main
#include <valgrind/callgrind.h>

int main(int argc, char **argv) {
    if (argc != 3 || (strcmp(argv[1], "count") && strcmp(argv[1], "time"))) {
        fprintf(stderr, "usage: %s count|time RAW_CSV\n", argv[0]);
        return 2;
    }
    _Static_assert(MAELYS_DATALOG_MAX_EDB_FACTS >= 2048, "LARGE diagnostic only");
    bench_case_t bench = {"solver_size_pure", "solver", "size_pure", 2048,
        useful_for_percent(2048, 10, 100), 0, "solve_calls/sec", 1};
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
        require_true(s_bench_ctx.edb.fact_count == 2048, "fixture fact count");
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
