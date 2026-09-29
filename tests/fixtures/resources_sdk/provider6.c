/* SPDX-License-Identifier: MPL-2.0 */
#include <maelys/datalog_resources.h>
#include <assert.h>
#include <string.h>

static maelys_datalog_session_resources_t seen;
static unsigned prepared;
static maelys_datalog_status_t requirements(const maelys_datalog_program_t *p,
    const maelys_datalog_session_resources_t *r, size_t *n, size_t *a) {
    maelys_datalog_program_info_t info;
    assert(maelys_datalog_program_info(p, &info) == 0);
    /* Program maxima are not the 4/8 per-session quotas. */
    assert(info.max_input_facts > r->input_facts && info.max_derived_facts > r->derived_facts);
    seen = *r; *n = sizeof(seen); *a = _Alignof(maelys_datalog_session_resources_t);
    return 0;
}
static maelys_datalog_status_t prepare(const maelys_datalog_program_t *p,
    const maelys_datalog_session_resources_t *r,
    const maelys_datalog_backend_storage_t *storage, void **out) {
    (void)p; assert(!memcmp(r, &seen, sizeof(seen)));
    assert(storage->size == sizeof(seen));
    memcpy(storage->bytes, r, sizeof(seen)); *out = storage->bytes; ++prepared; return 0;
}
static maelys_datalog_status_t solve(void *s, const maelys_datalog_fact_t *f,
    size_t n, maelys_datalog_backend_output_t *out, void **r, maelys_datalog_diagnostic_t *d) {
    (void)f; (void)n; (void)out; (void)d; *r = s; return 0;
}
static void release(void *s, void *r) { assert(s == r); }
static void destroy(void *s) { (void)s; }
static const struct {
    maelys_datalog_backend_v6_t v;
#ifdef OPTIONAL_TAIL
    size_t future_optional[3];
#endif
} descriptor = {{6, sizeof(descriptor), "sdk_matrix6", "test.sdk-matrix6.v1",
    MAELYS_DATALOG_CAP_POSITIVE, MAELYS_DATALOG_RESOURCE_SESSION_CAPACITIES,
    requirements, prepare, solve, NULL, NULL, NULL, release, release, destroy}
#ifdef OPTIONAL_TAIL
    , {17, 29, 43}
#endif
};
const maelys_datalog_backend_v6_t *matrix_provider6(void) { return &descriptor.v; }
unsigned matrix_provider6_prepared(void) { return prepared; }

/* Synthetic transport only: no engine call or allocation. A future-sized
 * resource vector must cross separately compiled public boundaries losslessly.
 * This does NOT claim the current engine admits an XLARGE session. */
maelys_datalog_status_t matrix_resource_echo(const maelys_datalog_program_info_t *bounds,
                                          const maelys_datalog_session_resources_t *ceilings,
                                          const maelys_datalog_session_resources_t *in,
                                          maelys_datalog_session_resources_t *out) {
    if (in->struct_size < MAELYS_DATALOG_RESOURCES_V1_SIZE ||
        out->struct_size < MAELYS_DATALOG_RESOURCES_V1_SIZE)
        return MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL;
    if (in->contract_version != 1 || in->memory_mode != MAELYS_DATALOG_MEMORY_FIXED ||
        (in->required_features & ~MAELYS_DATALOG_RESOURCE_SUPPORTED_014))
        return MAELYS_DATALOG_STATUS_UNSUPPORTED;
    assert(bounds->max_input_facts == ceilings->input_facts);
    assert(bounds->max_derived_facts == ceilings->derived_facts);
    assert(bounds->max_facts_per_predicate > 256);
    if (in->input_facts > ceilings->input_facts || in->derived_facts > ceilings->derived_facts ||
        in->symbols > ceilings->symbols || in->text_bytes > ceilings->text_bytes)
        return MAELYS_DATALOG_STATUS_UNSUPPORTED;
    size_t capacity = out->struct_size;
    *out = *in; out->struct_size = capacity; return 0;
}
