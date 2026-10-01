/* SPDX-License-Identifier: MPL-2.0 */
/* Unchanged binary object is linked to old and new hosts, static and shared. */
#include <maelys/datalog_backend.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
const maelys_datalog_backend_t *matrix_provider5(void);
void matrix_provider5_counts(unsigned counts[4]);
int main(void) {
    const maelys_datalog_predicate_t predicates[] = {
        MAELYS_DATALOG_EDB("seed", 1), MAELYS_DATALOG_IDB_QUERY("seen", 1)};
    const maelys_datalog_domain_t domain = {"matrix", predicates, 2, NULL, 0};
    assert(maelys_datalog_domain_register(&domain) == 0);
    const char *source = "seen(X) :- seed(X).";
    maelys_datalog_policy_t *p;
    assert(maelys_datalog_policy_load_inline("matrix", "p", source,
        strlen(source), &p, NULL) == 0);
    maelys_datalog_session_config_t *c;
    assert(maelys_datalog_session_config_create(&c) == 0);
    assert(maelys_datalog_session_config_set_backend(c, matrix_provider5()) == 0);
    unsigned state;
    const maelys_datalog_backend_storage_t storage = {
        sizeof(storage), &state, sizeof(state), _Alignof(unsigned)};
    assert(maelys_datalog_session_config_set_backend_storage(c, &storage) == 0);
    maelys_datalog_session_t *s;
    assert(maelys_datalog_session_create_configured(p, 0, c, &s) == 0);
    char fingerprint[MAELYS_DATALOG_PUBLIC_FINGERPRINT_BYTES];
    assert(maelys_datalog_session_execution_fingerprint(s, fingerprint) == 0);
    puts(fingerprint);
    const maelys_datalog_program_t *program; maelys_datalog_program_info_t info;
    assert(maelys_datalog_session_program(s, &program) == 0);
    assert(maelys_datalog_program_info(program, &info) == 0);
    printf("program %zu %zu %zu\n", info.max_input_facts,
        info.max_derived_facts, info.max_facts_per_predicate);
    maelys_datalog_fact_t f = {.predicate = "seed", .arity = 1};
    f.terms[0].kind = MAELYS_DATALOG_VALUE_INTEGER; f.terms[0].as.integer = 42;
    for (unsigned i = 0; i < 2; ++i) {
        maelys_datalog_result_t *r, *other = NULL;
        assert(maelys_datalog_session_solve(s, &f, 1, &r, NULL) == 0);
        assert(maelys_datalog_session_solve(s, &f, 1, &other, NULL) ==
            MAELYS_DATALOG_STATUS_INVALID_STATE);
        assert(!other);
        size_t count; int found;
        assert(maelys_datalog_result_derived_fact_count(r, &count) == 0 && count == 1);
        assert(maelys_datalog_result_query(r, "seen", f.terms, 1, &found) == 0 && found);
        assert(maelys_datalog_result_free(r) == 0);
    }
    assert(maelys_datalog_session_free(s) == 0);
    unsigned counts[4]; matrix_provider5_counts(counts);
    assert(counts[0] == 1 && counts[1] == 2 && counts[2] == 2 && counts[3] == 1);
    assert(maelys_datalog_session_config_free(c) == 0);
    assert(maelys_datalog_session_create(p, 0, &s) == 0);
    assert(maelys_datalog_session_execution_fingerprint(s, fingerprint) == 0);
    puts(fingerprint); /* Default reference identity must also stay unchanged. */
    maelys_datalog_result_t *reference_result;
    assert(maelys_datalog_session_solve(s, &f, 1, &reference_result, NULL) == 0);
    int found;
    assert(maelys_datalog_result_query(reference_result, "seen", f.terms, 1, &found) == 0 && found);
    assert(maelys_datalog_result_free(reference_result) == 0);
    assert(maelys_datalog_session_free(s) == 0);
    assert(maelys_datalog_policy_free(p) == 0);
    puts("ABI5 default, program bounds, query, result lease and callbacks PASS");
    return 0;
}
