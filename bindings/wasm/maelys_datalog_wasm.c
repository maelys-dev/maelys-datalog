/* SPDX-License-Identifier: MPL-2.0 */
#include "maelys_datalog_wasm.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(MAELYS_DATALOG_PUBLIC_API_VERSION == 2u, "Review consumer API changes");
_Static_assert(MAELYS_DATALOG_DIAGNOSTIC_ABI_VERSION == 1u, "Review diagnostic changes");
_Static_assert(MAELYS_DATALOG_PUBLIC_MAX_TERMS == 4u, "Review transport arity");
#ifdef __EMSCRIPTEN__
_Static_assert(SIZE_MAX == UINT32_MAX, "Transport requires wasm32");
#endif

static struct {
    int opened, closed;
    maelys_datalog_policy_t *policy;
    maelys_datalog_session_t *session;
    maelys_datalog_input_edb_t *input;
    maelys_datalog_result_t *result;
    void *scratch;
    size_t max_facts, max_predicates, max_per_predicate;
} state;
static maelys_datalog_diagnostic_t diagnostic = {
    .struct_size = sizeof(maelys_datalog_diagnostic_t),
    .abi_version = MAELYS_DATALOG_DIAGNOSTIC_ABI_VERSION
};
static int last_status;
#define OK MAELYS_DATALOG_STATUS_OK
#define INVALID MAELYS_DATALOG_STATUS_INVALID_ARGUMENT
#define CLOSED MAELYS_DATALOG_STATUS_INVALID_STATE
#define TOO_LARGE MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE

static int finish(int rc) { last_status = rc; return rc; }
static int begin(void) {
    (void)maelys_datalog_diagnostic_clear(&diagnostic);
    last_status = OK;
    return state.opened && !state.closed ? OK : finish(CLOSED);
}
static int checked_u32(size_t n, uint32_t *out) {
    if (!out) return INVALID;
    if (n > UINT32_MAX) return TOO_LARGE;
    *out = (uint32_t)n;
    return OK;
}
uint32_t maelys_datalog_wasm_transport_version(void) { return MAELYS_WASM_TRANSPORT_VERSION; }
int maelys_datalog_wasm_open(void) {
    (void)maelys_datalog_diagnostic_clear(&diagnostic);
    if (state.opened || state.closed) return finish(CLOSED);
    size_t scratch_bytes, predicates_bytes;
    int rc = maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_EDB_FACTS, &state.max_facts);
    if (!rc) rc = maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_PREDICATES, &state.max_predicates);
    if (!rc) rc = maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_FACTS_PER_PRED, &state.max_per_predicate);
    if (rc) return finish(rc);
    if (state.max_facts > SIZE_MAX / sizeof(maelys_datalog_fact_t) ||
        state.max_predicates > (SIZE_MAX - MAELYS_DATALOG_PUBLIC_MAX_POLICY_ATOMS * sizeof(char *)) / sizeof(maelys_datalog_predicate_t) ||
        state.max_per_predicate > SIZE_MAX / sizeof(maelys_datalog_fact_view_t)) return finish(TOO_LARGE);
    scratch_bytes = state.max_facts * sizeof(maelys_datalog_fact_t);
    predicates_bytes = state.max_predicates * sizeof(maelys_datalog_predicate_t) +
        MAELYS_DATALOG_PUBLIC_MAX_POLICY_ATOMS * sizeof(char *);
    if (scratch_bytes < predicates_bytes) scratch_bytes = predicates_bytes;
    if (scratch_bytes < state.max_per_predicate * sizeof(maelys_datalog_fact_view_t))
        scratch_bytes = state.max_per_predicate * sizeof(maelys_datalog_fact_view_t);
    /* One bounded reusable conversion allocation; no per-fact allocation. */
    state.scratch = malloc(scratch_bytes);
    if (!state.scratch) return finish(MAELYS_DATALOG_STATUS_INTERNAL);
    rc = maelys_datalog_input_edb_create(&state.input);
    if (rc) { free(state.scratch); state.scratch = NULL; return finish(rc); }
    state.opened = 1;
    return finish(OK);
}
static int release_result(void) {
    if (!state.result) return OK;
    int rc = maelys_datalog_result_free(state.result);
    if (!rc) state.result = NULL;
    return rc;
}
int maelys_datalog_wasm_close(void) {
    if (begin()) return last_status;
    int rc = release_result();
    if (rc) return finish(rc);
    if (state.session && (rc = maelys_datalog_session_free(state.session))) return finish(rc);
    state.session = NULL;
    if (state.policy && (rc = maelys_datalog_policy_free(state.policy))) return finish(rc);
    state.policy = NULL;
    rc = maelys_datalog_input_edb_free(state.input);
    if (rc) return finish(rc);
    state.input = NULL;
    free(state.scratch); state.scratch = NULL;
    state.closed = 1;
    return finish(OK);
}
int maelys_datalog_wasm_limit(uint32_t id, uint32_t *out) {
    if (begin()) return last_status;
    size_t value;
    int rc = maelys_datalog_limit_get((maelys_datalog_limit_t)id, &value);
    return finish(rc ? rc : checked_u32(value, out));
}
static const char *span(const char *text, uint32_t bytes, uint32_t offset, uint32_t length) {
    if (!text || offset >= bytes || length >= bytes - offset || text[offset + length] != '\0' ||
        memchr(text + offset, '\0', length)) return NULL;
    return text + offset;
}
int maelys_datalog_wasm_register_domain(const char *name, const uint32_t *words,
    uint32_t word_count, uint32_t predicates, uint32_t atoms, const char *text, uint32_t text_bytes) {
    if (begin()) return last_status;
    if (!name || !words || predicates == 0 || predicates > state.max_predicates ||
        atoms > MAELYS_DATALOG_PUBLIC_MAX_POLICY_ATOMS ||
        predicates > (UINT32_MAX - atoms * 2u) / 4u || word_count != predicates * 4u + atoms * 2u)
        return finish(INVALID);
    maelys_datalog_predicate_t *defs = state.scratch;
    const char **strings = (const char **)(defs + state.max_predicates);
    for (uint32_t i = 0; i < predicates; ++i) {
        const uint32_t *w = words + i * 4u;
        const char *p = span(text, text_bytes, w[0], w[1]);
        if (!p) return finish(INVALID);
        defs[i] = (maelys_datalog_predicate_t){p, w[2], w[3]};
    }
    for (uint32_t i = 0; i < atoms; ++i) {
        const uint32_t *w = words + predicates * 4u + i * 2u;
        strings[i] = span(text, text_bytes, w[0], w[1]);
        if (!strings[i]) return finish(INVALID);
    }
    const maelys_datalog_domain_t domain = { name, defs, predicates, strings, atoms };
    return finish(maelys_datalog_domain_register(&domain));
}
int maelys_datalog_wasm_load_policy(const char *domain, const char *id, const char *source, uint32_t bytes) {
    if (begin()) return last_status;
    if (state.result) return finish(CLOSED);
    maelys_datalog_policy_t *policy = NULL;
    maelys_datalog_session_t *session = NULL;
    int rc = maelys_datalog_policy_load_inline(domain, id, source, bytes, &policy, &diagnostic);
    if (!rc) rc = maelys_datalog_session_create(policy, 0u, &session);
    if (rc) { if (policy) (void)maelys_datalog_policy_free(policy); return finish(rc); }
    if (state.session) (void)maelys_datalog_session_free(state.session);
    if (state.policy) (void)maelys_datalog_policy_free(state.policy);
    state.policy = policy; state.session = session;
    return finish(maelys_datalog_input_edb_clear(state.input));
}
int maelys_datalog_wasm_clear_facts(void) {
    if (begin()) return last_status;
    if (state.result) return finish(CLOSED);
    return finish(maelys_datalog_input_edb_clear(state.input));
}
static int decode(const uint32_t *words, uint32_t word_count, uint32_t count,
                  const char *text, uint32_t text_bytes) {
    if (count > state.max_facts) return TOO_LARGE;
    if (count > UINT32_MAX / MAELYS_WASM_FACT_WORDS || word_count != count * MAELYS_WASM_FACT_WORDS ||
        (!words && count)) return INVALID;
    maelys_datalog_fact_t *facts = state.scratch;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t *w = words + i * MAELYS_WASM_FACT_WORDS;
        maelys_datalog_fact_t *f = &facts[i];
        memset(f, 0, sizeof(*f));
        f->predicate = span(text, text_bytes, w[0], w[1]); f->arity = w[2];
        if (!f->predicate || f->arity > MAELYS_DATALOG_PUBLIC_MAX_TERMS) return INVALID;
        for (size_t j = 0; j < f->arity; ++j) {
            const uint32_t *t = w + 3u + j * 3u;
            f->terms[j].kind = (maelys_datalog_value_kind_t)t[0];
            switch (t[0]) {
            case MAELYS_DATALOG_VALUE_SYMBOL:
                f->terms[j].as.symbol = span(text, text_bytes, t[1], t[2]);
                if (!f->terms[j].as.symbol) return INVALID;
                break;
            case MAELYS_DATALOG_VALUE_INTEGER: {
                uint64_t bits = (uint64_t)t[1] | ((uint64_t)t[2] << 32u);
                memcpy(&f->terms[j].as.integer, &bits, sizeof(bits));
                break;
            }
            case MAELYS_DATALOG_VALUE_BOOLEAN:
                if (t[1] > 1u || t[2] != 0u) return INVALID;
                f->terms[j].as.boolean = (int)t[1]; break;
            default: return INVALID;
            }
        }
    }
    return OK;
}
int maelys_datalog_wasm_add_facts(const uint32_t *w, uint32_t n, uint32_t count, const char *text, uint32_t bytes) {
    if (begin()) return last_status;
    if (state.result) return finish(CLOSED);
    int rc = decode(w, n, count, text, bytes);
    return finish(rc ? rc : maelys_datalog_input_edb_add_facts(state.input, state.scratch, count, &diagnostic));
}
int maelys_datalog_wasm_input_usage(uint32_t *out) {
    if (begin()) return last_status;
    if (!out) return finish(INVALID);
    size_t count, used, capacity;
    int rc = maelys_datalog_input_edb_count(state.input, &count);
    if (!rc) rc = maelys_datalog_input_edb_text_usage(state.input, &used, &capacity);
    if (!rc && (count > UINT32_MAX || used > UINT32_MAX || capacity > UINT32_MAX)) rc = TOO_LARGE;
    if (!rc) { out[0] = (uint32_t)count; out[1] = (uint32_t)used; out[2] = (uint32_t)capacity; }
    return finish(rc);
}
int maelys_datalog_wasm_solve(void) {
    if (begin()) return last_status;
    if (!state.session || state.result) return finish(CLOSED);
    return finish(maelys_datalog_session_solve_edb(state.session, state.input, &state.result, &diagnostic));
}
int maelys_datalog_wasm_free_result(void) {
    if (begin()) return last_status;
    return finish(release_result());
}
int maelys_datalog_wasm_query(const uint32_t *w, uint32_t n, const char *text, uint32_t bytes, uint32_t *out) {
    if (begin()) return last_status;
    if (!state.result) return finish(CLOSED);
    if (!out) return finish(INVALID);
    int rc = decode(w, n, 1u, text, bytes), present = 0;
    maelys_datalog_fact_t *f = state.scratch;
    if (!rc) rc = maelys_datalog_result_query(state.result, f->predicate, f->terms, f->arity, &present);
    if (!rc) *out = (uint32_t)present;
    return finish(rc);
}
int maelys_datalog_wasm_enumerate(const char *predicate, uint32_t arity, uint32_t *out, uint32_t capacity, uint32_t *count) {
    if (begin()) return last_status;
    if (!state.result) return finish(CLOSED);
    if (!count || (!out && capacity) || arity > MAELYS_DATALOG_PUBLIC_MAX_TERMS || capacity > state.max_per_predicate)
        return finish(INVALID);
    size_t n = 0;
    maelys_datalog_fact_view_t *views = state.scratch;
    int rc = maelys_datalog_result_enumerate(state.result, predicate, arity, capacity ? views : NULL, capacity, &n);
    if (rc) return finish(rc);
    rc = checked_u32(n, count);
    if (rc) return finish(rc);
    for (size_t i = 0; i < n && i < capacity; ++i) for (size_t j = 0; j < arity; ++j) {
        maelys_datalog_term_view_t *v = &views[i].terms[j];
        uint32_t *w = out + (i * arity + j) * 3u;
        uint64_t bits = 0;
        w[0] = (uint32_t)v->kind;
        if (v->kind == MAELYS_DATALOG_VALUE_INTEGER) memcpy(&bits, &v->as.integer, sizeof(bits));
        else if (v->kind == MAELYS_DATALOG_VALUE_SYMBOL) bits = v->as.symbol_id;
        else if (v->kind == MAELYS_DATALOG_VALUE_BOOLEAN) bits = (uint64_t)!!v->as.boolean;
        else return finish(MAELYS_DATALOG_STATUS_INTERNAL);
        w[1] = (uint32_t)bits; w[2] = (uint32_t)(bits >> 32u);
    }
    return finish(OK);
}
const char *maelys_datalog_wasm_symbol_text(uint32_t id) {
    if (begin()) return NULL;
    if (!state.result) { finish(CLOSED); return NULL; }
    const char *text = NULL; size_t length;
    int rc = maelys_datalog_result_symbol_text(state.result, id, &text, &length);
    finish(rc); return rc ? NULL : text;
}
int maelys_datalog_wasm_explain(uint32_t kind, const uint32_t *w, uint32_t n,
    const char *text, uint32_t bytes, char *out, uint32_t capacity, uint32_t *required) {
    if (begin()) return last_status;
    if (!state.result) return finish(CLOSED);
    if (!required || (!out && capacity) || (out && !capacity) || (kind != 1u && kind != 2u)) return finish(INVALID);
    int rc = decode(w, n, 1u, text, bytes);
    if (rc) return finish(rc);
    maelys_datalog_fact_t *f = state.scratch;
    size_t needed = 0;
    rc = kind == 1u
        ? maelys_datalog_result_explain_true_text(state.result, f->predicate, f->terms, f->arity, out, capacity, &needed)
        : maelys_datalog_result_explain_false_text(state.result, f->predicate, f->terms, f->arity, out, capacity, &needed);
    if (!rc || rc == TOO_LARGE) {
        int size_rc = checked_u32(needed, required);
        if (size_rc) return finish(size_rc);
    }
    return finish(rc);
}
int maelys_datalog_wasm_derived_count(uint32_t *out) {
    if (begin()) return last_status;
    if (!state.result) return finish(CLOSED);
    size_t n;
    int rc = maelys_datalog_result_derived_fact_count(state.result, &n);
    return finish(rc ? rc : checked_u32(n, out));
}
int maelys_datalog_wasm_fingerprint(uint32_t kind, char *out, uint32_t capacity) {
    if (begin()) return last_status;
    if (!state.policy || !state.session) return finish(CLOSED);
    if (!out || capacity < MAELYS_DATALOG_PUBLIC_FINGERPRINT_BYTES) return finish(INVALID);
    int rc;
    if (kind == 1u) rc = maelys_datalog_policy_fingerprint(state.policy, out);
    else if (kind == 2u) rc = maelys_datalog_session_fingerprint(state.session, out);
    else if (kind == 3u) rc = maelys_datalog_session_execution_fingerprint(state.session, out);
    else rc = INVALID;
    return finish(rc);
}
uint32_t maelys_datalog_wasm_diagnostic_scalar(uint32_t field) {
    switch (field) {
    case 0: return (uint32_t)last_status;
    case 1: return (uint32_t)diagnostic.source;
    case 2: return (uint32_t)diagnostic.code;
    case 3: return (uint32_t)diagnostic.present;
    case 4: return (uint32_t)(diagnostic.present >> 32u);
    case 5: return (uint32_t)diagnostic.line;
    case 6: return (uint32_t)diagnostic.column;
    case 7: return (uint32_t)diagnostic.arity;
    case 8: return (uint32_t)diagnostic.observed_count;
    case 9: return (uint32_t)diagnostic.limit;
    case 10: return (uint32_t)diagnostic.depth;
    case 11: return (uint32_t)diagnostic.depth_limit;
    case 12: return (uint32_t)diagnostic.rule_id;
    case 13: return diagnostic.comparison_result;
    case 14: return diagnostic.expected_kind;
    case 15: return diagnostic.lhs_kind;
    case 16: return diagnostic.rhs_kind;
    case 17: return diagnostic.comparison_op;
    case 18: return (uint32_t)diagnostic.limit_kind;
    case 19: return (uint32_t)diagnostic.term_index;
    case 20: return (uint32_t)diagnostic.expected_arity;
    case 21: return (uint32_t)diagnostic.observed_arity;
    case 22: return diagnostic.abi_version;
    case 23: return MAELYS_DATALOG_PUBLIC_API_VERSION;
    default: return 0;
    }
}
const char *maelys_datalog_wasm_diagnostic_text(uint32_t field) {
    switch (field) {
    case 0: return diagnostic.phase;
    case 1: return diagnostic.message;
    case 2: return diagnostic.hint;
    case 3: return diagnostic.file;
    case 4: return diagnostic.predicate;
    case 5: return diagnostic.token;
    case 6: return diagnostic.field;
    case 7: return diagnostic.domain;
    case 8: return maelys_datalog_diag_code_name(diagnostic.code);
    case 9: return maelys_datalog_status_name((maelys_datalog_status_t)last_status);
    default: return "";
    }
}
