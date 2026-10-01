/* SPDX-License-Identifier: MPL-2.0 */
/* Compiled independently against the old OR new installed public SDK. */
#include <maelys/datalog_backend.h>
#include <assert.h>
#include <stddef.h>
#include <string.h>

static unsigned prepared, committed, released, destroyed;
static maelys_datalog_program_info_t bounds;
static maelys_datalog_status_t requirements(const maelys_datalog_program_t *p,
                                         size_t *bytes, size_t *alignment) {
    assert(maelys_datalog_program_info(p, &bounds) == 0);
    *bytes = sizeof(unsigned); *alignment = _Alignof(unsigned); return 0;
}
static maelys_datalog_status_t prepare(const maelys_datalog_program_t *p,
    const maelys_datalog_backend_storage_t *storage, void **out) {
    maelys_datalog_program_info_t info;
    assert(maelys_datalog_program_info(p, &info) == 0);
    assert(info.max_input_facts == bounds.max_input_facts);
    assert(info.max_derived_facts == bounds.max_derived_facts);
    assert(info.max_facts_per_predicate == bounds.max_facts_per_predicate);
    assert(storage->size == sizeof(unsigned));
    *out = storage->bytes; *(unsigned *)*out = 0; ++prepared; return 0;
}
static maelys_datalog_status_t solve(void *s, const maelys_datalog_fact_t *facts,
    size_t n, maelys_datalog_backend_output_t *out, void **result,
    maelys_datalog_diagnostic_t *diag) {
    (void)diag; *result = s;
    for (size_t i = 0; i < n; ++i) {
        assert(!strcmp(facts[i].predicate, "seed"));
        maelys_datalog_fact_t f = facts[i]; f.predicate = "seen";
        maelys_datalog_status_t rc = maelys_datalog_backend_emit(out, &f);
        if (rc) return rc;
    }
    return 0;
}
static void commit(void *s, void *r) { assert(s == r); ++committed; }
static void release(void *s, void *r) { assert(s == r); ++released; }
static void destroy(void *s) { (void)s; ++destroyed; }
static const maelys_datalog_backend_t backend = {
    MAELYS_DATALOG_BACKEND_ABI_VERSION, sizeof(backend), "sdk_matrix5",
    "test.sdk-matrix5.v1", MAELYS_DATALOG_CAP_POSITIVE,
    requirements, prepare, solve, NULL, NULL, NULL, commit, release, destroy
};
const maelys_datalog_backend_t *matrix_provider5(void) { return &backend; }
void matrix_provider5_counts(unsigned counts[4]) {
    counts[0] = prepared; counts[1] = committed;
    counts[2] = released; counts[3] = destroyed;
}
