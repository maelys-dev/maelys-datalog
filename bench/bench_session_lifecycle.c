/* SPDX-License-Identifier: MPL-2.0 */
/* Public API lifecycle probe. Print raw samples only after timing finishes.
 * Build with MAELYS_BENCH_COUNT to count one named phase after 50 warmups;
 * preparation of inputs, clocks and answer checks stay outside that region. */
#define _POSIX_C_SOURCE 200809L
#include <maelys/datalog.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef MAELYS_BENCH_COUNT
#include <valgrind/callgrind.h>
#endif

enum { CREATE, SOLVE, RELEASE, DESTROY, PHASES, WARMUP = 50, SAMPLES = 501 };
#ifdef MAELYS_BENCH_COUNT
static const char *phase_names[] = {"create", "solve", "release", "destroy"};
#endif
static maelys_datalog_value_t symbol(const char *s) {
    maelys_datalog_value_t v = {.kind = MAELYS_DATALOG_VALUE_SYMBOL};
    v.as.symbol = s; return v;
}
static maelys_datalog_fact_t fact(const char *p, const char *a, const char *b) {
    maelys_datalog_fact_t f = {.predicate = p, .arity = b ? 2 : 1};
    f.terms[0] = symbol(a); if (b) f.terms[1] = symbol(b); return f;
}
static void check(maelys_datalog_result_t *r, const char *p, const char *a,
                  const char *b, int expected) {
    maelys_datalog_value_t terms[] = {symbol(a), symbol(b)};
    int present = -1;
    assert(maelys_datalog_result_query(r, p, terms, b ? 2 : 1, &present) == 0);
    assert(present == expected);
}
#ifndef MAELYS_BENCH_COUNT
static double now_ns(void) {
    struct timespec t; assert(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    return (double)t.tv_sec * 1e9 + t.tv_nsec;
}
#endif
int main(int argc, char **argv) {
    if (argc < 2 || (strcmp(argv[1], "7") && strcmp(argv[1], "93"))) {
        fprintf(stderr, "usage: %s 7|93 [create|solve|release|destroy]\n", argv[0]);
        return 2;
    }
    const int large = !strcmp(argv[1], "93");
#ifdef MAELYS_BENCH_COUNT
    int selected = -1;
    for (int p = 0; p < PHASES; ++p)
        if (argc == 3 && !strcmp(argv[2], phase_names[p])) selected = p;
    if (selected < 0) return 2;
    const unsigned rounds = WARMUP + 1;
#else
    if (argc != 2) return 2;
    const unsigned rounds = WARMUP + SAMPLES;
    double samples[WARMUP + SAMPLES][PHASES];
#endif
    const maelys_datalog_predicate_t predicates[] = {
        {"user", 1, MAELYS_DATALOG_PREDICATE_EDB},
        {"owns", 2, MAELYS_DATALOG_PREDICATE_EDB},
        {"delegated", 2, MAELYS_DATALOG_PREDICATE_EDB},
        {"blocked", 1, MAELYS_DATALOG_PREDICATE_EDB},
        {"can_read", 2, MAELYS_DATALOG_PREDICATE_IDB},
        {"has_any_document", 1, MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY},
        {"allow", 2, MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY},
    };
    const maelys_datalog_domain_t domain = {"lifecycle", predicates, 7, NULL, 0};
    const char source[] = "can_read(User, Doc) :- owns(User, Doc) or delegated(User, Doc), not(blocked(User)).\n"
        "has_any_document(User) :- owns(User, _).\nallow(User, Doc) :- user(User), can_read(User, Doc).\n";
    assert(maelys_datalog_domain_register(&domain) == 0);
    maelys_datalog_policy_t *policy = NULL;
    assert(maelys_datalog_policy_load_inline(domain.name, "documents", source,
        sizeof(source) - 1, &policy, NULL) == 0);
    maelys_datalog_fact_t facts[93];
    char users[30][16], docs[30][16];
    size_t count = 0;
    if (large) {
        for (unsigned i = 0; i < 30; ++i) {
            snprintf(users[i], sizeof(users[i]), "user%u", i);
            snprintf(docs[i], sizeof(docs[i]), "doc%u.pdf", i);
        }
        for (unsigned i = 0; i < 30; ++i) {
            facts[count++] = fact("user", users[i], NULL);
            facts[count++] = fact("owns", users[i], docs[i]);
            facts[count++] = fact("delegated", users[i], docs[(i + 1) % 30]);
            if (!(i % 10)) facts[count++] = fact("blocked", users[i], NULL);
        }
    } else {
        facts[count++] = fact("user", "alice", NULL);
        facts[count++] = fact("user", "bob", NULL);
        facts[count++] = fact("user", "mallory", NULL);
        facts[count++] = fact("owns", "alice", "roadmap.pdf");
        facts[count++] = fact("delegated", "bob", "roadmap.pdf");
        facts[count++] = fact("owns", "mallory", "roadmap.pdf");
        facts[count++] = fact("blocked", "mallory", NULL);
    }
    maelys_datalog_input_edb_t *edb = NULL;
    assert(maelys_datalog_input_edb_create_with_capacity(count, 4096, &edb) == 0);
    assert(maelys_datalog_input_edb_add_facts(edb, facts, count, NULL) == 0);
    for (unsigned i = 0; i < rounds; ++i) {
        maelys_datalog_session_t *session = NULL;
        maelys_datalog_result_t *result = NULL;
        for (int p = 0; p < PHASES; ++p) {
#ifdef MAELYS_BENCH_COUNT
            if (i == WARMUP && p == selected) { CALLGRIND_TOGGLE_COLLECT; }
#else
            double start = now_ns();
#endif
            maelys_datalog_status_t status;
            switch (p) {
            case CREATE: status = maelys_datalog_session_create(policy, 0, &session); break;
            case SOLVE: status = maelys_datalog_session_solve_edb(session, edb, &result, NULL); break;
            case RELEASE: status = maelys_datalog_result_free(result); break;
            default: status = maelys_datalog_session_free(session); break;
            }
#ifdef MAELYS_BENCH_COUNT
            if (i == WARMUP && p == selected) { CALLGRIND_TOGGLE_COLLECT; }
#else
            samples[i][p] = now_ns() - start;
#endif
            assert(status == 0);
            if (p == SOLVE) {
                if (large) {
                    check(result, "allow", "user1", "doc1.pdf", 1);
                    check(result, "allow", "user10", "doc10.pdf", 0);
                } else {
                    check(result, "allow", "alice", "roadmap.pdf", 1);
                    check(result, "allow", "bob", "roadmap.pdf", 1);
                    check(result, "allow", "mallory", "roadmap.pdf", 0);
                    check(result, "has_any_document", "alice", NULL, 1);
                    check(result, "has_any_document", "bob", NULL, 0);
                }
            }
        }
    }
    assert(maelys_datalog_input_edb_free(edb) == 0);
    assert(maelys_datalog_policy_free(policy) == 0);
#ifndef MAELYS_BENCH_COUNT
    printf("sample,create_ns,solve_ns,release_ns,destroy_ns\n");
    for (unsigned i = 0; i < rounds; ++i) {
        if (i && i < WARMUP) continue;
        printf("%s%u", i ? "warm-" : "cold-", i);
        for (int p = 0; p < PHASES; ++p) printf(",%.0f", samples[i][p]);
        putchar('\n');
    }
#else
    printf("checked facts=%zu phase=%s\n", count, phase_names[selected]);
#endif
    return 0;
}
