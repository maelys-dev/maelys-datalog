/* SPDX-License-Identifier: MPL-2.0 */
/* Public-only consumer, also compiled outside the tree in C11 and C++17.
 * Make/CMake instrument every engine translation unit with allocator hooks. */
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef memset
#include <maelys/datalog_program.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)
#define OK(x) CHECK((x) == MAELYS_DATALOG_STATUS_OK)

static int forbid_allocator;
static size_t forbidden_calls;
static int refuse_allocation(void) {
    if (!forbid_allocator) return 0;
    ++forbidden_calls;
    return 1;
}
void *maelys_test_malloc(size_t n) { return refuse_allocation() ? NULL : malloc(n); }
void *maelys_test_calloc(size_t n, size_t s) { return refuse_allocation() ? NULL : calloc(n, s); }
void *maelys_test_realloc(void *p, size_t n) { return refuse_allocation() ? NULL : realloc(p, n); }
void maelys_test_free(void *p) {
    if (forbid_allocator) ++forbidden_calls;
    free(p);
}
void *maelys_test_memset(void *p, int c, size_t n) { return memset(p, c, n); }

/* Registry order deliberately differs from name/arity and manifest order. */
static const maelys_datalog_predicate_t predicates[] = {
    {"seed", 1, MAELYS_DATALOG_PREDICATE_EDB},
    {"pair", 2, MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY},
    {"helper", 1, MAELYS_DATALOG_PREDICATE_IDB},
    {"visible", 1, MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY},
    {"empty", 1, MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY},
    {"ask", 1, MAELYS_DATALOG_PREDICATE_EDB | MAELYS_DATALOG_PREDICATE_QUERY},
    {"fixed", 1, MAELYS_DATALOG_PREDICATE_POLICY_FACT | MAELYS_DATALOG_PREDICATE_QUERY}
};
static const maelys_datalog_domain_t domain = {"program_queries", predicates, 7, NULL, 0};
static const char source[] =
    "pair(X, X) :- seed(X). helper(X) :- seed(X). visible(X) :- seed(X). fixed(7).";

static int load(const char *queries_field, maelys_datalog_policy_t **out) {
    if (!queries_field) {
        OK(maelys_datalog_policy_load_inline(domain.name, "policy", source, sizeof(source) - 1, out, NULL));
    } else {
        char manifest[2048];
        int n = snprintf(manifest, sizeof(manifest),
            "{\"policy_set_id\":\"program_queries\",\"policy_set_version\":\"1\","
            "\"manifest_version\":\"1\",\"default_profile\":\"enforce\","
            "\"strict_loading\":true,\"fail_closed\":true,\"created_for\":\"test\","
            "\"capabilities\":[],\"policies\":[{\"policy_id\":\"policy\","
            "\"domain\":\"program_queries\",\"file\":\"policy.dl\","
            "\"sha256\":\"bb28de5810582a65b612c8f779c5fac02208e35d896b54eea2e92659bdbf56f1\","
            "\"mode\":\"enforce\",\"enabled\":true,\"description\":\"queries\"%s}]}",
            queries_field);
        CHECK(n > 0 && (size_t)n < sizeof(manifest));
        const maelys_datalog_policy_bundle_entry_t bundle = {"policy", source, sizeof(source) - 1};
        OK(maelys_datalog_policy_load_manifest_buffer(manifest, (size_t)n, &bundle, 1, 0, out, NULL));
    }
    return 0;
}

static int inspect(const maelys_datalog_program_t *program, const unsigned admitted[7]) {
    size_t expected = 0, count = SIZE_MAX;
    for (size_t i = 0; i < 7; ++i) expected += admitted[i];
    forbidden_calls = 0;
    forbid_allocator = 1;
    OK(maelys_datalog_program_query_count(program, &count));
    CHECK(count == expected);
    size_t q = 0;
    for (size_t i = 0; i < 7; ++i) {
        maelys_datalog_predicate_t raw, query;
        OK(maelys_datalog_program_predicate(program, i, &raw));
        CHECK(strcmp(raw.name, predicates[i].name) == 0 && raw.arity == predicates[i].arity);
        CHECK(raw.flags == predicates[i].flags);
        if (!admitted[i]) continue;
        OK(maelys_datalog_program_query(program, q++, &query));
        CHECK(query.name == raw.name && query.arity == raw.arity && query.flags == raw.flags);
    }
    maelys_datalog_predicate_t untouched, out;
    memset(&untouched, 0xa5, sizeof(untouched));
    memcpy(&out, &untouched, sizeof(out));
    CHECK(maelys_datalog_program_query(program, count, &out) == MAELYS_DATALOG_STATUS_NOT_FOUND);
    CHECK(memcmp(&out, &untouched, sizeof(out)) == 0);
    CHECK(maelys_datalog_program_query(program, SIZE_MAX, &out) == MAELYS_DATALOG_STATUS_NOT_FOUND);
    CHECK(memcmp(&out, &untouched, sizeof(out)) == 0);
    CHECK(maelys_datalog_program_query(NULL, 0, &out) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    CHECK(memcmp(&out, &untouched, sizeof(out)) == 0);
    CHECK(maelys_datalog_program_query(program, 0, NULL) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    count = 73;
    CHECK(maelys_datalog_program_query_count(NULL, &count) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    CHECK(count == 73);
    CHECK(maelys_datalog_program_query_count(program, NULL) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    forbid_allocator = 0;
    CHECK(forbidden_calls == 0);
    return 0;
}

static int scenario(const char *queries_field, const unsigned admitted[7]) {
    maelys_datalog_policy_t *policy = NULL;
    maelys_datalog_session_t *session = NULL;
    const maelys_datalog_program_t *program = NULL;
    CHECK(load(queries_field, &policy) == 0);
    OK(maelys_datalog_session_create(policy, 0, &session));
    OK(maelys_datalog_session_program(session, &program));
    char before[MAELYS_DATALOG_PUBLIC_FINGERPRINT_BYTES], after[MAELYS_DATALOG_PUBLIC_FINGERPRINT_BYTES];
    OK(maelys_datalog_program_fingerprint(program, before));
    CHECK(inspect(program, admitted) == 0);
    OK(maelys_datalog_policy_free(policy)); /* Program and names remain session-owned. */
    CHECK(inspect(program, admitted) == 0);
    maelys_datalog_fact_t fact;
    memset(&fact, 0, sizeof(fact));
    fact.predicate = "seed";
    fact.arity = 1;
    fact.terms[0].kind = MAELYS_DATALOG_VALUE_INTEGER;
    fact.terms[0].as.integer = 7;
    maelys_datalog_value_t terms[2] = {fact.terms[0], fact.terms[0]};
    /* Check authorization against the actual result API, including an allowed
     * predicate with no facts. Inspection also works before any solve. */
    for (size_t transaction = 0; transaction < 2; ++transaction) {
        maelys_datalog_result_t *result = NULL;
        OK(maelys_datalog_session_solve(session, &fact, transaction == 0 ? 1 : 0, &result, NULL));
        CHECK(inspect(program, admitted) == 0);
        for (size_t i = 0; i < 7; ++i) {
            int present = -1;
            maelys_datalog_status_t rc = maelys_datalog_result_query(
                result, predicates[i].name, terms, predicates[i].arity, &present);
            if (admitted[i]) {
                CHECK(rc == MAELYS_DATALOG_STATUS_OK);
                CHECK(present == (i == 6 || (transaction == 0 && (i == 1 || i == 3))));
            } else {
                CHECK(rc == (queries_field ? MAELYS_DATALOG_STATUS_FORBIDDEN : MAELYS_DATALOG_STATUS_INVALID_FIELD));
            }
        }
        OK(maelys_datalog_result_free(result));
    }
    OK(maelys_datalog_program_fingerprint(program, after));
    CHECK(strcmp(before, after) == 0);
    OK(maelys_datalog_session_free(session));
    return 0;
}

int main(void) {
    OK(maelys_datalog_domain_register(&domain));
    const unsigned all[7] = {0, 1, 0, 1, 1, 1, 1};
    const unsigned one[7] = {0, 0, 0, 1, 0, 0, 0};
    const unsigned selected[7] = {0, 1, 0, 0, 1, 0, 0};
    const unsigned none[7] = {0, 0, 0, 0, 0, 0, 0};
    CHECK(scenario(NULL, all) == 0);
    CHECK(scenario(",\"queries\":[{\"name\":\"visible\",\"arity\":1}]", one) == 0);
    CHECK(scenario(",\"queries\":[{\"name\":\"empty\",\"arity\":1},{\"name\":\"pair\",\"arity\":2}]", selected) == 0);
    CHECK(scenario(",\"queries\":[]", none) == 0);
    CHECK(scenario("", none) == 0);
    puts("program queries: effective authorization, arities, order, empty/absent whitelist, errors, lifetimes and no allocation PASS");
    return 0;
}
