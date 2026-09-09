/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_CONFORMANCE_H
#define MAELYS_CONFORMANCE_H
/* Test-only, header-only kit: no private headers, no runtime library additions.
 * Return 0 on success, 1 on a named contract failure. Callers supply fixtures
 * for their supported dialect/features; this does not prove arbitrary native
 * callbacks safe, bounded or semantically complete. */
#include <maelys/datalog_extension.h>
#include <stdio.h>
#include <string.h>
#define MC_REQUIRE(expr)                                                                           \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "conformance %s:%d: %s\n", __FILE__, __LINE__, #expr);                 \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
#define MC_OK(expr) MC_REQUIRE((expr) == MAELYS_DATALOG_STATUS_OK)

typedef struct {
    const unsigned char *value, *pattern;
    size_t value_length, pattern_length;
    maelys_datalog_status_t validation_status, cost_status, evaluation_status;
    int matched;
} maelys_conformance_filter_case_t;
static inline int maelys_conformance_filter(const maelys_datalog_filter_module_t *f,
                                            const maelys_conformance_filter_case_t *cases,
                                            size_t count) {
    MC_REQUIRE(f && cases && count && f->validate_pattern && f->cost && f->evaluate);
    for (size_t i = 0; i < count; ++i) {
        const maelys_conformance_filter_case_t *c = &cases[i];
        MC_REQUIRE(f->validate_pattern(c->pattern, c->pattern_length) == c->validation_status);
        if (c->validation_status)
            continue;
        size_t cost = 0;
        MC_REQUIRE(f->cost(c->value_length, c->pattern_length, &cost) == c->cost_status);
        if (c->cost_status)
            continue;
        MC_REQUIRE(cost > 0);
        int result = -1;
        MC_REQUIRE(f->evaluate(c->value, c->value_length, c->pattern, c->pattern_length, &result) ==
                   c->evaluation_status);
        if (!c->evaluation_status)
            MC_REQUIRE((result == 0 || result == 1) && result == c->matched);
    }
    return 0;
}
static inline int maelys_conformance_planner(const maelys_datalog_planner_module_t *p,
                                             const maelys_datalog_join_candidate_t *candidates,
                                             size_t count) {
    MC_REQUIRE(p && p->choose && candidates && count);
    size_t first = SIZE_MAX, repeated = SIZE_MAX;
    MC_OK(p->choose(candidates, count, &first));
    MC_REQUIRE(first < count);
    MC_OK(p->choose(candidates, count, &repeated));
    MC_REQUIRE(repeated == first);
    return 0;
}
static inline maelys_datalog_extension_t maelys_conformance_declaration(void) {
    maelys_datalog_extension_t e = {0};
    e.abi_version = MAELYS_DATALOG_EXTENSION_ABI_VERSION;
    e.struct_size = sizeof(e);
    e.name = "conformance";
    e.semantic_id = "conformance.v1";
    return e;
}
/* A rejection fixture exercises the host's mandatory IR validation, not just
 * the frontend callback. rule_count is checked on successful loads. */
static inline int maelys_conformance_frontend(const maelys_datalog_frontend_t *frontend,
                                              const char *domain, const char *source,
                                              maelys_datalog_status_t expected, size_t rule_count) {
    MC_REQUIRE(frontend && domain && source);
    maelys_datalog_context_t *context = NULL;
    maelys_datalog_policy_t *policy = NULL;
    maelys_datalog_session_t *session = NULL;
    maelys_datalog_extension_t e = maelys_conformance_declaration();
    e.frontends = frontend;
    e.frontend_count = 1;
    MC_OK(maelys_datalog_context_create(&context));
    maelys_datalog_status_t rc = maelys_datalog_context_register(context, &e);
    if (!rc)
        rc = maelys_datalog_context_seal(context, NULL);
    int ready = rc == MAELYS_DATALOG_STATUS_OK;
    if (!rc)
        rc = maelys_datalog_context_load_inline(context, frontend->name, domain, "conformance",
                                                source, strlen(source), &policy, NULL);
    int pass = ready && rc == expected && (rc == 0 || policy == NULL);
    if (!rc && pass) {
        rc = maelys_datalog_session_create(policy, 0, &session);
        const maelys_datalog_program_t *program = NULL;
        maelys_datalog_program_info_t info = {0};
        if (!rc)
            rc = maelys_datalog_session_program(session, &program);
        if (!rc)
            rc = maelys_datalog_program_info(program, &info);
        pass = !rc && info.rule_count == rule_count;
    }
    if (session)
        maelys_datalog_session_free(session);
    if (policy)
        maelys_datalog_policy_free(policy);
    maelys_datalog_context_free(context);
    MC_REQUIRE(pass);
    return 0;
}
/* Compare caller-supplied ground probes and IDB enumeration counts. Add probes
 * for every relevant predicate, absent cases and intermediate helpers that are
 * queryable. This is a finite fixture check, not a proof of equivalence. */
static inline int maelys_conformance_backend(const maelys_datalog_backend_t *backend,
                                             const char *domain, const char *source,
                                             const maelys_datalog_public_fact_t *inputs,
                                             size_t input_count,
                                             const maelys_datalog_public_fact_t *probes,
                                             size_t probe_count) {
    MC_REQUIRE(backend && source && probes && probe_count);
    maelys_datalog_context_t *context = NULL;
    maelys_datalog_policy_t *policy = NULL;
    maelys_datalog_session_t *sessions[2] = {NULL, NULL};
    maelys_datalog_result_t *results[2] = {NULL, NULL};
    maelys_datalog_extension_t e = maelys_conformance_declaration();
    e.backends = backend;
    e.backend_count = 1;
    MC_OK(maelys_datalog_context_create(&context));
    maelys_datalog_status_t rc = maelys_datalog_context_register(context, &e);
    if (!rc)
        rc = maelys_datalog_context_seal(context, NULL);
    if (!rc)
        rc = maelys_datalog_context_load_inline(context, NULL, domain, "conformance", source,
                                                strlen(source), &policy, NULL);
    for (size_t i = 0; i < 2 && !rc; ++i) {
        rc = maelys_datalog_context_session_create(context, policy, 0, i ? backend->name : NULL, 0,
                                                   0, &sessions[i]);
        if (!rc)
            rc = maelys_datalog_session_solve(sessions[i], inputs, input_count, &results[i], NULL);
    }
    int pass = !rc;
    for (size_t i = 0; i < probe_count && pass; ++i) {
        int present[2] = {-1, -1};
        for (size_t j = 0; j < 2 && !rc; ++j)
            rc = maelys_datalog_result_query(results[j], probes[i].predicate, probes[i].terms,
                                             probes[i].arity, &present[j]);
        pass = !rc && present[0] == present[1];
        size_t counts[2] = {0, 0};
        for (size_t j = 0; j < 2 && pass; ++j) {
            maelys_datalog_status_t enumeration = maelys_datalog_result_enumerate(
                results[j], probes[i].predicate, probes[i].arity, NULL, 0, &counts[j]);
            pass = enumeration == MAELYS_DATALOG_STATUS_OK ||
                   enumeration == MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE;
        }
        pass = pass && counts[0] == counts[1];
    }
    for (size_t i = 0; i < 2; ++i) {
        if (results[i])
            maelys_datalog_result_free(results[i]);
        if (sessions[i])
            maelys_datalog_session_free(sessions[i]);
    }
    if (policy)
        maelys_datalog_policy_free(policy);
    maelys_datalog_context_free(context);
    MC_REQUIRE(pass);
    return 0;
}
#endif
