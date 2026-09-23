/* SPDX-License-Identifier: MPL-2.0 */
/* White-box runtime test: instrument a canonical reference session's callback
 * after creation without pretending that a copied descriptor has a bound.
 * Link every other engine unit with allocation_guard, never a second runtime. */
#include "tests/fixtures/allocation_guard.h"
#include "src/runtime/maelys_datalog_runtime.c"
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef memset
#include <assert.h>

static size_t allocations, deallocations, fail_at, prepares;
static int forbidden, fail_prepare, fail_write;
void *maelys_test_malloc(size_t n) {
    ++allocations;
    return forbidden || (fail_at && allocations == fail_at) ? NULL : malloc(n);
}
void *maelys_test_calloc(size_t n, size_t size) {
    ++allocations;
    return forbidden || (fail_at && allocations == fail_at) ? NULL : calloc(n, size);
}
void *maelys_test_realloc(void *p, size_t n) {
    ++allocations;
    return forbidden || (fail_at && allocations == fail_at) ? NULL : realloc(p, n);
}
void maelys_test_free(void *p) { if (p) ++deallocations; free(p); }
void *maelys_test_memset(void *p, int c, size_t n) { return memset(p, c, n); }
#define OK(call) assert((call) == MAELYS_DATALOG_STATUS_OK)
#define BOTH ((unsigned)(MAELYS_DATALOG_EXPLAIN_TRUE | MAELYS_DATALOG_EXPLAIN_FALSE))

static maelys_datalog_status_t counted_prepare(void *s, void *r,
    maelys_datalog_explanation_kind_t kind, const char *name,
    const maelys_datalog_value_t *terms, size_t arity,
    void *storage, size_t bytes, size_t *length) {
    ++prepares;
    if (fail_prepare) return MAELYS_DATALOG_STATUS_INTERNAL;
    return maelys_datalog_backend_reference()->explanation_prepare(
        s, r, kind, name, terms, arity, storage, bytes, length);
}
static maelys_datalog_status_t checked_write(void *s, void *r,
    maelys_datalog_explanation_kind_t kind, const void *storage, char *text, size_t bytes) {
    if (fail_write) return MAELYS_DATALOG_STATUS_IO;
    return maelys_datalog_backend_reference()->explanation_write_text(s, r, kind, storage, text, bytes);
}
static maelys_datalog_value_t symbol(const char *text) {
    maelys_datalog_value_t term = MAELYS_DATALOG_SYMBOL(text);
    return term;
}
static maelys_datalog_session_t *configured(maelys_datalog_policy_t *policy, unsigned kinds) {
    maelys_datalog_session_config_t *config;
    maelys_datalog_session_t *session;
    OK(maelys_datalog_session_config_create(&config));
    OK(maelys_datalog_session_config_set_explanation_workspace(config, kinds));
    OK(maelys_datalog_session_create_configured(policy, 0, config, &session));
    OK(maelys_datalog_session_config_free(config));
    session->backend.explanation_prepare = counted_prepare;
    session->backend.explanation_write_text = checked_write;
    return session;
}

static void creation_contract(maelys_datalog_policy_t *policy) {
    maelys_datalog_session_config_t *config;
    OK(maelys_datalog_session_config_create(&config));
    assert(maelys_datalog_session_config_set_explanation_workspace(NULL, BOTH) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    maelys_datalog_session_t *plain, *owned, *other = NULL;
    size_t start = allocations;
    OK(maelys_datalog_session_create_configured(policy, 0, config, &plain));
    size_t base_allocations = allocations - start;
    assert(!plain->explanation_storage && !plain->explanation_kinds);
    size_t sizes[2], alignment;
    for (unsigned i = 0; i < 2; ++i)
        OK(maelys_datalog_session_explanation_storage_bound(plain,
            (maelys_datalog_explanation_kind_t)(i + 1), &sizes[i], &alignment));
    size_t bound = sizes[0] > sizes[1] ? sizes[0] : sizes[1];
    void *buffer = malloc(bound + alignment); assert(buffer);
    memset(buffer, 0x5a, bound + alignment);

    OK(maelys_datalog_session_config_set_explanation_workspace(config, BOTH));
    assert(maelys_datalog_session_config_set_explanation_workspace(config, 4) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    assert(config->explanation_kinds == BOTH);
    start = allocations;
    OK(maelys_datalog_session_create_configured(policy, 0, config, &owned));
    assert(allocations - start == base_allocations + 1);
    assert(owned->explanation_bytes == bound && owned->owns_explanation_storage);
    char first[65], second[65];
    OK(maelys_datalog_session_execution_fingerprint(plain, first));
    OK(maelys_datalog_session_execution_fingerprint(owned, second));
    assert(!strcmp(first, second));
    printf("session explanation workspace true=%zu false=%zu reserved=%zu additional_allocations=1\n",
           sizes[0], sizes[1], bound);
    OK(maelys_datalog_session_free(owned));

    /* Every constructor failure clears the output and leaves no registered range. */
    for (size_t i = 1; i <= base_allocations + 1; ++i) {
        fail_at = allocations + i;
        other = (void *)(uintptr_t)1;
        assert(maelys_datalog_session_create_configured(policy, 0, config, &other) != MAELYS_DATALOG_STATUS_OK);
        assert(!other && !workspaces);
        fail_at = 0;
    }
    assert(maelys_datalog_session_config_set_explanation_storage(config, BOTH,
        (unsigned char *)buffer + 1, bound) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    OK(maelys_datalog_session_config_set_explanation_storage(config, BOTH, buffer, bound - 1));
    assert(maelys_datalog_session_create_configured(policy, 0, config, &other) == MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL);
    assert(!other);
    OK(maelys_datalog_session_config_set_explanation_storage(config, BOTH, buffer, bound));
    start = allocations;
    OK(maelys_datalog_session_create_configured(policy, 0, config, &owned));
    assert(allocations - start == base_allocations && !owned->owns_explanation_storage);
    for (size_t i = 0; i < bound; ++i) assert(((unsigned char *)buffer)[i] == 0x5a);
    assert(maelys_datalog_session_create_configured(policy, 0, config, &other) == MAELYS_DATALOG_STATUS_INVALID_STATE);
    assert(!other);
    OK(maelys_datalog_session_config_set_explanation_storage(config, BOTH,
        (unsigned char *)buffer + alignment, bound));
    assert(maelys_datalog_session_create_configured(policy, 0, config, &other) == MAELYS_DATALOG_STATUS_INVALID_STATE);
    assert(!other);
    OK(maelys_datalog_session_free(owned));
    OK(maelys_datalog_session_create_configured(policy, 0, config, &other));
    OK(maelys_datalog_session_free(other));
    assert(!workspaces);

    /* Replacing the mode disables borrowing; freeing/reusing config is independent. */
    OK(maelys_datalog_session_config_set_explanation_workspace(config, 0));
    OK(maelys_datalog_session_create_configured(policy, 0, config, &other));
    assert(!other->explanation_storage);
    OK(maelys_datalog_session_free(other));
    OK(maelys_datalog_session_config_set_explanation_workspace(config, BOTH));
    maelys_datalog_backend_t copy = *maelys_datalog_backend_reference();
    maelys_datalog_session_options_t options = {MAELYS_DATALOG_BACKEND_ABI_VERSION, sizeof(options), &copy, 0, 0};
    OK(maelys_datalog_session_create_ex(policy, 0, &options, &other));
    assert(configure_explanation_workspace(other, config) == MAELYS_DATALOG_STATUS_UNSUPPORTED);
    assert(!other->explanation_storage && !workspaces);
    OK(maelys_datalog_session_free(other));
    OK(maelys_datalog_session_config_free(config));
    OK(maelys_datalog_session_free(plain));
    free(buffer);
    assert(!strcmp(maelys_datalog_status_name(MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL), "storage_too_small"));
    assert(maelys_datalog_callback_status(MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL) == MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL);
}

static void cache_contract(maelys_datalog_policy_t *policy, maelys_datalog_input_edb_t *edb,
                           int borrowed) {
    maelys_datalog_session_t *s = configured(policy, BOTH);
    void *buffer = NULL;
    if (borrowed) {
        size_t bound = s->explanation_bytes;
        OK(maelys_datalog_session_free(s));
        buffer = malloc(bound); assert(buffer);
        maelys_datalog_session_config_t *config;
        OK(maelys_datalog_session_config_create(&config));
        OK(maelys_datalog_session_config_set_explanation_storage(config, BOTH, buffer, bound));
        OK(maelys_datalog_session_create_configured(policy, 0, config, &s));
        OK(maelys_datalog_session_config_free(config));
        s->backend.explanation_prepare = counted_prepare;
        s->backend.explanation_write_text = checked_write;
    }
    size_t a0 = allocations, f0 = deallocations, p0 = prepares;
    forbidden = 1;
    maelys_datalog_result_t *r;
    OK(maelys_datalog_session_solve_edb(s, edb, &r, NULL));
    char text[8192];
    size_t length = 0;
    for (unsigned i = 0; i < 40; ++i) {
        maelys_datalog_value_t term = symbol(i % 2 ? "bob" : "alice");
        maelys_datalog_status_t (*explain)(const maelys_datalog_result_t *, const char *,
            const maelys_datalog_value_t *, size_t, char *, size_t, size_t *) = i % 2
            ? maelys_datalog_result_explain_false_text : maelys_datalog_result_explain_true_text;
        OK(explain(r, "allow", &term, 1, NULL, 0, &length));
        assert(prepares == p0 + i + 1);
        int present;
        OK(maelys_datalog_result_query(r, "allow", &term, 1, &present));
        assert(present == !(i % 2));
        text[0] = 'X';
        assert(explain(r, "allow", &term, 1, text, 1, &length) == MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
        assert(text[0] == '\0');
        OK(explain(r, "allow", &term, 1, text, sizeof(text), &length));
        assert(prepares == p0 + i + 1 && strlen(text) == length);
        assert(strstr(text, i % 2 ? "document=why-false" : "document=why-true"));
    }
    assert(allocations == a0 && deallocations == f0);
    maelys_datalog_value_t term = symbol("alice");
    OK(maelys_datalog_result_explain_true_text(r, "allow", &term, 1, NULL, 0, &length));
    p0 = prepares;
    char same_value[] = "alice", same_name[] = "allow";
    term.as.symbol = same_value;
    OK(maelys_datalog_result_explain_true_text(r, same_name, &term, 1, text, sizeof(text), &length));
    assert(prepares == p0); /* Value identity, not pointer identity. */
    OK(maelys_datalog_result_explain_false_text(r, same_name, &term, 1, text, sizeof(text), &length));
    assert(prepares == ++p0 && strstr(text, "status=not-applicable"));
    strcpy(same_value, "bob");
    OK(maelys_datalog_result_explain_false_text(r, same_name, &term, 1, text, sizeof(text), &length));
    assert(prepares == ++p0 && strstr(text, "status=complete"));
    length = 123;
    assert(maelys_datalog_result_explain_false_text(r, "absent", &term, 1,
        text, sizeof(text), &length) != MAELYS_DATALOG_STATUS_OK);
    assert(length == 123 && prepares == p0);
    assert(maelys_datalog_result_explain_false_text(r, "allow", &term, 2,
        text, sizeof(text), &length) != MAELYS_DATALOG_STATUS_OK);
    assert(prepares == p0);
    maelys_datalog_value_t unknown = symbol("unknown");
    assert(maelys_datalog_result_explain_false_text(r, "allow", &unknown, 1,
        text, sizeof(text), &length) == MAELYS_DATALOG_STATUS_NOT_FOUND);
    assert(prepares == p0);
    OK(maelys_datalog_result_explain_false_text(r, "allow", &term, 1, text, sizeof(text), &length));
    assert(prepares == p0); /* Invalid requests did not replace the cache. */
    assert(maelys_datalog_result_explain_false_text(r, "allow", &term, 1,
        NULL, 0, s->explanation_storage) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    assert(maelys_datalog_result_explain_false_text(r, "allow", &term, 1,
        s->explanation_storage, s->explanation_bytes, &length) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    OK(maelys_datalog_result_explain_false_text(r, "allow", &term, 1, text, sizeof(text), &length));
    assert(prepares == p0); /* Alias rejection left the cached handle intact. */
    fail_write = 1;
    assert(maelys_datalog_result_explain_false_text(r, "allow", &term, 1,
        text, sizeof(text), &length) == MAELYS_DATALOG_STATUS_IO);
    fail_write = 0;
    OK(maelys_datalog_result_explain_false_text(r, "allow", &term, 1, text, sizeof(text), &length));
    assert(prepares == p0);
    fail_prepare = 1;
    term = symbol("alice");
    assert(maelys_datalog_result_explain_true_text(r, "allow", &term, 1,
        text, sizeof(text), &length) == MAELYS_DATALOG_STATUS_INTERNAL);
    assert(!s->explanation_cache && !r->explanations);
    fail_prepare = 0;
    OK(maelys_datalog_result_explain_true_text(r, "allow", &term, 1, NULL, 0, &length));
    /* Measuring only must not prevent result release, nor release the session. */
    assert(maelys_datalog_session_free(s) == MAELYS_DATALOG_STATUS_INVALID_STATE);
    OK(maelys_datalog_result_free(r));
    assert(!s->explanation_cache);
    p0 = prepares;
    OK(maelys_datalog_session_solve_edb(s, edb, &r, NULL));
    OK(maelys_datalog_result_explain_true_text(r, "allow", &term, 1, NULL, 0, &length));
    assert(prepares == p0 + 1); /* The reused result address is not an identity. */
    /* Distinct value kinds must not share a cache entry. */
    term = (maelys_datalog_value_t){.kind = MAELYS_DATALOG_VALUE_INTEGER, .as.integer = 1};
    OK(maelys_datalog_result_explain_true_text(r, "allow", &term, 1, text, sizeof(text), &length));
    p0 = prepares;
    term = (maelys_datalog_value_t){.kind = MAELYS_DATALOG_VALUE_BOOLEAN, .as.boolean = 1};
    OK(maelys_datalog_result_explain_true_text(r, "allow", &term, 1, text, sizeof(text), &length));
    assert(prepares == p0 + 1);
    OK(maelys_datalog_result_explain_true_text(r, "copy", &term, 1, text, sizeof(text), &length));
    assert(prepares == p0 + 2);
    OK(maelys_datalog_result_free(r));
    assert(allocations == a0 && deallocations == f0);
    forbidden = 0;
    OK(maelys_datalog_session_free(s));
    free(buffer);
}

static void distinct_requests(maelys_datalog_policy_t *policy) {
    maelys_datalog_session_t *s = configured(policy, BOTH);
    maelys_datalog_input_edb_t *edb;
    OK(maelys_datalog_input_edb_create(&edb));
    maelys_datalog_value_t alice = symbol("alice");
    char first[8192], text[8192];
    size_t length, a0 = allocations, f0 = deallocations, p0 = prepares;
    forbidden = 1;
    for (unsigned i = 0; i < 3; ++i) {
        OK(maelys_datalog_input_edb_clear(edb));
        OK(maelys_datalog_input_edb_add_fact(edb, "seed", &alice, 1, NULL));
        if (i == 1) OK(maelys_datalog_input_edb_add_fact(edb, "blocked", &alice, 1, NULL));
        maelys_datalog_result_t *r;
        OK(maelys_datalog_session_solve_edb(s, edb, &r, NULL));
        OK(maelys_datalog_result_explain_true_text(r, "allow", &alice, 1, NULL, 0, &length));
        OK(maelys_datalog_result_explain_true_text(r, "allow", &alice, 1, text, sizeof(text), &length));
        assert(prepares == p0 + i + 1);
        assert(strstr(text, i == 1 ? "status=not-derived" : "status=complete"));
        if (i == 0) strcpy(first, text);
        if (i == 2) assert(!strcmp(first, text));
        OK(maelys_datalog_result_free(r));
    }
    assert(allocations == a0 && deallocations == f0);
    forbidden = 0;
    OK(maelys_datalog_input_edb_free(edb));
    OK(maelys_datalog_session_free(s));
}

static void leases_and_fallback(maelys_datalog_policy_t *policy, maelys_datalog_input_edb_t *edb) {
    maelys_datalog_session_t *s = configured(policy, MAELYS_DATALOG_EXPLAIN_TRUE);
    maelys_datalog_result_t *r;
    OK(maelys_datalog_session_solve_edb(s, edb, &r, NULL));
    size_t bytes, alignment, required;
    OK(maelys_datalog_result_explanation_storage_requirements(r, MAELYS_DATALOG_EXPLAIN_TRUE, &bytes, &alignment));
    void *storage = malloc(bytes); assert(storage);
    maelys_datalog_value_t term = symbol("alice");
    maelys_datalog_prepared_explanation_t *external;
    size_t a0 = allocations, f0 = deallocations;
    forbidden = 1;
    OK(maelys_datalog_result_explain_true_text(r, "allow", &term, 1, NULL, 0, &required));
    assert(maelys_datalog_result_explain_false_text(r, "allow", &term, 1, NULL, 0, &required) == MAELYS_DATALOG_STATUS_UNSUPPORTED);
    OK(maelys_datalog_result_prepare_explanation(r, MAELYS_DATALOG_EXPLAIN_TRUE,
        "allow", &term, 1, storage, bytes, &external));
    assert(maelys_datalog_result_free(r) == MAELYS_DATALOG_STATUS_INVALID_STATE);
    assert(s->explanation_cache);
    OK(maelys_datalog_prepared_explanation_release(external));
    OK(maelys_datalog_result_free(r));
    assert(allocations == a0 && deallocations == f0);
    forbidden = 0;
    OK(maelys_datalog_session_free(s));
    free(storage);
    s = configured(policy, 0);
    OK(maelys_datalog_session_solve_edb(s, edb, &r, NULL));
    a0 = allocations;
    size_t p0 = prepares;
    char text[8192];
    OK(maelys_datalog_result_explain_true_text(r, "allow", &term, 1, NULL, 0, &required));
    OK(maelys_datalog_result_explain_true_text(r, "allow", &term, 1, text, sizeof(text), &required));
    assert(allocations == a0 + 2 && prepares == p0 + 2);
    OK(maelys_datalog_result_free(r));
    OK(maelys_datalog_session_free(s));
}

int main(void) {
    const maelys_datalog_predicate_t predicates[] = {
        MAELYS_DATALOG_EDB("seed", 1), MAELYS_DATALOG_EDB("blocked", 1),
        MAELYS_DATALOG_IDB_QUERY("allow", 1), MAELYS_DATALOG_IDB_QUERY("copy", 1),
    };
    const maelys_datalog_domain_t domain = {"session_explanations", predicates, 4, NULL, 0};
    OK(maelys_datalog_domain_register(&domain));
    const char *source = "allow(X) :- seed(X), not(blocked(X)). copy(X) :- allow(X).";
    maelys_datalog_policy_t *policy;
    OK(maelys_datalog_policy_load_inline(domain.name, "example", source, strlen(source), &policy, NULL));
    maelys_datalog_input_edb_t *edb;
    OK(maelys_datalog_input_edb_create(&edb));
    OK(MAELYS_DATALOG_ADD_FACTS(edb, NULL,
        MAELYS_DATALOG_FACT("seed", "alice"), MAELYS_DATALOG_FACT("seed", "bob"),
        MAELYS_DATALOG_FACT("blocked", "bob"), MAELYS_DATALOG_FACT("seed", 1),
        MAELYS_DATALOG_FACT("seed", MAELYS_DATALOG_BOOL(1))));
    creation_contract(policy);
    cache_contract(policy, edb, 0);
    cache_contract(policy, edb, 1);
    distinct_requests(policy);
    leases_and_fallback(policy, edb);
    assert(!workspaces);
    OK(maelys_datalog_input_edb_free(edb));
    OK(maelys_datalog_policy_free(policy));
    puts("session explanation allocation/cache/ownership contracts: OK");
    return 0;
}
