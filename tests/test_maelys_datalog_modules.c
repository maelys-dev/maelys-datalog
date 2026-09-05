/* SPDX-License-Identifier: MPL-2.0 */
#include <maelys/datalog_module.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
#define OK(expr) CHECK((expr) == MAELYS_DATALOG_STATUS_OK)
extern maelys_datalog_status_t example_exact_match_register(void);

static maelys_datalog_status_t accept(const unsigned char *p, size_t n) {
    (void)p;
    (void)n;
    return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t reject(const unsigned char *p, size_t n) {
    (void)p;
    (void)n;
    return MAELYS_DATALOG_STATUS_INVALID_FIELD;
}
static maelys_datalog_status_t unit_cost(size_t v, size_t p, size_t *out) {
    (void)v;
    (void)p;
    *out = 1u;
    return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t huge_cost(size_t v, size_t p, size_t *out) {
    (void)v;
    (void)p;
    *out = SIZE_MAX;
    return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t zero_cost(size_t v, size_t p, size_t *out) {
    (void)v;
    (void)p;
    *out = 0u;
    return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t yes(const unsigned char *v, size_t vn, const unsigned char *p,
                                   size_t pn, int *out) {
    (void)v;
    (void)vn;
    (void)p;
    (void)pn;
    *out = 1;
    return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t bad_boolean(const unsigned char *v, size_t vn,
                                           const unsigned char *p, size_t pn, int *out) {
    (void)v;
    (void)vn;
    (void)p;
    (void)pn;
    *out = 2;
    return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t error_evaluate(const unsigned char *v, size_t vn,
                                              const unsigned char *p, size_t pn, int *out) {
    (void)v;
    (void)vn;
    (void)p;
    (void)pn;
    *out = 0;
    return MAELYS_DATALOG_STATUS_INTERNAL;
}
static maelys_datalog_status_t must_not_run(const unsigned char *v, size_t vn,
                                            const unsigned char *p, size_t pn, int *out) {
    (void)v;
    (void)vn;
    (void)p;
    (void)pn;
    (void)out;
    abort();
}
static maelys_datalog_filter_module_t filter_module(void) {
    return (maelys_datalog_filter_module_t){
        1u, sizeof(maelys_datalog_filter_module_t), "custom", "test.custom.v1", accept, unit_cost,
        yes};
}

static maelys_datalog_status_t domain(void) {
    static const maelys_datalog_public_predicate_t predicates[] = {
        {"ref", 1u, MAELYS_DATALOG_PREDICATE_EDB},
        {"other", 1u, MAELYS_DATALOG_PREDICATE_EDB},
        {"edge", 2u, MAELYS_DATALOG_PREDICATE_EDB},
        {"blocked", 1u, MAELYS_DATALOG_PREDICATE_EDB},
        {"allow", 1u, MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY},
    };
    static const char *const atoms[] = {"alice", "bob"};
    const maelys_datalog_public_domain_t d = {
        "modules", predicates, sizeof(predicates) / sizeof(predicates[0]), atoms, 2u};
    return maelys_datalog_domain_register(&d);
}
static maelys_datalog_status_t load(const char *source, maelys_datalog_policy_t **policy) {
    return maelys_datalog_policy_load_inline("modules", "modules.test", source, strlen(source),
                                             policy, NULL);
}
static maelys_datalog_public_value_t symbol(const char *s) {
    maelys_datalog_public_value_t v = {0};
    v.kind = MAELYS_DATALOG_VALUE_SYMBOL;
    v.as.symbol = s;
    return v;
}
static maelys_datalog_public_fact_t fact(const char *name, const char *s) {
    maelys_datalog_public_fact_t f = {0};
    f.predicate = name;
    f.arity = 1u;
    f.terms[0] = symbol(s);
    return f;
}

static int registration(void) {
    maelys_datalog_filter_module_t m = filter_module();
    CHECK(maelys_datalog_register_filter_module(NULL) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    m.abi_version = 2u;
    CHECK(maelys_datalog_register_filter_module(&m) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    m = filter_module();
    m.struct_size = 0u;
    CHECK(maelys_datalog_register_filter_module(&m) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    m = filter_module();
    m.evaluate = NULL;
    CHECK(maelys_datalog_register_filter_module(&m) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    m = filter_module();
    m.name = "not";
    CHECK(maelys_datalog_register_filter_module(&m) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    m = filter_module();
    m.semantic_id = "test\nforged";
    CHECK(maelys_datalog_register_filter_module(&m) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    m = filter_module();
    m.name = "starts_with";
    CHECK(maelys_datalog_register_filter_module(&m) == MAELYS_DATALOG_STATUS_INVALID_FIELD);
    m = filter_module();
    m.semantic_id = "string.contains.utf8-bytes-v1";
    CHECK(maelys_datalog_register_filter_module(&m) == MAELYS_DATALOG_STATUS_INVALID_FIELD);
    m = filter_module();
    OK(maelys_datalog_register_filter_module(&m));
    CHECK(maelys_datalog_register_filter_module(&m) == MAELYS_DATALOG_STATUS_INVALID_FIELD);
    for (size_t i = 1u; i < MAELYS_DATALOG_MODULE_MAX_FILTERS; ++i) {
        char name[64], id[128];
        snprintf(name, sizeof(name), "custom_%zu", i);
        snprintf(id, sizeof(id), "test.custom.%zu", i);
        m = filter_module();
        m.name = name;
        m.semantic_id = id;
        OK(maelys_datalog_register_filter_module(&m));
    }
    m = filter_module();
    m.name = "overflow";
    m.semantic_id = "test.overflow";
    CHECK(maelys_datalog_register_filter_module(&m) == MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
    OK(domain());
    maelys_datalog_policy_t *policy = NULL;
    OK(load("allow(X) :- ref(X), custom_1(X, \"x\").", &policy));
    CHECK(maelys_datalog_register_filter_module(&m) == MAELYS_DATALOG_STATUS_INVALID_STATE);
    OK(maelys_datalog_policy_free(policy));
    return 0;
}

static int lifecycle(void) {
    OK(example_exact_match_register());
    OK(domain());
    maelys_datalog_policy_t *policy = NULL;
    OK(load("allow(X) :- exact_match(X, \"alice\"), ref(X).", &policy));
    maelys_datalog_session_t *session = NULL;
    OK(maelys_datalog_session_create(policy, 0u, &session));
    OK(maelys_datalog_policy_free(policy));
    const maelys_datalog_public_fact_t facts[] = {fact("ref", "alice"), fact("ref", "bob")};
    char first[4096] = {0};
    for (int i = 0; i < 3; ++i) {
        maelys_datalog_result_t *result = NULL;
        OK(maelys_datalog_session_solve(session, facts, 2u, &result, NULL));
        maelys_datalog_public_value_t value = symbol("alice");
        int present = -1;
        OK(maelys_datalog_result_query(result, "allow", &value, 1u, &present));
        CHECK(present == 1);
        char text[4096];
        size_t required = 0u;
        OK(maelys_datalog_result_explain_true_text(result, "allow", &value, 1u, text, sizeof(text),
                                                   &required));
        CHECK(strstr(text, "example.exact-match.bytes-v1") != NULL);
        CHECK(strstr(text, "exact_match") != NULL);
        if (i == 0)
            strcpy(first, text);
        else
            CHECK(strcmp(first, text) == 0);
        value = symbol("bob");
        OK(maelys_datalog_result_query(result, "allow", &value, 1u, &present));
        CHECK(present == 0);
        OK(maelys_datalog_result_free(result));
    }
    OK(maelys_datalog_session_free(session));
    return 0;
}
static int missing_and_invalid(void) {
    maelys_datalog_filter_module_t m = filter_module();
    m.validate_pattern = reject;
    OK(maelys_datalog_register_filter_module(&m));
    OK(domain());
    maelys_datalog_policy_t *policy = NULL;
    CHECK(load("allow(X) :- ref(X), missing_module(X, \"x\").", &policy) !=
          MAELYS_DATALOG_STATUS_OK);
    CHECK(policy == NULL);
    CHECK(load("allow(X) :- ref(X), custom(X, \"x\").", &policy) ==
          MAELYS_DATALOG_STATUS_INVALID_FIELD);
    CHECK(policy == NULL);
    OK(load("allow(X) :- ref(X), starts_with(X, \"ali\").", &policy));
    OK(maelys_datalog_policy_free(policy));
    return 0;
}
static int failing_filter(maelys_datalog_filter_cost_fn cost,
                          maelys_datalog_filter_evaluate_fn eval,
                          maelys_datalog_status_t expected) {
    maelys_datalog_filter_module_t m = filter_module();
    m.cost = cost;
    m.evaluate = eval;
    OK(maelys_datalog_register_filter_module(&m));
    OK(domain());
    maelys_datalog_policy_t *policy = NULL;
    OK(load("allow(X) :- ref(X), custom(X, \"x\").", &policy));
    maelys_datalog_session_t *session = NULL;
    OK(maelys_datalog_session_create(policy, 0u, &session));
    maelys_datalog_result_t *result = NULL;
    maelys_datalog_public_fact_t f = fact("ref", "alice");
    for (int i = 0; i < 2; ++i) {
        CHECK(maelys_datalog_session_solve(session, &f, 1u, &result, NULL) == expected);
        CHECK(result == NULL);
    }
    OK(maelys_datalog_session_free(session));
    OK(maelys_datalog_policy_free(policy));
    return 0;
}
static int budget(void) {
    return failing_filter(huge_cost, must_not_run, MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
}
static int zero_budget(void) {
    return failing_filter(zero_cost, must_not_run, MAELYS_DATALOG_STATUS_INVALID_STATE);
}
static int boolean(void) {
    return failing_filter(unit_cost, bad_boolean, MAELYS_DATALOG_STATUS_INVALID_STATE);
}
static int failure(void) {
    return failing_filter(unit_cost, error_evaluate, MAELYS_DATALOG_STATUS_INTERNAL);
}

static maelys_datalog_status_t choose_last(const maelys_datalog_join_candidate_t *c, size_t n,
                                           size_t *out) {
    if (!n)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    for (size_t i = 0; i < n; ++i) {
        if ((c[i].kind == MAELYS_DATALOG_JOIN_FILTER ||
             c[i].kind == MAELYS_DATALOG_JOIN_NEGATED_ATOM) &&
            (c[i].variable_mask & ~c[i].bound_variable_mask))
            abort();
    }
    *out = n - 1u;
    return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t choose_invalid(const maelys_datalog_join_candidate_t *c, size_t n,
                                              size_t *out) {
    (void)c;
    *out = n;
    return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t choose_error(const maelys_datalog_join_candidate_t *c, size_t n,
                                            size_t *out) {
    (void)c;
    (void)n;
    (void)out;
    return MAELYS_DATALOG_STATUS_INTERNAL;
}
static int planner_case(maelys_datalog_join_choose_fn choose, maelys_datalog_status_t expected) {
    const maelys_datalog_planner_module_t module = {1u, sizeof(maelys_datalog_planner_module_t),
                                                    "reverse", "test.reverse.v1", choose};
    OK(maelys_datalog_register_planner_module(&module));
    CHECK(maelys_datalog_register_planner_module(&module) == MAELYS_DATALOG_STATUS_INVALID_STATE);
    OK(domain());
    maelys_datalog_policy_t *policy = NULL;
    OK(load("allow(X) :- starts_with(X, \"ali\"), not(blocked(X)), ref(X), other(X). "
            "allow(Y) :- allow(X), edge(X,Y).",
            &policy));
    CHECK(maelys_datalog_register_planner_module(&module) == MAELYS_DATALOG_STATUS_INVALID_STATE);
    maelys_datalog_session_t *session = NULL;
    OK(maelys_datalog_session_create(policy, 0u, &session));
    maelys_datalog_public_fact_t edge = fact("edge", "alice");
    edge.arity = 2u;
    edge.terms[1] = symbol("bob");
    const maelys_datalog_public_fact_t facts[] = {fact("ref", "alice"), fact("other", "alice"),
                                                  edge};
    maelys_datalog_result_t *result = NULL;
    CHECK(maelys_datalog_session_solve(session, facts, 3u, &result, NULL) == expected);
    if (expected == MAELYS_DATALOG_STATUS_OK) {
        maelys_datalog_public_value_t v = symbol("alice");
        int present = 0;
        OK(maelys_datalog_result_query(result, "allow", &v, 1u, &present));
        CHECK(present == 1);
        v = symbol("bob");
        OK(maelys_datalog_result_query(result, "allow", &v, 1u, &present));
        CHECK(present == 1);
        OK(maelys_datalog_result_free(result));
    } else
        CHECK(result == NULL);
    OK(maelys_datalog_session_free(session));
    OK(maelys_datalog_policy_free(policy));
    return 0;
}
static int planner_valid(void) { return planner_case(choose_last, MAELYS_DATALOG_STATUS_OK); }
static int planner_invalid(void) {
    return planner_case(choose_invalid, MAELYS_DATALOG_STATUS_INVALID_STATE);
}
static int planner_error(void) {
    return planner_case(choose_error, MAELYS_DATALOG_STATUS_INTERNAL);
}

static int hash_child(int variant, int fd) {
    maelys_datalog_filter_module_t m = filter_module();
    if (variant == 1)
        OK(example_exact_match_register());
    if (variant == 2)
        m.semantic_id = "test.custom.v2";
    OK(maelys_datalog_register_filter_module(&m));
    if (variant == 3 || variant == 4) {
        maelys_datalog_planner_module_t p = {1u, sizeof(maelys_datalog_planner_module_t), "reverse",
                                             variant == 3 ? "test.reverse.v1" : "test.reverse.v2",
                                             choose_last};
        OK(maelys_datalog_register_planner_module(&p));
    }
    OK(domain());
    maelys_datalog_policy_t *policy = NULL;
    OK(load("allow(X) :- ref(X), custom(X, \"x\").", &policy));
    char hash[65];
    OK(maelys_datalog_policy_fingerprint(policy, hash));
    CHECK(write(fd, hash, sizeof(hash)) == (ssize_t)sizeof(hash));
    OK(maelys_datalog_policy_free(policy));
    return 0;
}
static int fingerprints(void) {
    char hashes[5][65];
    for (int i = 0; i < 5; ++i) {
        int fds[2];
        CHECK(pipe(fds) == 0);
        pid_t pid = fork();
        CHECK(pid >= 0);
        if (pid == 0) {
            close(fds[0]);
            _Exit(hash_child(i, fds[1]));
        }
        close(fds[1]);
        size_t used = 0;
        while (used < sizeof(hashes[i])) {
            ssize_t n = read(fds[0], hashes[i] + used, sizeof(hashes[i]) - used);
            CHECK(n > 0);
            used += (size_t)n;
        }
        close(fds[0]);
        int status;
        CHECK(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    CHECK(strcmp(hashes[0], hashes[1]) == 0); /* unused module/registration ID do not matter */
    CHECK(strcmp(hashes[0], hashes[2]) != 0); /* filter semantics do matter */
    CHECK(strcmp(hashes[0], hashes[3]) != 0); /* planner selection does matter */
    CHECK(strcmp(hashes[3], hashes[4]) != 0); /* planner version does matter */
    return 0;
}

static atomic_int loaders_ready;
static atomic_int loaders_start;
static int load_standard_repeatedly(void) {
    atomic_fetch_add(&loaders_ready, 1);
    while (!atomic_load(&loaders_start)) {
    }
    for (int i = 0; i < 10; ++i) {
        maelys_datalog_policy_t *policy = NULL;
        OK(load("allow(X) :- ref(X), starts_with(X, \"ali\").", &policy));
        OK(maelys_datalog_policy_free(policy));
    }
    return 0;
}
static void *loader_thread(void *result) {
    *(int *)result = load_standard_repeatedly();
    return NULL;
}
static int concurrent_standard_startup(void) {
    OK(domain()); /* Domain setup, like module registration, precedes readers. */
    pthread_t threads[8];
    int results[8] = {0};
    for (size_t i = 0; i < 8u; ++i)
        CHECK(pthread_create(&threads[i], NULL, loader_thread, &results[i]) == 0);
    while (atomic_load(&loaders_ready) != 8) {
    }
    atomic_store(&loaders_start, 1);
    for (size_t i = 0; i < 8u; ++i) {
        CHECK(pthread_join(threads[i], NULL) == 0);
        CHECK(results[i] == 0);
    }
    return 0;
}

/* Independent processes exercise startup registration without a production
 * reset API. No test instrumentation is compiled into the engine or provider. */
int main(void) {
    const struct {
        const char *name;
        int (*run)(void);
    } cases[] = {
        {"concurrent_standard_startup", concurrent_standard_startup},
        {"registration_capacity_copy_and_seal", registration},
        {"external_filter_lifecycle_and_explanation", lifecycle},
        {"missing_and_invalid_modules", missing_and_invalid},
        {"budget_precedes_callback", budget},
        {"zero_cost_rejected", zero_budget},
        {"invalid_boolean_rejected", boolean},
        {"error_is_not_nonmatch", failure},
        {"planner_binding_safety", planner_valid},
        {"planner_invalid_choice", planner_invalid},
        {"planner_error_propagation", planner_error},
        {"semantic_fingerprints_across_processes", fingerprints},
    };
    int failed = 0;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        fflush(NULL);
        pid_t pid = fork();
        if (pid == 0)
            _Exit(cases[i].run());
        int status = 0;
        int ok = pid > 0 && waitpid(pid, &status, 0) == pid && WIFEXITED(status) &&
                 WEXITSTATUS(status) == 0;
        printf("modules/%s: %s\n", cases[i].name, ok ? "OK" : "FAILED");
        failed += !ok;
    }
    return failed ? 1 : 0;
}
