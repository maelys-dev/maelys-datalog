/* SPDX-License-Identifier: MPL-2.0 */
#include "transport.h"
#include <maelys/datalog.h>
#include <maelys/datalog_resources.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#if !defined(MAELYS_DATALOG_HAS_MANIFEST_BUFFER) || !MAELYS_DATALOG_HAS_MANIFEST_BUFFER
#error "JavaScript requires an SDK with policy_load_manifest_buffer (after v0.13.0)"
#endif
_Static_assert(MAELYS_DATALOG_PUBLIC_API_VERSION == 2u, "Review consumer API changes");
_Static_assert(MAELYS_DATALOG_DIAGNOSTIC_ABI_VERSION == 1u, "Review diagnostic changes");
_Static_assert(MAELYS_DATALOG_PUBLIC_MAX_TERMS == 4u, "Review transport arity");
#define OK MAELYS_DATALOG_STATUS_OK
#define INVALID MAELYS_DATALOG_STATUS_INVALID_ARGUMENT
#define CLOSED MAELYS_DATALOG_STATUS_INVALID_STATE
#define TOO_LARGE MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE
#define INTERNAL MAELYS_DATALOG_STATUS_INTERNAL
#define MAELYS_WASM_FACT_WORDS 15u

enum { POLICY = 1, EDB, SESSION, RESULT };
typedef struct handle {
    struct handle *next;
    uint32_t id, parent, kind, result_id;
    int frozen;
    union { maelys_datalog_policy_t *policy; maelys_datalog_input_edb_t *edb;
            maelys_datalog_session_t *session; } p;
    maelys_datalog_result_t *result;
} handle;
struct maelys_js_context {
    handle *handles;
    uint32_t serial;
    void *scratch;
    size_t max_facts, max_predicates, max_per_predicate;
    uint32_t *words, word_count;
    char fingerprint[MAELYS_DATALOG_PUBLIC_FINGERPRINT_BYTES];
    char *owned_text;
    const char *text;
    maelys_datalog_diagnostic_t diagnostic;
    int status;
};
static handle *lookup(maelys_js_context *c, uint32_t id, uint32_t kind) {
    if (!id) return NULL;
    for (handle *h = c->handles; h; h = h->next)
        if (kind == RESULT ? h->kind == SESSION && h->result_id == id && h->result
                           : h->id == id && h->kind == kind) return h;
    return NULL;
}
static handle *reserve(maelys_js_context *c, uint32_t kind, uint32_t parent) {
    if (c->serial == UINT32_MAX) return NULL;
    handle *h = calloc(1, sizeof(*h));
    if (h) { h->id = ++c->serial; h->kind = kind; h->parent = parent; }
    return h;
}
static void commit(maelys_js_context *c, handle *h) {
    h->next = c->handles; c->handles = h; c->words[0] = h->id; c->word_count = 1;
}
static int drop(maelys_js_context *c, uint32_t id, uint32_t kind) {
    handle *h = lookup(c, id, kind);
    if (!h) return CLOSED;
    int rc = OK;
    if (kind == RESULT) {
        rc = maelys_datalog_result_free(h->result);
        if (!rc) { h->result = NULL; h->result_id = 0; }
        return rc;
    }
    /* The same cascade applies to explicit close and native finalization. */
    if (kind == POLICY) {
        for (;;) {
            handle *child = c->handles;
            while (child && child->parent != id) child = child->next;
            if (!child) break;
            rc = drop(c, child->id, child->kind);
            if (rc) return rc;
        }
        rc = maelys_datalog_policy_free(h->p.policy);
    } else if (kind == SESSION) {
        if (h->result && (rc = drop(c, h->result_id, RESULT))) return rc;
        rc = maelys_datalog_session_free(h->p.session);
    } else if (kind == EDB) rc = maelys_datalog_input_edb_free(h->p.edb);
    if (rc) return rc;
    handle **link = &c->handles;
    while (*link != h) link = &(*link)->next;
    *link = h->next; free(h);
    return OK;
}
maelys_js_context *maelys_js_create(void) {
    maelys_js_context *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->diagnostic.struct_size = sizeof(c->diagnostic);
    c->diagnostic.abi_version = MAELYS_DATALOG_DIAGNOSTIC_ABI_VERSION;
    if (maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_EDB_FACTS, &c->max_facts) ||
        maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_PREDICATES, &c->max_predicates) ||
        maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_FACTS_PER_PRED, &c->max_per_predicate)) {
        free(c); return NULL;
    }
    if (c->max_facts > SIZE_MAX / sizeof(maelys_datalog_fact_t) ||
        c->max_predicates > (SIZE_MAX - 256 * sizeof(char *)) / sizeof(maelys_datalog_predicate_t) ||
        c->max_per_predicate > (UINT32_MAX - 16u) / 12u ||
        c->max_per_predicate > (SIZE_MAX / sizeof(uint32_t) - 16u) / 12u) { free(c); return NULL; }
    size_t bytes = c->max_facts * sizeof(maelys_datalog_fact_t);
    size_t predicates = c->max_predicates * sizeof(maelys_datalog_predicate_t) + 256 * sizeof(char *);
    size_t views = c->max_per_predicate * sizeof(maelys_datalog_fact_view_t);
    if (bytes < predicates) bytes = predicates;
    if (bytes < views) bytes = views;
    c->scratch = malloc(bytes);
    c->words = calloc(16 + c->max_per_predicate * 12, sizeof(uint32_t));
    if (!c->scratch || !c->words) { free(c->scratch); free(c->words); free(c); return NULL; }
    c->text = "";
    return c;
}
void maelys_js_destroy(maelys_js_context *c) {
    if (!c) return;
    while (c->handles) {
        /* Engine-owned objects have no external borrowers in this adapter. */
        if (drop(c, c->handles->id, c->handles->kind)) abort();
    }
    free(c->owned_text); free(c->scratch); free(c->words); free(c);
}
const uint32_t *maelys_js_words(maelys_js_context *c) { return c->words; }
uint32_t maelys_js_word_count(maelys_js_context *c) { return c->word_count; }
const char *maelys_js_text(maelys_js_context *c) { return c->text ? c->text : ""; }
static const char *span(const char *text, uint32_t bytes, uint32_t offset, uint32_t length) {
    if (!text || offset >= bytes || length >= bytes - offset || text[offset + length] != '\0' ||
        memchr(text + offset, '\0', length)) return NULL;
    return text + offset;
}
static int decode(maelys_js_context *c, const uint32_t *words, uint32_t word_count, uint32_t count,
                  const char *text, uint32_t text_bytes) {
    if (count > c->max_facts) return TOO_LARGE;
    if (count > UINT32_MAX / MAELYS_WASM_FACT_WORDS || word_count != count * MAELYS_WASM_FACT_WORDS ||
        (!words && count)) return INVALID;
    maelys_datalog_fact_t *facts = c->scratch;
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
uint32_t maelys_js_scalar(maelys_js_context *c, uint32_t field) {
    switch (field) {
    case 0: return (uint32_t)c->status;
    case 1: return (uint32_t)c->diagnostic.source;
    case 2: return (uint32_t)c->diagnostic.code;
    case 3: return (uint32_t)c->diagnostic.present;
    case 4: return (uint32_t)(c->diagnostic.present >> 32u);
    case 5: return (uint32_t)c->diagnostic.line;
    case 6: return (uint32_t)c->diagnostic.column;
    case 7: return (uint32_t)c->diagnostic.arity;
    case 8: return (uint32_t)c->diagnostic.observed_count;
    case 9: return (uint32_t)c->diagnostic.limit;
    case 10: return (uint32_t)c->diagnostic.depth;
    case 11: return (uint32_t)c->diagnostic.depth_limit;
    case 12: return (uint32_t)c->diagnostic.rule_id;
    case 13: return c->diagnostic.comparison_result;
    case 14: return c->diagnostic.expected_kind;
    case 15: return c->diagnostic.lhs_kind;
    case 16: return c->diagnostic.rhs_kind;
    case 17: return c->diagnostic.comparison_op;
    case 18: return (uint32_t)c->diagnostic.limit_kind;
    case 19: return (uint32_t)c->diagnostic.term_index;
    case 20: return (uint32_t)c->diagnostic.expected_arity;
    case 21: return (uint32_t)c->diagnostic.observed_arity;
    case 22: return c->diagnostic.abi_version;
    case 23: return MAELYS_DATALOG_PUBLIC_API_VERSION;
    default: return 0;
    }
}
const char *maelys_js_diagnostic(maelys_js_context *c, uint32_t field) {
    switch (field) {
    case 0: return c->diagnostic.phase;
    case 1: return c->diagnostic.message;
    case 2: return c->diagnostic.hint;
    case 3: return c->diagnostic.file;
    case 4: return c->diagnostic.predicate;
    case 5: return c->diagnostic.token;
    case 6: return c->diagnostic.field;
    case 7: return c->diagnostic.domain;
    case 8: return maelys_datalog_diag_code_name(c->diagnostic.code);
    case 9: return maelys_datalog_status_name((maelys_datalog_status_t)c->status);
    default: return "";
    }
}
static uint64_t wide(const uint32_t *w) { return (uint64_t)w[0] | ((uint64_t)w[1] << 32); }
#define REQUIRE(test) do { if (!(test)) return INVALID; } while (0)
#define TRY(call) do { int step = (call); if (step) return step; } while (0)
#define TEXT(i) span(text, bytes, w[i], w[(i) + 1])
#define HANDLE(var, id, type) handle *var = lookup(c, id, type); if (!var) return CLOSED
static int dispatch(maelys_js_context *c, uint32_t op, const uint32_t *w, uint32_t n,
                    const char *text, uint32_t bytes) {
    REQUIRE((w || !n) && (text || !bytes));
    switch (op) {
    case 0:
        REQUIRE(n == 0); c->words[0] = 1; c->words[1] = MAELYS_DATALOG_PUBLIC_API_VERSION;
        c->word_count = 2; return OK;
    case 1:
        REQUIRE(n == 0);
        for (unsigned i = 0; i < 12; ++i) {
            size_t value;
            TRY(maelys_datalog_limit_get((maelys_datalog_limit_t)(i + 1), &value));
            if (value > UINT32_MAX) return TOO_LARGE;
            c->words[i] = (uint32_t)value;
        }
        c->word_count = 12; return OK;
    case 2: {
        REQUIRE(n >= 4);
        uint32_t predicates = w[2], atoms = w[3];
        const char *name = TEXT(0);
        REQUIRE(name && predicates && predicates <= c->max_predicates && atoms <= 256 &&
                predicates <= (UINT32_MAX - 4 - atoms * 2) / 4 && n == 4 + predicates * 4 + atoms * 2);
        maelys_datalog_predicate_t *defs = c->scratch;
        const char **strings = (const char **)(defs + c->max_predicates);
        for (uint32_t i = 0; i < predicates; ++i) {
            uint32_t j = 4 + i * 4;
            REQUIRE(TEXT(j));
            defs[i] = (maelys_datalog_predicate_t){TEXT(j), w[j + 2], w[j + 3]};
        }
        for (uint32_t i = 0; i < atoms; ++i) {
            strings[i] = TEXT(4 + predicates * 4 + i * 2);
            REQUIRE(strings[i]);
        }
        const maelys_datalog_domain_t domain = {name, defs, predicates, strings, atoms};
        return maelys_datalog_domain_register(&domain);
    }
    case 3: case 4: case 5: {
        if (op == 3) REQUIRE(n == 6 && TEXT(0) && TEXT(2) && TEXT(4));
        if (op == 4) REQUIRE(n == 3 && TEXT(0));
        if (op == 5) REQUIRE(n >= 4 && TEXT(0) && w[3] <= (UINT32_MAX - 4) / 4 && n == 4 + w[3] * 4);
        handle *h = reserve(c, POLICY, 0);
        if (!h) return INTERNAL;
        int rc;
        if (op == 3) rc = maelys_datalog_policy_load_inline(TEXT(0), TEXT(2), TEXT(4), w[5], &h->p.policy, &c->diagnostic);
        else if (op == 4) {
#ifdef __EMSCRIPTEN__
            rc = MAELYS_DATALOG_STATUS_UNSUPPORTED;
#else
            rc = maelys_datalog_policy_load_manifest(TEXT(0), w[2], &h->p.policy, &c->diagnostic);
#endif
        } else {
            /* One allocation for a load's bundle descriptors, never for facts. */
            size_t count = w[3];
            if (count > SIZE_MAX / sizeof(maelys_datalog_policy_bundle_entry_t)) { free(h); return TOO_LARGE; }
            maelys_datalog_policy_bundle_entry_t *bundle = calloc(count ? count : 1, sizeof(*bundle));
            if (!bundle) { free(h); return INTERNAL; }
            rc = OK;
            for (uint32_t i = 0; i < count; ++i) {
                uint32_t j = 4 + i * 4;
                if (!TEXT(j) || !TEXT(j + 2)) { rc = INVALID; break; }
                bundle[i] = (maelys_datalog_policy_bundle_entry_t){TEXT(j), TEXT(j + 2), w[j + 3]};
            }
            if (!rc) rc = maelys_datalog_policy_load_manifest_buffer(TEXT(0), w[1], bundle, count, w[2], &h->p.policy, &c->diagnostic);
            free(bundle);
        }
        if (rc) { free(h); return rc; }
        commit(c, h); return OK;
    }
    case 6:
        REQUIRE(n == 2 && w[0] >= POLICY && w[0] <= RESULT);
        return drop(c, w[1], w[0]);
    case 7: {
        REQUIRE(n == 1); HANDLE(h, w[0], POLICY);
        size_t count;
        TRY(maelys_datalog_policy_count(h->p.policy, &count));
        if (count > UINT32_MAX) return TOO_LARGE;
        c->words[0] = (uint32_t)count; c->word_count = 1;
        return OK;
    }
    case 8: {
        REQUIRE(n == 3); HANDLE(policy, w[0], POLICY); (void)policy;
        handle *h = reserve(c, EDB, w[0]);
        if (!h) return INTERNAL;
        int rc = maelys_datalog_input_edb_create_with_capacity(w[1], w[2], &h->p.edb);
        if (rc) { free(h); return rc; }
        commit(c, h); return OK;
    }
    case 9: {
        REQUIRE(n == 12); HANDLE(policy, w[0], POLICY);
        handle *h = reserve(c, SESSION, w[0]);
        if (!h) return INTERNAL;
        maelys_datalog_session_config_t *config = NULL;
        int rc = maelys_datalog_session_config_create(&config);
        if (!rc) rc = maelys_datalog_session_config_set_required_capabilities(config, wide(w + 2));
        if (!rc) rc = maelys_datalog_session_config_set_work_limit(config, wide(w + 4));
        if (!rc) rc = maelys_datalog_session_config_set_explanation_workspace(config, w[6]);
        maelys_datalog_session_resource_request_t resources = MAELYS_DATALOG_RESOURCE_REQUEST_INIT;
        resources.capacity_mask = w[7]; resources.input_facts = w[8]; resources.derived_facts = w[9];
        resources.symbols = w[10]; resources.text_bytes = w[11];
        if (!rc && w[7]) rc = maelys_datalog_session_config_set_resources(config, &resources);
        if (!rc) rc = maelys_datalog_session_create_configured(policy->p.policy, w[1], config, &h->p.session);
        if (config) (void)maelys_datalog_session_config_free(config);
        if (rc) { free(h); return rc; }
        commit(c, h); return OK;
    }
    case 10: {
        REQUIRE(n == 1); HANDLE(h, w[0], SESSION);
        maelys_datalog_session_resources_t r = MAELYS_DATALOG_RESOURCES_INIT;
        TRY(maelys_datalog_session_get_resources(h->p.session, &r));
        if (r.input_facts > UINT32_MAX || r.derived_facts > UINT32_MAX || r.symbols > UINT32_MAX || r.text_bytes > UINT32_MAX) return TOO_LARGE;
        c->words[0] = (uint32_t)r.input_facts; c->words[1] = (uint32_t)r.derived_facts;
        c->words[2] = (uint32_t)r.symbols; c->words[3] = (uint32_t)r.text_bytes;
        c->word_count = 4; return OK;
    }
    case 11: {
        REQUIRE(n == 2 && w[0] >= 1 && w[0] <= 3);
        HANDLE(h, w[1], w[0] == 1 ? POLICY : SESSION);
        int rc = w[0] == 1 ? maelys_datalog_policy_fingerprint(h->p.policy, c->fingerprint)
            : w[0] == 2 ? maelys_datalog_session_fingerprint(h->p.session, c->fingerprint)
            : maelys_datalog_session_execution_fingerprint(h->p.session, c->fingerprint);
        if (!rc) c->text = c->fingerprint;
        return rc;
    }
    case 12: {
        REQUIRE(n == 2 && w[1] <= 1); HANDLE(h, w[0], EDB);
        if (h->frozen && !w[1]) return CLOSED;
        TRY(maelys_datalog_input_edb_clear(h->p.edb)); h->frozen = 0; return OK;
    }
    case 13: {
        REQUIRE(n >= 2); HANDLE(h, w[0], EDB);
        if (h->frozen) return CLOSED;
        TRY(decode(c, w + 2, n - 2, w[1], text, bytes));
        return maelys_datalog_input_edb_add_facts(h->p.edb, c->scratch, w[1], &c->diagnostic);
    }
    case 14: {
        REQUIRE(n == 1); HANDLE(h, w[0], EDB);
        size_t count, used, capacity;
        TRY(maelys_datalog_input_edb_count(h->p.edb, &count));
        TRY(maelys_datalog_input_edb_text_usage(h->p.edb, &used, &capacity));
        if (count > UINT32_MAX || used > UINT32_MAX || capacity > UINT32_MAX) return TOO_LARGE;
        c->words[0] = (uint32_t)count; c->words[1] = (uint32_t)used; c->words[2] = (uint32_t)capacity;
        c->word_count = 3; return OK;
    }
    case 15: {
        REQUIRE(n == 2); HANDLE(h, w[0], SESSION); HANDLE(edb, w[1], EDB);
        if (h->parent != edb->parent || h->result) return CLOSED;
        if (c->serial == UINT32_MAX) return TOO_LARGE;
        TRY(maelys_datalog_session_solve_edb(h->p.session, edb->p.edb, &h->result, &c->diagnostic));
        h->result_id = ++c->serial; edb->frozen = 1;
        c->words[0] = h->result_id; c->word_count = 1; return OK;
    }
    case 16: case 19: {
        uint32_t prefix = op == 16 ? 1 : 2;
        REQUIRE(n == prefix + MAELYS_WASM_FACT_WORDS); HANDLE(h, w[0], RESULT);
        REQUIRE(op == 16 || w[1] == 1 || w[1] == 2);
        TRY(decode(c, w + prefix, n - prefix, 1, text, bytes));
        const maelys_datalog_fact_t *f = c->scratch;
        if (op == 16) {
            int present;
            TRY(maelys_datalog_result_query(h->result, f->predicate, f->terms, f->arity, &present));
            c->words[0] = (uint32_t)present; c->word_count = 1; return OK;
        }
        size_t required = 0;
        int (*explain)(const maelys_datalog_result_t *, const char *, const maelys_datalog_value_t *, size_t, char *, size_t, size_t *) =
            w[1] == 1 ? maelys_datalog_result_explain_true_text : maelys_datalog_result_explain_false_text;
        TRY(explain(h->result, f->predicate, f->terms, f->arity, NULL, 0, &required));
        if (required >= UINT32_MAX) return TOO_LARGE;
        c->owned_text = malloc(required + 1);
        if (!c->owned_text) return INTERNAL;
        TRY(explain(h->result, f->predicate, f->terms, f->arity, c->owned_text, required + 1, &required));
        c->text = c->owned_text; return OK;
    }
    case 17: {
        REQUIRE(n == 4 && w[1] <= 4 && TEXT(2)); HANDLE(h, w[0], RESULT);
        size_t count;
        maelys_datalog_fact_view_t *views = c->scratch;
        TRY(maelys_datalog_result_enumerate(h->result, TEXT(2), w[1], views, c->max_per_predicate, &count));
        if (count > c->max_per_predicate) return INTERNAL;
        c->words[0] = (uint32_t)count;
        c->word_count = 1 + (uint32_t)count * w[1] * 3;
        for (size_t i = 0; i < count; ++i) for (size_t j = 0; j < w[1]; ++j) {
            const maelys_datalog_term_view_t *v = &views[i].terms[j];
            uint32_t *out = c->words + 1 + (i * w[1] + j) * 3;
            uint64_t bits = 0;
            if (v->kind == MAELYS_DATALOG_VALUE_INTEGER) memcpy(&bits, &v->as.integer, sizeof(bits));
            else if (v->kind == MAELYS_DATALOG_VALUE_SYMBOL) bits = v->as.symbol_id;
            else if (v->kind == MAELYS_DATALOG_VALUE_BOOLEAN) bits = (uint64_t)!!v->as.boolean;
            else return INTERNAL;
            out[0] = (uint32_t)v->kind; out[1] = (uint32_t)bits; out[2] = (uint32_t)(bits >> 32);
        }
        return OK;
    }
    case 18: {
        REQUIRE(n == 2); HANDLE(h, w[0], RESULT);
        size_t length;
        return maelys_datalog_result_symbol_text(h->result, w[1], &c->text, &length);
    }
    case 20: {
        REQUIRE(n == 1); HANDLE(h, w[0], RESULT);
        size_t count;
        TRY(maelys_datalog_result_derived_fact_count(h->result, &count));
        if (count > UINT32_MAX) return TOO_LARGE;
        c->words[0] = (uint32_t)count; c->word_count = 1; return OK;
    }
    default: return INVALID;
    }
}
int maelys_js_call(maelys_js_context *c, uint32_t op, const uint32_t *w, uint32_t n,
                   const char *text, uint32_t bytes) {
    if (!c) return CLOSED;
    free(c->owned_text); c->owned_text = NULL; c->text = ""; c->word_count = 0;
    (void)maelys_datalog_diagnostic_clear(&c->diagnostic);
    c->status = dispatch(c, op, w, n, text, bytes);
    return c->status;
}
