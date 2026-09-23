/* SPDX-License-Identifier: MPL-2.0 */
#include "tests/fixtures/allocation_guard.h"
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef memset
#include "maelys/datalog_backend.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int forbidden, mode;
static size_t attempts, frees, prepares, writes;
void *maelys_test_malloc(size_t n) { if (forbidden) { ++attempts; return NULL; } return malloc(n); }
void *maelys_test_calloc(size_t n, size_t s) { if (forbidden) { ++attempts; return NULL; } return calloc(n, s); }
void *maelys_test_realloc(void *p, size_t n) { if (forbidden) { ++attempts; return NULL; } return realloc(p, n); }
void maelys_test_free(void *p) { if (forbidden && p) ++frees; free(p); }
void *maelys_test_memset(void *p, int c, size_t n) { return memset(p, c, n); }
#define OK(call) assert((call) == MAELYS_DATALOG_STATUS_OK)

static maelys_datalog_status_t requirements(void *s, void *r, maelys_datalog_explanation_kind_t k,
    size_t *n, size_t *a) {
    if (mode == 1) { *n = 1; *a = 3; return 0; }
    if (mode == 2) { *n = SIZE_MAX; *a = 1; return 0; }
    return maelys_datalog_backend_reference()->explanation_storage_requirements(s, r, k, n, a);
}
static maelys_datalog_status_t prepare(void *s, void *r, maelys_datalog_explanation_kind_t k,
    const char *predicate, const maelys_datalog_value_t *terms, size_t arity,
    void *storage, size_t bytes, size_t *text_size) {
    ++prepares;
    if (mode == 3) return (maelys_datalog_status_t)12345;
    if (mode == 4) return 0; /* Missing length must be rejected. */
    return maelys_datalog_backend_reference()->explanation_prepare(s, r, k, predicate, terms,
        arity, storage, bytes, text_size);
}
static maelys_datalog_status_t write_text(void *s, void *r, maelys_datalog_explanation_kind_t k,
    const void *storage, char *text, size_t bytes) {
    ++writes;
    if (mode == 5) { text[0] = 0; return 0; }
    if (mode == 6) return MAELYS_DATALOG_STATUS_IO;
    return maelys_datalog_backend_reference()->explanation_write_text(s, r, k, storage, text, bytes);
}
static maelys_datalog_value_t symbol(const char *s) {
    maelys_datalog_value_t v = {.kind = MAELYS_DATALOG_VALUE_SYMBOL, .as.symbol = s};
    return v;
}
static maelys_datalog_session_t *cached_session(maelys_datalog_policy_t *policy) {
    maelys_datalog_session_config_t *config;
    maelys_datalog_session_t *session;
    OK(maelys_datalog_session_config_create(&config));
    OK(maelys_datalog_session_config_set_explanation_workspace(config,
        MAELYS_DATALOG_EXPLAIN_TRUE | MAELYS_DATALOG_EXPLAIN_FALSE));
    OK(maelys_datalog_session_create_configured(policy, 0, config, &session));
    OK(maelys_datalog_session_config_free(config));
    return session;
}
static void bounded_case(const char *source, const char *expected_status) {
    maelys_datalog_policy_t *policy;
    maelys_datalog_session_t *session;
    maelys_datalog_result_t *result;
    maelys_datalog_input_edb_t *edb;
    OK(maelys_datalog_policy_load_inline("prepared_explanation", "bounded", source, strlen(source), &policy, NULL));
    OK(maelys_datalog_session_create(policy, 0, &session));
    OK(maelys_datalog_input_edb_create(&edb));
    maelys_datalog_value_t alice = symbol("alice");
    OK(maelys_datalog_input_edb_add_fact(edb, "seed", &alice, 1, NULL));
    OK(maelys_datalog_session_solve_edb(session, edb, &result, NULL));
    size_t bytes, alignment, required;
    for (int i = 1; i <= 2; ++i) {
        maelys_datalog_explanation_kind_t kind = (maelys_datalog_explanation_kind_t)i;
        size_t bound, bound_alignment;
        OK(maelys_datalog_session_explanation_storage_bound(session, kind, &bound, &bound_alignment));
        OK(maelys_datalog_result_explanation_storage_requirements(result, kind, &bytes, &alignment));
        assert(bytes <= bound && alignment == bound_alignment);
    }
    OK(maelys_datalog_result_explanation_storage_requirements(result, MAELYS_DATALOG_EXPLAIN_FALSE, &bytes, &alignment));
    void *storage = malloc(bytes); assert(storage);
    char expected[32768], text[32768];
    OK(maelys_datalog_result_explain_false_text(result, "allow", &alice, 1, expected, sizeof(expected), &required));
    assert(strstr(expected, expected_status));
    maelys_datalog_session_t *cached = cached_session(policy);
    maelys_datalog_result_t *cached_result;
    OK(maelys_datalog_session_solve_edb(cached, edb, &cached_result, NULL));
    forbidden = 1;
    OK(maelys_datalog_result_explain_false_text(cached_result, "allow", &alice, 1, NULL, 0, &required));
    OK(maelys_datalog_result_explain_false_text(cached_result, "allow", &alice, 1, text, sizeof(text), &required));
    assert(!strcmp(text, expected));
    OK(maelys_datalog_result_free(cached_result));
    maelys_datalog_prepared_explanation_t *p;
    OK(maelys_datalog_result_prepare_explanation(result, MAELYS_DATALOG_EXPLAIN_FALSE,
        "allow", &alice, 1, storage, bytes, &p));
    OK(maelys_datalog_prepared_explanation_write_text(p, text, sizeof(text)));
    assert(!strcmp(text, expected));
    OK(maelys_datalog_prepared_explanation_release(p));
    OK(maelys_datalog_result_explain_text_in(result, MAELYS_DATALOG_EXPLAIN_FALSE,
        "allow", &alice, 1, storage, bytes, text, sizeof(text), &required));
    assert(!strcmp(text, expected) && required == strlen(expected));
    OK(maelys_datalog_result_free(result));
    assert(attempts == 0 && frees == 0);
    forbidden = 0;
    OK(maelys_datalog_session_free(cached));
    free(storage);
    OK(maelys_datalog_input_edb_free(edb));
    OK(maelys_datalog_session_free(session));
    OK(maelys_datalog_policy_free(policy));
}

static void reference_conveniences(maelys_datalog_policy_t *policy) {
    maelys_datalog_session_t *session;
    OK(maelys_datalog_session_create(policy, 0, &session));
    size_t bounds[2], alignments[2];
    forbidden = 1;
    for (int i = 0; i < 2; ++i)
        OK(maelys_datalog_session_explanation_storage_bound(session,
            (maelys_datalog_explanation_kind_t)(i + 1), &bounds[i], &alignments[i]));
    size_t unchanged = 123;
    assert(maelys_datalog_session_explanation_storage_bound(session,
        (maelys_datalog_explanation_kind_t)0, &unchanged, &unchanged) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    assert(unchanged == 123);
    assert(maelys_datalog_session_explanation_storage_bound(NULL,
        MAELYS_DATALOG_EXPLAIN_TRUE, &unchanged, &unchanged) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    assert(maelys_datalog_session_explanation_storage_bound(session,
        MAELYS_DATALOG_EXPLAIN_TRUE, NULL, &unchanged) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    forbidden = 0;
    maelys_datalog_input_edb_t *edb;
    OK(maelys_datalog_input_edb_create(&edb));
    maelys_datalog_value_t alice = symbol("alice"), bob = symbol("bob");
    OK(maelys_datalog_input_edb_add_fact(edb, "seed", &alice, 1, NULL));
    OK(maelys_datalog_input_edb_add_fact(edb, "seed", &bob, 1, NULL));
    OK(maelys_datalog_input_edb_add_fact(edb, "blocked", &bob, 1, NULL));
    maelys_datalog_result_t *result;
    OK(maelys_datalog_session_solve_edb(session, edb, &result, NULL));
    char text[8192], expected[8192];
    for (int i = 0; i < 2; ++i) {
        maelys_datalog_explanation_kind_t kind = (maelys_datalog_explanation_kind_t)(i + 1);
        const maelys_datalog_value_t *query = i ? &bob : &alice;
        size_t bytes, alignment, required;
        OK(maelys_datalog_result_explanation_storage_requirements(result, kind, &bytes, &alignment));
        assert(bytes <= bounds[i] && alignment == alignments[i]);
        printf("reference explanation kind=%d exact=%zu bound=%zu alignment=%zu\n", i + 1, bytes, bounds[i], alignment);
        void *storage = malloc(bounds[i]); assert(storage);
        if (i) {
            OK(maelys_datalog_result_explain_false_text(result, "allow", query, 1, expected, sizeof(expected), &required));
        } else {
            OK(maelys_datalog_result_explain_true_text(result, "allow", query, 1, expected, sizeof(expected), &required));
        }
        size_t length = strlen(expected);
        forbidden = 1;
        required = 123;
        text[0] = 'X';
        assert(maelys_datalog_result_explain_text_in(result, kind, "allow", query, 1,
            storage, bytes - 1, text, sizeof(text), &required) == MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL);
        assert(required == 123 && text[0] == 'X');
        assert(maelys_datalog_result_explain_text_in(result, kind, "allow", query, 1,
            (char *)storage + 1, bounds[i] - 1, text, sizeof(text), &required) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
        assert(required == 123 && text[0] == 'X');
        assert(maelys_datalog_result_explain_text_in(result, kind, "allow", query, 1,
            storage, bounds[i], NULL, 0, &required) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
        assert(maelys_datalog_result_explain_text_in(result, kind, "allow", query, 1,
            storage, bounds[i], storage, bounds[i], &required) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
        assert(maelys_datalog_result_explain_text_in(result, kind, "allow", query, 1,
            storage, bounds[i], text, 0, &required) == MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
        assert(required == length && text[0] == 'X');
        memset(text, 'X', sizeof(text));
        assert(maelys_datalog_result_explain_text_in(result, kind, "allow", query, 1,
            storage, bounds[i], text, length, &required) == MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
        assert(required == length && text[0] == 0 && text[1] == 'X');
        OK(maelys_datalog_result_explain_text_in(result, kind, "allow", query, 1,
            storage, bounds[i], text, length + 1, &required));
        assert(required == length && !strcmp(text, expected));
        assert(attempts == 0 && frees == 0);
        forbidden = 0;
        free(storage);
    }
    forbidden = 1;
    OK(maelys_datalog_result_free(result));
    assert(attempts == 0 && frees == 0);
    forbidden = 0;
    OK(maelys_datalog_input_edb_free(edb));
    OK(maelys_datalog_session_free(session));
}

static void truncated_true_bound(void) {
    const maelys_datalog_predicate_t predicates[] = {
        MAELYS_DATALOG_EDB("edge", 2), MAELYS_DATALOG_IDB_QUERY("path", 2),
        MAELYS_DATALOG_IDB_QUERY("reach", 2),
    };
    const maelys_datalog_domain_t domain = {"bound_path", predicates, 3, NULL, 0};
    OK(maelys_datalog_domain_register(&domain));
    const char *source = "path(X,Y) :- edge(X,Y).\npath(X,Z) :- path(X,Y), edge(Y,Z).\nreach(X,Y) :- path(X,Y).";
    maelys_datalog_policy_t *policy;
    maelys_datalog_session_t *session;
    maelys_datalog_input_edb_t *edb;
    maelys_datalog_result_t *result;
    OK(maelys_datalog_policy_load_inline(domain.name, "path", source, strlen(source), &policy, NULL));
    OK(maelys_datalog_session_create(policy, 0, &session));
    OK(maelys_datalog_input_edb_create(&edb));
    for (int i = 0; i < 8; ++i) {
        char first[8], second[8];
        snprintf(first, sizeof(first), "n%d", i);
        snprintf(second, sizeof(second), "n%d", i + 1);
        maelys_datalog_value_t terms[] = {symbol(first), symbol(second)};
        OK(maelys_datalog_input_edb_add_fact(edb, "edge", terms, 2, NULL));
    }
    OK(maelys_datalog_session_solve_edb(session, edb, &result, NULL));
    maelys_datalog_session_t *cached = cached_session(policy);
    maelys_datalog_result_t *cached_result;
    OK(maelys_datalog_session_solve_edb(cached, edb, &cached_result, NULL));
    maelys_datalog_value_t query[] = {symbol("n0"), symbol("n8")};
    int present;
    OK(maelys_datalog_result_query(result, "path", query, 2, &present));
    assert(present);
    for (int i = 1; i <= 2; ++i) {
        maelys_datalog_explanation_kind_t kind = (maelys_datalog_explanation_kind_t)i;
        size_t bound, alignment, exact, exact_alignment, length;
        OK(maelys_datalog_session_explanation_storage_bound(session, kind, &bound, &alignment));
        OK(maelys_datalog_result_explanation_storage_requirements(result, kind, &exact, &exact_alignment));
        assert(exact <= bound && exact_alignment == alignment);
        void *storage = malloc(bound); assert(storage);
        char text[1024];
        forbidden = 1;
        OK(maelys_datalog_result_explain_text_in(result, kind, "path", query, 2,
            storage, bound, text, sizeof(text), &length));
        assert(length == strlen(text));
        assert(strstr(text, i == 1 ? "status=truncated" : "status=not-applicable"));
        char cached_text[1024];
        if (i == 1) {
            OK(maelys_datalog_result_explain_true_text(cached_result, "path", query, 2, cached_text, sizeof(cached_text), &length));
        } else {
            OK(maelys_datalog_result_explain_false_text(cached_result, "path", query, 2, cached_text, sizeof(cached_text), &length));
        }
        assert(!strcmp(text, cached_text));
        assert(attempts == 0 && frees == 0);
        forbidden = 0;
        free(storage);
    }
    OK(maelys_datalog_result_free(result));
    OK(maelys_datalog_result_free(cached_result));
    OK(maelys_datalog_session_free(cached));
    OK(maelys_datalog_input_edb_free(edb));
    OK(maelys_datalog_session_free(session));
    OK(maelys_datalog_policy_free(policy));
}

int main(void) {
    const maelys_datalog_predicate_t predicates[] = {
        {"seed", 1, MAELYS_DATALOG_PREDICATE_EDB},
        {"blocked", 1, MAELYS_DATALOG_PREDICATE_EDB},
        {"allow", 1, MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY},
    };
    const maelys_datalog_domain_t domain = {"prepared_explanation", predicates, 3, NULL, 0};
    OK(maelys_datalog_domain_register(&domain));
    const char *source = "allow(X) :- seed(X), not(blocked(X)).";
    maelys_datalog_policy_t *policy;
    OK(maelys_datalog_policy_load_inline(domain.name, "example", source, strlen(source), &policy, NULL));
    reference_conveniences(policy);
    maelys_datalog_backend_t backend = *maelys_datalog_backend_reference();
    backend.explanation_storage_requirements = requirements;
    backend.explanation_prepare = prepare;
    backend.explanation_write_text = write_text;
    maelys_datalog_session_options_t options = {MAELYS_DATALOG_BACKEND_ABI_VERSION, sizeof(options), &backend, 0, 0};
    maelys_datalog_session_t *session = NULL;
    /* ABI 2 is rejected before any new callback is read or called. */
    backend.abi_version = 2;
    assert(maelys_datalog_session_create_ex(policy, 0, &options, &session) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    backend = *maelys_datalog_backend_reference();
    backend.explanation_storage_requirements = requirements;
    backend.explanation_prepare = prepare;
    backend.explanation_write_text = NULL;
    assert(maelys_datalog_session_create_ex(policy, 0, &options, &session) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    backend.explanation_write_text = write_text;
    OK(maelys_datalog_session_create_ex(policy, 0, &options, &session));
    size_t bound = 123, bound_alignment = 456;
    assert(maelys_datalog_session_explanation_storage_bound(session, MAELYS_DATALOG_EXPLAIN_TRUE,
        &bound, &bound_alignment) == MAELYS_DATALOG_STATUS_UNSUPPORTED);
    assert(bound == 123 && bound_alignment == 456);
    maelys_datalog_input_edb_t *edb;
    OK(maelys_datalog_input_edb_create(&edb));
    maelys_datalog_value_t alice = symbol("alice"), bob = symbol("bob"), missing = symbol("unknown");
    OK(maelys_datalog_input_edb_add_fact(edb, "seed", &alice, 1, NULL));
    OK(maelys_datalog_input_edb_add_fact(edb, "seed", &bob, 1, NULL));
    OK(maelys_datalog_input_edb_add_fact(edb, "blocked", &bob, 1, NULL));
    maelys_datalog_result_t *result;
    OK(maelys_datalog_session_solve_edb(session, edb, &result, NULL));
    char expected[2][8192], text[8192];
    size_t needed;
    OK(maelys_datalog_result_explain_true_text(result, "allow", &alice, 1, expected[0], sizeof(expected[0]), &needed));
    OK(maelys_datalog_result_explain_false_text(result, "allow", &bob, 1, expected[1], sizeof(expected[1]), &needed));
    assert(strstr(expected[1], "negative-contradicted"));
    size_t bytes[2], alignment;
    void *storage[2];
    for (int i = 0; i < 2; ++i) {
        OK(maelys_datalog_result_explanation_storage_requirements(result, (maelys_datalog_explanation_kind_t)(i + 1), &bytes[i], &alignment));
        storage[i] = malloc(bytes[i]); assert(storage[i] && (uintptr_t)storage[i] % alignment == 0);
    }
    printf("prepared explanation caller bytes: why-true=%zu why-false=%zu alignment=%zu\n", bytes[0], bytes[1], alignment);
    forbidden = 1;
    maelys_datalog_prepared_explanation_t *p[2] = {NULL, NULL}, *sentinel;
    size_t before = prepares;
    for (int i = 0; i < 2; ++i) {
        maelys_datalog_explanation_kind_t kind = (maelys_datalog_explanation_kind_t)(i + 1);
        const maelys_datalog_value_t *value = i ? &bob : &alice;
        sentinel = (void *)(uintptr_t)1;
        assert(maelys_datalog_result_prepare_explanation(result, kind, "allow", value, 1, storage[i], bytes[i] - 1, &sentinel) == MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL);
        assert(sentinel == (void *)(uintptr_t)1);
        assert(maelys_datalog_result_prepare_explanation(result, kind, "allow", value, 1, (char *)storage[i] + 1, bytes[i] - 1, &sentinel) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
        assert(maelys_datalog_result_prepare_explanation(result, kind, "allow", &missing, 1, storage[i], bytes[i], &sentinel) == MAELYS_DATALOG_STATUS_NOT_FOUND);
        assert(maelys_datalog_result_prepare_explanation(result, kind, "seed", value, 1, storage[i], bytes[i], &sentinel) != 0);
        OK(maelys_datalog_result_prepare_explanation(result, kind, "allow", value, 1, storage[i], bytes[i], &p[i]));
        assert(maelys_datalog_result_prepare_explanation(result, kind, "allow", value, 1, storage[i], bytes[i], &sentinel) == MAELYS_DATALOG_STATUS_INVALID_STATE);
        OK(maelys_datalog_prepared_explanation_text_size(p[i], &needed));
        assert(needed == strlen(expected[i]));
        memset(text, 'X', sizeof(text));
        assert(maelys_datalog_prepared_explanation_write_text(p[i], text, needed) == MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
        assert(text[0] == 0 && text[1] == 'X');
        assert(maelys_datalog_prepared_explanation_write_text(p[i], NULL, 0) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
        assert(maelys_datalog_prepared_explanation_write_text(p[i], storage[i], bytes[i]) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
        for (size_t n = 0; n < 5; ++n) {
            OK(maelys_datalog_prepared_explanation_text_size(p[i], &needed));
            OK(maelys_datalog_prepared_explanation_write_text(p[i], text, needed + 1));
            assert(!strcmp(text, expected[i]));
        }
        assert(maelys_datalog_result_free(result) == MAELYS_DATALOG_STATUS_INVALID_STATE);
        assert(maelys_datalog_session_free(session) == MAELYS_DATALOG_STATUS_INVALID_STATE);
    }
    assert(prepares == before + 2 && writes >= 10);
    OK(maelys_datalog_prepared_explanation_release(p[0]));
    assert(maelys_datalog_prepared_explanation_release(p[0]) == MAELYS_DATALOG_STATUS_INVALID_STATE);
    assert(maelys_datalog_result_free(result) == MAELYS_DATALOG_STATUS_INVALID_STATE);
    /* Input may be recycled; retained result and explanation remain unchanged. */
    OK(maelys_datalog_input_edb_clear(edb));
    OK(maelys_datalog_prepared_explanation_write_text(p[1], text, sizeof(text)));
    assert(!strcmp(text, expected[1]));
    mode = 5;
    assert(maelys_datalog_prepared_explanation_write_text(p[1], text, sizeof(text)) == MAELYS_DATALOG_STATUS_INVALID_STATE);
    mode = 0;
    OK(maelys_datalog_prepared_explanation_release(p[1]));
    for (mode = 1; mode <= 4; ++mode) {
        sentinel = (void *)(uintptr_t)1;
        assert(maelys_datalog_result_prepare_explanation(result, MAELYS_DATALOG_EXPLAIN_TRUE,
            "allow", &alice, 1, storage[0], bytes[0], &sentinel) != 0);
        assert(sentinel == (void *)(uintptr_t)1);
    }
    mode = 0;
    /* Released caller memory can be reused; no fresh allocation is needed. */
    OK(maelys_datalog_result_prepare_explanation(result, MAELYS_DATALOG_EXPLAIN_TRUE,
        "allow", &alice, 1, storage[0], bytes[0], &p[0]));
    OK(maelys_datalog_prepared_explanation_release(p[0]));
    /* Both a malformed successful write and an explicit backend error must
     * release the one-shot lease. Retry proves preparation happens each time. */
    before = prepares;
    for (mode = 5; mode <= 6; ++mode) {
        maelys_datalog_status_t expected_error = mode == 5
            ? MAELYS_DATALOG_STATUS_INVALID_STATE : MAELYS_DATALOG_STATUS_IO;
        assert(maelys_datalog_result_explain_text_in(result, MAELYS_DATALOG_EXPLAIN_TRUE,
            "allow", &alice, 1, storage[0], bytes[0], text, sizeof(text), &needed) == expected_error);
        assert(needed == strlen(expected[0]));
    }
    assert(prepares == before + 2);
    mode = 0;
    OK(maelys_datalog_result_free(result));
    assert(attempts == 0 && frees == 0);
    forbidden = 0;
    free(storage[0]); free(storage[1]);
    OK(maelys_datalog_input_edb_free(edb));
    OK(maelys_datalog_session_free(session));
    OK(maelys_datalog_policy_free(policy));
    bounded_case("allow(X) :- seed(X), seed(Y), blocked(Y).", "positive-no-match");
    bounded_case("allow(X) :- allow(X).", "recursive-no-base-support");
    bounded_case("allow(X) :- seed(X), starts_with(X, \"z\").", "filter-false");
    bounded_case("allow(X) :- seed(X).", "status=not-applicable");
    char many_rules[2048] = {0};
    for (size_t i = 0; i < 17; ++i) strcat(many_rules, "allow(X) :- seed(X), blocked(X).\n");
    bounded_case(many_rules, "status=truncated");
    truncated_true_bound();
    puts("prepared explanations: one extraction per prepare, zero allocator calls, leases and buffers verified");
    return 0;
}
