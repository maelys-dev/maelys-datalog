#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach/mach_time.h>
#endif

#include "common/maelys_errors.h"
#include "src/core/maelys_datalog_edb.h"
#include "src/core/maelys_datalog_parser.h"
#include "src/core/maelys_datalog_predicate_registry.h"
#include "src/core/maelys_datalog_ruleset.h"
#include "src/core/maelys_datalog_solver.h"
#include "src/core/maelys_datalog_symbol_table.h"
#include "src/core/maelys_datalog_types.h"

#ifndef BENCH_COMMIT
#define BENCH_COMMIT "unknown"
#endif
#ifndef BENCH_DIRTY
#define BENCH_DIRTY "unknown"
#endif
#ifndef BENCH_COMPILER
#define BENCH_COMPILER "unknown"
#endif
#ifndef BENCH_CFLAGS
#define BENCH_CFLAGS "unknown"
#endif
#ifndef BENCH_OPT_LEVEL
#define BENCH_OPT_LEVEL "unknown"
#endif

#define WARMUP_MIN_NS 300000000ULL
#define WARMUP_MIN_ITERS 500u
#define INNER_REPEAT_TARGET_NS 1000ULL
#define INNER_REPEAT_MAX 1048576u
#define DEFAULT_SAMPLES 1000u
#define MAX_RESULTS 128u
#define MAX_VALUES (MAELYS_DATALOG_MAX_EDB_FACTS * 2u)

static volatile uint64_t bench_sink;

typedef struct {
  const char *benchmark;
  const char *group;
  const char *mode;
  size_t size;
  double selectivity;
  const char *op_unit;
  size_t op_count;
} bench_case_t;

typedef struct {
  char benchmark[64];
  char group[32];
  char mode[64];
  size_t size;
  double selectivity;
  size_t samples;
  size_t inner_repeats;
  size_t warmup;
  double warmup_us;
  double median_us;
  double p95_us;
  double min_us;
  double max_us;
  double total_us;
  double measured_total_us;
  double ops_per_sec;
  char op_unit[32];
} bench_result_t;

typedef struct {
  maelys_datalog_symbol_table_t symbols;
  maelys_datalog_predicate_registry_t registry;
  maelys_datalog_ruleset_t ruleset;
  maelys_datalog_fact_t fact_pool[MAELYS_DATALOG_MAX_EDB_FACTS];
  maelys_datalog_edb_t edb;
  maelys_datalog_symbol_id_t ids[MAX_VALUES];
  const char *values[MAX_VALUES];
  char strings[MAX_VALUES][32];
  maelys_datalog_term_t query_terms[2];
} bench_ctx_t;

typedef struct {
  FILE *csv;
  bench_result_t rows[MAX_RESULTS];
  size_t row_count;
  size_t samples;
  char date_utc[32];
} bench_output_t;

typedef struct {
  uint64_t payload_ns;
  uint64_t acc;
} repeated_payload_t;

typedef void (*prepare_fn_t)(bench_ctx_t *ctx, const bench_case_t *bench);
typedef uint64_t (*run_fn_t)(bench_ctx_t *ctx, const bench_case_t *bench);

static uint64_t now_ns(void) {
#if defined(__APPLE__)
  static mach_timebase_info_data_t timebase;
  if (timebase.denom == 0u) {
    mach_timebase_info(&timebase);
  }
  const uint64_t ticks = mach_absolute_time();
  return (ticks * (uint64_t)timebase.numer) / (uint64_t)timebase.denom;
#else
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    perror("clock_gettime");
    exit(2);
  }
  return ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
#endif
}

static void consume_u64(uint64_t value) {
#if defined(__GNUC__) || defined(__clang__)
  __asm__ __volatile__("" : : "g"(value) : "memory");
#endif
  bench_sink ^= value + 0x9e3779b97f4a7c15ULL + (bench_sink << 6) + (bench_sink >> 2);
}

static int compare_double(const void *left, const void *right) {
  const double a = *(const double *)left;
  const double b = *(const double *)right;
  return (a > b) - (a < b);
}

static void require_ok(maelys_result_t rc, const char *what) {
  if (rc != MAELYS_OK) {
    fprintf(stderr, "%s failed rc=%d\n", what, (int)rc);
    exit(2);
  }
}

static void require_true(bool ok, const char *what) {
  if (!ok) {
    fprintf(stderr, "%s failed\n", what);
    exit(2);
  }
}

static void copy_string(char *dst, size_t dst_len, const char *src) {
  if (dst_len == 0u) {
    return;
  }
  snprintf(dst, dst_len, "%s", src);
}

static void fill_date_utc(char out[32]) {
  time_t t = time(NULL);
  struct tm tm_utc;
  gmtime_r(&t, &tm_utc);
  strftime(out, 32, "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
}

static size_t sample_count(void) {
  const char *env = getenv("MAELYS_BENCH_SAMPLES");
  if (!env || !*env) {
    return DEFAULT_SAMPLES;
  }
  errno = 0;
  char *end = NULL;
  unsigned long parsed = strtoul(env, &end, 10);
  if (errno != 0 || end == env || *end != '\0' || parsed == 0ul || parsed > 100000ul) {
    fprintf(stderr, "invalid MAELYS_BENCH_SAMPLES=%s\n", env);
    exit(2);
  }
  return (size_t)parsed;
}

static void init_registry(bench_ctx_t *ctx) {
  maelys_datalog_symbol_table_init(&ctx->symbols);
  maelys_datalog_predicate_registry_init(&ctx->registry);
  require_ok(maelys_datalog_predicate_registry_add_domain(&ctx->registry,
                                                          "bench",
                                                          1u,
                                                          MAELYS_DATALOG_PRED_KIND_EDB),
             "add bench domain");
  require_ok(maelys_datalog_predicate_registry_add_domain(&ctx->registry,
                                                          "item",
                                                          1u,
                                                          MAELYS_DATALOG_PRED_KIND_EDB),
             "add item domain");
  require_ok(maelys_datalog_predicate_registry_add_domain(&ctx->registry,
                                                          "pair",
                                                          2u,
                                                          MAELYS_DATALOG_PRED_KIND_EDB),
             "add pair domain");

  for (unsigned i = 0; i < 32u; ++i) {
    char name[32];
    snprintf(name, sizeof(name), "noise_%03u", i);
    require_ok(maelys_datalog_predicate_registry_add_domain(&ctx->registry,
                                                            name,
                                                            1u,
                                                            MAELYS_DATALOG_PRED_KIND_EDB),
               "add noise atom");
  }
  require_ok(maelys_datalog_predicate_registry_freeze(&ctx->registry), "freeze registry");
}

static void edb_reset(bench_ctx_t *ctx) {
  maelys_datalog_edb_init(&ctx->edb,
                          ctx->fact_pool,
                          MAELYS_DATALOG_MAX_EDB_FACTS,
                          &ctx->symbols,
                          &ctx->registry);
}

static void fill_values(bench_ctx_t *ctx, const char *prefix, size_t count) {
  require_true(count <= MAX_VALUES, "value count");
  for (size_t i = 0; i < count; ++i) {
    snprintf(ctx->strings[i], sizeof(ctx->strings[i]), "%s_%03zu", prefix, i);
    ctx->values[i] = ctx->strings[i];
  }
}

static void intern_values(bench_ctx_t *ctx, size_t count) {
  require_true(count <= MAX_VALUES, "intern value count");
  for (size_t i = 0; i < count; ++i) {
    require_ok(maelys_datalog_edb_intern_runtime_symbol(&ctx->edb,
                                                        ctx->values[i],
                                                        &ctx->ids[i]),
               "intern value");
  }
}

static void prepare_intern_distinct(bench_ctx_t *ctx, const bench_case_t *bench) {
  (void)bench;
  maelys_datalog_symbol_table_init(&ctx->symbols);
  edb_reset(ctx);
  fill_values(ctx, "sym", bench->size);
}

static uint64_t run_intern_distinct(bench_ctx_t *ctx, const bench_case_t *bench) {
  uint64_t acc = 0;
  for (size_t i = 0; i < bench->size; ++i) {
    maelys_datalog_symbol_id_t id = 0;
    maelys_result_t rc = maelys_datalog_edb_intern_runtime_symbol(&ctx->edb,
                                                                  ctx->values[i],
                                                                  &id);
    acc += (uint64_t)rc + (uint64_t)id;
  }
  consume_u64(acc);
  return acc;
}

static void prepare_intern_reexisting(bench_ctx_t *ctx, const bench_case_t *bench) {
  maelys_datalog_symbol_table_init(&ctx->symbols);
  edb_reset(ctx);
  fill_values(ctx, "sym", bench->size);
  intern_values(ctx, bench->size);
}

static uint64_t run_intern_reexisting(bench_ctx_t *ctx, const bench_case_t *bench) {
  return run_intern_distinct(ctx, bench);
}

static void prepare_intern_mixed(bench_ctx_t *ctx, const bench_case_t *bench) {
  const size_t half = bench->size / 2u;
  maelys_datalog_symbol_table_init(&ctx->symbols);
  edb_reset(ctx);
  fill_values(ctx, "mix", bench->size);
  intern_values(ctx, half);
}

static uint64_t run_intern_mixed(bench_ctx_t *ctx, const bench_case_t *bench) {
  return run_intern_distinct(ctx, bench);
}

static void prepare_symbol_insert(bench_ctx_t *ctx, const bench_case_t *bench) {
  maelys_datalog_symbol_table_init(&ctx->symbols);
  edb_reset(ctx);
  fill_values(ctx, "fact", bench->size * 2u);
  intern_values(ctx, bench->size * 2u);
}

static uint64_t run_symbol_unit_unary(bench_ctx_t *ctx, const bench_case_t *bench) {
  uint64_t acc = 0;
  for (size_t i = 0; i < bench->size; ++i) {
    maelys_result_t rc = maelys_datalog_edb_add_symbol_id_fact(&ctx->edb, "item", ctx->ids[i]);
    acc += (uint64_t)rc + (uint64_t)ctx->ids[i];
  }
  consume_u64(acc);
  return acc;
}

static uint64_t run_symbol_batch_unary(bench_ctx_t *ctx, const bench_case_t *bench) {
  maelys_result_t rc = maelys_datalog_edb_add_symbol_id_facts(&ctx->edb,
                                                              "item",
                                                              ctx->ids,
                                                              bench->size);
  uint64_t acc = (uint64_t)rc + ctx->edb.fact_count;
  consume_u64(acc);
  return acc;
}

static uint64_t run_symbol_unit_binary(bench_ctx_t *ctx, const bench_case_t *bench) {
  uint64_t acc = 0;
  for (size_t i = 0; i < bench->size; ++i) {
    maelys_result_t rc = maelys_datalog_edb_add_symbol_ids_fact(&ctx->edb,
                                                                "pair",
                                                                ctx->ids[i * 2u],
                                                                ctx->ids[(i * 2u) + 1u]);
    acc += (uint64_t)rc + (uint64_t)ctx->ids[i * 2u];
  }
  consume_u64(acc);
  return acc;
}

static uint64_t run_symbol_batch_binary(bench_ctx_t *ctx, const bench_case_t *bench) {
  maelys_result_t rc = maelys_datalog_edb_add_symbol_ids_facts(&ctx->edb,
                                                               "pair",
                                                               ctx->ids,
                                                               bench->size);
  uint64_t acc = (uint64_t)rc + ctx->edb.fact_count;
  consume_u64(acc);
  return acc;
}

static void prepare_string_insert(bench_ctx_t *ctx, const bench_case_t *bench) {
  maelys_datalog_symbol_table_init(&ctx->symbols);
  edb_reset(ctx);
  fill_values(ctx, "runtime", bench->size * 2u);
}

static uint64_t run_string_unit_unary(bench_ctx_t *ctx, const bench_case_t *bench) {
  uint64_t acc = 0;
  for (size_t i = 0; i < bench->size; ++i) {
    maelys_result_t rc = maelys_datalog_edb_add_runtime_symbol_fact(&ctx->edb,
                                                                    "item",
                                                                    ctx->values[i]);
    acc += (uint64_t)rc + ctx->edb.fact_count;
  }
  consume_u64(acc);
  return acc;
}

static uint64_t run_string_batch_unary(bench_ctx_t *ctx, const bench_case_t *bench) {
  maelys_result_t rc = maelys_datalog_edb_add_runtime_symbol_facts(&ctx->edb,
                                                                   "item",
                                                                   ctx->values,
                                                                   bench->size);
  uint64_t acc = (uint64_t)rc + ctx->edb.fact_count;
  consume_u64(acc);
  return acc;
}

static uint64_t run_string_composed_binary(bench_ctx_t *ctx, const bench_case_t *bench) {
  uint64_t acc = 0;
  for (size_t i = 0; i < bench->size; ++i) {
    maelys_datalog_symbol_id_t left = 0;
    maelys_datalog_symbol_id_t right = 0;
    maelys_result_t rc = maelys_datalog_edb_intern_runtime_symbol(&ctx->edb,
                                                                  ctx->values[i * 2u],
                                                                  &left);
    acc += (uint64_t)rc + (uint64_t)left;
    rc = maelys_datalog_edb_intern_runtime_symbol(&ctx->edb,
                                                  ctx->values[(i * 2u) + 1u],
                                                  &right);
    acc += (uint64_t)rc + (uint64_t)right;
    rc = maelys_datalog_edb_add_symbol_ids_fact(&ctx->edb, "pair", left, right);
    acc += (uint64_t)rc + ctx->edb.fact_count;
  }
  consume_u64(acc);
  return acc;
}

static uint64_t run_string_batch_binary(bench_ctx_t *ctx, const bench_case_t *bench) {
  maelys_result_t rc = maelys_datalog_edb_add_runtime_symbol_pair_facts(&ctx->edb,
                                                                        "pair",
                                                                        ctx->values,
                                                                        bench->size);
  uint64_t acc = (uint64_t)rc + ctx->edb.fact_count;
  consume_u64(acc);
  return acc;
}

static const char *ruleset_for_mode(const char *mode) {
  if (strcmp(mode, "simple_join") == 0) {
    return "allow(U) :- user(U), owns(U,F), target(F).";
  }
  if (strcmp(mode, "noisy_join") == 0) {
    return "allow(U) :- user(U), owns(U,F), target(F).";
  }
  if (strcmp(mode, "absent_predicate") == 0) {
    return "allow(U) :- missing_edb(U).";
  }
  return "allow(U) :- user(U).";
}

static void prepare_solve_base(bench_ctx_t *ctx, const bench_case_t *bench) {
  const char *src = ruleset_for_mode(bench->mode);
  require_ok(maelys_datalog_ruleset_init(&ctx->ruleset,
                                         "policy.bench",
                                         "decision",
                                         MAELYS_DATALOG_SHA256_UNSET,
                                         0),
             "ruleset init");
  require_ok(maelys_datalog_predicate_registry_add_domain(&ctx->ruleset.registry,
                                                          "allow",
                                                          1u,
                                                          MAELYS_DATALOG_PRED_KIND_IDB |
                                                              MAELYS_DATALOG_PRED_KIND_QUERY),
             "ruleset allow");
  require_ok(maelys_datalog_predicate_registry_add_domain(&ctx->ruleset.registry,
                                                          "user",
                                                          1u,
                                                          MAELYS_DATALOG_PRED_KIND_EDB),
             "ruleset user");
  require_ok(maelys_datalog_predicate_registry_add_domain(&ctx->ruleset.registry,
                                                          "owns",
                                                          2u,
                                                          MAELYS_DATALOG_PRED_KIND_EDB),
             "ruleset owns");
  require_ok(maelys_datalog_predicate_registry_add_domain(&ctx->ruleset.registry,
                                                          "target",
                                                          1u,
                                                          MAELYS_DATALOG_PRED_KIND_EDB),
             "ruleset target");
  require_ok(maelys_datalog_predicate_registry_add_domain(&ctx->ruleset.registry,
                                                          "missing_edb",
                                                          1u,
                                                          MAELYS_DATALOG_PRED_KIND_EDB),
             "ruleset missing");
  for (unsigned i = 0; i < 32u; ++i) {
    char name[32];
    snprintf(name, sizeof(name), "noise_%03u", i);
    require_ok(maelys_datalog_predicate_registry_add_domain(&ctx->ruleset.registry,
                                                            name,
                                                            1u,
                                                            MAELYS_DATALOG_PRED_KIND_EDB),
               "ruleset noise");
  }
  require_ok(maelys_datalog_predicate_registry_freeze(&ctx->ruleset.registry),
             "ruleset freeze");
  require_ok(maelys_datalog_parse_ruleset(&ctx->ruleset, src, strlen(src)),
             "parse ruleset");
  maelys_datalog_edb_init(&ctx->edb,
                          ctx->fact_pool,
                          MAELYS_DATALOG_MAX_EDB_FACTS,
                          &ctx->ruleset.symbols,
                          &ctx->ruleset.registry);
}

static void add_noise_facts(bench_ctx_t *ctx, size_t total_noise, size_t symbol_mod) {
  for (size_t i = 0; i < total_noise; ++i) {
    char pred_name[32];
    char value[32];
    const size_t pred_index = i / MAELYS_DATALOG_MAX_FACTS_PER_PRED;
    snprintf(pred_name, sizeof(pred_name), "noise_%03zu", pred_index);
    snprintf(value, sizeof(value), "n%03zu", i % symbol_mod);
    require_ok(maelys_datalog_edb_add_runtime_symbol_fact(&ctx->edb, pred_name, value),
               "add noise");
  }
}

static void populate_selectivity(bench_ctx_t *ctx, const bench_case_t *bench) {
  const size_t total = bench->size;
  size_t useful = 64u;
  if (strcmp(bench->mode, "selectivity_1pct") == 0) {
    useful = 8u;
  } else if (strcmp(bench->mode, "selectivity_100pct") == 0) {
    useful = total;
  }
  for (size_t i = 0; i < useful; ++i) {
    char value[32];
    snprintf(value, sizeof(value), "u%03zu", i);
    require_ok(maelys_datalog_edb_add_runtime_symbol_fact(&ctx->edb, "user", value),
               "add user");
  }
  if (total > useful) {
    add_noise_facts(ctx, total - useful, 64u);
  }
}

static void populate_noise_total(bench_ctx_t *ctx, const bench_case_t *bench) {
  const size_t useful = bench->size == 64u ? 8u : 16u;
  for (size_t i = 0; i < useful; ++i) {
    char value[32];
    snprintf(value, sizeof(value), "target%03zu", i);
    require_ok(maelys_datalog_edb_add_runtime_symbol_fact(&ctx->edb, "user", value),
               "add target users");
  }
  add_noise_facts(ctx, bench->size - useful, 64u);
}

static void populate_repeated_solve(bench_ctx_t *ctx) {
  for (size_t i = 0; i < 16u; ++i) {
    char value[32];
    snprintf(value, sizeof(value), "u%03zu", i);
    require_ok(maelys_datalog_edb_add_runtime_symbol_fact(&ctx->edb, "user", value),
               "add repeated user");
  }
}

static void populate_simple_join(bench_ctx_t *ctx, const bench_case_t *bench) {
  for (size_t i = 0; i < bench->size; ++i) {
    char user_value[32];
    snprintf(user_value, sizeof(user_value), "u%03zu", i);
    require_ok(maelys_datalog_edb_add_runtime_symbol_fact(&ctx->edb, "user", user_value),
               "add join user");
    const char *doc = (i % 2u) == 0u ? "doc.pdf" : "other.pdf";
    const char *pairs[2] = {user_value, doc};
    require_ok(maelys_datalog_edb_add_runtime_symbol_pair_facts(&ctx->edb,
                                                                "owns",
                                                                pairs,
                                                               1u),
               "add join owns");
  }
  require_ok(maelys_datalog_edb_add_runtime_symbol_fact(&ctx->edb, "target", "doc.pdf"),
             "add join target");
}

static void populate_noisy_join(bench_ctx_t *ctx, const bench_case_t *bench) {
  const size_t logical_total = bench->size;
  const size_t n = logical_total >= 128u ? 32u : 16u;
  for (size_t i = 0; i < n; ++i) {
    char user_value[32];
    char doc_value[32];
    snprintf(user_value, sizeof(user_value), "u%03zu", i);
    snprintf(doc_value, sizeof(doc_value), "doc%03zu", i % 16u);
    require_ok(maelys_datalog_edb_add_runtime_symbol_fact(&ctx->edb, "user", user_value),
               "add noisy user");
    const char *pairs[2] = {user_value, doc_value};
    require_ok(maelys_datalog_edb_add_runtime_symbol_pair_facts(&ctx->edb,
                                                                "owns",
                                                                pairs,
                                                                1u),
               "add noisy owns");
    if ((i % 2u) == 0u) {
      require_ok(maelys_datalog_edb_add_runtime_symbol_fact(&ctx->edb, "target", doc_value),
                 "add noisy target");
    }
  }
  const size_t current = ctx->edb.fact_count;
  if (logical_total > current) {
    add_noise_facts(ctx, logical_total - current, 64u);
  }
}

static void prepare_solver(bench_ctx_t *ctx, const bench_case_t *bench) {
  prepare_solve_base(ctx, bench);
  if (strncmp(bench->mode, "selectivity_", 12) == 0) {
    populate_selectivity(ctx, bench);
  } else if (strcmp(bench->mode, "noise_total") == 0) {
    populate_noise_total(ctx, bench);
  } else if (strcmp(bench->mode, "simple_join") == 0) {
    populate_simple_join(ctx, bench);
  } else if (strcmp(bench->mode, "noisy_join") == 0) {
    populate_noisy_join(ctx, bench);
  } else if (strcmp(bench->mode, "repeated") == 0) {
    populate_repeated_solve(ctx);
  } else if (strcmp(bench->mode, "absent_predicate") != 0) {
    populate_selectivity(ctx, bench);
  }
  ctx->query_terms[0].kind = MAELYS_DATALOG_TERM_SYMBOL;
  require_ok(maelys_datalog_edb_intern_runtime_symbol(&ctx->edb,
                                                      "u000",
                                                      &ctx->query_terms[0].as.symbol),
             "intern query symbol");
}

static uint64_t run_solver_once(bench_ctx_t *ctx, const bench_case_t *bench) {
  const size_t solve_count = bench->op_count;
  uint64_t acc = 0;
  for (size_t i = 0; i < solve_count; ++i) {
    require_ok(maelys_datalog_edb_finalize(&ctx->edb), "edb finalize");
    maelys_datalog_solve_result_t *result = NULL;
    maelys_result_t rc = maelys_datalog_solve_once(&ctx->ruleset, &ctx->edb, &result);
    acc += (uint64_t)rc;
    if (rc == MAELYS_OK) {
      bool solved = false;
      if (strcmp(bench->mode, "absent_predicate") != 0) {
        require_ok(maelys_datalog_query_solved_ground_fact(result,
                                                           "allow",
                                                           ctx->query_terms,
                                                           1u,
                                                           &solved),
                   "query solved fact");
      }
      acc += solved ? 17u : 3u;
      maelys_datalog_solve_result_free(result);
    }
  }
  consume_u64(acc);
  return acc;
}

static void cleanup_case(bench_ctx_t *ctx, const bench_case_t *bench) {
  (void)bench;
  maelys_datalog_ruleset_clear(&ctx->ruleset);
}

static repeated_payload_t run_payload_repeats(bench_ctx_t *ctx,
                                              const bench_case_t *bench,
                                              prepare_fn_t prepare,
                                              run_fn_t run,
                                              bool cleanup_ruleset,
                                              size_t repeats) {
  uint64_t acc = 0;
  uint64_t payload_ns = 0;
  for (size_t i = 0; i < repeats; ++i) {
    prepare(ctx, bench);
    const uint64_t start = now_ns();
    acc += run(ctx, bench);
    payload_ns += now_ns() - start;
    if (cleanup_ruleset) {
      cleanup_case(ctx, bench);
    }
  }
  consume_u64(acc);
  return (repeated_payload_t){.payload_ns = payload_ns, .acc = acc};
}

static size_t calibrate_inner_repeats(bench_ctx_t *ctx,
                                      const bench_case_t *bench,
                                      prepare_fn_t prepare,
                                      run_fn_t run,
                                      bool cleanup_ruleset) {
  uint64_t payload_ns = 0;
  size_t repeats = 0u;
  size_t batch = 1u;
  while (payload_ns < INNER_REPEAT_TARGET_NS && repeats < INNER_REPEAT_MAX) {
    size_t remaining = INNER_REPEAT_MAX - repeats;
    size_t batch_repeats = batch < remaining ? batch : remaining;
    repeated_payload_t measured =
        run_payload_repeats(ctx, bench, prepare, run, cleanup_ruleset, batch_repeats);
    payload_ns += measured.payload_ns;
    repeats += batch_repeats;
    consume_u64(measured.acc);
    if (batch < (INNER_REPEAT_MAX / 2u)) {
      batch *= 2u;
    } else {
      batch = INNER_REPEAT_MAX - repeats;
      if (batch == 0u) {
        break;
      }
    }
  }
  return repeats == 0u ? 1u : repeats;
}

static void measure_case(bench_output_t *out,
                         const bench_case_t *bench,
                         prepare_fn_t prepare,
                         run_fn_t run,
                         bool cleanup_ruleset) {
  bench_ctx_t ctx;
  memset(&ctx, 0, sizeof(ctx));
  init_registry(&ctx);
  const size_t inner_repeats =
      calibrate_inner_repeats(&ctx, bench, prepare, run, cleanup_ruleset);

  uint64_t warmup_start = now_ns();
  uint64_t warmup_elapsed = 0;
  size_t warmup_iters = 0;
  while (warmup_elapsed < WARMUP_MIN_NS || warmup_iters < WARMUP_MIN_ITERS) {
    repeated_payload_t measured =
        run_payload_repeats(&ctx, bench, prepare, run, cleanup_ruleset, 1u);
    consume_u64(measured.acc);
    ++warmup_iters;
    warmup_elapsed = now_ns() - warmup_start;
  }

  double *durations = calloc(out->samples, sizeof(*durations));
  if (!durations) {
    perror("calloc durations");
    exit(2);
  }
  uint64_t measured_total_ns = 0;
  double total_ns = 0.0;
  double min_ns = HUGE_VAL;
  double max_ns = 0.0;
  size_t max_inner_repeats_used = inner_repeats;
  for (size_t i = 0; i < out->samples; ++i) {
    size_t sample_repeats = inner_repeats;
    repeated_payload_t measured = {0};
    while (sample_repeats <= INNER_REPEAT_MAX) {
      measured = run_payload_repeats(&ctx, bench, prepare, run, cleanup_ruleset, sample_repeats);
      if (measured.payload_ns > 0u) {
        break;
      }
      if (sample_repeats > (INNER_REPEAT_MAX / 2u)) {
        break;
      }
      sample_repeats *= 2u;
    }
    consume_u64(measured.acc);
    require_true(measured.payload_ns > 0u, "positive raw timing");
    if (sample_repeats > max_inner_repeats_used) {
      max_inner_repeats_used = sample_repeats;
    }
    measured_total_ns += measured.payload_ns;
    const double per_workload_ns = (double)measured.payload_ns / (double)sample_repeats;
    durations[i] = per_workload_ns;
    total_ns += per_workload_ns;
    if (per_workload_ns < min_ns) {
      min_ns = per_workload_ns;
    }
    if (per_workload_ns > max_ns) {
      max_ns = per_workload_ns;
    }
  }

  qsort(durations, out->samples, sizeof(*durations), compare_double);
  const double median_ns = out->samples % 2u == 0u
                               ? (durations[(out->samples / 2u) - 1u] +
                                  durations[out->samples / 2u]) /
                                     2.0
                               : durations[out->samples / 2u];
  size_t p95_index = ((out->samples * 95u) + 99u) / 100u;
  if (p95_index == 0u) {
    p95_index = 1u;
  }
  --p95_index;
  if (p95_index >= out->samples) {
    p95_index = out->samples - 1u;
  }
  const double p95_ns = durations[p95_index];
  free(durations);

  require_true(total_ns > 0.0 && median_ns > 0.0 && p95_ns > 0.0, "positive timings");
  const double op_count = (double)(bench->op_count == 0u ? bench->size : bench->op_count);
  const double ops_per_sec = (op_count * (double)out->samples * 1000000000.0) / total_ns;

  if (out->row_count >= MAX_RESULTS) {
    fprintf(stderr, "too many benchmark rows\n");
    exit(2);
  }
  bench_result_t *row = &out->rows[out->row_count++];
  memset(row, 0, sizeof(*row));
  copy_string(row->benchmark, sizeof(row->benchmark), bench->benchmark);
  copy_string(row->group, sizeof(row->group), bench->group);
  copy_string(row->mode, sizeof(row->mode), bench->mode);
  row->size = bench->size;
  row->selectivity = bench->selectivity;
  row->samples = out->samples;
  row->inner_repeats = max_inner_repeats_used;
  row->warmup = warmup_iters;
  row->warmup_us = (double)warmup_elapsed / 1000.0;
  row->median_us = median_ns / 1000.0;
  row->p95_us = p95_ns / 1000.0;
  row->min_us = min_ns / 1000.0;
  row->max_us = max_ns / 1000.0;
  row->total_us = total_ns / 1000.0;
  row->measured_total_us = (double)measured_total_ns / 1000.0;
  row->ops_per_sec = ops_per_sec;
  copy_string(row->op_unit, sizeof(row->op_unit), bench->op_unit);

  fprintf(out->csv,
          "%s,%s,%s,%zu,%.6f,%zu,%zu,%zu,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%s,%s,%s,%s,%s,%s\n",
          row->benchmark,
          row->group,
          row->mode,
          row->size,
          row->selectivity,
          row->samples,
          row->inner_repeats,
          row->warmup,
          row->warmup_us,
          row->median_us,
          row->p95_us,
          row->min_us,
          row->max_us,
          row->total_us,
          row->measured_total_us,
          row->ops_per_sec,
          row->op_unit,
          BENCH_COMMIT,
          BENCH_COMPILER,
          BENCH_CFLAGS,
          BENCH_OPT_LEVEL,
          out->date_utc);
  fflush(out->csv);
}

static void write_json(const char *path, const bench_output_t *out) {
  FILE *fp = fopen(path, "w");
  if (!fp) {
    perror(path);
    exit(2);
  }

  char host[128] = "unknown";
  (void)gethostname(host, sizeof(host));
  host[sizeof(host) - 1u] = '\0';

  struct utsname un;
  const char *sysname = "unknown";
  const char *release = "unknown";
  const char *machine = "unknown";
  if (uname(&un) == 0) {
    sysname = un.sysname;
    release = un.release;
    machine = un.machine;
  }

  fprintf(fp,
          "{\n"
          "  \"metadata\": {\n"
          "    \"schema_version\": 1,\n"
          "    \"commit\": \"%s\",\n"
          "    \"dirty\": %s,\n"
          "    \"date_utc\": \"%s\",\n"
          "    \"compiler\": \"%s\",\n"
          "    \"cflags\": \"%s\",\n"
          "    \"opt_level\": \"%s\",\n"
          "    \"host\": \"%s\",\n"
          "    \"os\": \"%s %s\",\n"
          "    \"cpu\": \"%s\",\n"
          "    \"notes\": \"hot-cache / warm-pool native microbenchmarks; timing-only C49 run\"\n"
          "  },\n"
          "  \"results\": [\n",
          BENCH_COMMIT,
          strcmp(BENCH_DIRTY, "true") == 0 ? "true" : "false",
          out->date_utc,
          BENCH_COMPILER,
          BENCH_CFLAGS,
          BENCH_OPT_LEVEL,
          host,
          sysname,
          release,
          machine);

  for (size_t i = 0; i < out->row_count; ++i) {
    const bench_result_t *row = &out->rows[i];
    fprintf(fp,
            "    {\"benchmark\":\"%s\",\"group\":\"%s\",\"mode\":\"%s\","
            "\"size\":%zu,\"selectivity\":%.6f,\"samples\":%zu,"
            "\"inner_repeats\":%zu,\"warmup\":%zu,\"warmup_us\":%.6f,\"median_us\":%.6f,"
            "\"p95_us\":%.6f,\"min_us\":%.6f,\"max_us\":%.6f,"
            "\"total_us\":%.6f,\"measured_total_us\":%.6f,\"ops_per_sec\":%.6f,"
            "\"op_unit\":\"%s\",\"commit\":\"%s\",\"compiler\":\"%s\","
            "\"cflags\":\"%s\",\"opt_level\":\"%s\",\"date_utc\":\"%s\"}%s\n",
            row->benchmark,
            row->group,
            row->mode,
            row->size,
            row->selectivity,
            row->samples,
            row->inner_repeats,
            row->warmup,
            row->warmup_us,
            row->median_us,
            row->p95_us,
            row->min_us,
            row->max_us,
            row->total_us,
            row->measured_total_us,
            row->ops_per_sec,
            row->op_unit,
            BENCH_COMMIT,
            BENCH_COMPILER,
            BENCH_CFLAGS,
            BENCH_OPT_LEVEL,
            out->date_utc,
            i + 1u == out->row_count ? "" : ",");
  }
  fprintf(fp, "  ]\n}\n");
  fclose(fp);
}

static void run_all(bench_output_t *out) {
  static const size_t intern_sizes[] = {1u, 8u, 16u, 32u, 64u, 128u, 256u, 512u};
  static const size_t fact_sizes[] = {1u, 8u, 16u, 32u, 64u};
  static const size_t selectivity_totals[] = {800u, 640u, 128u, 64u};
  static const char *selectivity_modes[] = {
      "selectivity_1pct", "selectivity_10pct", "selectivity_50pct", "selectivity_100pct"};
  static const double selectivities[] = {0.01, 0.10, 0.50, 1.00};
  static const size_t noise_totals[] = {64u, 128u, 256u, 512u, 960u};
  static const size_t repeat_sizes[] = {1u, 10u, 100u, 1000u};
  static const size_t join_sizes[] = {8u, 16u, 32u, 64u};
  static const size_t noisy_join_sizes[] = {128u, 512u, 960u};

  for (size_t i = 0; i < sizeof(intern_sizes) / sizeof(intern_sizes[0]); ++i) {
    bench_case_t bench = {"intern_distinct_symbols",
                          "ingestion",
                          "distinct",
                          intern_sizes[i],
                          1.0,
                          "symbols/sec",
                          intern_sizes[i]};
    measure_case(out, &bench, prepare_intern_distinct, run_intern_distinct, false);
    bench.benchmark = "reintern_existing_symbols";
    bench.mode = "reintern";
    measure_case(out, &bench, prepare_intern_reexisting, run_intern_reexisting, false);
    bench.benchmark = "mixed_intern_symbols";
    bench.mode = "mixed";
    measure_case(out, &bench, prepare_intern_mixed, run_intern_mixed, false);
  }

  for (size_t i = 0; i < sizeof(fact_sizes) / sizeof(fact_sizes[0]); ++i) {
    const size_t n = fact_sizes[i];
    bench_case_t bench = {"edb_symbol_id_insert", "ingestion", "unit_unary", n, 1.0, "facts/sec", n};
    measure_case(out, &bench, prepare_symbol_insert, run_symbol_unit_unary, false);
    bench.mode = "batch_unary";
    measure_case(out, &bench, prepare_symbol_insert, run_symbol_batch_unary, false);
    bench.mode = "unit_binary";
    measure_case(out, &bench, prepare_symbol_insert, run_symbol_unit_binary, false);
    bench.mode = "batch_binary";
    measure_case(out, &bench, prepare_symbol_insert, run_symbol_batch_binary, false);

    bench.benchmark = "edb_runtime_string_insert";
    bench.mode = "unit_unary";
    measure_case(out, &bench, prepare_string_insert, run_string_unit_unary, false);
    bench.mode = "batch_unary";
    measure_case(out, &bench, prepare_string_insert, run_string_batch_unary, false);

    bench.benchmark = "edb_runtime_string_pair_insert";
    bench.mode = "composed_unit_binary";
    measure_case(out, &bench, prepare_string_insert, run_string_composed_binary, false);
    bench.mode = "batch_binary";
    measure_case(out, &bench, prepare_string_insert, run_string_batch_binary, false);
  }

  for (size_t i = 0; i < sizeof(selectivity_totals) / sizeof(selectivity_totals[0]); ++i) {
    bench_case_t bench = {"solver_predicate_dense_ranges",
                          "solver",
                          selectivity_modes[i],
                          selectivity_totals[i],
                          selectivities[i],
                          "solve_calls/sec",
                          1u};
    measure_case(out, &bench, prepare_solver, run_solver_once, true);
  }

  for (size_t i = 0; i < sizeof(noise_totals) / sizeof(noise_totals[0]); ++i) {
    bench_case_t bench = {"solver_predicate_dense_ranges",
                          "solver",
                          "noise_total",
                          noise_totals[i],
                          noise_totals[i] == 64u ? 0.125 : (16.0 / (double)noise_totals[i]),
                          "solve_calls/sec",
                          1u};
    measure_case(out, &bench, prepare_solver, run_solver_once, true);
  }

  bench_case_t absent = {"solver_predicate_dense_ranges",
                         "solver",
                         "absent_predicate",
                         512u,
                         0.0,
                         "solve_calls/sec",
                         1u};
  measure_case(out, &absent, prepare_solver, run_solver_once, true);

  for (size_t i = 0; i < sizeof(repeat_sizes) / sizeof(repeat_sizes[0]); ++i) {
    bench_case_t bench = {"solver_repeated_solve",
                          "solver",
                          "repeated",
                          repeat_sizes[i],
                          1.0,
                          "solve_calls/sec",
                          repeat_sizes[i]};
    measure_case(out, &bench, prepare_solver, run_solver_once, true);
  }

  for (size_t i = 0; i < sizeof(join_sizes) / sizeof(join_sizes[0]); ++i) {
    bench_case_t bench = {"solver_join_bindings",
                          "solver",
                          "simple_join",
                          join_sizes[i],
                          0.5,
                          "solve_calls/sec",
                          1u};
    measure_case(out, &bench, prepare_solver, run_solver_once, true);
  }

  for (size_t i = 0; i < sizeof(noisy_join_sizes) / sizeof(noisy_join_sizes[0]); ++i) {
    bench_case_t bench = {"solver_join_bindings",
                          "solver",
                          "noisy_join",
                          noisy_join_sizes[i],
                          0.5,
                          "solve_calls/sec",
                          1u};
    measure_case(out, &bench, prepare_solver, run_solver_once, true);
  }
}

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: %s <results.csv> <results.json>\n", argv[0]);
    return 2;
  }

  bench_output_t out;
  memset(&out, 0, sizeof(out));
  out.samples = sample_count();
  fill_date_utc(out.date_utc);

  out.csv = fopen(argv[1], "w");
  if (!out.csv) {
    perror(argv[1]);
    return 2;
  }
  fprintf(out.csv,
          "benchmark,group,mode,size,selectivity,samples,inner_repeats,warmup,warmup_us,"
          "median_us,p95_us,min_us,max_us,total_us,measured_total_us,"
          "ops_per_sec,op_unit,"
          "commit,compiler,cflags,opt_level,date_utc\n");

  run_all(&out);
  fclose(out.csv);
  write_json(argv[2], &out);
  fprintf(stderr,
          "wrote %zu rows to %s and %s (sink=%" PRIu64 ")\n",
          out.row_count,
          argv[1],
          argv[2],
          (uint64_t)bench_sink);
  return 0;
}
