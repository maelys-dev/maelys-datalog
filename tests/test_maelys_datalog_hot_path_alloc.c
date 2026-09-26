/* SPDX-License-Identifier: MPL-2.0 */
#include "tests/fixtures/allocation_guard.h"
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef memset
#include "maelys/datalog.h"
#include "maelys/datalog_advanced.h"
#include "src/core/maelys_datalog_solver.h"
#include "src/core/maelys_datalog_domain_registry.h"
#include "src/core/maelys_datalog_ruleset.h"
#include "src/core/maelys_datalog_edb.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int forbidden;
static size_t attempts, hot_frees, live, total, fault_after = SIZE_MAX;
static size_t memset_bytes, max_release_bytes, allocated_bytes;
void *maelys_test_memset(void *p, int value, size_t n) {
    memset_bytes += n;
    return memset(p, value, n);
}
static void release_bounded(maelys_datalog_result_t *result) {
    size_t before = memset_bytes;
    assert(maelys_datalog_result_free(result) == 0);
    size_t written = memset_bytes - before;
    /* Source-level bulk reset accounting (not a hardware store counter).
     * Both profiles must reset metadata only, never their 200+ KiB payload.
     * The memset hook keeps these writes observable even when optimized. */
    assert(written > 0u && written <= 4096u);
    if (written > max_release_bytes) max_release_bytes = written;
}
static void owned_release_does_not_clear(void) {
    static maelys_datalog_internal_ruleset_t ruleset;
    static maelys_datalog_internal_edb_t edb;
    static maelys_datalog_internal_fact_t facts[1];
    assert(maelys_datalog_ruleset_init(&ruleset, "empty", "hot_path", "", 1) == MAELYS_OK);
    assert(maelys_datalog_edb_init(&edb, facts, 1u, &ruleset.symbols, &ruleset.registry) == MAELYS_OK);
    assert(maelys_datalog_edb_finalize(&edb) == MAELYS_OK);
    maelys_datalog_internal_solve_result_t *result = NULL;
    size_t baseline = live;
    assert(maelys_datalog_solve_once(&ruleset, &edb, &result) == MAELYS_OK);
    size_t before = memset_bytes;
    maelys_datalog_solve_result_free(result);
    assert(memset_bytes == before && live == baseline);
}
static int refuse(void) {
    ++total;
    if (forbidden) { ++attempts; return 1; }
    if (fault_after == 0u) return 1;
    if (fault_after != SIZE_MAX) --fault_after;
    return 0;
}
void *maelys_test_malloc(size_t n) {
    if (refuse()) return NULL;
    void *p = malloc(n);
    if (p) {
        /* In particular, the native result workspace must initialize every
         * live payload entry without relying on fresh allocator zeroes. */
        memset(p, 0xa5, n);
        ++live; allocated_bytes += n;
    }
    return p;
}
void *maelys_test_calloc(size_t n, size_t width) {
    if (refuse()) return NULL;
    void *p = calloc(n, width); if (p) { ++live; allocated_bytes += n * width; } return p;
}
void *maelys_test_realloc(void *p, size_t n) {
    if (refuse()) return NULL;
    int had_pointer = p != NULL;
    void *q = realloc(p, n);
    if (q && !had_pointer) ++live;
    if (!q && had_pointer && !n) --live;
    return q;
}
void maelys_test_free(void *p) {
    if (forbidden && p) ++hot_frees;
    if (p) { assert(live); --live; }
    free(p);
}
static maelys_datalog_value_t symbol(const char *s) {
    maelys_datalog_value_t v = {.kind=MAELYS_DATALOG_VALUE_SYMBOL};
    v.as.symbol = s; return v;
}
static void caller_owned_policy_snapshot(void) {
    size_t bytes, alignment;
    assert(maelys_datalog_policy_storage_requirements(&bytes, &alignment) == 0);
    void *storage = malloc(bytes); assert(storage && (uintptr_t)storage % alignment == 0);
    maelys_datalog_policy_t *policy = NULL;
    const char source[] = "allow(X) :- seed(X).";
    assert(maelys_datalog_policy_load_frontend_in(storage, bytes, "hot_path", "copied",
        source, sizeof(source) - 1, NULL, &policy, NULL) == 0);
    const size_t before = total, bytes_before = allocated_bytes, baseline = live;
    maelys_datalog_session_t *session = NULL;
    assert(maelys_datalog_session_create(policy, 0, &session) == 0);
    const size_t reservation = allocated_bytes - bytes_before;
    assert(total - before == 3u);
#ifdef MAELYS_DATALOG_PROFILE_LARGE
    assert(reservation <= 1300000u);
#else
    assert(reservation <= 900000u);
#endif
    assert(maelys_datalog_policy_free(policy) == 0);
    memset(storage, 0xa5, bytes); free(storage);
    forbidden = 1;
    maelys_datalog_fact_t fact = {.predicate = "seed", .arity = 1};
    fact.terms[0] = symbol("caller-storage-reused");
    maelys_datalog_result_t *result = NULL;
    assert(maelys_datalog_session_solve(session, &fact, 1, &result, NULL) == 0);
    int present;
    assert(maelys_datalog_result_query(result, "allow", fact.terms, 1, &present) == 0 && present);
    release_bounded(result);
    forbidden = 0;
    assert(maelys_datalog_session_free(session) == 0 && live == baseline);
    printf("copied session reservation: 3 allocations, %zu bytes\n", reservation);
}
static void aggregate_without_allocator(unsigned op) {
    const char *names[] = {"count", "min", "max", "sum"};
    char source[100];
    snprintf(source,sizeof(source),"allow(N) :- %s(I,seed(I),N).",names[op]);
    maelys_datalog_policy_t *policy = NULL;
    assert(maelys_datalog_policy_load_inline("hot_path", "count", source,
        strlen(source), &policy, NULL) == 0);
    maelys_datalog_session_config_t *config = NULL;
    maelys_datalog_session_t *session = NULL;
    assert(maelys_datalog_session_config_create(&config) == 0);
    assert(maelys_datalog_session_config_set_explanation_workspace(config,
        MAELYS_DATALOG_EXPLAIN_TRUE | MAELYS_DATALOG_EXPLAIN_FALSE) == 0);
    assert(maelys_datalog_session_create_configured(policy, 0, config, &session) == 0);
    assert(maelys_datalog_session_config_free(config) == 0);
    maelys_datalog_fact_t facts[40] = {0};
    for (size_t i = 0; i < 40; ++i) {
        facts[i].predicate = "seed"; facts[i].arity = 1;
        facts[i].terms[0].kind = MAELYS_DATALOG_VALUE_INTEGER;
        facts[i].terms[0].as.integer = (int64_t)(39u-i);
    }
    forbidden = 1;
    for (size_t n = 0; n <= 40; n += 8) {
        maelys_datalog_result_t *result = NULL;
        assert(maelys_datalog_session_solve(session,facts,n,&result,NULL) == 0);
        int64_t expected = op == 0 ? (int64_t)n : op == 1 ? (int64_t)(40u-n) : op == 2 ? 39 : (int64_t)(n*(79u-n)/2u);
        const int empty = !n && (op == 1 || op == 2);
        maelys_datalog_value_t query = {.kind=MAELYS_DATALOG_VALUE_INTEGER,.as.integer=expected};
        int present;
        assert(maelys_datalog_result_query(result,"allow",&query,1,&present) == 0 && present == !empty);
        char text[4096]; size_t required;
        if (!empty) {
            assert(maelys_datalog_result_explain_true_text(result,"allow",&query,1,
                text,sizeof(text),&required) == 0);
            char marker[32];snprintf(marker,sizeof(marker),"kind=%s",names[op]);
            assert(strstr(text,marker) && strstr(text,"status=complete"));
        }
        ++query.as.integer;
        assert(maelys_datalog_result_explain_false_text(result,"allow",&query,1,
            text,sizeof(text),&required) == 0);
        assert(strstr(text,empty ? "-empty" : "-mismatch"));
        release_bounded(result);
    }
    if (op) {
        const maelys_datalog_value_t bad[] = {
            {.kind=MAELYS_DATALOG_VALUE_INTEGER,.as.integer=-1},
            {.kind=MAELYS_DATALOG_VALUE_INTEGER,.as.integer=INT64_MIN},
            {.kind=MAELYS_DATALOG_VALUE_INTEGER,.as.integer=INT64_MAX},
            {.kind=MAELYS_DATALOG_VALUE_INTEGER,.as.integer=INT64_C(2147483648)},
            {.kind=MAELYS_DATALOG_VALUE_BOOLEAN,.as.boolean=1},
            {.kind=MAELYS_DATALOG_VALUE_SYMBOL,.as.symbol="wrong"},
        };
        const char *tokens[] = {"-1","-9223372036854775808","9223372036854775807","2147483648","true","wrong"};
        for (size_t i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
            maelys_datalog_result_t *result = NULL;
            maelys_datalog_diagnostic_t diag = MAELYS_DATALOG_DIAGNOSTIC_INIT;
            facts[0].terms[0] = bad[i];
            assert(maelys_datalog_session_solve(session,facts,1,&result,&diag) == MAELYS_DATALOG_STATUS_INVALID_FIELD && !result);
            assert(diag.code == MAELYS_DATALOG_DIAG_SOLVE_AGGREGATE_DOMAIN_ERROR);
            assert(!strcmp(maelys_datalog_diag_code_name(diag.code),"solve_aggregate_domain_error"));
            assert(diag.present == (MAELYS_DATALOG_DIAGNOSTIC_AGGREGATE |
                MAELYS_DATALOG_DIAGNOSTIC_PREDICATE | MAELYS_DATALOG_DIAGNOSTIC_CONTEXT));
            assert(!strcmp(diag.predicate,"seed") && diag.arity == 1 && diag.term_index == 0);
            assert(!strcmp(diag.token,tokens[i]) && !strcmp(diag.field,names[op]));
            assert(diag.limit == INT32_MAX && diag.lhs_kind == (unsigned)bad[i].kind);
            assert(diag.struct_size == sizeof(diag) && diag.abi_version == MAELYS_DATALOG_DIAGNOSTIC_ABI_VERSION);
            /* A diagnostic snapshot survives subsequent success and vocabulary reset. */
            assert(maelys_datalog_session_solve(session,NULL,0,&result,NULL) == 0);
            release_bounded(result);
            assert(!strcmp(diag.token,tokens[i]));
        }
        if (op == 3) {
            maelys_datalog_result_t *result = NULL;
            maelys_datalog_diagnostic_t diag = MAELYS_DATALOG_DIAGNOSTIC_INIT;
            facts[0].terms[0].kind=MAELYS_DATALOG_VALUE_INTEGER;
            facts[0].terms[0].as.integer=INT32_MAX;
            facts[1].terms[0].as.integer=1;
            assert(maelys_datalog_session_solve(session,facts,2,&result,&diag) == MAELYS_DATALOG_STATUS_INVALID_FIELD && !result);
            assert(diag.code == MAELYS_DATALOG_DIAG_SOLVE_SUM_OVERFLOW);
            assert(!strcmp(maelys_datalog_diag_code_name(diag.code),"solve_sum_overflow"));
            assert(!strcmp(diag.predicate,"seed") && !strcmp(diag.field,"sum"));
            assert(!strcmp(diag.token,"2147483648") && diag.limit == INT32_MAX);
            assert(!(diag.present & MAELYS_DATALOG_DIAGNOSTIC_CAPACITY));
            assert(maelys_datalog_session_solve(session,NULL,0,&result,&diag) == 0);
            assert(diag.present == 0 && diag.token[0] == 0);
            release_bounded(result);
        }
    }
    assert(attempts == 0 && hot_frees == 0);
    forbidden = 0;
    assert(maelys_datalog_session_free(session) == 0);
    assert(maelys_datalog_policy_free(policy) == 0);
}

/* Same declaration through both entry points, with all engine allocation
 * forbidden. Run last: the final cases deliberately exhaust the global slots. */
static void predicate_declarations_without_allocator(void) {
    char low_name[] = "decl_owned_low", public_name[] = "decl_owned_public";
    char name[] = "edge", atom[] = "alice", description[] = "owned metadata";
    const char *atoms[] = {atom};
    maelys_datalog_predicate_t declarations[] = {
        {name, 2u, MAELYS_DATALOG_PREDICATE_EDB},
    };
    maelys_datalog_domain_def_t low = {
        .domain_name = low_name, .predicates = declarations, .predicate_count = 1u,
        .atoms = atoms, .atom_count = 1u, .description = description,
    };
    maelys_datalog_domain_t public = {public_name, declarations, 1u, atoms, 1u};
    forbidden = 1;
    assert(maelys_datalog_domain_registry_register(&low) == MAELYS_OK);
    assert(maelys_datalog_domain_register(&public) == MAELYS_DATALOG_STATUS_OK);
    assert(maelys_datalog_domain_register(&public) == MAELYS_DATALOG_STATUS_OK);
    declarations[0].arity = 1u;
    assert(maelys_datalog_domain_register(&public) == MAELYS_DATALOG_STATUS_INVALID_FIELD);
    declarations[0].flags = 0u;
    memset(name, 'x', sizeof(name) - 1u);
    memset(atom, 'x', sizeof(atom) - 1u);
    memset(description, 'x', sizeof(description) - 1u);
    memset(low_name, 'x', sizeof(low_name) - 1u);
    memset(public_name, 'x', sizeof(public_name) - 1u);
    const maelys_datalog_domain_entry_t *owned = maelys_datalog_domain_registry_find("decl_owned_low");
    assert(owned && strcmp(owned->description, "owned metadata") == 0);
    assert(strcmp(owned->predicates[0].name, "edge") == 0);
    assert(owned->predicates[0].arity == 2u && owned->predicates[0].kind_flags == MAELYS_DATALOG_PREDICATE_EDB);
    assert(strcmp(owned->atoms[0], "alice") == 0);
    maelys_datalog_predicate_registry_t a, b;
    maelys_datalog_predicate_registry_init(&a);
    maelys_datalog_predicate_registry_init(&b);
    assert(maelys_datalog_domain_registry_install("decl_owned_low", &a) == MAELYS_OK);
    assert(maelys_datalog_domain_registry_install("decl_owned_public", &b) == MAELYS_OK);
    assert(memcmp(&a, &b, sizeof(a)) == 0);

    char long_name[65]; memset(long_name, 'p', 64u); long_name[64] = '\0';
    maelys_datalog_predicate_t batch[] = {{"first", 1u, MAELYS_DATALOG_PREDICATE_EDB},
                                               {long_name, 1u, MAELYS_DATALOG_PREDICATE_EDB}};
    low = (maelys_datalog_domain_def_t){.domain_name="decl_retry", .predicates=batch, .predicate_count=2u};
    public = (maelys_datalog_domain_t){"decl_retry_public", batch, 2u, NULL, 0u};
    assert(maelys_datalog_domain_registry_register(&low) == MAELYS_ERR_INVALID_FIELD);
    assert(maelys_datalog_domain_register(&public) == MAELYS_DATALOG_STATUS_INVALID_FIELD);
    assert(!maelys_datalog_domain_registry_find(low.domain_name));
    assert(!maelys_datalog_domain_registry_find(public.name));
    batch[1].name = NULL;
    assert(maelys_datalog_domain_registry_register(&low) == MAELYS_ERR_INVALID_FIELD);
    assert(maelys_datalog_domain_register(&public) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    batch[1].name = "";
    assert(maelys_datalog_domain_registry_register(&low) == MAELYS_ERR_INVALID_FIELD);
    assert(maelys_datalog_domain_register(&public) == MAELYS_DATALOG_STATUS_INVALID_FIELD);
    batch[1].name = long_name; long_name[63] = '\0';
    assert(maelys_datalog_domain_registry_register(&low) == MAELYS_OK);
    assert(maelys_datalog_domain_register(&public) == MAELYS_DATALOG_STATUS_OK);
    memset(long_name, 'x', 63u);
    assert(maelys_datalog_domain_registry_find("decl_retry")->predicates[1].name[0] == 'p');

    char names[MAELYS_DATALOG_MAX_PREDICATES][16];
    maelys_datalog_predicate_t full[MAELYS_DATALOG_MAX_PREDICATES];
    for (size_t i = 0; i < MAELYS_DATALOG_MAX_PREDICATES; ++i) {
        snprintf(names[i], sizeof(names[i]), "p%zu", i);
        full[i] = (maelys_datalog_predicate_t){names[i], 1u, MAELYS_DATALOG_PREDICATE_EDB};
    }
    low = (maelys_datalog_domain_def_t){.domain_name="decl_full", .predicates=full,
        .predicate_count=MAELYS_DATALOG_MAX_PREDICATES + 1u};
    assert(maelys_datalog_domain_registry_register(&low) == MAELYS_ERR_PAYLOAD_TOO_LARGE);
    --low.predicate_count;
    assert(maelys_datalog_domain_registry_register(&low) == MAELYS_OK);
    maelys_datalog_predicate_registry_init(&a);
    assert(maelys_datalog_domain_registry_install(low.domain_name, &a) == MAELYS_OK);
    assert(a.count == MAELYS_DATALOG_MAX_PREDICATES);
    maelys_result_t rc = MAELYS_OK;
    for (size_t i = 0; i <= MAELYS_DATALOG_MAX_REGISTERED_DOMAINS && rc == MAELYS_OK; ++i) {
        char domain[32]; snprintf(domain, sizeof(domain), "decl_capacity_%zu", i);
        low.domain_name = domain; low.predicate_count = 1u;
        rc = maelys_datalog_domain_registry_register(&low);
    }
    assert(rc == MAELYS_ERR_PAYLOAD_TOO_LARGE);
    low.domain_name = "decl_owned_low";
    assert(maelys_datalog_domain_registry_register(&low) == MAELYS_OK);
    assert(strcmp(owned->predicates[0].name, "edge") == 0);
    assert(attempts == 0u && hot_frees == 0u);
    forbidden = 0;
}

int main(void) {
    const maelys_datalog_predicate_t predicates[] = {
        {"seed", 1, MAELYS_DATALOG_PREDICATE_EDB},
        {"blocked", 1, MAELYS_DATALOG_PREDICATE_EDB},
        {"allow", 1, MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY},
    };
    const maelys_datalog_domain_t domain = {"hot_path", predicates, 3, NULL, 0};
    assert(maelys_datalog_domain_register(&domain) == 0);
    caller_owned_policy_snapshot();
    const char *source = "allow(X) :- seed(X), not(blocked(X)).";
    maelys_datalog_diagnostic_t diag = MAELYS_DATALOG_DIAGNOSTIC_INIT;
    maelys_datalog_policy_t *policy = NULL;
    assert(maelys_datalog_policy_load_inline("hot_path", "hot", source, strlen(source), &policy, &diag) == 0);
    owned_release_does_not_clear();
    for (unsigned op=0;op<4;++op) aggregate_without_allocator(op);
    maelys_datalog_session_t *session = NULL, *second = NULL, *filtered = NULL;
    size_t before = total, baseline = live, bytes_before = allocated_bytes;
    assert(maelys_datalog_session_create(policy, 0, &session) == 0);
    size_t create_allocations = total - before;
    size_t create_bytes = allocated_bytes - bytes_before;
    assert(create_allocations == 3u);
    /* Bound the total reservation, including both public/native result storage.
     * A second full ruleset copy must not silently return. */
#ifdef MAELYS_DATALOG_PROFILE_LARGE
    assert(create_bytes <= 980000u);
#else
    assert(create_bytes <= 550000u);
#endif
    size_t reset_before = memset_bytes;
    assert(maelys_datalog_session_free(session) == 0);
    assert(memset_bytes == reset_before);
    assert(live == baseline);
    for (size_t fail = 0; fail < create_allocations; ++fail) {
        fault_after = fail;
        session = (void *)(uintptr_t)1;
        assert(maelys_datalog_session_create(policy, 0, &session) != 0 && session == NULL);
        assert(live == baseline);
    }
    fault_after = SIZE_MAX;
    assert(maelys_datalog_session_create(policy, 0, &session) == 0);
    assert(maelys_datalog_session_create(policy, 0, &second) == 0);
    maelys_datalog_policy_t *filter_policy = NULL;
    const char *filter_source = "allow(X) :- seed(X), starts_with(X, \"docs/\").";
    assert(maelys_datalog_policy_load_inline("hot_path", "filter", filter_source,
        strlen(filter_source), &filter_policy, &diag) == 0);
    assert(maelys_datalog_session_create(filter_policy, 0, &filtered) == 0);
    /* Sessions own their compiled snapshot and dictionary, even when the
     * original policy storage has already been released. */
    assert(maelys_datalog_policy_free(filter_policy) == 0);
    assert(maelys_datalog_policy_free(policy) == 0);
    /* The retained allocation does not keep the released public handle open. */
    size_t policies = 99u;
    assert(maelys_datalog_policy_count(policy, &policies) == MAELYS_DATALOG_STATUS_INVALID_STATE);
    assert(policies == 99u);
    assert(maelys_datalog_policy_free(policy) == MAELYS_DATALOG_STATUS_INVALID_STATE);
    maelys_datalog_session_t *rejected_session = (void *)(uintptr_t)1;
    assert(maelys_datalog_session_create(policy, 0, &rejected_session) == MAELYS_DATALOG_STATUS_INVALID_STATE);
    assert(!rejected_session);
    maelys_datalog_input_edb_t *edb = NULL;
    assert(maelys_datalog_input_edb_create_with_capacity(MAELYS_DATALOG_MAX_FACTS_PER_PRED + 1u, 4096u, &edb) == 0);
    maelys_datalog_result_t *result = NULL, *other = NULL;
    char names[32][16];
    strcpy(names[0], "alpha"); strcpy(names[31], "zeta");
    for (size_t i = 1u; i < 31u; ++i)
        snprintf(names[i], sizeof(names[i]), "name-%02zu", i);
    forbidden = 1;
    for (size_t cycle = 0; cycle < 40u; ++cycle) {
        assert(maelys_datalog_input_edb_clear(edb) == 0);
        /* Sorting, duplicate text and multi-instance isolation are exercised. */
        /* More than the insertion cutoff: exercise the partitioning path in
         * both symbol and fact sorts, not just the tiny-array fast path. */
        maelys_datalog_fact_t facts[33] = {0};
        for (size_t i = 0; i < 32u; ++i) {
            facts[i].predicate = "seed"; facts[i].arity = 1;
            facts[i].terms[0] = symbol(names[cycle % 2u ? 31u-i : (i * 13u) % 32u]);
        }
        facts[32].predicate = "blocked"; facts[32].arity = 1;
        facts[32].terms[0] = symbol("zeta");
        assert(maelys_datalog_input_edb_add_facts(edb, facts, 33u, &diag) == 0);
        assert(maelys_datalog_session_solve_edb(session, edb, &result, &diag) == 0);
        assert(maelys_datalog_session_solve_edb(second, edb, &other, &diag) == 0);
        assert(maelys_datalog_session_free(session) == MAELYS_DATALOG_STATUS_INVALID_STATE);
        maelys_datalog_result_t *rejected = NULL;
        assert(maelys_datalog_session_solve_edb(session, edb, &rejected, &diag) == MAELYS_DATALOG_STATUS_INVALID_STATE && !rejected);
        assert(maelys_datalog_input_edb_clear(edb) == 0);
        maelys_datalog_value_t alpha = symbol("alpha"), zeta = symbol("zeta");
        int present = 0;
        assert(maelys_datalog_result_query(result, "allow", &alpha, 1u, &present) == 0 && present);
        assert(maelys_datalog_result_query(result, "allow", &zeta, 1u, &present) == 0 && !present);
        release_bounded(result);
        assert(maelys_datalog_result_query(other, "allow", &alpha, 1u, &present) == 0 && present);
        release_bounded(other);
        /* A rejected input must not poison the reserved result or symbols. */
        assert(maelys_datalog_input_edb_add_fact(edb, "unknown", &alpha, 1u, NULL) == 0);
        assert(maelys_datalog_session_solve_edb(session, edb, &result, &diag) != 0 && !result);
        assert(maelys_datalog_input_edb_clear(edb) == 0);
        size_t reset_before = memset_bytes;
        assert(maelys_datalog_session_solve_edb(session, edb, &result, &diag) == 0);
        /* Empty success resets bounded metadata/index, not whole fact/symbol
         * payloads. This source-level byte budget includes the full solve. */
        assert(memset_bytes - reset_before < 32768u);
        size_t derived = SIZE_MAX;
        assert(maelys_datalog_result_derived_fact_count(result, &derived) == 0 && derived == 0u);
        release_bounded(result);
    }
    /* Fill the native per-predicate capacity with the allocator disabled.
     * A subsequent duplicate still fails before dedup; rejection is reusable. */
    assert(maelys_datalog_input_edb_clear(edb) == 0);
    for (size_t i = 0; i < MAELYS_DATALOG_MAX_FACTS_PER_PRED; ++i) {
        maelys_datalog_value_t v = {.kind=MAELYS_DATALOG_VALUE_INTEGER, .as.integer=(int64_t)i};
        assert(maelys_datalog_input_edb_add_fact(edb, "seed", &v, 1u, NULL) == 0);
    }
    assert(maelys_datalog_session_solve_edb(session, edb, &result, &diag) == 0);
    release_bounded(result);
    maelys_datalog_value_t duplicate = {.kind=MAELYS_DATALOG_VALUE_INTEGER, .as.integer=0};
    assert(maelys_datalog_input_edb_add_fact(edb, "seed", &duplicate, 1u, NULL) == 0);
    assert(maelys_datalog_session_solve_edb(session, edb, &result, &diag) ==
           MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE && result == NULL);
    assert(strstr(diag.message, "per-predicate"));
    assert(maelys_datalog_input_edb_clear(edb) == 0);
    assert(maelys_datalog_session_solve_edb(session, edb, &result, &diag) == 0);
    release_bounded(result);
    /* Fail inside the solver (not just during input validation), then reuse the
     * same native result workspace. No result is published on filter errors. */
    maelys_datalog_value_t integer = {.kind=MAELYS_DATALOG_VALUE_INTEGER, .as.integer=7};
    assert(maelys_datalog_input_edb_add_fact(edb, "seed", &integer, 1u, NULL) == 0);
    assert(maelys_datalog_session_solve_edb(filtered, edb, &result, &diag) != 0 && !result);
    assert(maelys_datalog_input_edb_clear(edb) == 0);
    maelys_datalog_value_t path = symbol("docs/api");
    assert(maelys_datalog_input_edb_add_fact(edb, "seed", &path, 1u, NULL) == 0);
    assert(maelys_datalog_session_solve_edb(filtered, edb, &result, &diag) == 0);
    int present = 0;
    assert(maelys_datalog_result_query(result, "allow", &path, 1u, &present) == 0 && present);
    assert(attempts == 0u && hot_frees == 0u);
    forbidden = 0;
    /* Explanations remain deliberately outside the no-allocation contract. */
    char explanation[8192]; size_t required;
    before = total;
    assert(maelys_datalog_result_explain_true_text(result, "allow", &path, 1u,
        explanation, sizeof(explanation), &required) == 0);
    assert(total > before);
    forbidden = 1;
    release_bounded(result);
    assert(attempts == 0u && hot_frees == 0u);
    forbidden = 0;
    assert(maelys_datalog_input_edb_free(edb) == 0);
    assert(maelys_datalog_session_free(second) == 0);
    assert(maelys_datalog_session_free(session) == 0);
    assert(maelys_datalog_session_free(filtered) == 0);
    predicate_declarations_without_allocator();
    printf("reference hot path: 40 repeated transactions, zero allocator calls; %zu constructor failure points checked\n", create_allocations);
    printf("reference session reservation: %zu allocations, %zu bytes; destruction reset=0 bytes\n", create_allocations, create_bytes);
    printf("release reset: owned=0 bytes, reusable maximum=%zu bytes (budget=4096, both profiles)\n", max_release_bytes);
    return 0;
}
