/* SPDX-License-Identifier: MPL-2.0 */
/* A fixed, complete one-rule projection, compiled against installed headers.
 * No private engine headers, allocation, delta callback, or retained input. */
#include <maelys/datalog_backend.h>
#include "host_delta_backend.h"
#define NI __attribute__((noinline))
static int delta_backend_equal(const char *, const char *);
static NI maelys_datalog_status_t delta_backend_requirements(const maelys_datalog_program_t *p, size_t *n, size_t *a) {
    (void)p; *n = sizeof(delta_backend_state); *a = _Alignof(delta_backend_state); return 0;
}
static NI maelys_datalog_status_t delta_backend_prepare(const maelys_datalog_program_t *p,
    const maelys_datalog_backend_storage_t *storage, void **out) {
    maelys_datalog_program_info_t info;
    maelys_datalog_ir_rule_t rule;
    if (maelys_datalog_program_info(p, &info) || info.rule_count != 1 ||
        info.required_capabilities & ~MAELYS_DATALOG_CAP_POSITIVE ||
        maelys_datalog_program_rule(p, 0, &rule) || rule.body_count != 1 || rule.head.arity != 3 ||
        rule.body[0].kind != MAELYS_DATALOG_IR_ATOM || rule.body[0].atom.arity != 3)
        return MAELYS_DATALOG_STATUS_UNSUPPORTED;
    for (size_t i = 0; i < 3; ++i)
        if (rule.head.terms[i].kind != MAELYS_DATALOG_IR_VARIABLE ||
            rule.body[0].atom.terms[i].kind != MAELYS_DATALOG_IR_VARIABLE ||
            rule.head.terms[i].as.variable != rule.body[0].atom.terms[i].as.variable)
            return MAELYS_DATALOG_STATUS_UNSUPPORTED;
    for (size_t i = 0; i < 3; ++i)
        for (size_t j = 0; j < i; ++j)
            if (rule.head.terms[i].as.variable == rule.head.terms[j].as.variable)
                return MAELYS_DATALOG_STATUS_UNSUPPORTED;
    for (size_t i = 0; i < info.fact_count; ++i) {
        maelys_datalog_ir_atom_t f;
        if (maelys_datalog_program_fact(p, i, &f) || !delta_backend_equal(f.predicate, "vocab") ||
            f.arity != 1 || f.terms[0].kind != MAELYS_DATALOG_IR_SYMBOL ||
            delta_backend_equal(rule.body[0].atom.predicate, "vocab") || delta_backend_equal(rule.head.predicate, "vocab"))
            return MAELYS_DATALOG_STATUS_UNSUPPORTED;
    }
    delta_backend_state *s = storage->bytes;
    s->input = rule.body[0].atom.predicate; s->output = rule.head.predicate;
    *out = s; return 0;
}
static NI uint64_t delta_backend_text(uint64_t h, const char *s) {
    do { h = (h ^ (unsigned char)*s) * UINT64_C(1099511628211); } while (*s++);
    return h;
}
static NI int delta_backend_equal(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; } return *a == *b;
}
static NI maelys_datalog_status_t delta_backend_solve(void *state, const maelys_datalog_fact_t *inputs,
    size_t count, maelys_datalog_backend_output_t *out, void **result, maelys_datalog_diagnostic_t *diag) {
    (void)diag;
    delta_backend_state *s = state;
    if (count > 512) return MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE;
    s->recorded_count = count;
    s->live = 1; s->accepted = 0; *result = s;
    uint64_t h = UINT64_C(14695981039346656037) ^ count;
    for (size_t i = 0; i < count; ++i) {
        const maelys_datalog_fact_t *f = &inputs[i];
        s->recorded[i] = *f;
        h = delta_backend_text(h, f->predicate); h = (h ^ f->arity) * UINT64_C(1099511628211);
        for (size_t j = 0; j < f->arity; ++j) {
            h = (h ^ f->terms[j].kind) * UINT64_C(1099511628211);
            if (f->terms[j].kind == MAELYS_DATALOG_VALUE_SYMBOL) h = delta_backend_text(h, f->terms[j].as.symbol);
            else h = (h ^ (f->terms[j].kind == MAELYS_DATALOG_VALUE_INTEGER ?
                (uint64_t)f->terms[j].as.integer : (uint64_t)f->terms[j].as.boolean)) * UINT64_C(1099511628211);
        }
        if (delta_backend_equal(f->predicate, s->input)) {
            maelys_datalog_fact_t derived = *f; derived.predicate = s->output;
            maelys_datalog_status_t rc = maelys_datalog_backend_emit(out, &derived);
            if (rc) return rc;
        }
    }
    s->pending_hash = h;
    if (s->fail == 1) return MAELYS_DATALOG_STATUS_UNSUPPORTED;
    if (s->fail == 2) (void)maelys_datalog_backend_charge(out, UINT64_MAX);
    return 0; /* Sticky host errors must still reject. */
}
static NI void delta_backend_commit(void *state, void *result) {
    (void)result; delta_backend_state *s = state;
    s->committed_hash = s->pending_hash; ++s->commits; s->accepted = 1;
}
static NI void delta_backend_release(void *state, void *result) {
    (void)result; delta_backend_state *s = state;
    if (s->accepted) ++s->releases; else ++s->aborts;
    s->live = 0; s->pending_hash = 0; s->accepted = 0;
}
static void delta_backend_destroy(void *state) { (void)state; }
const maelys_datalog_backend_t *delta_snapshot_backend(void) {
    static const maelys_datalog_backend_t b = {
        .abi_version = MAELYS_DATALOG_BACKEND_ABI_VERSION, .struct_size = sizeof(b),
        .name = "delta_probe", .semantic_id = "bench.snapshot.projection.v1",
        .capabilities = MAELYS_DATALOG_CAP_POSITIVE | MAELYS_DATALOG_CAP_WORK_LIMIT,
        .storage_requirements = delta_backend_requirements, .prepare = delta_backend_prepare,
        .solve = delta_backend_solve, .commit = delta_backend_commit,
        .destroy_result = delta_backend_release, .destroy = delta_backend_destroy
    }; return &b;
}
