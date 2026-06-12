#include "src/core/maelys_datalog_policy.h"

#include "common/maelys_sha256.h"
#include "src/core/maelys_datalog_audit.h"
#include "src/core/maelys_datalog_diagnostic.h"
#include "src/core/maelys_datalog_edb.h"
#include "src/core/maelys_datalog_predicate_registry.h"
#include "src/core/maelys_datalog_symbol_table.h"

#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define MAELYS_DATALOG_NO_DELTA_INDEX ((size_t)-1)

_Static_assert(MAELYS_DATALOG_MAX_BODY_LITERALS <= 64u,
               "planned_mask width insufficient for MAX_BODY_LITERALS");
_Static_assert(MAELYS_DATALOG_MAX_RULE_VARIABLES <= 64u,
               "bound_var_mask width insufficient for MAX_RULE_VARIABLES");

typedef struct {
    int bound[MAELYS_DATALOG_MAX_RULE_VARIABLES];
    maelys_datalog_term_t value[MAELYS_DATALOG_MAX_RULE_VARIABLES];
} solve_once_bindings_t;

typedef enum {
    MAELYS_DATALOG_COMPARE_TRUE = 0,
    MAELYS_DATALOG_COMPARE_FALSE,
    MAELYS_DATALOG_COMPARE_INVALID_KIND,
    MAELYS_DATALOG_COMPARE_INVALID_ORDINAL_TYPE,
    MAELYS_DATALOG_COMPARE_UNBOUND_VARIABLE,
    MAELYS_DATALOG_COMPARE_UNKNOWN_OPERATOR,
    MAELYS_DATALOG_COMPARE_UNKNOWN_TERM_KIND
} maelys_datalog_compare_result_t;

struct maelys_datalog_solve_result {
    const maelys_datalog_ruleset_t *ruleset;
    maelys_datalog_fact_t edb_facts[MAELYS_DATALOG_MAX_EDB_FACTS];
    maelys_datalog_fact_t idb_facts[MAELYS_DATALOG_MAX_IDB_FACTS];
    uint16_t idb_proof_index[MAELYS_DATALOG_MAX_IDB_FACTS];
    maelys_datalog_fact_set_t edb_snapshot;
    maelys_datalog_fact_set_t idb_final;
    size_t facts_per_pred[MAELYS_DATALOG_MAX_PREDICATES];
    size_t stratum_idb_end[MAELYS_DATALOG_MAX_STRATA + 1u];
    size_t idb_current_end;
    size_t idb_delta_begin;
    size_t idb_delta_end;
    size_t idb_merge_end;
    uint32_t active_stratum;
    int stratified;
    maelys_datalog_proof_tree_t proof;
    maelys_datalog_deny_reason_t failure_reason;
    maelys_result_t failure_error;
    maelys_datalog_diagnostic_t runtime_diag;
    int finalized;
    int failed;
};

static void solve_once_init_proof_indices(maelys_datalog_solve_result_t *result) {
    if (!result) return;
    for (size_t i = 0; i < MAELYS_DATALOG_MAX_IDB_FACTS; i++) {
        result->idb_proof_index[i] = MAELYS_DATALOG_PROOF_NO_PARENT;
    }
}

static void solve_once_assert_windows(const maelys_datalog_solve_result_t *result) {
    assert(result);
    assert(result->idb_delta_begin <= result->idb_delta_end);
    assert(result->idb_delta_end <= result->idb_current_end);
    assert(result->idb_current_end <= result->idb_merge_end);
    assert(result->idb_merge_end <= MAELYS_DATALOG_MAX_IDB_FACTS);
    assert(result->stratum_idb_end[0] == 0);
    for (size_t i = 0; i < MAELYS_DATALOG_MAX_STRATA; i++) {
        assert(result->stratum_idb_end[i] <= result->stratum_idb_end[i + 1u]);
        assert(result->stratum_idb_end[i + 1u] <= MAELYS_DATALOG_MAX_IDB_FACTS);
    }
}

const char *maelys_datalog_decision_name(maelys_datalog_decision_t decision) {
    switch (decision) {
        case MAELYS_DATALOG_DECISION_ALLOW: return MAELYS_DATALOG_DECISION_NAME_ALLOW;
        case MAELYS_DATALOG_DECISION_REDUCED: return MAELYS_DATALOG_DECISION_NAME_REDUCED;
        case MAELYS_DATALOG_DECISION_DENY_DEFAULT: return MAELYS_DATALOG_DECISION_NAME_DENY_DEFAULT;
        case MAELYS_DATALOG_DECISION_DENY_CONFLICT: return MAELYS_DATALOG_DECISION_NAME_DENY_CONFLICT;
        case MAELYS_DATALOG_DECISION_DENY:
        default: return MAELYS_DATALOG_DECISION_NAME_DENY;
    }
}

static int datalog_term_kind_known(maelys_datalog_term_kind_t kind);
static int datalog_fact_structurally_valid(const maelys_datalog_predicate_registry_t *registry,
                                           const maelys_datalog_fact_t *fact);

static maelys_result_t canonical_update(maelys_sha256_ctx_t *ctx, const char *text) {
    if (!ctx || !text) return MAELYS_ERR_INVALID_ARGUMENT;
    maelys_sha256_update(ctx, (const unsigned char *)text, strlen(text));
    return MAELYS_OK;
}

static maelys_result_t canonical_printf(maelys_sha256_ctx_t *ctx, const char *fmt, ...) {
    if (!ctx || !fmt) return MAELYS_ERR_INVALID_ARGUMENT;
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0 || (size_t)n >= sizeof(buf)) return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    maelys_sha256_update(ctx, (const unsigned char *)buf, (size_t)n);
    return MAELYS_OK;
}

static const maelys_datalog_predicate_def_t *canonical_predicate_def(
    const maelys_datalog_ruleset_t *ruleset,
    maelys_datalog_predicate_id_t id) {
    if (!ruleset || id >= ruleset->registry.count || id >= MAELYS_DATALOG_MAX_PREDICATES) return NULL;
    return &ruleset->registry.defs[id];
}

static maelys_result_t canonical_stream_term(maelys_sha256_ctx_t *ctx,
                                             const maelys_datalog_ruleset_t *ruleset,
                                             const maelys_datalog_term_t *term) {
    if (!ctx || !ruleset || !term) return MAELYS_ERR_INVALID_ARGUMENT;
    switch (term->kind) {
        case MAELYS_DATALOG_TERM_SYMBOL: {
            const char *text = maelys_datalog_symbol_text(&ruleset->symbols, term->as.symbol);
            if (!text) return MAELYS_ERR_INVALID_STATE;
            return canonical_printf(ctx, "sym:%s", text);
        }
        case MAELYS_DATALOG_TERM_INT:
            return canonical_printf(ctx, "int:%lld", term->as.integer);
        case MAELYS_DATALOG_TERM_BOOL:
            return canonical_printf(ctx, "bool:%d", term->as.boolean ? 1 : 0);
        case MAELYS_DATALOG_TERM_VAR:
            return canonical_printf(ctx, "var:%u", term->as.variable);
        default:
            return MAELYS_ERR_INVALID_STATE;
    }
}

static maelys_result_t canonical_stream_terms(maelys_sha256_ctx_t *ctx,
                                              const maelys_datalog_ruleset_t *ruleset,
                                              const maelys_datalog_term_t *terms,
                                              size_t arity) {
    if (!ctx || !ruleset || (!terms && arity > 0) || arity > MAELYS_DATALOG_MAX_TERMS) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < arity; i++) {
        if (i > 0) {
            maelys_result_t rc = canonical_update(ctx, ",");
            if (rc != MAELYS_OK) return rc;
        }
        maelys_result_t rc = canonical_stream_term(ctx, ruleset, &terms[i]);
        if (rc != MAELYS_OK) return rc;
    }
    return MAELYS_OK;
}

static maelys_result_t canonical_stream_atom(maelys_sha256_ctx_t *ctx,
                                             const maelys_datalog_ruleset_t *ruleset,
                                             const char *prefix,
                                             const maelys_datalog_fact_t *atom) {
    if (!ctx || !ruleset || !prefix || !atom || atom->arity > MAELYS_DATALOG_MAX_TERMS) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    const maelys_datalog_predicate_def_t *def = canonical_predicate_def(ruleset, atom->predicate_id);
    if (!def) return MAELYS_ERR_INVALID_STATE;
    maelys_result_t rc = canonical_printf(ctx, "%s%s/%u(", prefix, def->name, (unsigned)atom->arity);
    if (rc != MAELYS_OK) return rc;
    rc = canonical_stream_terms(ctx, ruleset, atom->terms, atom->arity);
    if (rc != MAELYS_OK) return rc;
    return canonical_update(ctx, ")\n");
}

static maelys_result_t canonical_stream_literal(maelys_sha256_ctx_t *ctx,
                                                const maelys_datalog_ruleset_t *ruleset,
                                                const maelys_datalog_literal_t *literal) {
    if (!ctx || !ruleset || !literal) return MAELYS_ERR_INVALID_ARGUMENT;
    switch (literal->kind) {
        case MAELYS_DATALOG_LITERAL_ATOM:
            return canonical_stream_atom(ctx, ruleset, "body.atom=", &literal->atom);
        case MAELYS_DATALOG_LITERAL_NEGATED_ATOM:
            return canonical_stream_atom(ctx, ruleset, "body.not=", &literal->atom);
        case MAELYS_DATALOG_LITERAL_COMPARISON: {
            maelys_result_t rc = canonical_printf(ctx, "body.cmp=%u:", (unsigned)literal->op);
            if (rc != MAELYS_OK) return rc;
            rc = canonical_stream_term(ctx, ruleset, &literal->lhs);
            if (rc != MAELYS_OK) return rc;
            rc = canonical_update(ctx, ",");
            if (rc != MAELYS_OK) return rc;
            rc = canonical_stream_term(ctx, ruleset, &literal->rhs);
            if (rc != MAELYS_OK) return rc;
            return canonical_update(ctx, "\n");
        }
        default:
            return MAELYS_ERR_INVALID_STATE;
    }
}

static maelys_result_t ruleset_stream_canonical(maelys_sha256_ctx_t *ctx,
                                                const maelys_datalog_ruleset_t *ruleset) {
    if (!ctx || !ruleset || !ruleset->loaded) return MAELYS_ERR_INVALID_ARGUMENT;
    maelys_result_t rc = canonical_printf(ctx, "policy_id=%s\n", ruleset->policy_id);
    if (rc != MAELYS_OK) return rc;
    rc = canonical_printf(ctx, "domain=%s\n", ruleset->domain);
    if (rc != MAELYS_OK) return rc;
    rc = canonical_printf(ctx, "profile=%s\n", MAELYS_DATALOG_PROFILE_NAME);
    if (rc != MAELYS_OK) return rc;

    for (size_t i = 0; i < ruleset->registry.count; i++) {
        const maelys_datalog_predicate_def_t *def = &ruleset->registry.defs[i];
        rc = canonical_printf(ctx, "pred=%s/%zu/%u\n", def->name, def->arity, def->kind_flags);
        if (rc != MAELYS_OK) return rc;
    }
    for (size_t i = 0; i < ruleset->fact_count; i++) {
        rc = canonical_stream_atom(ctx, ruleset, "fact=", &ruleset->facts[i]);
        if (rc != MAELYS_OK) return rc;
    }
    for (size_t i = 0; i < ruleset->rule_count; i++) {
        const maelys_datalog_rule_t *rule = &ruleset->rules[i];
        rc = canonical_printf(ctx, "rule=%zu\n", rule->rule_id);
        if (rc != MAELYS_OK) return rc;
        rc = canonical_stream_atom(ctx, ruleset, "head=", &rule->head);
        if (rc != MAELYS_OK) return rc;
        for (size_t j = 0; j < rule->body_count; j++) {
            rc = canonical_stream_literal(ctx, ruleset, &rule->body[j]);
            if (rc != MAELYS_OK) return rc;
        }
    }
    for (size_t i = 0; i < ruleset->registry.count; i++) {
        const maelys_datalog_predicate_def_t *def = &ruleset->registry.defs[i];
        if ((def->kind_flags & MAELYS_DATALOG_PRED_KIND_QUERY) &&
            (def->kind_flags & MAELYS_DATALOG_PRED_KIND_IDB)) {
            rc = canonical_printf(ctx, "query=%s/%zu\n", def->name, def->arity);
            if (rc != MAELYS_OK) return rc;
        }
    }
    return MAELYS_OK;
}

maelys_result_t maelys_datalog_ruleset_finalize_sha256(maelys_datalog_ruleset_t *ruleset) {
    if (!ruleset) return MAELYS_ERR_INVALID_ARGUMENT;
    if (maelys_sha256_hex_is_lowercase(ruleset->sha256)) return MAELYS_OK;

    maelys_sha256_ctx_t ctx;
    maelys_sha256_init(&ctx);
    maelys_result_t rc = ruleset_stream_canonical(&ctx, ruleset);
    if (rc != MAELYS_OK) return rc;

    unsigned char digest[MAELYS_SHA256_DIGEST_BYTES];
    maelys_sha256_final(&ctx, digest);
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < MAELYS_SHA256_DIGEST_BYTES; i++) {
        ruleset->sha256[2u * i] = hex[(digest[i] >> 4) & 0x0fu];
        ruleset->sha256[2u * i + 1u] = hex[digest[i] & 0x0fu];
    }
    ruleset->sha256[MAELYS_SHA256_HEX_BYTES] = '\0';
    return MAELYS_OK;
}

const char *maelys_datalog_solve_diagnostic_category_name(
    maelys_datalog_solve_diag_category_t category) {
    switch (category) {
        case MAELYS_DATALOG_SOLVE_DIAG_NONE: return "none";
        case MAELYS_DATALOG_SOLVE_DIAG_MAX_DEPTH: return "max_depth";
        case MAELYS_DATALOG_SOLVE_DIAG_IDB_OVERFLOW: return "idb_overflow";
        case MAELYS_DATALOG_SOLVE_DIAG_COMPARISON_TYPE_ERROR: return "comparison_type_error";
        case MAELYS_DATALOG_SOLVE_DIAG_MALFORMED_FACT: return "malformed_fact";
        case MAELYS_DATALOG_SOLVE_DIAG_MALFORMED_EDB: return "malformed_edb";
        case MAELYS_DATALOG_SOLVE_DIAG_INVALID_STATE: return "invalid_state";
        case MAELYS_DATALOG_SOLVE_DIAG_INVALID_ARGUMENT: return "invalid_argument";
        case MAELYS_DATALOG_SOLVE_DIAG_INTERNAL_ERROR: return "internal_error";
        default: return "unknown";
    }
}

static void solve_once_diag_clear(maelys_datalog_solve_diagnostic_t *diag) {
    if (!diag) return;
    memset(diag, 0, sizeof(*diag));
}

static void solve_once_diag_base(maelys_datalog_solve_diagnostic_t *diag,
                                 maelys_datalog_solve_diag_category_t category,
                                 maelys_result_t failure_error,
                                 maelys_datalog_deny_reason_t failure_reason) {
    if (!diag || diag->category != MAELYS_DATALOG_SOLVE_DIAG_NONE) return;
    diag->category = category;
    diag->failure_error = failure_error;
    diag->failure_reason = failure_reason;
}

static void solve_once_diag_from_fact(maelys_datalog_solve_diagnostic_t *diag,
                                      maelys_datalog_solve_diag_category_t category,
                                      maelys_result_t failure_error,
                                      maelys_datalog_deny_reason_t failure_reason,
                                      const maelys_datalog_predicate_registry_t *registry,
                                      const maelys_datalog_fact_t *fact) {
    solve_once_diag_base(diag, category, failure_error, failure_reason);
    if (!diag || !fact) return;
    diag->predicate_id = fact->predicate_id;
    diag->arity_observed = fact->arity;
    if (registry) {
        const maelys_datalog_predicate_def_t *def =
            maelys_datalog_predicate_registry_get(registry, fact->predicate_id);
        if (def) diag->arity_expected = def->arity;
    }
    for (size_t i = 0; i < fact->arity && i < MAELYS_DATALOG_MAX_TERMS; i++) {
        if (!datalog_term_kind_known(fact->terms[i].kind) ||
            fact->terms[i].kind == MAELYS_DATALOG_TERM_VAR) {
            diag->term_index = (uint8_t)i;
            diag->lhs_kind = (uint8_t)fact->terms[i].kind;
            break;
        }
    }
}

static const maelys_datalog_fact_t *solve_once_first_invalid_fact_in_set(
    const maelys_datalog_predicate_registry_t *registry,
    const maelys_datalog_fact_set_t *set) {
    if (!set || (!set->facts && set->count > 0) || set->count > set->capacity) return NULL;
    for (size_t i = 0; i < set->count; i++) {
        if (!datalog_fact_structurally_valid(registry, &set->facts[i])) return &set->facts[i];
    }
    return NULL;
}

static const maelys_datalog_fact_t *solve_once_first_invalid_fact_in_slice(
    const maelys_datalog_predicate_registry_t *registry,
    const maelys_datalog_fact_t *facts,
    size_t count) {
    if (!facts && count > 0) return NULL;
    for (size_t i = 0; i < count; i++) {
        if (!datalog_fact_structurally_valid(registry, &facts[i])) return &facts[i];
    }
    return NULL;
}

static void solve_once_diag_malformed_edb(maelys_datalog_solve_diagnostic_t *diag,
                                          const maelys_datalog_predicate_registry_t *registry,
                                          const maelys_datalog_fact_set_t *set) {
    solve_once_diag_base(diag,
                         MAELYS_DATALOG_SOLVE_DIAG_MALFORMED_EDB,
                         MAELYS_ERR_INVALID_STATE,
                         MAELYS_DATALOG_DENY_NONE);
    if (!diag || !set) return;
    diag->count_observed = (uint16_t)set->count;
    diag->capacity = (uint16_t)set->capacity;
    const maelys_datalog_fact_t *fact = solve_once_first_invalid_fact_in_set(registry, set);
    if (fact) solve_once_diag_from_fact(diag,
                                        MAELYS_DATALOG_SOLVE_DIAG_MALFORMED_EDB,
                                        MAELYS_ERR_INVALID_STATE,
                                        MAELYS_DATALOG_DENY_NONE,
                                        registry,
                                        fact);
}

static void solve_once_diag_malformed_fact(maelys_datalog_solve_diagnostic_t *diag,
                                           const maelys_datalog_predicate_registry_t *registry,
                                           const maelys_datalog_fact_t *fact) {
    solve_once_diag_from_fact(diag,
                              MAELYS_DATALOG_SOLVE_DIAG_MALFORMED_FACT,
                              MAELYS_ERR_INVALID_STATE,
                              MAELYS_DATALOG_DENY_POLICY_LOAD_ERROR,
                              registry,
                              fact);
}

static void solve_once_diag_idb_overflow(maelys_datalog_solve_diagnostic_t *diag,
                                         const maelys_datalog_solve_result_t *result) {
    solve_once_diag_base(diag,
                         MAELYS_DATALOG_SOLVE_DIAG_IDB_OVERFLOW,
                         MAELYS_ERR_PAYLOAD_TOO_LARGE,
                         MAELYS_DATALOG_DENY_IDB_OVERFLOW);
    if (!diag || !result) return;
    diag->capacity = (uint16_t)MAELYS_DATALOG_MAX_IDB_FACTS;
    diag->count_observed = (uint16_t)result->idb_merge_end;
}

static void solve_once_diag_comparison(maelys_datalog_solve_diagnostic_t *diag,
                                       const maelys_datalog_solve_result_t *result) {
    solve_once_diag_base(diag,
                         MAELYS_DATALOG_SOLVE_DIAG_COMPARISON_TYPE_ERROR,
                         MAELYS_ERR_INVALID_FIELD,
                         MAELYS_DATALOG_DENY_COMPARISON_TYPE_ERROR);
    if (!diag || !result) return;
    diag->lhs_kind = result->runtime_diag.observed_lhs_kind;
    diag->rhs_kind = result->runtime_diag.observed_rhs_kind;
    diag->comparison_op = result->runtime_diag.failed_op;
    diag->term_index = result->runtime_diag.term_index;
}

static void solve_once_diag_failed_result(maelys_datalog_solve_diagnostic_t *diag,
                                          const maelys_datalog_solve_result_t *result,
                                          maelys_result_t rc) {
    if (!diag || !result) return;
    switch (result->failure_reason) {
        case MAELYS_DATALOG_DENY_COMPARISON_TYPE_ERROR:
            solve_once_diag_comparison(diag, result);
            return;
        case MAELYS_DATALOG_DENY_IDB_OVERFLOW:
            solve_once_diag_idb_overflow(diag, result);
            return;
        case MAELYS_DATALOG_DENY_POLICY_LOAD_ERROR:
            if (rc == MAELYS_ERR_INVALID_STATE) {
                solve_once_diag_base(diag,
                                     MAELYS_DATALOG_SOLVE_DIAG_MALFORMED_FACT,
                                     MAELYS_ERR_INVALID_STATE,
                                     MAELYS_DATALOG_DENY_POLICY_LOAD_ERROR);
                if (diag) {
                    diag->predicate_id = result->runtime_diag.predicate_id;
                    diag->arity_expected = result->runtime_diag.expected_arity;
                    diag->arity_observed = result->runtime_diag.observed_arity;
                    diag->term_index = result->runtime_diag.term_index;
                    diag->lhs_kind = result->runtime_diag.observed_lhs_kind;
                }
                return;
            }
            break;
        default:
            break;
    }
    if (rc == MAELYS_ERR_INVALID_STATE) {
        solve_once_diag_base(diag,
                             MAELYS_DATALOG_SOLVE_DIAG_INVALID_STATE,
                             rc,
                             result->failure_reason);
    } else if (rc == MAELYS_ERR_PAYLOAD_TOO_LARGE) {
        solve_once_diag_base(diag,
                             MAELYS_DATALOG_SOLVE_DIAG_IDB_OVERFLOW,
                             rc,
                             result->failure_reason);
    }
}

static int fact_matches_query(const maelys_datalog_fact_t *fact,
                              maelys_datalog_predicate_id_t pid,
                              const maelys_datalog_term_t *args,
                              size_t arity) {
    if (!fact || fact->predicate_id != pid || fact->arity != arity) return 0;
    for (size_t i = 0; i < arity; i++) {
        /* Defensive: callers already require ground query terms, but this helper
         * re-checks to keep matching safe if reused internally. */
        if (args[i].kind == MAELYS_DATALOG_TERM_VAR) return 0;
        if (!maelys_datalog_term_equal(&fact->terms[i], &args[i])) return 0;
    }
    return 1;
}

static int solve_once_fact_in_slice(const maelys_datalog_fact_t *facts,
                                    size_t count,
                                    const maelys_datalog_fact_t *fact) {
    if (!facts || !fact) return 0;
    for (size_t i = 0; i < count; i++) {
        if (maelys_datalog_fact_equals(&facts[i], fact)) return 1;
    }
    return 0;
}

static int solve_once_fact_in_range(const maelys_datalog_fact_t *facts,
                                    size_t begin,
                                    size_t end,
                                    const maelys_datalog_fact_t *fact) {
    if (!facts || !fact || end < begin) return 0;
    for (size_t i = begin; i < end; i++) {
        if (maelys_datalog_fact_equals(&facts[i], fact)) return 1;
    }
    return 0;
}

static void solve_once_swap_idb_fact_with_proof(maelys_datalog_solve_result_t *result,
                                                size_t a,
                                                size_t b) {
    if (!result || a == b) return;
    maelys_datalog_fact_t fact_tmp = result->idb_facts[a];
    result->idb_facts[a] = result->idb_facts[b];
    result->idb_facts[b] = fact_tmp;
    uint16_t proof_tmp = result->idb_proof_index[a];
    result->idb_proof_index[a] = result->idb_proof_index[b];
    result->idb_proof_index[b] = proof_tmp;
}

static void sort_idb_slice_with_proof(maelys_datalog_solve_result_t *result,
                                      size_t begin,
                                      size_t end) {
    if (!result || end <= begin + 1u) return;
    for (size_t i = begin + 1u; i < end; i++) {
        size_t j = i;
        while (j > begin &&
               maelys_datalog_fact_cmp(&result->idb_facts[j - 1u],
                                       &result->idb_facts[j]) > 0) {
            solve_once_swap_idb_fact_with_proof(result, j - 1u, j);
            j--;
        }
    }
}

static size_t dedup_idb_slice_with_proof(maelys_datalog_solve_result_t *result,
                                         size_t begin,
                                         size_t end) {
    if (!result || end <= begin + 1u) return end;
    size_t out = begin + 1u;
    for (size_t i = begin + 1u; i < end; i++) {
        if (maelys_datalog_fact_equals(&result->idb_facts[out - 1u],
                                       &result->idb_facts[i])) {
            continue;
        }
        if (out != i) {
            result->idb_facts[out] = result->idb_facts[i];
            result->idb_proof_index[out] = result->idb_proof_index[i];
        }
        out++;
    }
    for (size_t i = out; i < end; i++) {
        memset(&result->idb_facts[i], 0, sizeof(result->idb_facts[i]));
        result->idb_proof_index[i] = MAELYS_DATALOG_PROOF_NO_PARENT;
    }
    return out;
}

static int solve_once_bind_or_match(solve_once_bindings_t *bindings,
                                    const maelys_datalog_term_t *pattern,
                                    const maelys_datalog_term_t *value) {
    if (pattern->kind != MAELYS_DATALOG_TERM_VAR) return maelys_datalog_term_equal(pattern, value);
    unsigned variable = pattern->as.variable;
    if (variable >= MAELYS_DATALOG_MAX_RULE_VARIABLES) return 0;
    if (!bindings->bound[variable]) {
        bindings->bound[variable] = 1;
        bindings->value[variable] = *value;
        return 1;
    }
    return maelys_datalog_term_equal(&bindings->value[variable], value);
}

static int solve_once_instantiate_term(const solve_once_bindings_t *bindings,
                                       const maelys_datalog_term_t *src,
                                       maelys_datalog_term_t *dst) {
    if (src->kind != MAELYS_DATALOG_TERM_VAR) {
        *dst = *src;
        return 1;
    }
    unsigned variable = src->as.variable;
    if (variable >= MAELYS_DATALOG_MAX_RULE_VARIABLES || !bindings->bound[variable]) return 0;
    *dst = bindings->value[variable];
    return 1;
}

static int datalog_term_kind_known(maelys_datalog_term_kind_t kind) {
    switch (kind) {
        case MAELYS_DATALOG_TERM_SYMBOL:
        case MAELYS_DATALOG_TERM_INT:
        case MAELYS_DATALOG_TERM_BOOL:
        case MAELYS_DATALOG_TERM_VAR:
            return 1;
        default:
            return 0;
    }
}

static int datalog_fact_structurally_valid(const maelys_datalog_predicate_registry_t *registry,
                                           const maelys_datalog_fact_t *fact) {
    if (!registry || !fact || fact->arity > MAELYS_DATALOG_MAX_TERMS) return 0;
    const maelys_datalog_predicate_def_t *def =
        maelys_datalog_predicate_registry_get(registry, fact->predicate_id);
    if (!def || fact->arity != def->arity) return 0;
    for (size_t i = 0; i < fact->arity; i++) {
        if (!datalog_term_kind_known(fact->terms[i].kind)) return 0;
        if (fact->terms[i].kind == MAELYS_DATALOG_TERM_VAR) return 0;
    }
    return 1;
}

static int datalog_fact_set_structurally_valid(const maelys_datalog_predicate_registry_t *registry,
                                               const maelys_datalog_fact_set_t *set) {
    if (!set || (!set->facts && set->count > 0) || set->count > set->capacity) return 0;
    for (size_t i = 0; i < set->count; i++) {
        if (!datalog_fact_structurally_valid(registry, &set->facts[i])) return 0;
    }
    return 1;
}

static int datalog_fact_slice_structurally_valid(const maelys_datalog_predicate_registry_t *registry,
                                                 const maelys_datalog_fact_t *facts,
                                                 size_t count) {
    if (!facts && count > 0) return 0;
    for (size_t i = 0; i < count; i++) {
        if (!datalog_fact_structurally_valid(registry, &facts[i])) return 0;
    }
    return 1;
}

static void solve_once_set_comparison_failure(maelys_datalog_solve_result_t *result,
                                              maelys_datalog_compare_result_t compare_result,
                                              const maelys_datalog_term_t *lhs,
                                              maelys_datalog_cmp_op_t op,
                                              const maelys_datalog_term_t *rhs) {
    if (!result || result->failure_error != MAELYS_OK) return;
    result->failure_error = MAELYS_ERR_INVALID_FIELD;
    result->failure_reason = MAELYS_DATALOG_DENY_COMPARISON_TYPE_ERROR;
    maelys_datalog_diagnostic_set_comparison_error(&result->runtime_diag,
                                                   (uint8_t)compare_result,
                                                   (uint8_t)MAELYS_DATALOG_TERM_INT,
                                                   lhs ? (uint8_t)lhs->kind : 0u,
                                                   rhs ? (uint8_t)rhs->kind : 0u,
                                                   (uint8_t)op,
                                                   0u);
}

static void solve_once_set_join_order_failure(maelys_datalog_solve_result_t *result,
                                              maelys_result_t plan_rc) {
    if (!result || result->failure_error != MAELYS_OK) return;
    if (plan_rc == MAELYS_ERR_INVALID_FIELD) {
        result->failure_error = MAELYS_ERR_INVALID_FIELD;
        result->failure_reason = MAELYS_DATALOG_DENY_COMPARISON_TYPE_ERROR;
        maelys_datalog_diagnostic_set_comparison_error(&result->runtime_diag,
                                                       (uint8_t)MAELYS_DATALOG_COMPARE_UNBOUND_VARIABLE,
                                                       (uint8_t)MAELYS_DATALOG_TERM_INT,
                                                       (uint8_t)MAELYS_DATALOG_TERM_VAR,
                                                       (uint8_t)MAELYS_DATALOG_TERM_VAR,
                                                       0u,
                                                       0u);
        return;
    }
    result->failure_error = plan_rc != MAELYS_OK ? plan_rc : MAELYS_ERR_INVALID_STATE;
    result->failure_reason = MAELYS_DATALOG_DENY_POLICY_LOAD_ERROR;
}

static void solve_once_set_invalid_state(maelys_datalog_solve_result_t *result) {
    if (!result || result->failure_error != MAELYS_OK) return;
    result->failure_error = MAELYS_ERR_INVALID_STATE;
    result->failure_reason = MAELYS_DATALOG_DENY_POLICY_LOAD_ERROR;
}

static void solve_once_set_invalid_fact(maelys_datalog_solve_result_t *result,
                                        const maelys_datalog_predicate_registry_t *registry,
                                        const maelys_datalog_fact_t *fact) {
    solve_once_set_invalid_state(result);
    if (!result || !fact) return;
    result->runtime_diag.predicate_id = fact->predicate_id;
    result->runtime_diag.observed_arity = fact->arity;
    if (registry) {
        const maelys_datalog_predicate_def_t *def =
            maelys_datalog_predicate_registry_get(registry, fact->predicate_id);
        if (def) result->runtime_diag.expected_arity = def->arity;
    }
    for (size_t i = 0; i < fact->arity && i < MAELYS_DATALOG_MAX_TERMS; i++) {
        if (!datalog_term_kind_known(fact->terms[i].kind) ||
            fact->terms[i].kind == MAELYS_DATALOG_TERM_VAR) {
            result->runtime_diag.term_index = (uint8_t)i;
            result->runtime_diag.observed_lhs_kind = (uint8_t)fact->terms[i].kind;
            break;
        }
    }
}

static maelys_datalog_compare_result_t solve_once_instantiate_comparison_term(
    const solve_once_bindings_t *bindings,
    const maelys_datalog_term_t *src,
    maelys_datalog_term_t *dst) {
    if (!src || !dst || !datalog_term_kind_known(src->kind)) {
        return MAELYS_DATALOG_COMPARE_UNKNOWN_TERM_KIND;
    }
    if (src->kind != MAELYS_DATALOG_TERM_VAR) {
        *dst = *src;
        return MAELYS_DATALOG_COMPARE_TRUE;
    }
    unsigned variable = src->as.variable;
    if (!bindings || variable >= MAELYS_DATALOG_MAX_RULE_VARIABLES || !bindings->bound[variable]) {
        return MAELYS_DATALOG_COMPARE_UNBOUND_VARIABLE;
    }
    *dst = bindings->value[variable];
    if (!datalog_term_kind_known(dst->kind)) return MAELYS_DATALOG_COMPARE_UNKNOWN_TERM_KIND;
    if (dst->kind == MAELYS_DATALOG_TERM_VAR) return MAELYS_DATALOG_COMPARE_UNBOUND_VARIABLE;
    return MAELYS_DATALOG_COMPARE_TRUE;
}

static maelys_datalog_compare_result_t solve_once_evaluate_comparison(const maelys_datalog_term_t *lhs,
                                                                      maelys_datalog_cmp_op_t op,
                                                                      const maelys_datalog_term_t *rhs) {
    if (!lhs || !rhs) return MAELYS_DATALOG_COMPARE_UNKNOWN_TERM_KIND;
    if (!datalog_term_kind_known(lhs->kind) || !datalog_term_kind_known(rhs->kind)) {
        return MAELYS_DATALOG_COMPARE_UNKNOWN_TERM_KIND;
    }
    if (lhs->kind == MAELYS_DATALOG_TERM_VAR || rhs->kind == MAELYS_DATALOG_TERM_VAR) {
        return MAELYS_DATALOG_COMPARE_UNBOUND_VARIABLE;
    }
    switch (op) {
        case MAELYS_DATALOG_CMP_EQ:
            return maelys_datalog_term_equal(lhs, rhs)
                ? MAELYS_DATALOG_COMPARE_TRUE
                : MAELYS_DATALOG_COMPARE_FALSE;
        case MAELYS_DATALOG_CMP_NEQ:
            return maelys_datalog_term_equal(lhs, rhs)
                ? MAELYS_DATALOG_COMPARE_FALSE
                : MAELYS_DATALOG_COMPARE_TRUE;
        case MAELYS_DATALOG_CMP_LT:
        case MAELYS_DATALOG_CMP_LTE:
        case MAELYS_DATALOG_CMP_GT:
        case MAELYS_DATALOG_CMP_GTE:
            if (lhs->kind != rhs->kind) return MAELYS_DATALOG_COMPARE_INVALID_KIND;
            if (lhs->kind != MAELYS_DATALOG_TERM_INT) return MAELYS_DATALOG_COMPARE_INVALID_ORDINAL_TYPE;
            switch (op) {
                case MAELYS_DATALOG_CMP_LT:
                    return lhs->as.integer < rhs->as.integer
                        ? MAELYS_DATALOG_COMPARE_TRUE
                        : MAELYS_DATALOG_COMPARE_FALSE;
                case MAELYS_DATALOG_CMP_LTE:
                    return lhs->as.integer <= rhs->as.integer
                        ? MAELYS_DATALOG_COMPARE_TRUE
                        : MAELYS_DATALOG_COMPARE_FALSE;
                case MAELYS_DATALOG_CMP_GT:
                    return lhs->as.integer > rhs->as.integer
                        ? MAELYS_DATALOG_COMPARE_TRUE
                        : MAELYS_DATALOG_COMPARE_FALSE;
                case MAELYS_DATALOG_CMP_GTE:
                    return lhs->as.integer >= rhs->as.integer
                        ? MAELYS_DATALOG_COMPARE_TRUE
                        : MAELYS_DATALOG_COMPARE_FALSE;
                default:
                    return MAELYS_DATALOG_COMPARE_UNKNOWN_OPERATOR;
            }
        default:
            return MAELYS_DATALOG_COMPARE_UNKNOWN_OPERATOR;
    }
}

static int solve_once_fact_in_base(const maelys_datalog_solve_result_t *result,
                                   const maelys_datalog_fact_t *fact) {
    if (!result || !fact) return 0;
    if (result->ruleset && solve_once_fact_in_slice(result->ruleset->facts, result->ruleset->fact_count, fact)) {
        return 1;
    }
    return solve_once_fact_in_slice(result->edb_snapshot.facts, result->edb_snapshot.count, fact);
}

static int solve_once_append_idb_merge(maelys_datalog_solve_result_t *result,
                                       const maelys_datalog_fact_t *fact,
                                       size_t rule_id,
                                       size_t depth,
                                       uint16_t parent_proof_index) {
    solve_once_assert_windows(result);
    if (!datalog_fact_structurally_valid(&result->ruleset->registry, fact)) {
        solve_once_set_invalid_fact(result, &result->ruleset->registry, fact);
        return 0;
    }
    if (solve_once_fact_in_base(result, fact)) return 1;
    /* [0, idb_current_end) includes base/current/delta accepted facts.
     * [idb_current_end, idb_merge_end) is the current merge window.
     * idb_delta is included in the accepted current prefix, so a separate
     * delta-range duplicate check is redundant. */
    if (solve_once_fact_in_slice(result->idb_facts, result->idb_current_end, fact)) return 1;
    if (solve_once_fact_in_range(result->idb_facts, result->idb_current_end, result->idb_merge_end, fact)) return 1;
    if (result->idb_merge_end >= result->idb_final.capacity ||
        result->idb_merge_end >= MAELYS_DATALOG_MAX_IDB_FACTS) {
        result->failure_reason = MAELYS_DATALOG_DENY_IDB_OVERFLOW;
        return 0;
    }
    if (fact->predicate_id >= MAELYS_DATALOG_MAX_PREDICATES) {
        result->failure_reason = MAELYS_DATALOG_DENY_IDB_OVERFLOW;
        return 0;
    }
    if (result->facts_per_pred[fact->predicate_id] >= MAELYS_DATALOG_MAX_FACTS_PER_PRED) {
        result->failure_reason = MAELYS_DATALOG_DENY_IDB_OVERFLOW;
        return 0;
    }
    const size_t insert_index = result->idb_merge_end;
    result->idb_facts[result->idb_merge_end++] = *fact;
    result->idb_final.count = result->idb_merge_end;
    result->idb_final.sorted = 0;
    result->facts_per_pred[fact->predicate_id]++;
    if (parent_proof_index != MAELYS_DATALOG_PROOF_NO_PARENT &&
        parent_proof_index >= result->proof.node_count) {
        parent_proof_index = MAELYS_DATALOG_PROOF_NO_PARENT;
    }
    uint16_t proof_node_idx = MAELYS_DATALOG_PROOF_NO_PARENT;
    if (result->proof.node_count < MAELYS_DATALOG_MAX_PROOF_NODES &&
        depth < MAELYS_DATALOG_MAX_PROOF_DEPTH) {
        proof_node_idx = (uint16_t)result->proof.node_count;
    }
    maelys_datalog_proof_add(&result->proof,
                              rule_id,
                              fact->predicate_id,
                              fact,
                              MAELYS_DATALOG_DENY_NONE,
                              depth,
                              parent_proof_index);
    result->idb_proof_index[insert_index] = proof_node_idx;
    solve_once_assert_windows(result);
    return 1;
}

static int solve_once_literal_delta_eligible(const maelys_datalog_ruleset_t *ruleset,
                                             const maelys_datalog_literal_t *literal) {
    if (!ruleset || !literal || literal->kind != MAELYS_DATALOG_LITERAL_ATOM) return 0;
    const maelys_datalog_predicate_def_t *def =
        maelys_datalog_predicate_registry_get(&ruleset->registry, literal->atom.predicate_id);
    return def && (def->kind_flags & MAELYS_DATALOG_PRED_KIND_IDB);
}

static uint64_t literal_var_mask(const maelys_datalog_literal_t *literal) {
    uint64_t mask = 0;
    if (!literal || literal->kind != MAELYS_DATALOG_LITERAL_ATOM) return mask;
    for (uint8_t i = 0; i < literal->atom.arity; i++) {
        const maelys_datalog_term_t *term = &literal->atom.terms[i];
        if (term->kind == MAELYS_DATALOG_TERM_VAR && term->as.variable < 64u) {
            mask |= (uint64_t)1u << term->as.variable;
        }
    }
    return mask;
}

static int comparison_term_safe_with_bound_vars(const maelys_datalog_term_t *term,
                                                uint64_t bound_var_mask) {
    if (!term) return 0;
    if (term->kind != MAELYS_DATALOG_TERM_VAR) return 1;
    if (term->as.variable >= 64u) return 0;
    return (bound_var_mask & ((uint64_t)1u << term->as.variable)) != 0;
}

static int literal_safe_with_bound_vars(const maelys_datalog_rule_t *rule,
                                        uint8_t index,
                                        uint64_t bound_var_mask) {
    if (!rule || index >= rule->body_count) return 0;
    const maelys_datalog_literal_t *literal = &rule->body[index];
    if (literal->kind == MAELYS_DATALOG_LITERAL_ATOM) return 1;
    if (literal->kind == MAELYS_DATALOG_LITERAL_NEGATED_ATOM) {
        for (uint8_t i = 0; i < literal->atom.arity; i++) {
            const maelys_datalog_term_t *term = &literal->atom.terms[i];
            if (term->kind != MAELYS_DATALOG_TERM_VAR) continue;
            if (!comparison_term_safe_with_bound_vars(term,
                                                      bound_var_mask)) {
                return 0;
            }
        }
        return 1;
    }
    if (literal->kind != MAELYS_DATALOG_LITERAL_COMPARISON) return 0;
    return comparison_term_safe_with_bound_vars(&literal->lhs, bound_var_mask) &&
           comparison_term_safe_with_bound_vars(&literal->rhs, bound_var_mask);
}

static int64_t literal_static_score(const maelys_datalog_ruleset_t *ruleset,
                                    const maelys_datalog_rule_t *rule,
                                    uint8_t index,
                                    uint64_t bound_var_mask) {
    if (!rule || index >= rule->body_count) return 0;
    const maelys_datalog_literal_t *literal = &rule->body[index];
    if (literal->kind == MAELYS_DATALOG_LITERAL_COMPARISON ||
        literal->kind == MAELYS_DATALOG_LITERAL_NEGATED_ATOM) {
        return 0;
    }
    int64_t score = 100;
    for (uint8_t i = 0; i < literal->atom.arity; i++) {
        const maelys_datalog_term_t *term = &literal->atom.terms[i];
        if (term->kind == MAELYS_DATALOG_TERM_VAR) {
            if (term->as.variable < 64u &&
                (bound_var_mask & ((uint64_t)1u << term->as.variable))) {
                score += 20;
            }
        } else {
            score += 10;
        }
    }
    if (ruleset) {
        const maelys_datalog_predicate_def_t *def =
            maelys_datalog_predicate_registry_get(&ruleset->registry, literal->atom.predicate_id);
        if (def && (def->kind_flags & MAELYS_DATALOG_PRED_KIND_IDB)) score -= 50;
    }
    return score;
}

static maelys_result_t build_static_join_order(
    const maelys_datalog_ruleset_t *ruleset,
    const maelys_datalog_rule_t *rule,
    int delta_body_index,
    uint8_t out_order[MAELYS_DATALOG_MAX_BODY_LITERALS],
    uint8_t *out_count) {
    if (!ruleset || !rule || !out_order || !out_count) return MAELYS_ERR_INVALID_ARGUMENT;
    if (rule->body_count > MAELYS_DATALOG_MAX_BODY_LITERALS ||
        rule->body_count > 64u) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }

    uint64_t planned_mask = 0;
    uint64_t bound_var_mask = 0;
    uint8_t pos = 0;
    *out_count = 0;

    if (delta_body_index >= 0) {
        if ((size_t)delta_body_index >= rule->body_count) return MAELYS_ERR_INVALID_ARGUMENT;
        const uint8_t delta_index = (uint8_t)delta_body_index;
        const maelys_datalog_literal_t *delta_literal = &rule->body[delta_index];
        if (delta_literal->kind != MAELYS_DATALOG_LITERAL_ATOM) return MAELYS_ERR_INVALID_ARGUMENT;
        const maelys_datalog_predicate_def_t *def =
            maelys_datalog_predicate_registry_get(&ruleset->registry, delta_literal->atom.predicate_id);
        if (!def || !(def->kind_flags & MAELYS_DATALOG_PRED_KIND_IDB)) {
            return MAELYS_ERR_INVALID_ARGUMENT;
        }
        out_order[pos++] = delta_index;
        planned_mask |= (uint64_t)1u << delta_index;
        bound_var_mask |= literal_var_mask(delta_literal);
    }

    while (pos < rule->body_count) {
        int best = -1;
        int64_t best_score = 0;
        for (uint8_t i = 0; i < rule->body_count; i++) {
            if (planned_mask & ((uint64_t)1u << i)) continue;
            if (!literal_safe_with_bound_vars(rule, i, bound_var_mask)) continue;
            const int64_t score = literal_static_score(ruleset, rule, i, bound_var_mask);
            if (best < 0 || score > best_score ||
                (score == best_score && i < (uint8_t)best)) {
                best = (int)i;
                best_score = score;
            }
        }
        if (best < 0) return MAELYS_ERR_INVALID_FIELD;
        out_order[pos++] = (uint8_t)best;
        planned_mask |= (uint64_t)1u << (uint8_t)best;
        bound_var_mask |= literal_var_mask(&rule->body[best]);
    }

    *out_count = pos;
    return MAELYS_OK;
}

/* Recursion depth is bounded by MAELYS_DATALOG_MAX_BODY_LITERALS because each
 * recursive step advances to the next body literal.
 *
 * The effective call chain is derive_recursive -> scan_candidates ->
 * match_candidate -> derive_recursive, so stack depth is bounded by roughly
 * 2 * MAELYS_DATALOG_MAX_BODY_LITERALS frames.
 *
 * Each successful candidate path copies solve_once_bindings_t for implicit
 * backtracking. This is acceptable for the configured bounds and should be
 * revisited before embedded/small-stack targets. */
static int solve_once_derive_recursive(const maelys_datalog_ruleset_t *ruleset,
                                       maelys_datalog_solve_result_t *result,
                                       const maelys_datalog_rule_t *rule,
                                       size_t literal_index,
                                       solve_once_bindings_t *bindings,
                                       size_t delta_literal_index,
                                       size_t depth,
                                       uint16_t parent_proof_index);

static int solve_once_match_candidate(const maelys_datalog_ruleset_t *ruleset,
                                      maelys_datalog_solve_result_t *result,
                                      const maelys_datalog_rule_t *rule,
                                      size_t literal_index,
                                      solve_once_bindings_t *bindings,
                                      const maelys_datalog_fact_t *candidate,
                                      size_t delta_literal_index,
                                      size_t depth,
                                      uint16_t parent_proof_index) {
    /* Returns 0 on fatal internal overflow, 1 on match/skip.
     * Predicate mismatch is not an error: the candidate scan continues. */
    const maelys_datalog_literal_t *literal = &rule->body[literal_index];
    if (!datalog_fact_structurally_valid(&ruleset->registry, candidate)) {
        solve_once_set_invalid_fact(result, &ruleset->registry, candidate);
        return 0;
    }
    if (candidate->predicate_id != literal->atom.predicate_id || candidate->arity != literal->atom.arity) return 1;
    /* Copy bindings before attempting to unify this candidate. If unification
     * fails for any term, discard next and continue with the next candidate.
     * This is the local backtracking mechanism. */
    solve_once_bindings_t next = *bindings;
    for (size_t i = 0; i < candidate->arity; i++) {
        if (!solve_once_bind_or_match(&next, &literal->atom.terms[i], &candidate->terms[i])) return 1;
    }
    return solve_once_derive_recursive(ruleset,
                                       result,
                                       rule,
                                       literal_index + 1u,
                                       &next,
                                       delta_literal_index,
                                       depth,
                                       parent_proof_index);
}

static int solve_once_scan_candidates(const maelys_datalog_ruleset_t *ruleset,
                                      maelys_datalog_solve_result_t *result,
                                      const maelys_datalog_rule_t *rule,
                                      size_t literal_index,
                                      solve_once_bindings_t *bindings,
                                      const maelys_datalog_fact_t *facts,
                                      size_t fact_count,
                                      size_t delta_literal_index,
                                      size_t depth,
                                      const uint16_t *parent_proof_indices,
                                      uint16_t parent_proof_index) {
    /* Scans all candidates exhaustively.
     * Returns 0 only on fatal internal overflow; it does not stop on first match. */
    for (size_t i = 0; i < fact_count; i++) {
        const uint16_t candidate_parent = parent_proof_indices
            ? parent_proof_indices[i]
            : parent_proof_index;
        if (!solve_once_match_candidate(ruleset,
                                        result,
                                        rule,
                                        literal_index,
                                        bindings,
                                        &facts[i],
                                        delta_literal_index,
                                        depth,
                                        candidate_parent)) {
            return 0;
        }
    }
    return 1;
}

static int solve_once_idb_scan_window(const maelys_datalog_ruleset_t *ruleset,
                                      const maelys_datalog_solve_result_t *result,
                                      maelys_datalog_predicate_id_t predicate_id,
                                      size_t *out_begin,
                                      size_t *out_count) {
    if (!ruleset || !result || !out_begin || !out_count ||
        predicate_id >= MAELYS_DATALOG_MAX_PREDICATES) {
        return 0;
    }
    if (!result->stratified) {
        *out_begin = 0;
        *out_count = result->idb_current_end;
        return 1;
    }
    const uint32_t predicate_stratum = ruleset->strata[predicate_id];
    if (predicate_stratum > result->active_stratum ||
        predicate_stratum + 1u >= (MAELYS_DATALOG_MAX_STRATA + 1u)) {
        return 0;
    }
    if (predicate_stratum < result->active_stratum) {
        const size_t begin = result->stratum_idb_end[predicate_stratum];
        const size_t end = result->stratum_idb_end[predicate_stratum + 1u];
        if (end < begin || end > result->idb_current_end) return 0;
        *out_begin = begin;
        *out_count = end - begin;
        return 1;
    }
    const size_t begin = result->stratum_idb_end[result->active_stratum];
    if (begin > result->idb_current_end) return 0;
    *out_begin = begin;
    *out_count = result->idb_current_end - begin;
    return 1;
}

static int solve_negated_literal(maelys_datalog_solve_result_t *result,
                                 const maelys_datalog_ruleset_t *ruleset,
                                 const maelys_datalog_literal_t *literal,
                                 const solve_once_bindings_t *bindings) {
    if (!result || !ruleset || !literal ||
        literal->kind != MAELYS_DATALOG_LITERAL_NEGATED_ATOM ||
        literal->atom.predicate_id >= MAELYS_DATALOG_MAX_PREDICATES) {
        return 0;
    }
    maelys_datalog_fact_t query;
    memset(&query, 0, sizeof(query));
    query.predicate_id = literal->atom.predicate_id;
    query.arity = literal->atom.arity;
    for (size_t i = 0; i < query.arity; i++) {
        if (!solve_once_instantiate_term(bindings,
                                         &literal->atom.terms[i],
                                         &query.terms[i])) {
            return 0;
        }
    }
    if (!datalog_fact_structurally_valid(&ruleset->registry, &query)) return 0;

    const maelys_datalog_predicate_def_t *def =
        maelys_datalog_predicate_registry_get(&ruleset->registry, query.predicate_id);
    if (!def) return 0;

    if (def->kind_flags & MAELYS_DATALOG_PRED_KIND_EDB) {
        return !maelys_datalog_fact_set_contains(&result->edb_snapshot, &query);
    }
    if (def->kind_flags & MAELYS_DATALOG_PRED_KIND_POLICY_FACT) {
        return !solve_once_fact_in_slice(ruleset->facts, ruleset->fact_count, &query);
    }
    if (def->kind_flags & MAELYS_DATALOG_PRED_KIND_IDB) {
        if (!result->stratified) return 0;
        const uint32_t predicate_stratum = ruleset->strata[query.predicate_id];
        if (predicate_stratum >= result->active_stratum ||
            predicate_stratum + 1u >= (MAELYS_DATALOG_MAX_STRATA + 1u)) {
            return 0;
        }
        const size_t begin = result->stratum_idb_end[predicate_stratum];
        const size_t end = result->stratum_idb_end[predicate_stratum + 1u];
        if (end < begin || end > result->idb_current_end) return 0;
        maelys_datalog_fact_set_t frozen_view;
        maelys_datalog_fact_set_init(&frozen_view,
                                     &result->idb_facts[begin],
                                     end - begin);
        frozen_view.count = end - begin;
        frozen_view.sorted = 1;
        return !maelys_datalog_fact_set_contains(&frozen_view, &query);
    }
    return 0;
}

static int solve_once_derive_recursive(const maelys_datalog_ruleset_t *ruleset,
                                       maelys_datalog_solve_result_t *result,
                                       const maelys_datalog_rule_t *rule,
                                       size_t literal_index,
                                       solve_once_bindings_t *bindings,
                                       size_t delta_literal_index,
                                       size_t depth,
                                       uint16_t parent_proof_index) {
    if (literal_index == rule->body_count) {
        maelys_datalog_fact_t fact;
        memset(&fact, 0, sizeof(fact));
        fact.predicate_id = rule->head.predicate_id;
        fact.arity = rule->head.arity;
        for (size_t i = 0; i < fact.arity; i++) {
            if (!solve_once_instantiate_term(bindings, &rule->head.terms[i], &fact.terms[i])) return 1;
        }
        return solve_once_append_idb_merge(result, &fact, rule->rule_id, depth, parent_proof_index);
    }

    const maelys_datalog_literal_t *literal = &rule->body[literal_index];
    if (literal->kind == MAELYS_DATALOG_LITERAL_COMPARISON) {
        maelys_datalog_term_t lhs;
        maelys_datalog_term_t rhs;
        maelys_datalog_compare_result_t lhs_rc =
            solve_once_instantiate_comparison_term(bindings, &literal->lhs, &lhs);
        if (lhs_rc != MAELYS_DATALOG_COMPARE_TRUE) {
            solve_once_set_comparison_failure(result, lhs_rc, &literal->lhs, literal->op, &literal->rhs);
            return 0;
        }
        maelys_datalog_compare_result_t rhs_rc =
            solve_once_instantiate_comparison_term(bindings, &literal->rhs, &rhs);
        if (rhs_rc != MAELYS_DATALOG_COMPARE_TRUE) {
            solve_once_set_comparison_failure(result, rhs_rc, &lhs, literal->op, &literal->rhs);
            return 0;
        }
        maelys_datalog_compare_result_t cmp_rc = solve_once_evaluate_comparison(&lhs, literal->op, &rhs);
        if (cmp_rc == MAELYS_DATALOG_COMPARE_FALSE) {
            return 1;
        }
        if (cmp_rc != MAELYS_DATALOG_COMPARE_TRUE) {
            solve_once_set_comparison_failure(result, cmp_rc, &lhs, literal->op, &rhs);
            return 0;
        }
        return solve_once_derive_recursive(ruleset,
                                           result,
                                           rule,
                                           literal_index + 1u,
                                           bindings,
                                           delta_literal_index,
                                           depth,
                                           parent_proof_index);
    }
    if (literal->kind == MAELYS_DATALOG_LITERAL_NEGATED_ATOM) {
        if (!solve_negated_literal(result, ruleset, literal, bindings)) return 1;
        return solve_once_derive_recursive(ruleset,
                                           result,
                                           rule,
                                           literal_index + 1u,
                                           bindings,
                                           delta_literal_index,
                                           depth,
                                           parent_proof_index);
    }

    const maelys_datalog_predicate_def_t *def =
        maelys_datalog_predicate_registry_get(&ruleset->registry, literal->atom.predicate_id);
    if (!def) return 1;

    if (literal_index == delta_literal_index) {
        return solve_once_scan_candidates(ruleset,
                                          result,
                                          rule,
                                          literal_index,
                                          bindings,
                                          &result->idb_facts[result->idb_delta_begin],
                                          result->idb_delta_end - result->idb_delta_begin,
                                          delta_literal_index,
                                          depth,
                                          &result->idb_proof_index[result->idb_delta_begin],
                                          parent_proof_index);
    }

    if ((def->kind_flags & MAELYS_DATALOG_PRED_KIND_POLICY_FACT) &&
        !solve_once_scan_candidates(ruleset,
                                    result,
                                    rule,
                                    literal_index,
                                    bindings,
                                    ruleset->facts,
                                    ruleset->fact_count,
                                    delta_literal_index,
                                    depth,
                                    NULL,
                                    parent_proof_index)) {
        return 0;
    }
    if ((def->kind_flags & MAELYS_DATALOG_PRED_KIND_EDB) &&
        !solve_once_scan_candidates(ruleset,
                                    result,
                                    rule,
                                    literal_index,
                                    bindings,
                                    result->edb_snapshot.facts,
                                    result->edb_snapshot.count,
                                    delta_literal_index,
                                    depth,
                                    NULL,
                                    parent_proof_index)) {
        return 0;
    }
    if (def->kind_flags & MAELYS_DATALOG_PRED_KIND_IDB) {
        size_t begin = 0;
        size_t count = 0;
        if (!solve_once_idb_scan_window(ruleset, result, literal->atom.predicate_id, &begin, &count)) {
            solve_once_set_invalid_state(result);
            return 0;
        }
        if (!solve_once_scan_candidates(ruleset,
                                        result,
                                        rule,
                                        literal_index,
                                        bindings,
                                        &result->idb_facts[begin],
                                        count,
                                        delta_literal_index,
                                        depth,
                                        NULL,
                                        parent_proof_index)) {
            return 0;
        }
    }
    return 1;
}

static int solve_once_derive_ordered(const maelys_datalog_ruleset_t *ruleset,
                                     maelys_datalog_solve_result_t *result,
                                     const maelys_datalog_rule_t *rule,
                                     uint8_t order_pos,
                                     const uint8_t *join_order,
                                     uint8_t join_order_count,
                                     solve_once_bindings_t *bindings,
                                     size_t delta_literal_index,
                                     size_t depth,
                                     uint16_t parent_proof_index);

static int solve_once_match_candidate_ordered(const maelys_datalog_ruleset_t *ruleset,
                                              maelys_datalog_solve_result_t *result,
                                              const maelys_datalog_rule_t *rule,
                                              uint8_t order_pos,
                                              const uint8_t *join_order,
                                              uint8_t join_order_count,
                                              solve_once_bindings_t *bindings,
                                              const maelys_datalog_fact_t *candidate,
                                              size_t delta_literal_index,
                                              size_t depth,
                                              uint16_t parent_proof_index) {
    if (!rule || !join_order || order_pos >= join_order_count) return 0;
    const size_t literal_index = join_order[order_pos];
    if (literal_index >= rule->body_count) return 0;
    const maelys_datalog_literal_t *literal = &rule->body[literal_index];
    if (!datalog_fact_structurally_valid(&ruleset->registry, candidate)) {
        solve_once_set_invalid_fact(result, &ruleset->registry, candidate);
        return 0;
    }
    if (candidate->predicate_id != literal->atom.predicate_id || candidate->arity != literal->atom.arity) return 1;
    solve_once_bindings_t next = *bindings;
    for (size_t i = 0; i < candidate->arity; i++) {
        if (!solve_once_bind_or_match(&next, &literal->atom.terms[i], &candidate->terms[i])) return 1;
    }
    return solve_once_derive_ordered(ruleset,
                                     result,
                                     rule,
                                     (uint8_t)(order_pos + 1u),
                                     join_order,
                                     join_order_count,
                                     &next,
                                     delta_literal_index,
                                     depth,
                                     parent_proof_index);
}

static int solve_once_scan_candidates_ordered(const maelys_datalog_ruleset_t *ruleset,
                                              maelys_datalog_solve_result_t *result,
                                              const maelys_datalog_rule_t *rule,
                                              uint8_t order_pos,
                                              const uint8_t *join_order,
                                              uint8_t join_order_count,
                                              solve_once_bindings_t *bindings,
                                              const maelys_datalog_fact_t *facts,
                                              size_t fact_count,
                                              size_t delta_literal_index,
                                              size_t depth,
                                              const uint16_t *parent_proof_indices,
                                              uint16_t parent_proof_index) {
    for (size_t i = 0; i < fact_count; i++) {
        const uint16_t candidate_parent = parent_proof_indices
            ? parent_proof_indices[i]
            : parent_proof_index;
        if (!solve_once_match_candidate_ordered(ruleset,
                                                result,
                                                rule,
                                                order_pos,
                                                join_order,
                                                join_order_count,
                                                bindings,
                                                &facts[i],
                                                delta_literal_index,
                                                depth,
                                                candidate_parent)) {
            return 0;
        }
    }
    return 1;
}

static int solve_once_derive_ordered(const maelys_datalog_ruleset_t *ruleset,
                                     maelys_datalog_solve_result_t *result,
                                     const maelys_datalog_rule_t *rule,
                                     uint8_t order_pos,
                                     const uint8_t *join_order,
                                     uint8_t join_order_count,
                                     solve_once_bindings_t *bindings,
                                     size_t delta_literal_index,
                                     size_t depth,
                                     uint16_t parent_proof_index) {
    if (order_pos == join_order_count) {
        maelys_datalog_fact_t fact;
        memset(&fact, 0, sizeof(fact));
        fact.predicate_id = rule->head.predicate_id;
        fact.arity = rule->head.arity;
        for (size_t i = 0; i < fact.arity; i++) {
            if (!solve_once_instantiate_term(bindings, &rule->head.terms[i], &fact.terms[i])) return 1;
        }
        return solve_once_append_idb_merge(result, &fact, rule->rule_id, depth, parent_proof_index);
    }

    if (!join_order || order_pos >= join_order_count) return 0;
    const size_t literal_index = join_order[order_pos];
    if (literal_index >= rule->body_count) return 0;
    const maelys_datalog_literal_t *literal = &rule->body[literal_index];
    if (literal->kind == MAELYS_DATALOG_LITERAL_COMPARISON) {
        maelys_datalog_term_t lhs;
        maelys_datalog_term_t rhs;
        maelys_datalog_compare_result_t lhs_rc =
            solve_once_instantiate_comparison_term(bindings, &literal->lhs, &lhs);
        if (lhs_rc != MAELYS_DATALOG_COMPARE_TRUE) {
            solve_once_set_comparison_failure(result, lhs_rc, &literal->lhs, literal->op, &literal->rhs);
            return 0;
        }
        maelys_datalog_compare_result_t rhs_rc =
            solve_once_instantiate_comparison_term(bindings, &literal->rhs, &rhs);
        if (rhs_rc != MAELYS_DATALOG_COMPARE_TRUE) {
            solve_once_set_comparison_failure(result, rhs_rc, &lhs, literal->op, &literal->rhs);
            return 0;
        }
        maelys_datalog_compare_result_t cmp_rc = solve_once_evaluate_comparison(&lhs, literal->op, &rhs);
        if (cmp_rc == MAELYS_DATALOG_COMPARE_FALSE) {
            return 1;
        }
        if (cmp_rc != MAELYS_DATALOG_COMPARE_TRUE) {
            solve_once_set_comparison_failure(result, cmp_rc, &lhs, literal->op, &rhs);
            return 0;
        }
        return solve_once_derive_ordered(ruleset,
                                         result,
                                         rule,
                                         (uint8_t)(order_pos + 1u),
                                         join_order,
                                         join_order_count,
                                         bindings,
                                         delta_literal_index,
                                         depth,
                                         parent_proof_index);
    }
    if (literal->kind == MAELYS_DATALOG_LITERAL_NEGATED_ATOM) {
        if (!solve_negated_literal(result, ruleset, literal, bindings)) return 1;
        return solve_once_derive_ordered(ruleset,
                                         result,
                                         rule,
                                         (uint8_t)(order_pos + 1u),
                                         join_order,
                                         join_order_count,
                                         bindings,
                                         delta_literal_index,
                                         depth,
                                         parent_proof_index);
    }

    const maelys_datalog_predicate_def_t *def =
        maelys_datalog_predicate_registry_get(&ruleset->registry, literal->atom.predicate_id);
    if (!def) return 1;

    if (literal_index == delta_literal_index) {
        return solve_once_scan_candidates_ordered(ruleset,
                                                  result,
                                                  rule,
                                                  order_pos,
                                                  join_order,
                                                  join_order_count,
                                                  bindings,
                                                  &result->idb_facts[result->idb_delta_begin],
                                                  result->idb_delta_end - result->idb_delta_begin,
                                                  delta_literal_index,
                                                  depth,
                                                  &result->idb_proof_index[result->idb_delta_begin],
                                                  parent_proof_index);
    }

    if ((def->kind_flags & MAELYS_DATALOG_PRED_KIND_POLICY_FACT) &&
        !solve_once_scan_candidates_ordered(ruleset,
                                            result,
                                            rule,
                                            order_pos,
                                            join_order,
                                            join_order_count,
                                            bindings,
                                            ruleset->facts,
                                            ruleset->fact_count,
                                            delta_literal_index,
                                            depth,
                                            NULL,
                                            parent_proof_index)) {
        return 0;
    }
    if ((def->kind_flags & MAELYS_DATALOG_PRED_KIND_EDB) &&
        !solve_once_scan_candidates_ordered(ruleset,
                                            result,
                                            rule,
                                            order_pos,
                                            join_order,
                                            join_order_count,
                                            bindings,
                                            result->edb_snapshot.facts,
                                            result->edb_snapshot.count,
                                            delta_literal_index,
                                            depth,
                                            NULL,
                                            parent_proof_index)) {
        return 0;
    }
    if (def->kind_flags & MAELYS_DATALOG_PRED_KIND_IDB) {
        size_t begin = 0;
        size_t count = 0;
        if (!solve_once_idb_scan_window(ruleset, result, literal->atom.predicate_id, &begin, &count)) {
            solve_once_set_invalid_state(result);
            return 0;
        }
        if (!solve_once_scan_candidates_ordered(ruleset,
                                                result,
                                                rule,
                                                order_pos,
                                                join_order,
                                                join_order_count,
                                                bindings,
                                                &result->idb_facts[begin],
                                                count,
                                                delta_literal_index,
                                                depth,
                                                NULL,
                                                parent_proof_index)) {
            return 0;
        }
    }
    return 1;
}

static maelys_result_t solve_once_finalize(maelys_datalog_solve_result_t *result) {
    if (!result) return MAELYS_ERR_INVALID_ARGUMENT;
    if (result->finalized) return MAELYS_ERR_INVALID_STATE;
    maelys_result_t rc = maelys_datalog_fact_set_sort(&result->idb_final);
    if (rc != MAELYS_OK) return rc;
    rc = maelys_datalog_fact_set_dedup(&result->idb_final);
    if (rc != MAELYS_OK) return rc;
    result->finalized = 1;
    return MAELYS_OK;
}

static int query_terms_are_ground(const maelys_datalog_term_t *terms, size_t arity) {
    for (size_t i = 0; i < arity; i++) {
        switch (terms[i].kind) {
            case MAELYS_DATALOG_TERM_SYMBOL:
            case MAELYS_DATALOG_TERM_INT:
            case MAELYS_DATALOG_TERM_BOOL:
                break;
            case MAELYS_DATALOG_TERM_VAR:
            default:
                return 0;
        }
    }
    return 1;
}

static int solve_once_rule_in_stratum(const maelys_datalog_ruleset_t *ruleset,
                                      const maelys_datalog_rule_t *rule,
                                      uint32_t stratum) {
    return ruleset && rule &&
           rule->head.predicate_id < MAELYS_DATALOG_MAX_PREDICATES &&
           ruleset->strata[rule->head.predicate_id] == stratum;
}

static void solve_once_fill_future_stratum_bounds(maelys_datalog_solve_result_t *result,
                                                  uint32_t start) {
    if (!result) return;
    for (uint32_t i = start; i <= MAELYS_DATALOG_MAX_STRATA; i++) {
        result->stratum_idb_end[i] = result->idb_current_end;
    }
}

static int solve_once_literal_delta_eligible_in_active_stratum(
    const maelys_datalog_ruleset_t *ruleset,
    const maelys_datalog_solve_result_t *result,
    const maelys_datalog_literal_t *literal) {
    if (!solve_once_literal_delta_eligible(ruleset, literal) ||
        !result ||
        literal->atom.predicate_id >= MAELYS_DATALOG_MAX_PREDICATES) {
        return 0;
    }
    return ruleset->strata[literal->atom.predicate_id] == result->active_stratum;
}

static maelys_result_t solve_once_freeze_active_stratum(maelys_datalog_solve_result_t *result) {
    if (!result || result->active_stratum >= MAELYS_DATALOG_MAX_STRATA) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    const size_t begin = result->stratum_idb_end[result->active_stratum];
    if (begin > result->idb_current_end) return MAELYS_ERR_INVALID_STATE;
    sort_idb_slice_with_proof(result, begin, result->idb_current_end);
    result->idb_current_end = dedup_idb_slice_with_proof(result, begin, result->idb_current_end);
    result->idb_delta_begin = result->idb_current_end;
    result->idb_delta_end = result->idb_current_end;
    result->idb_merge_end = result->idb_current_end;
    result->idb_final.count = result->idb_current_end;
    result->idb_final.sorted = 0;
    result->stratum_idb_end[result->active_stratum + 1u] = result->idb_current_end;
    solve_once_fill_future_stratum_bounds(result, result->active_stratum + 2u);
    solve_once_assert_windows(result);
    return MAELYS_OK;
}

static maelys_result_t solve_stratified_path(
    const maelys_datalog_ruleset_t *ruleset,
    const maelys_datalog_edb_t *edb,
    maelys_datalog_solve_result_t **out_result,
    maelys_datalog_solve_diagnostic_t *out_diag) {
    solve_once_diag_clear(out_diag);
    if (!ruleset || !ruleset->loaded || !edb || !out_result) {
        solve_once_diag_base(out_diag,
                             MAELYS_DATALOG_SOLVE_DIAG_INVALID_ARGUMENT,
                             MAELYS_ERR_INVALID_ARGUMENT,
                             MAELYS_DATALOG_DENY_NONE);
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    if (!ruleset->negation_supported || !ruleset->strata_assigned ||
        ruleset->max_stratum >= MAELYS_DATALOG_MAX_STRATA) {
        solve_once_diag_base(out_diag,
                             MAELYS_DATALOG_SOLVE_DIAG_INVALID_STATE,
                             MAELYS_ERR_INVALID_STATE,
                             MAELYS_DATALOG_DENY_POLICY_LOAD_ERROR);
        return MAELYS_ERR_INVALID_STATE;
    }
    if (*out_result) {
        solve_once_diag_base(out_diag,
                             MAELYS_DATALOG_SOLVE_DIAG_INVALID_STATE,
                             MAELYS_ERR_INVALID_STATE,
                             MAELYS_DATALOG_DENY_NONE);
        return MAELYS_ERR_INVALID_STATE;
    }
    *out_result = NULL;
    if (!edb->immutable || !edb->fact_set.sorted) {
        solve_once_diag_malformed_edb(out_diag, &ruleset->registry, &edb->fact_set);
        return MAELYS_ERR_INVALID_STATE;
    }
    if (edb->fact_count > MAELYS_DATALOG_MAX_EDB_FACTS ||
        edb->fact_set.count > MAELYS_DATALOG_MAX_EDB_FACTS) {
        solve_once_diag_base(out_diag,
                             MAELYS_DATALOG_SOLVE_DIAG_MALFORMED_EDB,
                             MAELYS_ERR_PAYLOAD_TOO_LARGE,
                             MAELYS_DATALOG_DENY_EDB_OVERFLOW);
        if (out_diag) {
            out_diag->count_observed = (uint16_t)edb->fact_set.count;
            out_diag->capacity = (uint16_t)MAELYS_DATALOG_MAX_EDB_FACTS;
        }
        return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    }

    maelys_datalog_solve_result_t *result = calloc(1, sizeof(*result));
    if (!result) {
        solve_once_diag_base(out_diag,
                             MAELYS_DATALOG_SOLVE_DIAG_INTERNAL_ERROR,
                             MAELYS_ERR_INTERNAL,
                             MAELYS_DATALOG_DENY_NONE);
        return MAELYS_ERR_INTERNAL;
    }
    result->ruleset = ruleset;
    result->stratified = 1;
    result->failure_reason = MAELYS_DATALOG_DENY_NONE;
    solve_once_init_proof_indices(result);
    maelys_datalog_fact_set_init(&result->edb_snapshot, result->edb_facts, MAELYS_DATALOG_MAX_EDB_FACTS);
    maelys_datalog_fact_set_init(&result->idb_final, result->idb_facts, MAELYS_DATALOG_MAX_IDB_FACTS);
    if (edb->fact_set.count > 0) {
        memcpy(result->edb_facts,
               edb->fact_set.facts,
               edb->fact_set.count * sizeof(result->edb_facts[0]));
    }
    result->edb_snapshot.count = edb->fact_set.count;
    result->edb_snapshot.sorted = edb->fact_set.sorted;
    if (!datalog_fact_set_structurally_valid(&ruleset->registry, &result->edb_snapshot)) {
        result->failed = 1;
        solve_once_diag_malformed_edb(out_diag, &ruleset->registry, &result->edb_snapshot);
        maelys_datalog_solve_result_free(result);
        return MAELYS_ERR_INVALID_STATE;
    }
    if (!datalog_fact_slice_structurally_valid(&ruleset->registry, ruleset->facts, ruleset->fact_count)) {
        result->failed = 1;
        const maelys_datalog_fact_t *fact =
            solve_once_first_invalid_fact_in_slice(&ruleset->registry, ruleset->facts, ruleset->fact_count);
        solve_once_diag_malformed_fact(out_diag, &ruleset->registry, fact);
        maelys_datalog_solve_result_free(result);
        return MAELYS_ERR_INVALID_STATE;
    }
    maelys_datalog_proof_init(&result->proof, ruleset->policy_id, ruleset->sha256, 0);
    solve_once_assert_windows(result);

    for (uint32_t s = 0; s <= ruleset->max_stratum; s++) {
        result->active_stratum = s;
        result->stratum_idb_end[s] = result->idb_current_end;
        solve_once_fill_future_stratum_bounds(result, s + 1u);
        result->idb_merge_end = result->idb_current_end;
        result->idb_delta_begin = result->idb_current_end;
        result->idb_delta_end = result->idb_current_end;
        result->idb_final.count = result->idb_current_end;
        solve_once_assert_windows(result);

        for (size_t r = 0; r < ruleset->rule_count; r++) {
            const maelys_datalog_rule_t *rule = &ruleset->rules[r];
            if (!solve_once_rule_in_stratum(ruleset, rule, s)) continue;
            uint8_t join_order[MAELYS_DATALOG_MAX_BODY_LITERALS];
            uint8_t join_order_count = 0;
            maelys_result_t plan_rc = build_static_join_order(ruleset, rule, -1, join_order, &join_order_count);
            if (plan_rc != MAELYS_OK) {
                result->failed = 1;
                solve_once_set_join_order_failure(result, plan_rc);
                maelys_result_t rc = result->failure_error != MAELYS_OK
                    ? result->failure_error
                    : plan_rc;
                solve_once_diag_failed_result(out_diag, result, rc);
                maelys_datalog_solve_result_free(result);
                return rc;
            }
            solve_once_bindings_t bindings;
            memset(&bindings, 0, sizeof(bindings));
            if (!solve_once_derive_ordered(ruleset,
                                           result,
                                           rule,
                                           0,
                                           join_order,
                                           join_order_count,
                                           &bindings,
                                           MAELYS_DATALOG_NO_DELTA_INDEX,
                                           0,
                                           MAELYS_DATALOG_PROOF_NO_PARENT)) {
                result->failed = 1;
                maelys_result_t rc = result->failure_error != MAELYS_OK
                    ? result->failure_error
                    : MAELYS_ERR_PAYLOAD_TOO_LARGE;
                solve_once_diag_failed_result(out_diag, result, rc);
                maelys_datalog_solve_result_free(result);
                return rc;
            }
        }

        const size_t stratum_begin = result->stratum_idb_end[s];
        result->idb_current_end = result->idb_merge_end;
        result->idb_delta_begin = stratum_begin;
        result->idb_delta_end = result->idb_current_end;
        result->idb_final.count = result->idb_current_end;
        solve_once_assert_windows(result);

        int converged = 0;
        for (size_t depth = 1; depth <= MAELYS_DATALOG_MAX_DEPTH; depth++) {
            if (result->idb_delta_begin == result->idb_delta_end) {
                converged = 1;
                break;
            }

            const size_t merge_begin = result->idb_current_end;
            result->idb_merge_end = merge_begin;
            result->idb_final.count = merge_begin;
            solve_once_assert_windows(result);
            for (size_t r = 0; r < ruleset->rule_count; r++) {
                const maelys_datalog_rule_t *rule = &ruleset->rules[r];
                if (!solve_once_rule_in_stratum(ruleset, rule, s)) continue;
                for (size_t i = 0; i < rule->body_count; i++) {
                    if (!solve_once_literal_delta_eligible_in_active_stratum(ruleset, result, &rule->body[i])) {
                        continue;
                    }
                    uint8_t join_order[MAELYS_DATALOG_MAX_BODY_LITERALS];
                    uint8_t join_order_count = 0;
                    maelys_result_t plan_rc =
                        build_static_join_order(ruleset, rule, (int)i, join_order, &join_order_count);
                    if (plan_rc != MAELYS_OK) {
                        result->failed = 1;
                        solve_once_set_join_order_failure(result, plan_rc);
                        maelys_result_t rc = result->failure_error != MAELYS_OK
                            ? result->failure_error
                            : plan_rc;
                        solve_once_diag_failed_result(out_diag, result, rc);
                        maelys_datalog_solve_result_free(result);
                        return rc;
                    }
                    solve_once_bindings_t bindings;
                    memset(&bindings, 0, sizeof(bindings));
                    if (!solve_once_derive_ordered(ruleset,
                                                   result,
                                                   rule,
                                                   0,
                                                   join_order,
                                                   join_order_count,
                                                   &bindings,
                                                   i,
                                                   depth,
                                                   MAELYS_DATALOG_PROOF_NO_PARENT)) {
                        result->failed = 1;
                        maelys_result_t rc = result->failure_error != MAELYS_OK
                            ? result->failure_error
                            : MAELYS_ERR_PAYLOAD_TOO_LARGE;
                        solve_once_diag_failed_result(out_diag, result, rc);
                        maelys_datalog_solve_result_free(result);
                        return rc;
                    }
                }
            }
            result->idb_delta_begin = merge_begin;
            result->idb_delta_end = result->idb_merge_end;
            result->idb_current_end = result->idb_merge_end;
            result->idb_final.count = result->idb_current_end;
            solve_once_assert_windows(result);
        }
        if (!converged && result->idb_delta_begin != result->idb_delta_end) {
            result->failed = 1;
            result->failure_reason = MAELYS_DATALOG_DENY_MAX_DEPTH;
            maelys_datalog_proof_add(&result->proof,
                                      0,
                                      0,
                                      NULL,
                                      MAELYS_DATALOG_DENY_MAX_DEPTH,
                                      MAELYS_DATALOG_MAX_DEPTH,
                                      MAELYS_DATALOG_PROOF_NO_PARENT);
            solve_once_diag_base(out_diag,
                                 MAELYS_DATALOG_SOLVE_DIAG_MAX_DEPTH,
                                 MAELYS_ERR_PAYLOAD_TOO_LARGE,
                                 MAELYS_DATALOG_DENY_MAX_DEPTH);
            if (out_diag) {
                out_diag->depth = (uint16_t)MAELYS_DATALOG_MAX_DEPTH;
                out_diag->depth_limit = (uint16_t)MAELYS_DATALOG_MAX_DEPTH;
            }
            maelys_datalog_solve_result_free(result);
            return MAELYS_ERR_PAYLOAD_TOO_LARGE;
        }

        maelys_result_t rc = solve_once_freeze_active_stratum(result);
        if (rc != MAELYS_OK) {
            result->failed = 1;
            solve_once_diag_base(out_diag,
                                 MAELYS_DATALOG_SOLVE_DIAG_INVALID_STATE,
                                 rc,
                                 MAELYS_DATALOG_DENY_NONE);
            maelys_datalog_solve_result_free(result);
            return rc;
        }
    }

    maelys_result_t rc = solve_once_finalize(result);
    if (rc != MAELYS_OK) {
        result->failed = 1;
        solve_once_diag_base(out_diag,
                             MAELYS_DATALOG_SOLVE_DIAG_INVALID_STATE,
                             rc,
                             MAELYS_DATALOG_DENY_NONE);
        maelys_datalog_solve_result_free(result);
        return rc;
    }
    *out_result = result;
    return MAELYS_OK;
}

static maelys_result_t maelys_datalog_solve_once_run(
    const maelys_datalog_ruleset_t *ruleset,
    const maelys_datalog_edb_t *edb,
    maelys_datalog_solve_result_t **out_result,
    maelys_datalog_solve_diagnostic_t *out_diag,
    int use_static_join_order) {
    solve_once_diag_clear(out_diag);
    if (!ruleset || !ruleset->loaded || !edb || !out_result) {
        solve_once_diag_base(out_diag,
                             MAELYS_DATALOG_SOLVE_DIAG_INVALID_ARGUMENT,
                             MAELYS_ERR_INVALID_ARGUMENT,
                             MAELYS_DATALOG_DENY_NONE);
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    if (*out_result) {
        solve_once_diag_base(out_diag,
                             MAELYS_DATALOG_SOLVE_DIAG_INVALID_STATE,
                             MAELYS_ERR_INVALID_STATE,
                             MAELYS_DATALOG_DENY_NONE);
        return MAELYS_ERR_INVALID_STATE;
    }
    *out_result = NULL;
    if (!edb->immutable || !edb->fact_set.sorted) {
        solve_once_diag_malformed_edb(out_diag, &ruleset->registry, &edb->fact_set);
        return MAELYS_ERR_INVALID_STATE;
    }
    if (edb->fact_count > MAELYS_DATALOG_MAX_EDB_FACTS ||
        edb->fact_set.count > MAELYS_DATALOG_MAX_EDB_FACTS) {
        solve_once_diag_base(out_diag,
                             MAELYS_DATALOG_SOLVE_DIAG_MALFORMED_EDB,
                             MAELYS_ERR_PAYLOAD_TOO_LARGE,
                             MAELYS_DATALOG_DENY_EDB_OVERFLOW);
        if (out_diag) {
            out_diag->count_observed = (uint16_t)edb->fact_set.count;
            out_diag->capacity = (uint16_t)MAELYS_DATALOG_MAX_EDB_FACTS;
        }
        return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    }

    maelys_datalog_solve_result_t *result = calloc(1, sizeof(*result));
    if (!result) {
        solve_once_diag_base(out_diag,
                             MAELYS_DATALOG_SOLVE_DIAG_INTERNAL_ERROR,
                             MAELYS_ERR_INTERNAL,
                             MAELYS_DATALOG_DENY_NONE);
        return MAELYS_ERR_INTERNAL;
    }
    result->ruleset = ruleset;
    result->failure_reason = MAELYS_DATALOG_DENY_NONE;
    solve_once_init_proof_indices(result);
    maelys_datalog_fact_set_init(&result->edb_snapshot, result->edb_facts, MAELYS_DATALOG_MAX_EDB_FACTS);
    maelys_datalog_fact_set_init(&result->idb_final, result->idb_facts, MAELYS_DATALOG_MAX_IDB_FACTS);
    if (edb->fact_set.count > 0) {
        memcpy(result->edb_facts,
               edb->fact_set.facts,
               edb->fact_set.count * sizeof(result->edb_facts[0]));
    }
    result->edb_snapshot.count = edb->fact_set.count;
    result->edb_snapshot.sorted = edb->fact_set.sorted;
    if (!datalog_fact_set_structurally_valid(&ruleset->registry, &result->edb_snapshot)) {
        result->failed = 1;
        solve_once_diag_malformed_edb(out_diag, &ruleset->registry, &result->edb_snapshot);
        maelys_datalog_solve_result_free(result);
        return MAELYS_ERR_INVALID_STATE;
    }
    if (!datalog_fact_slice_structurally_valid(&ruleset->registry, ruleset->facts, ruleset->fact_count)) {
        result->failed = 1;
        const maelys_datalog_fact_t *fact =
            solve_once_first_invalid_fact_in_slice(&ruleset->registry, ruleset->facts, ruleset->fact_count);
        solve_once_diag_malformed_fact(out_diag, &ruleset->registry, fact);
        maelys_datalog_solve_result_free(result);
        return MAELYS_ERR_INVALID_STATE;
    }
    maelys_datalog_proof_init(&result->proof, ruleset->policy_id, ruleset->sha256, 0);

    result->idb_current_end = 0;
    result->idb_delta_begin = 0;
    result->idb_delta_end = 0;
    result->idb_merge_end = 0;
    solve_once_assert_windows(result);

    for (size_t r = 0; r < ruleset->rule_count; r++) {
        uint8_t join_order[MAELYS_DATALOG_MAX_BODY_LITERALS];
        uint8_t join_order_count = 0;
        if (use_static_join_order) {
            maelys_result_t plan_rc = build_static_join_order(ruleset,
                                                              &ruleset->rules[r],
                                                              -1,
                                                              join_order,
                                                              &join_order_count);
            if (plan_rc != MAELYS_OK) {
                result->failed = 1;
                solve_once_set_join_order_failure(result, plan_rc);
                maelys_result_t rc = result->failure_error != MAELYS_OK
                    ? result->failure_error
                    : plan_rc;
                solve_once_diag_failed_result(out_diag, result, rc);
                maelys_datalog_solve_result_free(result);
                return rc;
            }
        }
        solve_once_bindings_t bindings;
        memset(&bindings, 0, sizeof(bindings));
        int ok = use_static_join_order
            ? solve_once_derive_ordered(ruleset,
                                        result,
                                        &ruleset->rules[r],
                                        0,
                                        join_order,
                                        join_order_count,
                                        &bindings,
                                        MAELYS_DATALOG_NO_DELTA_INDEX,
                                        0,
                                        MAELYS_DATALOG_PROOF_NO_PARENT)
            : solve_once_derive_recursive(ruleset,
                                          result,
                                          &ruleset->rules[r],
                                          0,
                                          &bindings,
                                          MAELYS_DATALOG_NO_DELTA_INDEX,
                                          0,
                                          MAELYS_DATALOG_PROOF_NO_PARENT);
        if (!ok) {
            result->failed = 1;
            maelys_result_t rc = result->failure_error != MAELYS_OK
                ? result->failure_error
                : MAELYS_ERR_PAYLOAD_TOO_LARGE;
            solve_once_diag_failed_result(out_diag, result, rc);
            maelys_datalog_solve_result_free(result);
            return rc;
        }
    }

    result->idb_current_end = result->idb_merge_end;
    result->idb_delta_begin = 0;
    result->idb_delta_end = result->idb_current_end;
    result->idb_final.count = result->idb_current_end;
    solve_once_assert_windows(result);

    for (size_t depth = 1; depth <= MAELYS_DATALOG_MAX_DEPTH; depth++) {
        solve_once_assert_windows(result);
        if (result->idb_delta_begin == result->idb_delta_end) {
            maelys_result_t rc = solve_once_finalize(result);
            if (rc != MAELYS_OK) {
                result->failed = 1;
                solve_once_diag_base(out_diag,
                                     MAELYS_DATALOG_SOLVE_DIAG_INVALID_STATE,
                                     rc,
                                     MAELYS_DATALOG_DENY_NONE);
                maelys_datalog_solve_result_free(result);
                return rc;
            }
            *out_result = result;
            return MAELYS_OK;
        }

        size_t merge_begin = result->idb_current_end;
        result->idb_merge_end = merge_begin;
        result->idb_final.count = merge_begin;
        solve_once_assert_windows(result);
        for (size_t r = 0; r < ruleset->rule_count; r++) {
            const maelys_datalog_rule_t *rule = &ruleset->rules[r];
            for (size_t i = 0; i < rule->body_count; i++) {
                if (!solve_once_literal_delta_eligible(ruleset, &rule->body[i])) continue;
                uint8_t join_order[MAELYS_DATALOG_MAX_BODY_LITERALS];
                uint8_t join_order_count = 0;
                if (use_static_join_order) {
                    maelys_result_t plan_rc = build_static_join_order(ruleset,
                                                                      rule,
                                                                      (int)i,
                                                                      join_order,
                                                                      &join_order_count);
                    if (plan_rc != MAELYS_OK) {
                        result->failed = 1;
                        solve_once_set_join_order_failure(result, plan_rc);
                        maelys_result_t rc = result->failure_error != MAELYS_OK
                            ? result->failure_error
                            : plan_rc;
                        solve_once_diag_failed_result(out_diag, result, rc);
                        maelys_datalog_solve_result_free(result);
                        return rc;
                    }
                }
                solve_once_bindings_t bindings;
                memset(&bindings, 0, sizeof(bindings));
                int ok = use_static_join_order
                    ? solve_once_derive_ordered(ruleset,
                                                result,
                                                rule,
                                                0,
                                                join_order,
                                                join_order_count,
                                                &bindings,
                                                i,
                                                depth,
                                                MAELYS_DATALOG_PROOF_NO_PARENT)
                    : solve_once_derive_recursive(ruleset,
                                                  result,
                                                  rule,
                                                  0,
                                                  &bindings,
                                                  i,
                                                  depth,
                                                  MAELYS_DATALOG_PROOF_NO_PARENT);
                if (!ok) {
                    result->failed = 1;
                    maelys_result_t rc = result->failure_error != MAELYS_OK
                        ? result->failure_error
                        : MAELYS_ERR_PAYLOAD_TOO_LARGE;
                    solve_once_diag_failed_result(out_diag, result, rc);
                    maelys_datalog_solve_result_free(result);
                    return rc;
                }
            }
        }
        result->idb_delta_begin = merge_begin;
        result->idb_delta_end = result->idb_merge_end;
        result->idb_current_end = result->idb_merge_end;
        result->idb_final.count = result->idb_current_end;
        solve_once_assert_windows(result);
    }

    /* Natural loop exhaustion after MAELYS_DATALOG_MAX_DEPTH is a bounded-depth
     * failure path. Convergence must be observed at the top of an allowed
     * iteration, not finalized by a second ambiguous post-loop path. */
    result->failed = 1;
    result->failure_reason = MAELYS_DATALOG_DENY_MAX_DEPTH;
    maelys_datalog_proof_add(&result->proof,
                              0,
                              0,
                              NULL,
                              MAELYS_DATALOG_DENY_MAX_DEPTH,
                              MAELYS_DATALOG_MAX_DEPTH,
                              MAELYS_DATALOG_PROOF_NO_PARENT);
    solve_once_diag_base(out_diag,
                         MAELYS_DATALOG_SOLVE_DIAG_MAX_DEPTH,
                         MAELYS_ERR_PAYLOAD_TOO_LARGE,
                         MAELYS_DATALOG_DENY_MAX_DEPTH);
    if (out_diag) {
        out_diag->depth = (uint16_t)MAELYS_DATALOG_MAX_DEPTH;
        out_diag->depth_limit = (uint16_t)MAELYS_DATALOG_MAX_DEPTH;
    }
    maelys_datalog_solve_result_free(result);
    return MAELYS_ERR_PAYLOAD_TOO_LARGE;
}

maelys_result_t maelys_datalog_solve_once_ex(
    const maelys_datalog_ruleset_t *ruleset,
    const maelys_datalog_edb_t *edb,
    maelys_datalog_solve_result_t **out_result,
    maelys_datalog_solve_diagnostic_t *out_diag) {
    if (ruleset && ruleset->negation_supported) {
        return solve_stratified_path(ruleset, edb, out_result, out_diag);
    }
    return maelys_datalog_solve_once_run(ruleset, edb, out_result, out_diag, 1);
}

maelys_result_t maelys_datalog_solve_once(const maelys_datalog_ruleset_t *ruleset,
                                          const maelys_datalog_edb_t *edb,
                                          maelys_datalog_solve_result_t **out_result) {
    return maelys_datalog_solve_once_ex(ruleset, edb, out_result, NULL);
}

#ifdef MAELYS_TESTING
maelys_result_t maelys_datalog_test_build_static_join_order(
    const maelys_datalog_ruleset_t *ruleset,
    const maelys_datalog_rule_t *rule,
    int delta_body_index,
    uint8_t out_order[MAELYS_DATALOG_MAX_BODY_LITERALS],
    uint8_t *out_count) {
    return build_static_join_order(ruleset, rule, delta_body_index, out_order, out_count);
}

maelys_result_t maelys_datalog_test_solve_once_legacy_order(
    const maelys_datalog_ruleset_t *ruleset,
    const maelys_datalog_edb_t *edb,
    maelys_datalog_solve_result_t **out_result,
    maelys_datalog_solve_diagnostic_t *out_diag) {
    return maelys_datalog_solve_once_run(ruleset, edb, out_result, out_diag, 0);
}

maelys_result_t maelys_datalog_test_solve_result_idb_facts(
    const maelys_datalog_solve_result_t *result,
    const maelys_datalog_fact_t **out_facts,
    size_t *out_count) {
    if (!result || !out_facts || !out_count) return MAELYS_ERR_INVALID_ARGUMENT;
    *out_facts = result->idb_final.facts;
    *out_count = result->idb_final.count;
    return MAELYS_OK;
}

maelys_result_t maelys_datalog_test_solve_result_idb_proof_indices(
    const maelys_datalog_solve_result_t *result,
    const uint16_t **out_indices,
    size_t *out_count) {
    if (!result || !out_indices || !out_count) return MAELYS_ERR_INVALID_ARGUMENT;
    *out_indices = result->idb_proof_index;
    *out_count = result->idb_final.count;
    return MAELYS_OK;
}
#endif

const maelys_datalog_proof_tree_t *maelys_datalog_solve_result_proof(
    const maelys_datalog_solve_result_t *result) {
    if (!result || result->failed || !result->finalized) return NULL;
    return &result->proof;
}

maelys_result_t maelys_datalog_extract_proof_for_fact(
    const maelys_datalog_solve_result_t *result,
    const maelys_datalog_fact_t *queried_fact,
    maelys_datalog_proof_tree_t *out_proof) {
    if (!result || !queried_fact || !out_proof) return MAELYS_ERR_INVALID_ARGUMENT;
    if (!result->finalized || result->failed || !result->ruleset || !result->ruleset->loaded) {
        return MAELYS_ERR_INVALID_STATE;
    }
    if (!datalog_fact_structurally_valid(&result->ruleset->registry, queried_fact)) {
        return MAELYS_ERR_INVALID_FIELD;
    }

    maelys_datalog_proof_init(out_proof,
                              result->proof.policy_id,
                              result->proof.sha256,
                              result->proof.verbose);
    out_proof->truncated = result->proof.truncated ? 1 : 0;

    uint16_t local_index[MAELYS_DATALOG_MAX_PROOF_NODES];
    for (size_t i = 0; i < MAELYS_DATALOG_MAX_PROOF_NODES; i++) {
        local_index[i] = MAELYS_DATALOG_PROOF_NO_PARENT;
    }

    for (size_t n = 0; n < result->proof.node_count; n++) {
        const maelys_datalog_proof_node_t *node = &result->proof.nodes[n];
        if (node->deny_reason != MAELYS_DATALOG_DENY_NONE ||
            !maelys_datalog_fact_equals(&node->derived_fact, queried_fact)) {
            continue;
        }

        uint16_t chain[MAELYS_DATALOG_MAX_PROOF_NODES];
        size_t chain_count = 0;
        uint16_t current = (uint16_t)n;
        while (current != MAELYS_DATALOG_PROOF_NO_PARENT) {
            if (current >= result->proof.node_count ||
                chain_count >= MAELYS_DATALOG_MAX_PROOF_NODES) {
                out_proof->truncated = 1;
                return MAELYS_ERR_INVALID_STATE;
            }
            chain[chain_count++] = current;
            current = result->proof.nodes[current].parent_index;
        }

        for (size_t rev = chain_count; rev > 0; rev--) {
            const uint16_t original = chain[rev - 1u];
            if (local_index[original] != MAELYS_DATALOG_PROOF_NO_PARENT) continue;
            if (out_proof->node_count >= MAELYS_DATALOG_MAX_PROOF_NODES) {
                out_proof->truncated = 1;
                return MAELYS_OK;
            }
            maelys_datalog_proof_node_t copy = result->proof.nodes[original];
            if (copy.parent_index != MAELYS_DATALOG_PROOF_NO_PARENT) {
                uint16_t parent = copy.parent_index;
                if (parent >= result->proof.node_count) return MAELYS_ERR_INVALID_STATE;
                copy.parent_index = local_index[parent];
                if (copy.parent_index == MAELYS_DATALOG_PROOF_NO_PARENT) {
                    return MAELYS_ERR_INVALID_STATE;
                }
            }
            local_index[original] = (uint16_t)out_proof->node_count;
            out_proof->nodes[out_proof->node_count++] = copy;
        }
    }

    return MAELYS_OK;
}

void maelys_datalog_solve_result_free(maelys_datalog_solve_result_t *result) {
    if (!result) return;
    memset(result, 0, sizeof(*result));
    free(result);
}

static int query_whitelist_contains(const maelys_datalog_ruleset_t *ruleset,
                                    const char *predicate,
                                    size_t arity) {
    if (!ruleset || !predicate) return 0;
    for (size_t i = 0u; i < ruleset->query_whitelist_count; i++) {
        if (ruleset->query_whitelist[i].arity == arity &&
            strcmp(ruleset->query_whitelist[i].name, predicate) == 0) {
            return 1;
        }
    }
    return 0;
}

maelys_result_t maelys_datalog_query_solved_ground_fact(
    const maelys_datalog_solve_result_t *result,
    const char *predicate,
    const maelys_datalog_term_t *terms,
    size_t arity,
    bool *out_present) {
    if (!result || !predicate || (!terms && arity > 0) || !out_present) return MAELYS_ERR_INVALID_ARGUMENT;
    *out_present = false;
    if (arity > MAELYS_DATALOG_MAX_ARITY) return MAELYS_ERR_INVALID_FIELD;
    if (!result->finalized || result->failed || !result->ruleset || !result->ruleset->loaded) {
        return MAELYS_ERR_INVALID_STATE;
    }
    if (!query_terms_are_ground(terms, arity)) return MAELYS_ERR_INVALID_FIELD;

    if (result->ruleset->enforces_query_whitelist &&
        !query_whitelist_contains(result->ruleset, predicate, arity)) {
        return MAELYS_ERR_FORBIDDEN;
    }

    maelys_datalog_predicate_id_t pid = 0;
    if (!maelys_datalog_predicate_registry_find(&result->ruleset->registry, predicate, arity, &pid)) {
        return MAELYS_ERR_INVALID_FIELD;
    }
    const maelys_datalog_predicate_def_t *def =
        maelys_datalog_predicate_registry_get(&result->ruleset->registry, pid);
    if (!def || !(def->kind_flags & MAELYS_DATALOG_PRED_KIND_QUERY)) return MAELYS_ERR_INVALID_FIELD;
    for (size_t i = 0; i < result->ruleset->fact_count; i++) {
        if (fact_matches_query(&result->ruleset->facts[i], pid, terms, arity)) {
            *out_present = true;
            return MAELYS_OK;
        }
    }

    maelys_datalog_fact_t query_fact;
    memset(&query_fact, 0, sizeof(query_fact));
    query_fact.predicate_id = pid;
    query_fact.arity = (uint8_t)arity;
    for (size_t i = 0; i < arity; i++) query_fact.terms[i] = terms[i];
    if (!datalog_fact_structurally_valid(&result->ruleset->registry, &query_fact)) {
        return MAELYS_ERR_INVALID_STATE;
    }

    if (maelys_datalog_fact_set_contains(&result->edb_snapshot, &query_fact) ||
        maelys_datalog_fact_set_contains(&result->idb_final, &query_fact)) {
        *out_present = true;
    }
    return MAELYS_OK;
}

maelys_result_t maelys_datalog_decision_from_queries(
    const maelys_datalog_query_result_t *deny_result,
    const maelys_datalog_query_result_t *reduce_result,
    const maelys_datalog_query_result_t *allow_result,
    maelys_datalog_decision_t *out_decision) {
    if (!deny_result || !reduce_result || !allow_result || !out_decision) return MAELYS_ERR_INVALID_ARGUMENT;
    /* Decision precedence is deny > reduce > allow. A deny combined with allow
     * or reduce is surfaced as DENY_CONFLICT. Allow + reduce without deny
     * intentionally yields REDUCED; reduce is treated as conservative allow,
     * not as a conflict. */
    if (deny_result->derived && (allow_result->derived || reduce_result->derived)) {
        *out_decision = MAELYS_DATALOG_DECISION_DENY_CONFLICT;
        return MAELYS_OK;
    }
    if (deny_result->derived) {
        *out_decision = MAELYS_DATALOG_DECISION_DENY;
    } else if (reduce_result->derived) {
        *out_decision = MAELYS_DATALOG_DECISION_REDUCED;
    } else if (allow_result->derived) {
        *out_decision = MAELYS_DATALOG_DECISION_ALLOW;
    } else {
        *out_decision = MAELYS_DATALOG_DECISION_DENY_DEFAULT;
    }
    return MAELYS_OK;
}
