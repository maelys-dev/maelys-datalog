/* SPDX-License-Identifier: MPL-2.0 */
#include <maelys/datalog_extension.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
#define OK(x) CHECK((x) == MAELYS_DATALOG_STATUS_OK)
extern const maelys_datalog_frontend_t *example_arrow_frontend(void);
extern const maelys_datalog_backend_t *example_naive_backend(void);
static maelys_datalog_status_t accept(const unsigned char *p, size_t n) {
    (void)p;
    (void)n;
    return 0;
}
static maelys_datalog_status_t cost(size_t v, size_t p, size_t *out) {
    (void)v;
    (void)p;
    *out = 1;
    return 0;
}
static maelys_datalog_status_t yes(const unsigned char *v, size_t vn, const unsigned char *p,
                                   size_t pn, int *out) {
    (void)v;
    (void)vn;
    (void)p;
    (void)pn;
    *out = 1;
    return 0;
}
static maelys_datalog_status_t no(const unsigned char *v, size_t vn, const unsigned char *p,
                                  size_t pn, int *out) {
    (void)v;
    (void)vn;
    (void)p;
    (void)pn;
    *out = 0;
    return 0;
}
static maelys_datalog_status_t choose(const maelys_datalog_join_candidate_t *c, size_t n,
                                      size_t *out) {
    if (!c || !n || !out)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    *out = n - 1;
    return 0;
}
static maelys_datalog_status_t invalid_choose(const maelys_datalog_join_candidate_t *c, size_t n,
                                              size_t *out) {
    (void)c;
    *out = n;
    return 0;
}
static const char source[] = "allow(X) :- seed(X), gate(X, \"x\").\n";
static maelys_datalog_extension_t extension(void) {
    maelys_datalog_extension_t e = {0};
    e.abi_version = MAELYS_DATALOG_EXTENSION_ABI_VERSION;
    e.struct_size = sizeof(e);
    e.name = "bundle";
    e.semantic_id = "bundle.v1";
    return e;
}
static maelys_datalog_filter_module_t filter(int matched) {
    maelys_datalog_filter_module_t f = {MAELYS_DATALOG_MODULE_ABI_VERSION,
                                        sizeof(f),
                                        "gate",
                                        matched ? "gate.yes.v1" : "gate.no.v1",
                                        accept,
                                        cost,
                                        matched ? yes : no};
    return f;
}
static int setup(void) {
    const maelys_datalog_public_predicate_t p[] = {
        {"seed", 1, MAELYS_DATALOG_PREDICATE_EDB},
        {"allow", 1, MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY}};
    const maelys_datalog_public_domain_t d = {"context_test", p, 2, NULL, 0};
    OK(maelys_datalog_domain_register(&d));
    return 0;
}
static int make_context(int matched, int bad_planner, maelys_datalog_context_t **out) {
    OK(maelys_datalog_context_create(out));
    maelys_datalog_filter_module_t f = filter(matched);
    maelys_datalog_planner_module_t planner = {
        MAELYS_DATALOG_MODULE_ABI_VERSION, sizeof(planner), "reverse",
        bad_planner ? "reverse.bad" : "reverse.v1", bad_planner ? invalid_choose : choose};
    maelys_datalog_extension_t e = extension();
    e.filters = &f;
    e.filter_count = 1;
    e.planners = &planner;
    e.planner_count = 1;
    e.frontends = example_arrow_frontend();
    e.frontend_count = 1;
    e.backends = example_naive_backend();
    e.backend_count = 1;
    OK(maelys_datalog_context_register(*out, &e));
    OK(maelys_datalog_context_seal(*out, "reverse"));
    return 0;
}
static int solve_check(maelys_datalog_session_t *s, int matched) {
    maelys_datalog_public_fact_t f = {0};
    f.predicate = "seed";
    f.arity = 1;
    f.terms[0].kind = MAELYS_DATALOG_VALUE_SYMBOL;
    f.terms[0].as.symbol = "alice";
    maelys_datalog_result_t *r = NULL;
    OK(maelys_datalog_session_solve(s, &f, 1, &r, NULL));
    int present = -1;
    OK(maelys_datalog_result_query(r, "allow", f.terms, 1, &present));
    CHECK(present == matched);
    char text[4096];
    size_t required = 0;
    if (matched) {
        OK(maelys_datalog_result_explain_true_text(r, "allow", f.terms, 1, text, sizeof(text),
                                                   &required));
        CHECK(strstr(text, "gate.yes.v1"));
    } else {
        OK(maelys_datalog_result_explain_false_text(r, "allow", f.terms, 1, text, sizeof(text),
                                                    &required));
        CHECK(strstr(text, "gate.no.v1"));
    }
    OK(maelys_datalog_result_free(r));
    return 0;
}
typedef struct {
    maelys_datalog_session_t *session;
    int matched, failed;
} worker_t;
static void *worker(void *arg) {
    worker_t *w = arg;
    for (int i = 0; i < 20; ++i)
        if (solve_check(w->session, w->matched)) {
            w->failed = 1;
            break;
        }
    return NULL;
}
static int isolation_and_lifetime(void) {
    maelys_datalog_context_t *a, *b;
    CHECK(!make_context(1, 0, &a));
    CHECK(!make_context(0, 0, &b));
    maelys_datalog_policy_t *pa, *pb;
    OK(maelys_datalog_context_load_inline(a, NULL, "context_test", "p", source, strlen(source), &pa,
                                          NULL));
    OK(maelys_datalog_context_load_inline(b, NULL, "context_test", "p", source, strlen(source), &pb,
                                          NULL));
    char fa[65], fb[65];
    OK(maelys_datalog_policy_fingerprint(pa, fa));
    OK(maelys_datalog_policy_fingerprint(pb, fb));
    CHECK(strcmp(fa, fb));
    maelys_datalog_session_t *sa, *sb, *absent = (void *)1;
    CHECK(maelys_datalog_context_session_create(a, pb, 0, NULL, 0, 0, &absent) ==
          MAELYS_DATALOG_STATUS_INVALID_FIELD);
    CHECK(!absent);
    CHECK(maelys_datalog_context_session_create(a, pa, 0, "missing", 0, 0, &absent) ==
          MAELYS_DATALOG_STATUS_NOT_FOUND);
    CHECK(!absent);
    OK(maelys_datalog_context_session_create(a, pa, 0, NULL, 0, 0, &sa));
    OK(maelys_datalog_session_create(pb, 0, &sb)); /* old API retains explicit catalog */
    OK(maelys_datalog_context_free(a));
    OK(maelys_datalog_context_free(b));
    OK(maelys_datalog_policy_free(pa));
    OK(maelys_datalog_policy_free(pb));
    worker_t wa = {sa, 1, 0}, wb = {sb, 0, 0};
    pthread_t ta, tb;
    CHECK(!pthread_create(&ta, NULL, worker, &wa));
    CHECK(!pthread_create(&tb, NULL, worker, &wb));
    CHECK(!pthread_join(ta, NULL));
    CHECK(!pthread_join(tb, NULL));
    CHECK(!wa.failed && !wb.failed);
    OK(maelys_datalog_session_free(sa));
    OK(maelys_datalog_session_free(sb));
    return 0;
}
static int atomic_registration(void) {
    maelys_datalog_context_t *c;
    OK(maelys_datalog_context_create(&c));
    maelys_datalog_policy_t *p = (void *)1;
    CHECK(maelys_datalog_context_load_inline(c, NULL, "context_test", "p", source, strlen(source),
                                             &p, NULL) == MAELYS_DATALOG_STATUS_INVALID_STATE);
    CHECK(!p);
    maelys_datalog_extension_t e = extension();
    maelys_datalog_filter_module_t f = filter(1);
    e.filters = &f;
    e.filter_count = 1;
    maelys_datalog_backend_t bad = *example_naive_backend();
    bad.abi_version = 999;
    e.backends = &bad;
    e.backend_count = 1;
    CHECK(maelys_datalog_context_register(c, &e) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    size_t n;
    OK(maelys_datalog_context_component_count(c, MAELYS_DATALOG_EXTENSION_FILTER, &n));
    CHECK(n == 3);
    e.backends = NULL;
    e.backend_count = 0;
    char name[] = "gate", semantic[] = "gate.yes.v1";
    f.name = name;
    f.semantic_id = semantic;
    OK(maelys_datalog_context_register(c, &e));
    name[0] = 'z';
    semantic[0] = 'z';
    maelys_datalog_component_info_t info;
    OK(maelys_datalog_context_component_info(c, MAELYS_DATALOG_EXTENSION_FILTER, 3, &info));
    CHECK(!strcmp(info.name, "gate") && !strcmp(info.semantic_id, "gate.yes.v1"));
    CHECK(maelys_datalog_context_register(c, &e) == MAELYS_DATALOG_STATUS_INVALID_FIELD);
    CHECK(maelys_datalog_context_seal(c, "absent") == MAELYS_DATALOG_STATUS_NOT_FOUND);
    OK(maelys_datalog_context_seal(c, NULL));
    CHECK(maelys_datalog_context_register(c, &e) == MAELYS_DATALOG_STATUS_INVALID_STATE);
    CHECK(maelys_datalog_context_seal(c, NULL) == MAELYS_DATALOG_STATUS_INVALID_STATE);
    CHECK(maelys_datalog_context_load_inline(c, "missing", "context_test", "p", source,
                                             strlen(source), &p,
                                             NULL) == MAELYS_DATALOG_STATUS_NOT_FOUND);
    CHECK(!p);
    OK(maelys_datalog_context_free(c));
    return 0;
}
static int selections_and_compatibility(void) {
    maelys_datalog_context_t *c;
    CHECK(!make_context(1, 0, &c));
    maelys_datalog_policy_t *p;
    const char *arrow = "allow <- seed";
    OK(maelys_datalog_context_load_inline(c, "arrow", "context_test", "p", arrow, strlen(arrow), &p,
                                          NULL));
    maelys_datalog_session_t *s;
    OK(maelys_datalog_context_session_create(c, p, 0, example_naive_backend()->name, 0, 0, &s));
    OK(maelys_datalog_session_free(s));
    CHECK(maelys_datalog_context_session_create(c, p, 0, example_naive_backend()->name,
                                                MAELYS_DATALOG_CAP_EXPLAIN_FALSE, 0,
                                                &s) == MAELYS_DATALOG_STATUS_UNSUPPORTED);
    OK(maelys_datalog_policy_free(p));
    OK(maelys_datalog_context_free(c));
    /* Context creation/loading did not seal or populate the global registry. */
    maelys_datalog_filter_module_t f = filter(1);
    OK(maelys_datalog_register_filter_module(&f));
    OK(maelys_datalog_policy_load_inline("context_test", "p", source, strlen(source), &p, NULL));
    CHECK(maelys_datalog_register_filter_module(&f) == MAELYS_DATALOG_STATUS_INVALID_STATE);
    OK(maelys_datalog_policy_free(p));
    /* New explicit catalogs remain configurable after legacy sealing. */
    CHECK(!make_context(0, 0, &c));
    OK(maelys_datalog_context_free(c));
    return 0;
}
static int fingerprints_and_invalid_planner(void) {
    const char *plain = "allow(X) :- seed(X).";
    maelys_datalog_context_t *c;
    OK(maelys_datalog_context_create(&c));
    /* Merely registering a planner must not activate it. */
    maelys_datalog_planner_module_t bad = {MAELYS_DATALOG_MODULE_ABI_VERSION, sizeof(bad),
                                           "invalid", "invalid.v1", invalid_choose};
    maelys_datalog_extension_t e = extension();
    e.planners = &bad;
    e.planner_count = 1;
    OK(maelys_datalog_context_register(c, &e));
    OK(maelys_datalog_context_seal(c, NULL));
    maelys_datalog_policy_t *legacy, *explicit;
    OK(maelys_datalog_policy_load_inline("context_test", "plain", plain, strlen(plain), &legacy,
                                         NULL));
    OK(maelys_datalog_context_load_inline(c, NULL, "context_test", "plain", plain, strlen(plain),
                                          &explicit, NULL));
    char a[65], b[65];
    OK(maelys_datalog_policy_fingerprint(legacy, a));
    OK(maelys_datalog_policy_fingerprint(explicit, b));
    CHECK(!strcmp(a, b));
    maelys_datalog_session_t *s;
    OK(maelys_datalog_session_create(explicit, 0, &s));
    maelys_datalog_public_fact_t f = {0};
    f.predicate = "seed";
    f.arity = 1;
    f.terms[0].kind = MAELYS_DATALOG_VALUE_SYMBOL;
    f.terms[0].as.symbol = "alice";
    maelys_datalog_result_t *r = NULL;
    OK(maelys_datalog_session_solve(s, &f, 1, &r, NULL));
    OK(maelys_datalog_result_free(r));
    OK(maelys_datalog_session_free(s));
    OK(maelys_datalog_policy_free(legacy));
    OK(maelys_datalog_policy_free(explicit));
    OK(maelys_datalog_context_free(c));
    CHECK(!make_context(1, 1, &c));
    OK(maelys_datalog_context_load_inline(c, NULL, "context_test", "bad", plain, strlen(plain),
                                          &explicit, NULL));
    OK(maelys_datalog_session_create(explicit, 0, &s));
    r = (void *)1;
    CHECK(maelys_datalog_session_solve(s, &f, 1, &r, NULL) == MAELYS_DATALOG_STATUS_INVALID_STATE);
    CHECK(!r);
    OK(maelys_datalog_session_free(s));
    OK(maelys_datalog_policy_free(explicit));
    OK(maelys_datalog_context_free(c));
    return 0;
}
static int registration_order_and_capacity(void) {
    maelys_datalog_context_t *contexts[2];
    maelys_datalog_policy_t *policies[2];
    maelys_datalog_session_t *sessions[2];
    char policy_ids[2][65], program_ids[2][65], session_ids[2][65];
    for (size_t i = 0; i < 2; ++i) {
        OK(maelys_datalog_context_create(&contexts[i]));
        maelys_datalog_filter_module_t pair[2] = {filter(1), filter(0)};
        pair[1].name = "unused";
        pair[1].semantic_id = "unused.v1";
        if (i) {
            maelys_datalog_filter_module_t tmp = pair[0];
            pair[0] = pair[1];
            pair[1] = tmp;
        }
        maelys_datalog_extension_t e = extension();
        e.filters = pair;
        e.filter_count = SIZE_MAX;
        CHECK(maelys_datalog_context_register(contexts[i], &e) ==
              MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
        size_t n;
        OK(maelys_datalog_context_component_count(contexts[i], MAELYS_DATALOG_EXTENSION_FILTER,
                                                  &n));
        CHECK(n == 3);
        e.filter_count = 2;
        OK(maelys_datalog_context_register(contexts[i], &e));
        OK(maelys_datalog_context_seal(contexts[i], NULL));
        OK(maelys_datalog_context_load_inline(contexts[i], NULL, "context_test", "order", source,
                                              strlen(source), &policies[i], NULL));
        OK(maelys_datalog_policy_fingerprint(policies[i], policy_ids[i]));
        OK(maelys_datalog_session_create(policies[i], 0, &sessions[i]));
        const maelys_datalog_program_t *program;
        OK(maelys_datalog_session_program(sessions[i], &program));
        OK(maelys_datalog_program_fingerprint(program, program_ids[i]));
        OK(maelys_datalog_session_fingerprint(sessions[i], session_ids[i]));
        CHECK(!solve_check(sessions[i], 1));
    }
    CHECK(!strcmp(policy_ids[0], policy_ids[1]));
    CHECK(!strcmp(program_ids[0], program_ids[1]));
    CHECK(!strcmp(session_ids[0], session_ids[1]));
    for (size_t i = 0; i < 2; ++i) {
        OK(maelys_datalog_session_free(sessions[i]));
        OK(maelys_datalog_policy_free(policies[i]));
        OK(maelys_datalog_context_free(contexts[i]));
    }
    return 0;
}
int main(void) {
    CHECK(!setup());
    CHECK(!atomic_registration());
    CHECK(!isolation_and_lifetime());
    CHECK(!selections_and_compatibility());
    CHECK(!fingerprints_and_invalid_planner());
    CHECK(!registration_order_and_capacity());
    puts("context: atomic registration, isolation, lifetime, selection and legacy compatibility "
         "PASS");
    return 0;
}
