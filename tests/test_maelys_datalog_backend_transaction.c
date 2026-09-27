/* SPDX-License-Identifier: MPL-2.0 */
#include <maelys/datalog_advanced.h>
#include <maelys/datalog_window.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)
#define OK(c) CHECK((c) == MAELYS_DATALOG_STATUS_OK)
#define INVALID MAELYS_DATALOG_STATUS_INVALID_ARGUMENT
_Static_assert(MAELYS_DATALOG_BACKEND_ABI_VERSION == 5u, "backend ABI");
_Static_assert(MAELYS_DATALOG_PUBLIC_API_VERSION == 2u, "consumer API");
_Static_assert(MAELYS_DATALOG_PROGRAM_ABI_VERSION == 2u, "program ABI");
_Static_assert(MAELYS_DATALOG_DIAGNOSTIC_ABI_VERSION == 1u, "diagnostic ABI");
_Static_assert(sizeof(maelys_datalog_diagnostic_t) == 1328u, "diagnostic layout");

enum { GOOD, BAD_SYMBOL, CAPACITY, WORK, CALLBACK_ERROR, NULL_RESULT };
typedef struct {
    unsigned prepare, solve, commits, releases, aborts, destroy, explains;
    unsigned mode, live, accepted, serial, committed_serial;
    int fail_prepare;
    char order[128];
    size_t order_size;
    void *received;
    maelys_datalog_session_t *session;
    int inspect_inputs;
    const maelys_datalog_fact_t *expected_inputs;
    size_t expected_count;
} recording_t;
typedef union { max_align_t alignment; recording_t state; } pool_t;
static size_t queries, prepares, required_bytes = sizeof(recording_t);
static size_t required_alignment = _Alignof(max_align_t);
static maelys_datalog_status_t requirement_error;

static void record(recording_t *r, char event) {
    CHECK(r->order_size + 1 < sizeof(r->order));
    r->order[r->order_size++] = event;
    r->order[r->order_size] = 0;
}
static maelys_datalog_status_t requirements(const maelys_datalog_program_t *p, size_t *n, size_t *a) {
    maelys_datalog_program_info_t info;
    OK(maelys_datalog_program_info(p, &info));
    CHECK(info.rule_count == 1);
    ++queries;
    *n = required_bytes; *a = required_alignment;
    return requirement_error;
}
static maelys_datalog_status_t prepare(const maelys_datalog_program_t *p,
    const maelys_datalog_backend_storage_t *storage, void **out) {
    (void)p;
    ++prepares;
    CHECK(storage && storage->struct_size == sizeof(*storage));
    *out = storage->bytes;
    if (!*out) { CHECK(required_bytes == 0); return MAELYS_DATALOG_STATUS_OK; }
    recording_t *r = *out;
    ++r->prepare; r->received = storage->bytes;
    record(r, 'P');
    return r->fail_prepare ? MAELYS_DATALOG_STATUS_UNSUPPORTED : MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_fact_t output_fact(int64_t value) {
    maelys_datalog_fact_t f = {.predicate = "seen", .arity = 1};
    f.terms[0].kind = MAELYS_DATALOG_VALUE_INTEGER;
    f.terms[0].as.integer = value;
    return f;
}
static maelys_datalog_status_t solve(void *state, const maelys_datalog_fact_t *facts,
    size_t count, maelys_datalog_backend_output_t *out, void **result,
    maelys_datalog_diagnostic_t *diag) {
    (void)diag;
    recording_t *r = state;
    if (!r) { *result = NULL; return MAELYS_DATALOG_STATUS_OK; }
    CHECK(r->received == state && !r->live);
    if (r->inspect_inputs) {
        CHECK(count == r->expected_count);
        CHECK((facts == NULL) == (count == 0));
        for (size_t i = 0; i < count; ++i) {
            const maelys_datalog_fact_t *expected = &r->expected_inputs[i];
            CHECK(!strcmp(facts[i].predicate, expected->predicate));
            CHECK(facts[i].arity == expected->arity);
            for (size_t j = 0; j < expected->arity; ++j) {
                const maelys_datalog_value_t *got = &facts[i].terms[j], *want = &expected->terms[j];
                CHECK(got->kind == want->kind);
                switch (want->kind) {
                    case MAELYS_DATALOG_VALUE_SYMBOL: CHECK(!strcmp(got->as.symbol, want->as.symbol)); break;
                    case MAELYS_DATALOG_VALUE_INTEGER: CHECK(got->as.integer == want->as.integer); break;
                    case MAELYS_DATALOG_VALUE_BOOLEAN: CHECK(got->as.boolean == want->as.boolean); break;
                    default: CHECK(0);
                }
            }
        }
    }
    ++r->solve; ++r->serial; r->live = 1; r->accepted = 0;
    record(r, 'S');
    *result = r->mode == NULL_RESULT ? NULL : r;
    maelys_datalog_fact_t f = output_fact(7);
    if (r->mode == BAD_SYMBOL) {
        f.terms[0].kind = MAELYS_DATALOG_VALUE_SYMBOL;
        f.terms[0].as.symbol = "not_in_the_vocabulary";
    }
    if (r->mode == CAPACITY) {
        size_t limit;
        OK(maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_FACTS_PER_PRED, &limit));
        for (size_t i = 0; i <= limit; ++i) {
            f.terms[0].as.integer = (int64_t)i;
            (void)maelys_datalog_backend_emit(out, &f);
        }
    } else {
        (void)maelys_datalog_backend_emit(out, &f);
        (void)maelys_datalog_backend_emit(out, &f); /* host deduplication */
    }
    if (r->mode == WORK) (void)maelys_datalog_backend_charge(out, UINT64_MAX);
    /* Deliberately ignore sticky output errors: only the host can accept. */
    return r->mode == CALLBACK_ERROR ? MAELYS_DATALOG_STATUS_UNSUPPORTED : MAELYS_DATALOG_STATUS_OK;
}
static void commit(void *state, void *result) {
    recording_t *r = state;
    if (!r) { CHECK(!result); return; }
    CHECK(r->live && !r->accepted);
    CHECK(result == r || (r->mode == NULL_RESULT && !result));
    if (r->session) {
        maelys_datalog_result_t *unexpected = NULL;
        CHECK(maelys_datalog_session_solve(r->session, NULL, 0, &unexpected, NULL) ==
            MAELYS_DATALOG_STATUS_INVALID_STATE);
        CHECK(!unexpected);
    }
    ++r->commits; r->accepted = 1; r->committed_serial = r->serial;
    record(r, 'C');
}
static void destroy_result(void *state, void *result) {
    recording_t *r = state;
    if (!r) { CHECK(!result); return; }
    CHECK(r->live && (result == r || (r->mode == NULL_RESULT && !result)));
    if (r->accepted) { ++r->releases; record(r, 'R'); }
    else { ++r->aborts; record(r, 'A'); }
    r->live = 0;
}
static void destroy(void *state) {
    recording_t *r = state;
    if (r) { CHECK(!r->live); ++r->destroy; record(r, 'D'); }
}
static maelys_datalog_status_t explanation_requirements(void *state, void *result,
    maelys_datalog_explanation_kind_t kind, size_t *bytes, size_t *alignment) {
    recording_t *r = state; (void)kind;
    CHECK(r && result == r && r->live && r->accepted);
    ++r->explains; record(r, 'E');
    *bytes = 1; *alignment = 1;
    return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t explanation_prepare(void *state, void *result,
    maelys_datalog_explanation_kind_t kind, const char *name,
    const maelys_datalog_value_t *terms, size_t arity, void *storage, size_t bytes, size_t *length) {
    (void)kind; (void)name; (void)terms; (void)arity;
    recording_t *r = state;
    CHECK(r && result == r && r->accepted && bytes >= 1);
    *(char *)storage = 'x'; *length = 1;
    return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t explanation_write(void *state, void *result,
    maelys_datalog_explanation_kind_t kind, const void *storage, char *text, size_t size) {
    (void)kind;
    recording_t *r = state;
    CHECK(r && result == r && r->accepted && size >= 2);
    text[0] = *(const char *)storage; text[1] = 0;
    return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_backend_t backend(void) {
    return (maelys_datalog_backend_t){
        .abi_version = MAELYS_DATALOG_BACKEND_ABI_VERSION, .struct_size = sizeof(maelys_datalog_backend_t),
        .name = "recording", .semantic_id = "tests.recording.v1",
        .capabilities = MAELYS_DATALOG_CAP_POSITIVE | MAELYS_DATALOG_CAP_WORK_LIMIT | MAELYS_DATALOG_CAP_EXPLAIN_TRUE,
        .storage_requirements = requirements, .prepare = prepare, .solve = solve,
        .explanation_storage_requirements = explanation_requirements,
        .explanation_prepare = explanation_prepare, .explanation_write_text = explanation_write,
        .commit = commit, .destroy_result = destroy_result, .destroy = destroy
    };
}
static maelys_datalog_policy_t *policy;
static maelys_datalog_backend_storage_t storage_for(pool_t *pool) {
    return (maelys_datalog_backend_storage_t){sizeof(maelys_datalog_backend_storage_t),
        pool, sizeof(*pool), _Alignof(pool_t)};
}
static maelys_datalog_session_t *create_for_policy(pool_t *pool, const maelys_datalog_policy_t *p) {
    maelys_datalog_session_config_t *config = NULL;
    maelys_datalog_backend_t b = backend();
    maelys_datalog_backend_storage_t storage = storage_for(pool);
    maelys_datalog_session_t *session = NULL;
    OK(maelys_datalog_session_config_create(&config));
    OK(maelys_datalog_session_config_set_backend_storage(config, &storage));
    OK(maelys_datalog_session_config_set_backend(config, &b));
    storage.bytes = NULL; storage.size = 0; /* config owns the descriptor copy */
    size_t before = queries;
    OK(maelys_datalog_session_create_configured(p, 0, config, &session));
    CHECK(queries == before + 1 && pool->state.received == pool && pool->state.prepare == 1);
    pool->state.session = session;
    OK(maelys_datalog_session_config_free(config));
    return session;
}
static maelys_datalog_session_t *create(pool_t *pool) {
    return create_for_policy(pool, policy);
}
static void abi_rejection(void) {
    maelys_datalog_session_config_t *config = NULL;
    maelys_datalog_context_t *context = NULL;
    OK(maelys_datalog_session_config_create(&config));
    OK(maelys_datalog_context_create(&context));
    for (unsigned mode = 0; mode < 4; ++mode) {
        maelys_datalog_backend_t b = backend();
        if (mode == 0) b.abi_version = 4;
        if (mode == 1) --b.struct_size;
        if (mode == 2) b.commit = NULL;
        if (mode == 3) b.storage_requirements = NULL;
        CHECK(maelys_datalog_session_config_set_backend(config, &b) == INVALID);
        maelys_datalog_extension_t extension = {
            .abi_version = MAELYS_DATALOG_EXTENSION_ABI_VERSION, .struct_size = sizeof(extension),
            .name = "recording", .semantic_id = "tests.recording.package.v1", .backends = &b, .backend_count = 1};
        CHECK(maelys_datalog_context_register(context, &extension) == INVALID);
        maelys_datalog_session_options_t options = {5, sizeof(options), &b, 0, 0};
        maelys_datalog_session_t *s = (void *)1;
        CHECK(maelys_datalog_session_create_ex(policy, 0, &options, &s) == INVALID && !s);
        size_t n = 91, a = 92;
        CHECK(maelys_datalog_backend_storage_requirements(policy, 0, &b, &n, &a) == INVALID);
        CHECK(n == 91 && a == 92 && queries == 0 && prepares == 0);
    }
    /* A genuinely short old prefix: validation cannot read ABI-5 callbacks. */
    struct { uint32_t abi_version; size_t struct_size; } old = {4, 16};
    CHECK(maelys_datalog_session_config_set_backend(config, (const void *)&old) == INVALID);
    maelys_datalog_session_options_t options = {4, sizeof(options), NULL, 0, 0};
    maelys_datalog_session_t *s = NULL;
    CHECK(maelys_datalog_session_create_ex(policy, 0, &options, &s) == INVALID && !s);
    CHECK(queries == 0 && prepares == 0);
    OK(maelys_datalog_context_free(context)); OK(maelys_datalog_session_config_free(config));
}
static void storage_contract(void) {
    pool_t pool = {0};
    maelys_datalog_backend_t b = backend();
    maelys_datalog_session_config_t *config = NULL;
    maelys_datalog_session_t *s = NULL;
    OK(maelys_datalog_session_config_create(&config));
    OK(maelys_datalog_session_config_set_backend(config, &b));
    size_t n = 0, a = 0;
    OK(maelys_datalog_backend_storage_requirements(policy, 0, &b, &n, &a));
    CHECK(n == sizeof(recording_t) && a == _Alignof(max_align_t) && queries == 1);
    maelys_datalog_session_options_t options = {5, sizeof(options), &b, 0, 0};
    CHECK(maelys_datalog_session_create_ex(policy, 0, &options, &s) == INVALID && !s);
    CHECK(maelys_datalog_session_create_configured(policy, 0, config, &s) == INVALID && !s);
    maelys_datalog_backend_storage_t storage = storage_for(&pool);
    storage.size = required_bytes - 1;
    OK(maelys_datalog_session_config_set_backend_storage(config, &storage));
    CHECK(maelys_datalog_session_create_configured(policy, 0, config, &s) == INVALID && !s);
    storage = storage_for(&pool); storage.bytes = (unsigned char *)&pool + 1;
    CHECK(maelys_datalog_session_config_set_backend_storage(config, &storage) == INVALID);
    /* Alignment metadata may be valid itself but insufficient for this backend. */
    storage.alignment = 1; --storage.size;
    OK(maelys_datalog_session_config_set_backend_storage(config, &storage));
    CHECK(maelys_datalog_session_create_configured(policy, 0, config, &s) == INVALID && !s);
    for (unsigned i = 0; i < 5; ++i) {
        storage = storage_for(&pool);
        if (i == 0) storage.alignment = 0;
        if (i == 1) storage.alignment = 3;
        if (i == 2) --storage.struct_size;
        if (i == 3) storage.bytes = NULL;
        if (i == 4) storage.size = SIZE_MAX;
        CHECK(maelys_datalog_session_config_set_backend_storage(config, &storage) == INVALID);
    }
    CHECK(prepares == 0 && !memcmp(&pool, &(pool_t){0}, sizeof(pool)));
    OK(maelys_datalog_session_config_set_backend_storage(config, NULL));
    required_alignment = 3; n = 91; a = 92;
    CHECK(maelys_datalog_backend_storage_requirements(policy, 0, &b, &n, &a) == MAELYS_DATALOG_STATUS_INVALID_STATE);
    CHECK(n == 91 && a == 92);
    CHECK(maelys_datalog_session_create_configured(policy, 0, config, &s) == MAELYS_DATALOG_STATUS_INVALID_STATE);
    required_alignment = _Alignof(max_align_t);
    requirement_error = MAELYS_DATALOG_STATUS_UNSUPPORTED;
    CHECK(maelys_datalog_session_create_configured(policy, 0, config, &s) == requirement_error);
    CHECK(prepares == 0); requirement_error = MAELYS_DATALOG_STATUS_OK;
    required_bytes = 0;
    OK(maelys_datalog_session_create_configured(policy, 0, config, &s));
    OK(maelys_datalog_session_free(s)); required_bytes = sizeof(recording_t);
    storage = storage_for(&pool); pool.state.fail_prepare = 1;
    OK(maelys_datalog_session_config_set_backend_storage(config, &storage));
    CHECK(maelys_datalog_session_create_configured(policy, 0, config, &s) == MAELYS_DATALOG_STATUS_UNSUPPORTED);
    CHECK(!s && pool.state.prepare == 1 && pool.state.destroy == 1);
    OK(maelys_datalog_session_config_free(config));
    pool = (pool_t){0}; s = create(&pool); OK(maelys_datalog_session_free(s));
    CHECK(!strcmp(pool.state.order, "PD"));
    OK(maelys_datalog_backend_storage_requirements(policy, 0, NULL, &n, &a));
    CHECK(n == 0 && a == 1 && maelys_datalog_backend_reference()->abi_version == 5);
}
static void session_commit(void) {
    pool_t pool = {0}; maelys_datalog_session_t *s = create(&pool);
    maelys_datalog_result_t *r = NULL, *other = NULL;
    OK(maelys_datalog_session_solve(s, NULL, 0, &r, NULL));
    CHECK(pool.state.commits == 1 && !strcmp(pool.state.order, "PSC"));
    size_t n = 0; OK(maelys_datalog_result_derived_fact_count(r, &n)); CHECK(n == 1);
    CHECK(maelys_datalog_session_solve(s, NULL, 0, &other, NULL) == MAELYS_DATALOG_STATUS_INVALID_STATE);
    CHECK(!other && pool.state.solve == 1);
    OK(maelys_datalog_result_free(r));
    for (unsigned mode = BAD_SYMBOL; mode <= CALLBACK_ERROR; ++mode) {
        pool.state.mode = mode;
        maelys_datalog_status_t expected = mode == BAD_SYMBOL ? MAELYS_DATALOG_STATUS_INVALID_FIELD :
            mode == CALLBACK_ERROR ? MAELYS_DATALOG_STATUS_UNSUPPORTED : MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE;
        CHECK(maelys_datalog_session_solve(s, NULL, 0, &r, NULL) == expected && !r);
        CHECK(pool.state.commits == 1 && pool.state.committed_serial == 1 && pool.state.aborts == mode);
    }
    pool.state.mode = NULL_RESULT;
    OK(maelys_datalog_session_solve(s, NULL, 0, &r, NULL));
    CHECK(pool.state.commits == 2); OK(maelys_datalog_result_free(r));
    pool.state.mode = GOOD;
    OK(maelys_datalog_session_solve(s, NULL, 0, &r, NULL));
    size_t bytes, alignment; OK(maelys_datalog_result_explanation_storage_requirements(r, MAELYS_DATALOG_EXPLAIN_TRUE, &bytes, &alignment));
    CHECK(pool.state.commits == 3 && pool.state.explains == 1);
    OK(maelys_datalog_result_free(r)); OK(maelys_datalog_session_free(s));
    CHECK(pool.state.releases == 3 && pool.state.aborts == 4 && pool.state.destroy == 1);
}
static void canonical_inputs(void) {
#define S(s) {.kind = MAELYS_DATALOG_VALUE_SYMBOL, .as.symbol = (s)}
#define I(i) {.kind = MAELYS_DATALOG_VALUE_INTEGER, .as.integer = (i)}
#define B(b) {.kind = MAELYS_DATALOG_VALUE_BOOLEAN, .as.boolean = (b)}
    /* Literal semantic oracle: no pointer/padding comparison or reuse of the
     * production sorter. Checked against the pre-optimization runtime too. */
    const maelys_datalog_fact_t expected[] = {
        {.predicate="event", .arity=2, .terms={S("alpha"), I(INT64_MIN)}},
        {.predicate="event", .arity=2, .terms={S("alpha"), I(INT64_MAX)}},
        {.predicate="event", .arity=2, .terms={S("zeta"), B(1)}},
        {.predicate="event", .arity=2, .terms={I(-3), B(0)}},
        {.predicate="event", .arity=2, .terms={I(3), S("alpha")}},
        {.predicate="event", .arity=2, .terms={B(0), I(5)}},
        {.predicate="event", .arity=2, .terms={B(1), I(-5)}},
        {.predicate="other", .arity=2, .terms={S("alpha"), S("")}},
    };
#undef S
#undef I
#undef B
    pool_t pool = {0};
    maelys_datalog_session_t *s = create(&pool);
    pool.state.inspect_inputs = 1;
    pool.state.expected_inputs = expected;
    pool.state.expected_count = sizeof(expected) / sizeof(expected[0]);
    maelys_datalog_fact_t input[10];
    for (size_t pass = 0; pass < 3; ++pass) {
        for (size_t i = 0; i < 8; ++i) {
            size_t at = pass == 0 ? i : pass == 1 ? 7 - i : (i * 5) % 8;
            input[i] = expected[at];
        }
        input[8] = expected[2]; input[8].terms[1].as.boolean = -7;
        input[9] = expected[6]; input[9].terms[0].as.boolean = 42;
        maelys_datalog_result_t *r = NULL;
        OK(maelys_datalog_session_solve(s, input, 10, &r, NULL));
        CHECK(input[8].terms[1].as.boolean == -7 && input[9].terms[0].as.boolean == 42);
        OK(maelys_datalog_result_free(r));
    }
    unsigned calls = pool.state.solve;
    input[9].predicate = "missing";
    maelys_datalog_result_t *r = NULL;
    CHECK(maelys_datalog_session_solve(s, input, 10, &r, NULL) != MAELYS_DATALOG_STATUS_OK);
    CHECK(!r && pool.state.solve == calls);
    pool.state.expected_count = 0;
    OK(maelys_datalog_session_solve(s, NULL, 0, &r, NULL));
    OK(maelys_datalog_result_free(r));
    pool.state.expected_count = 8;
    OK(maelys_datalog_session_solve(s, expected, 8, &r, NULL));
    OK(maelys_datalog_result_free(r));
    OK(maelys_datalog_session_free(s));
}
static void canonical_capacity(void) {
    size_t limit, per_predicate;
    OK(maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_EDB_FACTS, &limit));
    OK(maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_FACTS_PER_PRED, &per_predicate));
    size_t groups = (limit + per_predicate - 1) / per_predicate;
    CHECK(groups <= 32);
    char names[32][16];
    maelys_datalog_predicate_t predicates[33];
    for (size_t i = 0; i < groups; ++i) {
        snprintf(names[i], sizeof(names[i]), "export%02zu", i);
        predicates[i] = (maelys_datalog_predicate_t)MAELYS_DATALOG_EDB(names[i], 1);
    }
    predicates[groups] = (maelys_datalog_predicate_t)MAELYS_DATALOG_IDB_QUERY("seen", 1);
    const maelys_datalog_domain_t domain = {"export_capacity", predicates, groups + 1, NULL, 0};
    OK(maelys_datalog_domain_register(&domain));
    const char source[] = "seen(V) :- export00(V).";
    maelys_datalog_policy_t *p = NULL;
    OK(maelys_datalog_policy_load_inline(domain.name, "p", source, sizeof(source) - 1, &p, NULL));
    maelys_datalog_fact_t *expected = calloc(limit, sizeof(*expected));
    maelys_datalog_fact_t *input = calloc(limit + 1, sizeof(*input));
    CHECK(expected && input);
    for (size_t i = 0; i < limit; ++i) {
        expected[i] = (maelys_datalog_fact_t){.predicate=names[i / per_predicate], .arity=1};
        expected[i].terms[0] = (maelys_datalog_value_t){.kind=MAELYS_DATALOG_VALUE_INTEGER,
            .as.integer=(int64_t)(i % per_predicate)};
        input[limit - 1 - i] = expected[i];
    }
    pool_t pool = {0};
    maelys_datalog_session_t *s = create_for_policy(&pool, p);
    pool.state.inspect_inputs = 1;
    pool.state.expected_inputs = expected;
    pool.state.expected_count = limit;
    maelys_datalog_result_t *r = NULL;
    OK(maelys_datalog_session_solve(s, input, limit, &r, NULL));
    OK(maelys_datalog_result_free(r));
    input[limit] = input[0];
    CHECK(maelys_datalog_session_solve(s, input, limit + 1, &r, NULL) == MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
    CHECK(!r && pool.state.solve == 1);
    OK(maelys_datalog_session_solve(s, expected, limit, &r, NULL));
    OK(maelys_datalog_result_free(r));
    OK(maelys_datalog_session_free(s));
    OK(maelys_datalog_policy_free(p));
    free(input); free(expected);
}
static maelys_datalog_status_t reference_through_emission(void *state,
    const maelys_datalog_fact_t *facts, size_t count, maelys_datalog_backend_output_t *output,
    void **result, maelys_datalog_diagnostic_t *diag) {
    return maelys_datalog_backend_reference()->solve(state, facts, count, output, result, diag);
}
static void reference_result_view(void) {
    maelys_datalog_backend_t emitted = *maelys_datalog_backend_reference();
    emitted.solve = reference_through_emission;
    const maelys_datalog_session_options_t options = {
        .abi_version=MAELYS_DATALOG_BACKEND_ABI_VERSION, .struct_size=sizeof(options), .backend=&emitted};
    maelys_datalog_session_t *sessions[2] = {NULL, NULL};
    OK(maelys_datalog_session_create(policy, 0, &sessions[0]));
    OK(maelys_datalog_session_create_ex(policy, 0, &options, &sessions[1]));
    const maelys_datalog_value_t values[] = {
        {.kind=MAELYS_DATALOG_VALUE_SYMBOL, .as.symbol="alpha"},
        {.kind=MAELYS_DATALOG_VALUE_SYMBOL, .as.symbol="zeta"},
        {.kind=MAELYS_DATALOG_VALUE_INTEGER, .as.integer=INT64_MIN},
        {.kind=MAELYS_DATALOG_VALUE_INTEGER, .as.integer=INT64_MAX},
        {.kind=MAELYS_DATALOG_VALUE_BOOLEAN, .as.boolean=0},
        {.kind=MAELYS_DATALOG_VALUE_BOOLEAN, .as.boolean=1},
    };
    uint32_t ids[2] = {0};
    for (unsigned pass = 0; pass < 3; ++pass) {
        maelys_datalog_result_t *results[2] = {NULL, NULL};
        for (size_t role = 0; role < 2; ++role) {
            maelys_datalog_fact_t facts[12];
            for (size_t i = 0; i < 12; ++i) {
                size_t index = (role + pass) % 2 ? 11-i : (i*5)%12;
                facts[i] = (maelys_datalog_fact_t){.predicate="event", .arity=2};
                facts[i].terms[0] = (maelys_datalog_value_t){.kind=MAELYS_DATALOG_VALUE_INTEGER, .as.integer=(int64_t)index};
                facts[i].terms[1] = values[index % 6];
                if (index % 6 == 5) facts[i].terms[1].as.boolean = -7;
            }
            OK(maelys_datalog_session_solve(sessions[role], facts, 12, &results[role], NULL));
            memset(facts, 0xa5, sizeof(facts)); /* result owns/borrows engine storage */
            maelys_datalog_result_t *busy = NULL;
            CHECK(maelys_datalog_session_solve(sessions[role], NULL, 0, &busy, NULL) == MAELYS_DATALOG_STATUS_INVALID_STATE);
            CHECK(!busy);
            size_t count = 0;
            maelys_datalog_fact_view_t views[6];
            OK(maelys_datalog_result_enumerate(results[role], "seen", 1, views, 1, &count));
            CHECK(count == 6);
            OK(maelys_datalog_result_enumerate(results[role], "seen", 1, views, 6, &count));
            CHECK(count == 6);
            for (size_t i = 0; i < 6; ++i) {
                CHECK(views[i].arity == 1 && views[i].terms[0].kind == values[i].kind);
                if (i < 2) {
                    const char *text; size_t length;
                    OK(maelys_datalog_result_symbol_text(results[role], views[i].terms[0].as.symbol_id, &text, &length));
                    CHECK(length == strlen(values[i].as.symbol) && !strcmp(text, values[i].as.symbol));
                    if (!role && !pass) ids[i] = views[i].terms[0].as.symbol_id;
                    CHECK(ids[i] == views[i].terms[0].as.symbol_id);
                } else if (i < 4) CHECK(views[i].terms[0].as.integer == values[i].as.integer);
                else CHECK(views[i].terms[0].as.boolean == values[i].as.boolean);
                int present = 0;
                OK(maelys_datalog_result_query(results[role], "seen", &values[i], 1, &present));
                CHECK(present);
            }
        }
        /* Each lease remains independent when the other is released/reused. */
        OK(maelys_datalog_result_free(results[0]));
        OK(maelys_datalog_session_solve(sessions[0], NULL, 0, &results[0], NULL));
        size_t n;
        OK(maelys_datalog_result_derived_fact_count(results[0], &n)); CHECK(n == 0);
        OK(maelys_datalog_result_derived_fact_count(results[1], &n)); CHECK(n == 6);
        OK(maelys_datalog_result_free(results[0]));
        OK(maelys_datalog_result_free(results[1]));
    }
    OK(maelys_datalog_session_free(sessions[0]));
    OK(maelys_datalog_session_free(sessions[1]));
}
static void context_storage(void) {
    maelys_datalog_context_t *context = NULL;
    maelys_datalog_backend_t b = backend();
    maelys_datalog_extension_t extension = {
        .abi_version = MAELYS_DATALOG_EXTENSION_ABI_VERSION, .struct_size = sizeof(extension),
        .name = "recording", .semantic_id = "tests.recording.package.v1", .backends = &b, .backend_count = 1};
    OK(maelys_datalog_context_create(&context)); OK(maelys_datalog_context_register(context, &extension));
    OK(maelys_datalog_context_seal(context, NULL));
    maelys_datalog_policy_t *p = NULL;
    const char source[] = "seen(V) :- event(_,V).";
    OK(maelys_datalog_context_load_inline(context, NULL, "transaction", "p", source, strlen(source), &p, NULL));
    pool_t pool = {0}; maelys_datalog_backend_storage_t storage = storage_for(&pool);
    maelys_datalog_session_config_t *config = NULL; maelys_datalog_session_t *s = NULL;
    OK(maelys_datalog_session_config_create(&config));
    OK(maelys_datalog_session_config_set_backend_storage(config, &storage));
    OK(maelys_datalog_session_config_set_context(config, context, "recording"));
    size_t before = queries;
    OK(maelys_datalog_session_create_configured(p, 0, config, &s));
    CHECK(queries == before + 1 && pool.state.received == &pool);
    OK(maelys_datalog_session_config_free(config)); OK(maelys_datalog_context_free(context));
    OK(maelys_datalog_policy_free(p)); OK(maelys_datalog_session_free(s));
}
static void window_commit(int groups) {
    pool_t pools[2];
    memset(pools, 0, sizeof(pools));
    maelys_datalog_session_t *a = create(&pools[0]), *b = create(&pools[1]);
    size_t bytes, alignment;
    maelys_datalog_group_window_capacities_t caps = {1, 1, 1, 16};
    if (groups) OK(maelys_datalog_group_window_storage_requirements(&caps, &bytes, &alignment));
    else OK(maelys_datalog_window_storage_requirements(1, 16, &bytes, &alignment));
    void *storage = malloc(bytes); CHECK(storage && (uintptr_t)storage % alignment == 0);
    maelys_datalog_window_t *w = NULL; maelys_datalog_group_window_t *g = NULL;
    if (groups) OK(maelys_datalog_group_window_init(storage, bytes, &caps, 0, a, b, &g, NULL));
    else OK(maelys_datalog_window_init(storage, bytes, 1, 16, 0, a, b, &w, NULL));
    CHECK(pools[0].state.commits == 1 && pools[1].state.commits == 0);
    CHECK(pools[1].state.solve == 1 && pools[1].state.aborts == 1); /* init probe is not published */
    maelys_datalog_result_t *old = NULL, *current = NULL;
    if (groups) OK(maelys_datalog_group_window_result(g, &old)); else OK(maelys_datalog_window_result(w, &old));
    size_t ebytes, ealign;
    OK(maelys_datalog_result_explanation_storage_requirements(old, MAELYS_DATALOG_EXPLAIN_TRUE, &ebytes, &ealign));
    void *es = malloc(ebytes); CHECK(es && (uintptr_t)es % ealign == 0);
    maelys_datalog_prepared_explanation_t *explanation = NULL;
    maelys_datalog_fact_t f = output_fact(7);
    OK(maelys_datalog_result_prepare_explanation(old, MAELYS_DATALOG_EXPLAIN_TRUE, "seen", f.terms, 1, es, ebytes, &explanation));
    maelys_datalog_fact_t input = {.predicate = "event", .arity = 2};
    input.terms[0] = input.terms[1] = f.terms[0];
    uint32_t id = UINT32_MAX;
    maelys_datalog_status_t rc = groups ? maelys_datalog_group_window_push(g, &input, 1, &id, NULL) :
        maelys_datalog_window_push(w, "event", f.terms, 1, &id, NULL);
    CHECK(rc == MAELYS_DATALOG_STATUS_INVALID_STATE && id == UINT32_MAX);
    CHECK(pools[1].state.solve == 2 && pools[1].state.aborts == 2 && pools[1].state.commits == 0);
    CHECK(pools[0].state.commits == 1 && pools[0].state.releases == 0);
    if (groups) OK(maelys_datalog_group_window_result(g, &current)); else OK(maelys_datalog_window_result(w, &current));
    CHECK(current == old);
    char text[2]; OK(maelys_datalog_prepared_explanation_write_text(explanation, text, sizeof(text))); CHECK(!strcmp(text, "x"));
    OK(maelys_datalog_prepared_explanation_release(explanation)); free(es);
    if (groups) OK(maelys_datalog_group_window_push(g, &input, 1, &id, NULL));
    else OK(maelys_datalog_window_push(w, "event", f.terms, 1, &id, NULL));
    CHECK(id == 0 && pools[1].state.commits == 1 && pools[0].state.releases == 1);
    if (groups) OK(maelys_datalog_group_window_result(g, &current)); else OK(maelys_datalog_window_result(w, &current));
    CHECK(current != old);
    /* Capacity/text rejections happen before solve; no artificial late path. */
    unsigned solves = pools[0].state.solve;
    id = UINT32_MAX;
    if (groups) {
        maelys_datalog_fact_t two[] = {input, input};
        CHECK(maelys_datalog_group_window_push(g, two, 2, &id, NULL) == MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
    } else {
        maelys_datalog_value_t long_text = {.kind = MAELYS_DATALOG_VALUE_SYMBOL, .as.symbol = "larger_than_the_window_text_capacity"};
        CHECK(maelys_datalog_window_push(w, "event", &long_text, 1, &id, NULL) == MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
    }
    CHECK(id == UINT32_MAX && pools[0].state.solve == solves && pools[0].state.commits == 1);
    if (groups) OK(maelys_datalog_group_window_result(g, &old)); else OK(maelys_datalog_window_result(w, &old));
    CHECK(old == current);
    if (groups) OK(maelys_datalog_group_window_free(g)); else OK(maelys_datalog_window_free(w));
    OK(maelys_datalog_session_free(a)); OK(maelys_datalog_session_free(b)); free(storage);
    CHECK(pools[1].state.releases == 1 && pools[0].state.destroy == 1 && pools[1].state.destroy == 1);
}
int main(int argc, char **argv) {
    static const maelys_datalog_predicate_t predicates[] = {
        MAELYS_DATALOG_EDB("event", 2), MAELYS_DATALOG_EDB("other", 2), MAELYS_DATALOG_IDB_QUERY("seen", 1)};
    const maelys_datalog_domain_t domain = {"transaction", predicates, 3, NULL, 0};
    OK(maelys_datalog_domain_register(&domain));
    const char source[] = "seen(V) :- event(_,V).";
    OK(maelys_datalog_policy_load_inline(domain.name, "p", source, strlen(source), &policy, NULL));
    const char *which = argc == 2 ? argv[1] : "all";
#define RUN(name, expression) do { if (!strcmp(which, "all") || !strcmp(which, name)) { queries = prepares = 0; expression; puts(name " PASS"); } } while (0)
    RUN("abi", abi_rejection()); RUN("storage", storage_contract());
    RUN("session", session_commit()); RUN("context", context_storage());
    RUN("inputs", canonical_inputs());
    RUN("input_capacity", canonical_capacity());
    RUN("result_view", reference_result_view());
    RUN("window", window_commit(0)); RUN("group", window_commit(1));
    OK(maelys_datalog_policy_free(policy));
    return 0;
}
