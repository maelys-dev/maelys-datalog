#include "src/core/maelys_datalog_solver.h"
#include "src/core/maelys_datalog_solver_internal.h"

#include "src/core/maelys_datalog_audit.h"
#include "src/core/maelys_datalog_diagnostic.h"
#include "src/core/maelys_datalog_edb.h"
#include "src/core/maelys_datalog_filter.h"
#include "src/core/maelys_datalog_predicate_registry.h"
#include "src/core/maelys_datalog_symbol_table.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define MAELYS_DATALOG_NO_DELTA_INDEX ((size_t)-1)

_Static_assert(MAELYS_DATALOG_MAX_BODY_LITERALS <= 64u,
               "planned_mask width insufficient for MAX_BODY_LITERALS");
_Static_assert(MAELYS_DATALOG_MAX_RULE_VARIABLES <= 64u,
               "bound_var_mask width insufficient for MAX_RULE_VARIABLES");
_Static_assert(MAELYS_DATALOG_MAX_RULE_VARIABLES <= 32u,
               "solve_once_bindings_t bound_mask width insufficient for MAX_RULE_VARIABLES");
_Static_assert(MAELYS_DATALOG_MAX_EDB_FACTS <= UINT16_MAX,
               "EDB predicate ranges require uint16_t fact indexes");
_Static_assert(MAELYS_DATALOG_MAX_BODY_LITERALS <= 32u,
               "witness_filled_mask width insufficient for MAX_BODY_LITERALS");
_Static_assert(MAELYS_DATALOG_MAX_EXPLANATION_PREMISES <= UINT16_MAX,
               "premise pool count must fit uint16 premise_pool_count");
_Static_assert(MAELYS_DATALOG_MAX_PROOF_NODES <= UINT16_MAX,
               "proof node index must fit uint16 witness range indices");

typedef struct {
    uint16_t begin;
    uint16_t count;
} maelys_datalog_pred_range_t;

typedef struct {
    uint32_t bound_mask;
    maelys_datalog_term_t value[MAELYS_DATALOG_MAX_RULE_VARIABLES];
} solve_once_bindings_t;

static int solve_once_bindings_is_bound(const solve_once_bindings_t *bindings,
                                        uint32_t variable) {
    if (!bindings) return 0;
    assert(variable < MAELYS_DATALOG_MAX_RULE_VARIABLES);
    return (bindings->bound_mask & ((uint32_t)1u << variable)) != 0;
}

static void solve_once_bindings_set_bound(solve_once_bindings_t *bindings,
                                          uint32_t variable) {
    assert(bindings);
    assert(variable < MAELYS_DATALOG_MAX_RULE_VARIABLES);
    bindings->bound_mask |= (uint32_t)1u << variable;
}

typedef enum {
    MAELYS_DATALOG_COMPARE_TRUE = 0,
    MAELYS_DATALOG_COMPARE_FALSE,
    MAELYS_DATALOG_COMPARE_INVALID_KIND,
    MAELYS_DATALOG_COMPARE_INVALID_ORDINAL_TYPE,
    MAELYS_DATALOG_COMPARE_UNBOUND_VARIABLE,
    MAELYS_DATALOG_COMPARE_UNKNOWN_OPERATOR,
    MAELYS_DATALOG_COMPARE_UNKNOWN_TERM_KIND,
    MAELYS_DATALOG_COMPARE_ARITH_OVERFLOW
} maelys_datalog_compare_result_t;

struct maelys_datalog_solve_result {
    const maelys_datalog_ruleset_t *ruleset;
    void *release_owner;
    maelys_datalog_solve_result_release_fn release;
    maelys_datalog_fact_t edb_facts[MAELYS_DATALOG_MAX_EDB_FACTS];
    maelys_datalog_fact_t idb_facts[MAELYS_DATALOG_MAX_IDB_FACTS];
    uint16_t idb_proof_index[MAELYS_DATALOG_MAX_IDB_FACTS];
    maelys_datalog_fact_set_t edb_snapshot;
    maelys_datalog_pred_range_t edb_ranges[MAELYS_DATALOG_MAX_PREDICATES];
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
    /* P4-C64 — Bounded Why-true provenance, stored in parallel with the
     * historic proof tree. The proof tree layout/bytes are unchanged; this
     * pool and its per-node ranges live only in the opaque solve_result.
     *
     *   premise_pool                : fixed pool, no per-premise malloc.
     *   node_premise_begin/count    : range into premise_pool, parallel to
     *                                 proof.nodes[], indexed by proof node idx.
     *   node_has_premises           : range committed for that proof node.
     *   witness_slots/filled_mask   : per-derivation builder, indexed by the
     *                                 lexical body_index; read only at a
     *                                 derivation terminal, committed only when
     *                                 a real proof node is added and the whole
     *                                 witness fits (atomic). */
    maelys_datalog_explanation_premise_t premise_pool[MAELYS_DATALOG_MAX_EXPLANATION_PREMISES];
    uint16_t node_premise_begin[MAELYS_DATALOG_MAX_PROOF_NODES];
    uint16_t node_premise_count[MAELYS_DATALOG_MAX_PROOF_NODES];
    uint8_t node_has_premises[MAELYS_DATALOG_MAX_PROOF_NODES];
    uint16_t premise_pool_count;
    maelys_datalog_explanation_premise_t witness_slots[MAELYS_DATALOG_MAX_BODY_LITERALS];
    uint32_t witness_filled_mask;
    maelys_datalog_deny_reason_t failure_reason;
    maelys_result_t failure_error;
    maelys_datalog_diagnostic_t runtime_diag;
    maelys_datalog_filter_statistics_t filter_statistics;
    int finalized;
    int failed;
#ifdef MAELYS_TESTING
    int edb_full_scan_reference;
#endif
};

maelys_result_t maelys_datalog_solve_result_symbol_text(
    const maelys_datalog_solve_result_t *result,
    maelys_datalog_symbol_id_t id,
    const char **out_text,
    size_t *out_length) {
    if (!result || !out_text || !out_length) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    if (!result->finalized || result->failed || !result->ruleset ||
        !result->ruleset->loaded ||
        !maelys_datalog_symbol_id_is_valid(&result->ruleset->symbols, id)) {
        return MAELYS_ERR_INVALID_STATE;
    }
    const char *text = maelys_datalog_symbol_text(&result->ruleset->symbols, id);
    if (!text) return MAELYS_ERR_INVALID_STATE;
    const size_t length = result->ruleset->symbols.entries[id - 1u].len;
    *out_text = text;
    *out_length = length;
    return MAELYS_OK;
}

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

static maelys_result_t solve_once_build_edb_ranges(maelys_datalog_solve_result_t *result) {
    if (!result || !result->edb_snapshot.sorted ||
        (!result->edb_snapshot.facts && result->edb_snapshot.count > 0)) {
        return MAELYS_ERR_INVALID_STATE;
    }

    memset(result->edb_ranges, 0, sizeof(result->edb_ranges));
    size_t i = 0;
    maelys_datalog_predicate_id_t previous_pid = 0;
    int have_previous = 0;
    while (i < result->edb_snapshot.count) {
        const maelys_datalog_predicate_id_t pid = result->edb_snapshot.facts[i].predicate_id;
        if (pid >= MAELYS_DATALOG_MAX_PREDICATES) return MAELYS_ERR_INVALID_STATE;
        if (have_previous && pid < previous_pid) return MAELYS_ERR_INVALID_STATE;

        const size_t begin = i;
        while (i < result->edb_snapshot.count &&
               result->edb_snapshot.facts[i].predicate_id == pid) {
            i++;
        }
        const size_t count = i - begin;
        if (begin > UINT16_MAX || count > UINT16_MAX) return MAELYS_ERR_INVALID_STATE;
        result->edb_ranges[pid].begin = (uint16_t)begin;
        result->edb_ranges[pid].count = (uint16_t)count;
        previous_pid = pid;
        have_previous = 1;
    }
    return MAELYS_OK;
}

static void solve_once_edb_slice(const maelys_datalog_solve_result_t *result,
                                 maelys_datalog_predicate_id_t pid,
                                 const maelys_datalog_fact_t **out_facts,
                                 size_t *out_count) {
    if (!out_facts || !out_count) return;
    *out_facts = result ? result->edb_snapshot.facts : NULL;
    *out_count = 0;
    if (!result || pid >= MAELYS_DATALOG_MAX_PREDICATES) return;

#ifdef MAELYS_TESTING
    if (result->edb_full_scan_reference) {
        *out_facts = result->edb_snapshot.facts;
        *out_count = result->edb_snapshot.count;
        return;
    }
#endif

    const maelys_datalog_pred_range_t range = result->edb_ranges[pid];
    if (range.count == 0) {
        *out_facts = result->edb_snapshot.facts;
        *out_count = 0;
        return;
    }
    *out_facts = result->edb_snapshot.facts + range.begin;
    *out_count = range.count;
}

static int datalog_term_kind_known(maelys_datalog_term_kind_t kind);
static int datalog_fact_structurally_valid(const maelys_datalog_predicate_registry_t *registry,
                                           const maelys_datalog_fact_t *fact);
static int query_whitelist_contains(const maelys_datalog_ruleset_t *ruleset,
                                    const char *predicate,
                                    size_t arity);

const char *maelys_datalog_solve_diagnostic_category_name(
    maelys_datalog_solve_diag_category_t category) {
    switch (category) {
        case MAELYS_DATALOG_SOLVE_DIAG_NONE: return "none";
        case MAELYS_DATALOG_SOLVE_DIAG_MAX_DEPTH: return "max_depth";
        case MAELYS_DATALOG_SOLVE_DIAG_IDB_OVERFLOW: return "idb_overflow";
        case MAELYS_DATALOG_SOLVE_DIAG_COMPARISON_TYPE_ERROR: return "comparison_type_error";
        case MAELYS_DATALOG_SOLVE_DIAG_FILTER_ERROR: return "filter_error";
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
        case MAELYS_DATALOG_DENY_FILTER_ERROR:
            solve_once_diag_base(diag,
                                 MAELYS_DATALOG_SOLVE_DIAG_FILTER_ERROR,
                                 rc,
                                 MAELYS_DATALOG_DENY_FILTER_ERROR);
            if (diag) {
                diag->count_observed = (uint16_t)result->runtime_diag.count;
                diag->capacity = (uint16_t)result->runtime_diag.limit;
                diag->lhs_kind = result->runtime_diag.observed_lhs_kind;
            }
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
    if (!solve_once_bindings_is_bound(bindings, variable)) {
        solve_once_bindings_set_bound(bindings, variable);
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
    if (variable >= MAELYS_DATALOG_MAX_RULE_VARIABLES ||
        !solve_once_bindings_is_bound(bindings, variable)) return 0;
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

static void solve_once_set_filter_failure(
    maelys_datalog_solve_result_t *result,
    maelys_result_t error,
    size_t count,
    size_t limit,
    maelys_datalog_term_kind_t observed_kind) {
    if (!result || result->failure_error != MAELYS_OK) return;
    result->failure_error = error;
    result->failure_reason = MAELYS_DATALOG_DENY_FILTER_ERROR;
    maelys_datalog_diagnostic_set(
        &result->runtime_diag,
        MAELYS_DATALOG_DIAG_RUNTIME_INVALID_FILTER,
        "solver",
        NULL,
        0u,
        0u,
        error == MAELYS_ERR_PAYLOAD_TOO_LARGE
            ? "filter evaluation budget exceeded"
            : "invalid filter evaluation state",
        "validate the filter program, ground symbol value, and configured capacities");
    maelys_datalog_diagnostic_set_limit(&result->runtime_diag, count, limit);
    result->runtime_diag.observed_lhs_kind = (uint8_t)observed_kind;
}

static int filter_program_resolve(
    const maelys_datalog_ruleset_t *ruleset,
    const maelys_datalog_literal_t *literal,
    const maelys_datalog_filter_program_t **out_program,
    const maelys_datalog_filter_definition_t **out_definition,
    const unsigned char **out_pattern) {
    if (!ruleset || !literal || !out_program || !out_definition || !out_pattern ||
        literal->kind != MAELYS_DATALOG_LITERAL_FILTER ||
        literal->filter_program_index >= ruleset->filter_program_count ||
        ruleset->filter_program_count > MAELYS_DATALOG_MAX_FILTER_PROGRAMS ||
        ruleset->filter_pattern_pool_used > MAELYS_DATALOG_FILTER_PATTERN_POOL_BYTES) {
        return 0;
    }
    const maelys_datalog_filter_program_t *program =
        &ruleset->filter_programs[literal->filter_program_index];
    if (program->kind != literal->filter_kind ||
        program->pattern_length > MAELYS_DATALOG_MAX_FILTER_PATTERN_BYTES ||
        program->pattern_offset > ruleset->filter_pattern_pool_used ||
        program->pattern_length > ruleset->filter_pattern_pool_used -
                                      program->pattern_offset) {
        return 0;
    }
    const maelys_datalog_filter_definition_t *definition =
        maelys_datalog_filter_by_kind((maelys_datalog_filter_kind_t)program->kind);
    if (!definition) return 0;
    *out_program = program;
    *out_definition = definition;
    *out_pattern = ruleset->filter_pattern_pool + program->pattern_offset;
    return 1;
}

static int filter_symbol_bytes_resolve(
    const maelys_datalog_symbol_table_t *symbols,
    maelys_datalog_symbol_id_t id,
    const unsigned char **out_bytes,
    size_t *out_length) {
    if (!symbols || !out_bytes || !out_length ||
        symbols->count > MAELYS_DATALOG_MAX_SYMBOLS ||
        symbols->used > MAELYS_DATALOG_STRING_POOL_BYTES ||
        !maelys_datalog_symbol_id_is_valid(symbols, id)) {
        return 0;
    }
    const size_t index = (size_t)id - 1u;
    const size_t offset = (size_t)symbols->entries[index].offset;
    const size_t length = (size_t)symbols->entries[index].len;
    if (length > MAELYS_DATALOG_MAX_STRING_BYTES ||
        offset > symbols->used ||
        length >= symbols->used - offset ||
        symbols->storage[offset + length] != '\0') {
        return 0;
    }
    *out_bytes = (const unsigned char *)symbols->storage + offset;
    *out_length = length;
    return 1;
}

static int ruleset_filters_structurally_valid(
    const maelys_datalog_ruleset_t *ruleset) {
    if (!ruleset || ruleset->rule_count > MAELYS_DATALOG_MAX_RULES ||
        ruleset->filter_program_count > MAELYS_DATALOG_MAX_FILTER_PROGRAMS ||
        ruleset->filter_pattern_pool_used >
            MAELYS_DATALOG_FILTER_PATTERN_POOL_BYTES) {
        return 0;
    }
    for (size_t rule_index = 0u;
         rule_index < ruleset->rule_count;
         rule_index++) {
        const maelys_datalog_rule_t *rule = &ruleset->rules[rule_index];
        if (rule->body_count > MAELYS_DATALOG_MAX_BODY_LITERALS) return 0;
        for (size_t body_index = 0u;
             body_index < rule->body_count;
             body_index++) {
            const maelys_datalog_literal_t *literal = &rule->body[body_index];
            if (literal->kind != MAELYS_DATALOG_LITERAL_FILTER) continue;
            const maelys_datalog_filter_program_t *program = NULL;
            const maelys_datalog_filter_definition_t *definition = NULL;
            const unsigned char *pattern = NULL;
            if ((literal->filter_value.kind != MAELYS_DATALOG_TERM_SYMBOL &&
                 literal->filter_value.kind != MAELYS_DATALOG_TERM_VAR) ||
                !filter_program_resolve(ruleset,
                                        literal,
                                        &program,
                                        &definition,
                                        &pattern)) {
                return 0;
            }
            (void)program;
            (void)definition;
            (void)pattern;
        }
    }
    return 1;
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
    if (!bindings || variable >= MAELYS_DATALOG_MAX_RULE_VARIABLES ||
        !solve_once_bindings_is_bound(bindings, variable)) {
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
            if (lhs->kind != rhs->kind) return MAELYS_DATALOG_COMPARE_INVALID_KIND;
            return maelys_datalog_term_equal(lhs, rhs)
                ? MAELYS_DATALOG_COMPARE_TRUE
                : MAELYS_DATALOG_COMPARE_FALSE;
        case MAELYS_DATALOG_CMP_NEQ:
            if (lhs->kind != rhs->kind) return MAELYS_DATALOG_COMPARE_INVALID_KIND;
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

static maelys_datalog_compare_result_t solve_once_eval_arith_expr(
    const maelys_datalog_rule_t *rule,
    uint8_t root,
    const solve_once_bindings_t *bindings,
    long long *out_value,
    maelys_datalog_term_kind_t *out_observed_kind) {
    if (!rule || !bindings || !out_value || root == MAELYS_DATALOG_ARITH_EXPR_NO_NODE ||
        root >= rule->expr_node_count) {
        return MAELYS_DATALOG_COMPARE_UNKNOWN_TERM_KIND;
    }
    const maelys_datalog_arith_expr_node_t *node = &rule->expr_nodes[root];
    switch (node->kind) {
        case MAELYS_DATALOG_ARITH_EXPR_INT_LITERAL:
            *out_value = node->term.as.integer;
            if (out_observed_kind) *out_observed_kind = MAELYS_DATALOG_TERM_INT;
            return MAELYS_DATALOG_COMPARE_TRUE;
        case MAELYS_DATALOG_ARITH_EXPR_VAR: {
            unsigned variable = node->term.as.variable;
            if (variable >= MAELYS_DATALOG_MAX_RULE_VARIABLES ||
                !solve_once_bindings_is_bound(bindings, variable)) {
                if (out_observed_kind) *out_observed_kind = MAELYS_DATALOG_TERM_VAR;
                return MAELYS_DATALOG_COMPARE_UNBOUND_VARIABLE;
            }
            const maelys_datalog_term_t *bound = &bindings->value[variable];
            if (!datalog_term_kind_known(bound->kind)) {
                if (out_observed_kind) *out_observed_kind = bound->kind;
                return MAELYS_DATALOG_COMPARE_UNKNOWN_TERM_KIND;
            }
            if (bound->kind != MAELYS_DATALOG_TERM_INT) {
                if (out_observed_kind) *out_observed_kind = bound->kind;
                return MAELYS_DATALOG_COMPARE_INVALID_ORDINAL_TYPE;
            }
            *out_value = bound->as.integer;
            if (out_observed_kind) *out_observed_kind = MAELYS_DATALOG_TERM_INT;
            return MAELYS_DATALOG_COMPARE_TRUE;
        }
        case MAELYS_DATALOG_ARITH_EXPR_ADD:
        case MAELYS_DATALOG_ARITH_EXPR_SUB:
        case MAELYS_DATALOG_ARITH_EXPR_MUL: {
            long long lhs = 0;
            long long rhs = 0;
            maelys_datalog_term_kind_t lhs_kind = 0;
            maelys_datalog_term_kind_t rhs_kind = 0;
            maelys_datalog_compare_result_t lhs_rc =
                solve_once_eval_arith_expr(rule, node->left, bindings, &lhs, &lhs_kind);
            if (lhs_rc != MAELYS_DATALOG_COMPARE_TRUE) {
                if (out_observed_kind) *out_observed_kind = lhs_kind;
                return lhs_rc;
            }
            maelys_datalog_compare_result_t rhs_rc =
                solve_once_eval_arith_expr(rule, node->right, bindings, &rhs, &rhs_kind);
            if (rhs_rc != MAELYS_DATALOG_COMPARE_TRUE) {
                if (out_observed_kind) *out_observed_kind = rhs_kind;
                return rhs_rc;
            }
            long long result = 0;
            int overflow = 0;
            if (node->kind == MAELYS_DATALOG_ARITH_EXPR_ADD) {
                overflow = __builtin_add_overflow(lhs, rhs, &result);
            } else if (node->kind == MAELYS_DATALOG_ARITH_EXPR_SUB) {
                overflow = __builtin_sub_overflow(lhs, rhs, &result);
            } else {
                overflow = __builtin_mul_overflow(lhs, rhs, &result);
            }
            if (overflow) {
                if (out_observed_kind) *out_observed_kind = MAELYS_DATALOG_TERM_INT;
                return MAELYS_DATALOG_COMPARE_ARITH_OVERFLOW;
            }
            *out_value = result;
            if (out_observed_kind) *out_observed_kind = MAELYS_DATALOG_TERM_INT;
            return MAELYS_DATALOG_COMPARE_TRUE;
        }
        default:
            return MAELYS_DATALOG_COMPARE_UNKNOWN_TERM_KIND;
    }
}

static int solve_once_evaluate_comparison_literal(maelys_datalog_solve_result_t *result,
                                                  const maelys_datalog_rule_t *rule,
                                                  const maelys_datalog_literal_t *literal,
                                                  const solve_once_bindings_t *bindings,
                                                  maelys_datalog_term_t *out_lhs,
                                                  maelys_datalog_term_t *out_rhs) {
    if (!rule || !literal || literal->kind != MAELYS_DATALOG_LITERAL_COMPARISON) return 0;
    maelys_datalog_term_t lhs;
    maelys_datalog_term_t rhs;
    memset(&lhs, 0, sizeof(lhs));
    memset(&rhs, 0, sizeof(rhs));
    if (literal->has_arith_expr) {
        long long lhs_value = 0;
        long long rhs_value = 0;
        maelys_datalog_term_kind_t lhs_kind = 0;
        maelys_datalog_term_kind_t rhs_kind = 0;
        maelys_datalog_compare_result_t lhs_rc =
            solve_once_eval_arith_expr(rule,
                                       literal->lhs_expr_root,
                                       bindings,
                                       &lhs_value,
                                       &lhs_kind);
        if (lhs_rc != MAELYS_DATALOG_COMPARE_TRUE) {
            lhs.kind = lhs_kind;
            rhs.kind = MAELYS_DATALOG_TERM_INT;
            solve_once_set_comparison_failure(result, lhs_rc, &lhs, literal->op, &rhs);
            return 0;
        }
        maelys_datalog_compare_result_t rhs_rc =
            solve_once_eval_arith_expr(rule,
                                       literal->rhs_expr_root,
                                       bindings,
                                       &rhs_value,
                                       &rhs_kind);
        if (rhs_rc != MAELYS_DATALOG_COMPARE_TRUE) {
            lhs.kind = MAELYS_DATALOG_TERM_INT;
            rhs.kind = rhs_kind;
            solve_once_set_comparison_failure(result, rhs_rc, &lhs, literal->op, &rhs);
            return 0;
        }
        lhs.kind = MAELYS_DATALOG_TERM_INT;
        lhs.as.integer = lhs_value;
        rhs.kind = MAELYS_DATALOG_TERM_INT;
        rhs.as.integer = rhs_value;
    } else {
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
    }
    maelys_datalog_compare_result_t cmp_rc = solve_once_evaluate_comparison(&lhs, literal->op, &rhs);
    if (cmp_rc == MAELYS_DATALOG_COMPARE_FALSE) return 1;
    if (cmp_rc != MAELYS_DATALOG_COMPARE_TRUE) {
        solve_once_set_comparison_failure(result, cmp_rc, &lhs, literal->op, &rhs);
        return 0;
    }
    /* Provenance: report the actually-evaluated ground terms (arith values
     * included), never a partial AST and never a re-evaluation in the accessor. */
    if (out_lhs) *out_lhs = lhs;
    if (out_rhs) *out_rhs = rhs;
    return 2;
}

static int solve_once_fact_in_base(const maelys_datalog_solve_result_t *result,
                                   const maelys_datalog_fact_t *fact) {
    if (!result || !fact) return 0;
    if (result->ruleset && solve_once_fact_in_slice(result->ruleset->facts, result->ruleset->fact_count, fact)) {
        return 1;
    }
    return solve_once_fact_in_slice(result->edb_snapshot.facts, result->edb_snapshot.count, fact);
}

/* Public explanations promise byte-identical output. Copying these nested
 * structures with assignment would also copy implementation-defined padding
 * and inactive union bytes, so build their canonical byte representation from
 * semantic fields instead. */
static void explanation_copy_term(maelys_datalog_term_t *dst,
                                  const maelys_datalog_term_t *src) {
    if (!dst || !src) return;
    memset(dst, 0, sizeof(*dst));
    dst->kind = src->kind;
    switch (src->kind) {
        case MAELYS_DATALOG_TERM_SYMBOL:
            dst->as.symbol = src->as.symbol;
            break;
        case MAELYS_DATALOG_TERM_INT:
            dst->as.integer = src->as.integer;
            break;
        case MAELYS_DATALOG_TERM_BOOL:
            dst->as.boolean = src->as.boolean;
            break;
        case MAELYS_DATALOG_TERM_VAR:
            dst->as.variable = src->as.variable;
            break;
        default:
            break;
    }
}

static void explanation_copy_fact(maelys_datalog_fact_t *dst,
                                  const maelys_datalog_fact_t *src) {
    if (!dst || !src) return;
    memset(dst, 0, sizeof(*dst));
    dst->predicate_id = src->predicate_id;
    dst->arity = src->arity;
    const size_t term_count = (src->arity <= MAELYS_DATALOG_MAX_TERMS)
        ? (size_t)src->arity
        : (size_t)MAELYS_DATALOG_MAX_TERMS;
    for (size_t i = 0; i < term_count; i++) {
        explanation_copy_term(&dst->terms[i], &src->terms[i]);
    }
}

static void explanation_copy_premise(
    maelys_datalog_explanation_premise_t *dst,
    const maelys_datalog_explanation_premise_t *src) {
    if (!dst || !src) return;
    memset(dst, 0, sizeof(*dst));
    dst->kind = src->kind;
    dst->origin = src->origin;
    dst->body_index = src->body_index;
    dst->parent_step = src->parent_step;
    dst->op = src->op;
    switch (src->kind) {
        case MAELYS_DATALOG_EXPLANATION_PREMISE_POSITIVE_FACT:
        case MAELYS_DATALOG_EXPLANATION_PREMISE_NEGATED_ABSENCE:
            explanation_copy_fact(&dst->as.fact, &src->as.fact);
            break;
        case MAELYS_DATALOG_EXPLANATION_PREMISE_COMPARISON_TRUE:
            explanation_copy_term(&dst->as.comparison.lhs, &src->as.comparison.lhs);
            explanation_copy_term(&dst->as.comparison.rhs, &src->as.comparison.rhs);
            break;
        case MAELYS_DATALOG_EXPLANATION_PREMISE_FILTER_TRUE:
            explanation_copy_term(&dst->as.filter.value, &src->as.filter.value);
            dst->as.filter.program_index = src->as.filter.program_index;
            dst->as.filter.filter_kind = src->as.filter.filter_kind;
            break;
        default:
            break;
    }
}

/* P4-C64 witness builder — write one lexical body slot. Positive candidates
 * write their slot before recursion; negation/comparison write theirs only
 * after success. Every write is by lexical body_index, so a terminal path holds
 * exactly body_count filled slots in lexical order regardless of join order. */
static void witness_record_positive(maelys_datalog_solve_result_t *result,
                                    size_t body_index,
                                    maelys_datalog_explanation_origin_t origin,
                                    const maelys_datalog_fact_t *candidate,
                                    uint16_t candidate_proof_index) {
    if (!result || !candidate || body_index >= MAELYS_DATALOG_MAX_BODY_LITERALS) return;
    maelys_datalog_explanation_premise_t *slot = &result->witness_slots[body_index];
    memset(slot, 0, sizeof(*slot));
    slot->kind = (uint8_t)MAELYS_DATALOG_EXPLANATION_PREMISE_POSITIVE_FACT;
    slot->origin = (uint8_t)origin;
    slot->body_index = (uint16_t)body_index;
    slot->op = 0u;
    slot->parent_step = (origin == MAELYS_DATALOG_EXPLANATION_ORIGIN_IDB)
        ? candidate_proof_index
        : (uint16_t)MAELYS_DATALOG_EXPLANATION_NO_STEP;
    explanation_copy_fact(&slot->as.fact, candidate);
    result->witness_filled_mask |= (uint32_t)1u << body_index;
}

static void witness_record_negation(maelys_datalog_solve_result_t *result,
                                    size_t body_index,
                                    maelys_datalog_explanation_origin_t origin,
                                    const maelys_datalog_fact_t *ground) {
    if (!result || !ground || body_index >= MAELYS_DATALOG_MAX_BODY_LITERALS) return;
    maelys_datalog_explanation_premise_t *slot = &result->witness_slots[body_index];
    memset(slot, 0, sizeof(*slot));
    slot->kind = (uint8_t)MAELYS_DATALOG_EXPLANATION_PREMISE_NEGATED_ABSENCE;
    slot->origin = (uint8_t)origin;
    slot->body_index = (uint16_t)body_index;
    slot->op = 0u;
    slot->parent_step = (uint16_t)MAELYS_DATALOG_EXPLANATION_NO_STEP;
    explanation_copy_fact(&slot->as.fact, ground);
    result->witness_filled_mask |= (uint32_t)1u << body_index;
}

static void witness_record_comparison(maelys_datalog_solve_result_t *result,
                                      size_t body_index,
                                      maelys_datalog_cmp_op_t op,
                                      const maelys_datalog_term_t *lhs,
                                      const maelys_datalog_term_t *rhs) {
    if (!result || !lhs || !rhs || body_index >= MAELYS_DATALOG_MAX_BODY_LITERALS) return;
    maelys_datalog_explanation_premise_t *slot = &result->witness_slots[body_index];
    memset(slot, 0, sizeof(*slot));
    slot->kind = (uint8_t)MAELYS_DATALOG_EXPLANATION_PREMISE_COMPARISON_TRUE;
    slot->origin = (uint8_t)MAELYS_DATALOG_EXPLANATION_ORIGIN_NOT_APPLICABLE;
    slot->body_index = (uint16_t)body_index;
    slot->op = (uint8_t)op;
    slot->parent_step = (uint16_t)MAELYS_DATALOG_EXPLANATION_NO_STEP;
    explanation_copy_term(&slot->as.comparison.lhs, lhs);
    explanation_copy_term(&slot->as.comparison.rhs, rhs);
    result->witness_filled_mask |= (uint32_t)1u << body_index;
}

static void witness_record_filter(
    maelys_datalog_solve_result_t *result,
    size_t body_index,
    const maelys_datalog_literal_t *literal,
    const maelys_datalog_term_t *value) {
    if (!result || !literal || !value ||
        body_index >= MAELYS_DATALOG_MAX_BODY_LITERALS) return;
    maelys_datalog_explanation_premise_t *slot =
        &result->witness_slots[body_index];
    memset(slot, 0, sizeof(*slot));
    slot->kind = (uint8_t)MAELYS_DATALOG_EXPLANATION_PREMISE_FILTER_TRUE;
    slot->origin = (uint8_t)MAELYS_DATALOG_EXPLANATION_ORIGIN_NOT_APPLICABLE;
    slot->body_index = (uint16_t)body_index;
    slot->parent_step = (uint16_t)MAELYS_DATALOG_EXPLANATION_NO_STEP;
    slot->as.filter.program_index = literal->filter_program_index;
    slot->as.filter.filter_kind = literal->filter_kind;
    explanation_copy_term(&slot->as.filter.value, value);
    result->witness_filled_mask |= (uint32_t)1u << body_index;
}

/* Commit the current builder as the premise range of a freshly added proof
 * node. Committed only when the complete witness fits (§5.4). If any slot is
 * missing or the pool is full, nothing is committed and the node is left
 * without a witness range (provenance unavailable/truncated for that fact) —
 * the resolution result and the historic proof tree are never affected. */
static void witness_commit_range(maelys_datalog_solve_result_t *result,
                                 uint16_t proof_node_idx,
                                 const maelys_datalog_rule_t *rule) {
    if (!result || !rule || proof_node_idx >= MAELYS_DATALOG_MAX_PROOF_NODES) return;
    const size_t body_count = rule->body_count;
    if (body_count > MAELYS_DATALOG_MAX_BODY_LITERALS) return;
    const uint32_t need_mask = (body_count == 0u)
        ? 0u
        : (((uint32_t)1u << body_count) - 1u);
    if ((result->witness_filled_mask & need_mask) != need_mask) return;
    for (size_t i = 0; i < body_count; i++) {
        if (result->witness_slots[i].kind == 0u ||
            result->witness_slots[i].body_index != (uint16_t)i) {
            return;
        }
    }
    if ((size_t)result->premise_pool_count + body_count >
        (size_t)MAELYS_DATALOG_MAX_EXPLANATION_PREMISES) {
        return;
    }
    const uint16_t begin = result->premise_pool_count;
    for (size_t i = 0; i < body_count; i++) {
        explanation_copy_premise(&result->premise_pool[begin + i],
                                 &result->witness_slots[i]);
    }
    result->node_premise_begin[proof_node_idx] = begin;
    result->node_premise_count[proof_node_idx] = (uint16_t)body_count;
    result->node_has_premises[proof_node_idx] = 1u;
    result->premise_pool_count = (uint16_t)(begin + body_count);
}

static int solve_once_append_idb_merge(maelys_datalog_solve_result_t *result,
                                       const maelys_datalog_fact_t *fact,
                                       size_t rule_id,
                                       size_t depth,
                                       uint16_t parent_proof_index,
                                       const maelys_datalog_rule_t *rule) {
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
    /* Associate the complete witness only when a real proof node was added
     * (proof_node_idx is a valid node index, i.e. capacity/depth allowed it). */
    if (proof_node_idx != MAELYS_DATALOG_PROOF_NO_PARENT) {
        witness_commit_range(result, proof_node_idx, rule);
    }
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
    if (!literal) return mask;
    if (literal->kind == MAELYS_DATALOG_LITERAL_FILTER) {
        if (literal->filter_value.kind == MAELYS_DATALOG_TERM_VAR &&
            literal->filter_value.as.variable < 64u) {
            mask |= (uint64_t)1u << literal->filter_value.as.variable;
        }
        return mask;
    }
    if (literal->kind != MAELYS_DATALOG_LITERAL_ATOM) return mask;
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

static int arith_expr_safe_with_bound_vars(const maelys_datalog_rule_t *rule,
                                           uint8_t root,
                                           uint64_t bound_var_mask) {
    if (!rule || root == MAELYS_DATALOG_ARITH_EXPR_NO_NODE ||
        root >= rule->expr_node_count) {
        return 0;
    }
    const maelys_datalog_arith_expr_node_t *node = &rule->expr_nodes[root];
    switch (node->kind) {
        case MAELYS_DATALOG_ARITH_EXPR_INT_LITERAL:
            return 1;
        case MAELYS_DATALOG_ARITH_EXPR_VAR:
            if (node->term.as.variable >= 64u) return 0;
            return (bound_var_mask & ((uint64_t)1u << node->term.as.variable)) != 0;
        case MAELYS_DATALOG_ARITH_EXPR_ADD:
        case MAELYS_DATALOG_ARITH_EXPR_SUB:
        case MAELYS_DATALOG_ARITH_EXPR_MUL:
            return arith_expr_safe_with_bound_vars(rule, node->left, bound_var_mask) &&
                   arith_expr_safe_with_bound_vars(rule, node->right, bound_var_mask);
        default:
            return 0;
    }
}

static int literal_safe_with_bound_vars(const maelys_datalog_rule_t *rule,
                                        uint8_t index,
                                        uint64_t bound_var_mask) {
    if (!rule || index >= rule->body_count) return 0;
    const maelys_datalog_literal_t *literal = &rule->body[index];
    if (literal->kind == MAELYS_DATALOG_LITERAL_ATOM) return 1;
    if (literal->kind == MAELYS_DATALOG_LITERAL_FILTER) {
        return comparison_term_safe_with_bound_vars(
            &literal->filter_value, bound_var_mask);
    }
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
    if (literal->has_arith_expr) {
        return arith_expr_safe_with_bound_vars(rule, literal->lhs_expr_root, bound_var_mask) &&
               arith_expr_safe_with_bound_vars(rule, literal->rhs_expr_root, bound_var_mask);
    }
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
        literal->kind == MAELYS_DATALOG_LITERAL_NEGATED_ATOM ||
        literal->kind == MAELYS_DATALOG_LITERAL_FILTER) {
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

/* Returns 0 on a fatal error, 1 on a normal non-match, and 2 on a match. */
static int solve_once_evaluate_filter_literal(
    maelys_datalog_solve_result_t *result,
    const maelys_datalog_ruleset_t *ruleset,
    const maelys_datalog_literal_t *literal,
    const solve_once_bindings_t *bindings,
    maelys_datalog_term_t *out_value) {
    if (!result || !ruleset || !literal || !bindings) return 0;
    const maelys_datalog_filter_program_t *program = NULL;
    const maelys_datalog_filter_definition_t *definition = NULL;
    const unsigned char *pattern = NULL;
    if (!filter_program_resolve(
            ruleset, literal, &program, &definition, &pattern)) {
        solve_once_set_filter_failure(
            result, MAELYS_ERR_INVALID_STATE, 0u, 0u, 0);
        return 0;
    }
    (void)definition;
    maelys_datalog_term_t value;
    memset(&value, 0, sizeof(value));
    const maelys_datalog_compare_result_t instantiate =
        solve_once_instantiate_comparison_term(
            bindings, &literal->filter_value, &value);
    if (instantiate != MAELYS_DATALOG_COMPARE_TRUE ||
        value.kind != MAELYS_DATALOG_TERM_SYMBOL) {
        solve_once_set_filter_failure(
            result, MAELYS_ERR_INVALID_FIELD, 0u, 0u, value.kind);
        return 0;
    }
    const unsigned char *value_bytes = NULL;
    size_t value_length = 0u;
    if (!filter_symbol_bytes_resolve(&ruleset->symbols,
                                     value.as.symbol,
                                     &value_bytes,
                                     &value_length)) {
        solve_once_set_filter_failure(
            result, MAELYS_ERR_INVALID_STATE, 0u, 0u, value.kind);
        return 0;
    }
    size_t cost = 0u;
    maelys_result_t rc = maelys_datalog_filter_cost(
        (maelys_datalog_filter_kind_t)program->kind,
        value_length,
        program->pattern_length,
        &cost);
    if (rc != MAELYS_OK) {
        solve_once_set_filter_failure(result, rc, 0u, 0u, value.kind);
        return 0;
    }
    if (result->filter_statistics.evaluations >=
            MAELYS_DATALOG_MAX_FILTER_EVALUATIONS ||
        result->filter_statistics.cost_units > MAELYS_DATALOG_MAX_FILTER_COST_UNITS ||
        cost > MAELYS_DATALOG_MAX_FILTER_COST_UNITS -
                   result->filter_statistics.cost_units) {
        const int evaluations_exhausted =
            result->filter_statistics.evaluations >=
            MAELYS_DATALOG_MAX_FILTER_EVALUATIONS;
        solve_once_set_filter_failure(
            result,
            MAELYS_ERR_PAYLOAD_TOO_LARGE,
            evaluations_exhausted
                ? result->filter_statistics.evaluations + 1u
                : result->filter_statistics.cost_units + cost,
            evaluations_exhausted
                ? MAELYS_DATALOG_MAX_FILTER_EVALUATIONS
                : MAELYS_DATALOG_MAX_FILTER_COST_UNITS,
            value.kind);
        return 0;
    }
    int matched = 0;
    rc = maelys_datalog_filter_evaluate(
        (maelys_datalog_filter_kind_t)program->kind,
        value_bytes,
        value_length,
        pattern,
        program->pattern_length,
        &matched);
    if (rc != MAELYS_OK) {
        solve_once_set_filter_failure(result, rc, 0u, 0u, value.kind);
        return 0;
    }
    result->filter_statistics.evaluations++;
    result->filter_statistics.cost_units += cost;
    if (matched) result->filter_statistics.matches++;
    else result->filter_statistics.non_matches++;
    if (out_value) explanation_copy_term(out_value, &value);
    return matched ? 2 : 1;
}

static int solve_once_match_candidate(const maelys_datalog_ruleset_t *ruleset,
                                      maelys_datalog_solve_result_t *result,
                                      const maelys_datalog_rule_t *rule,
                                      size_t literal_index,
                                      solve_once_bindings_t *bindings,
                                      const maelys_datalog_fact_t *candidate,
                                      size_t delta_literal_index,
                                      size_t depth,
                                      uint16_t parent_proof_index,
                                      maelys_datalog_explanation_origin_t candidate_origin,
                                      uint16_t candidate_proof_index) {
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
    /* Positive premise: capture the exact matched candidate before recursion. */
    witness_record_positive(result, literal_index, candidate_origin, candidate, candidate_proof_index);
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
                                      uint16_t parent_proof_index,
                                      maelys_datalog_explanation_origin_t candidate_origin,
                                      const uint16_t *witness_proof_indices) {
    /* Scans all candidates exhaustively.
     * Returns 0 only on fatal internal overflow; it does not stop on first match.
     *
     * parent_proof_indices drives the historic delta parent_index (unchanged).
     * witness_proof_indices is the independent per-candidate proof node used for
     * Why-true provenance; it is read for EVERY IDB scan, delta or not, without
     * altering the historic parent threaded to maelys_datalog_proof_add(). */
    for (size_t i = 0; i < fact_count; i++) {
        const uint16_t candidate_parent = parent_proof_indices
            ? parent_proof_indices[i]
            : parent_proof_index;
        const uint16_t witness_proof = witness_proof_indices
            ? witness_proof_indices[i]
            : (uint16_t)MAELYS_DATALOG_EXPLANATION_NO_STEP;
        if (!solve_once_match_candidate(ruleset,
                                        result,
                                        rule,
                                        literal_index,
                                        bindings,
                                        &facts[i],
                                        delta_literal_index,
                                        depth,
                                        candidate_parent,
                                        candidate_origin,
                                        witness_proof)) {
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
                                 const solve_once_bindings_t *bindings,
                                 maelys_datalog_fact_t *out_ground,
                                 maelys_datalog_explanation_origin_t *out_origin) {
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
    /* On a satisfied negation, publish the exact ground atom whose absence was
     * verified and the store the absence was checked in. This is bounded to the
     * evaluated snapshot/stratum, not a universal claim of impossibility. */
    if (out_ground) *out_ground = query;

    if (def->kind_flags & MAELYS_DATALOG_PRED_KIND_EDB) {
        if (out_origin) *out_origin = MAELYS_DATALOG_EXPLANATION_ORIGIN_EDB;
        return !maelys_datalog_fact_set_contains(&result->edb_snapshot, &query);
    }
    if (def->kind_flags & MAELYS_DATALOG_PRED_KIND_POLICY_FACT) {
        if (out_origin) *out_origin = MAELYS_DATALOG_EXPLANATION_ORIGIN_POLICY_FACT;
        return !solve_once_fact_in_slice(ruleset->facts, ruleset->fact_count, &query);
    }
    if (def->kind_flags & MAELYS_DATALOG_PRED_KIND_IDB) {
        if (out_origin) *out_origin = MAELYS_DATALOG_EXPLANATION_ORIGIN_IDB;
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
    if (literal_index == 0) {
        /* Outermost entry for this rule application: start a fresh witness. */
        result->witness_filled_mask = 0u;
    }
    if (literal_index == rule->body_count) {
        maelys_datalog_fact_t fact;
        memset(&fact, 0, sizeof(fact));
        fact.predicate_id = rule->head.predicate_id;
        fact.arity = rule->head.arity;
        for (size_t i = 0; i < fact.arity; i++) {
            if (!solve_once_instantiate_term(bindings, &rule->head.terms[i], &fact.terms[i])) return 1;
        }
        return solve_once_append_idb_merge(result, &fact, rule->rule_id, depth, parent_proof_index, rule);
    }

    const maelys_datalog_literal_t *literal = &rule->body[literal_index];
    if (literal->kind == MAELYS_DATALOG_LITERAL_COMPARISON) {
        maelys_datalog_term_t cmp_lhs;
        maelys_datalog_term_t cmp_rhs;
        memset(&cmp_lhs, 0, sizeof(cmp_lhs));
        memset(&cmp_rhs, 0, sizeof(cmp_rhs));
        int comparison =
            solve_once_evaluate_comparison_literal(result, rule, literal, bindings, &cmp_lhs, &cmp_rhs);
        if (comparison == 0) return 0;
        if (comparison == 1) return 1;
        witness_record_comparison(result, literal_index, literal->op, &cmp_lhs, &cmp_rhs);
        return solve_once_derive_recursive(ruleset,
                                           result,
                                           rule,
                                           literal_index + 1u,
                                           bindings,
                                           delta_literal_index,
                                           depth,
                                           parent_proof_index);
    }
    if (literal->kind == MAELYS_DATALOG_LITERAL_FILTER) {
        maelys_datalog_term_t value;
        memset(&value, 0, sizeof(value));
        const int filter = solve_once_evaluate_filter_literal(
            result, ruleset, literal, bindings, &value);
        if (filter == 0) return 0;
        if (filter == 1) return 1;
        witness_record_filter(result, literal_index, literal, &value);
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
        maelys_datalog_fact_t neg_ground;
        maelys_datalog_explanation_origin_t neg_origin =
            MAELYS_DATALOG_EXPLANATION_ORIGIN_NOT_APPLICABLE;
        memset(&neg_ground, 0, sizeof(neg_ground));
        if (!solve_negated_literal(result, ruleset, literal, bindings, &neg_ground, &neg_origin)) return 1;
        witness_record_negation(result, literal_index, neg_origin, &neg_ground);
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
                                          parent_proof_index,
                                          MAELYS_DATALOG_EXPLANATION_ORIGIN_IDB,
                                          &result->idb_proof_index[result->idb_delta_begin]);
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
                                    parent_proof_index,
                                    MAELYS_DATALOG_EXPLANATION_ORIGIN_POLICY_FACT,
                                    NULL)) {
        return 0;
    }
    if (def->kind_flags & MAELYS_DATALOG_PRED_KIND_EDB) {
        const maelys_datalog_fact_t *edb_facts = NULL;
        size_t edb_count = 0;
        solve_once_edb_slice(result, literal->atom.predicate_id, &edb_facts, &edb_count);
        if (!solve_once_scan_candidates(ruleset,
                                        result,
                                        rule,
                                        literal_index,
                                        bindings,
                                        edb_facts,
                                        edb_count,
                                        delta_literal_index,
                                        depth,
                                        NULL,
                                        parent_proof_index,
                                        MAELYS_DATALOG_EXPLANATION_ORIGIN_EDB,
                                        NULL)) {
            return 0;
        }
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
                                        parent_proof_index,
                                        MAELYS_DATALOG_EXPLANATION_ORIGIN_IDB,
                                        &result->idb_proof_index[begin])) {
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
                                              uint16_t parent_proof_index,
                                              maelys_datalog_explanation_origin_t candidate_origin,
                                              uint16_t candidate_proof_index) {
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
    /* Record into the lexical body slot (literal_index), not the join position,
     * so provenance stays in body order even under the static join plan. */
    witness_record_positive(result, literal_index, candidate_origin, candidate, candidate_proof_index);
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
                                              uint16_t parent_proof_index,
                                              maelys_datalog_explanation_origin_t candidate_origin,
                                              const uint16_t *witness_proof_indices) {
    for (size_t i = 0; i < fact_count; i++) {
        const uint16_t candidate_parent = parent_proof_indices
            ? parent_proof_indices[i]
            : parent_proof_index;
        const uint16_t witness_proof = witness_proof_indices
            ? witness_proof_indices[i]
            : (uint16_t)MAELYS_DATALOG_EXPLANATION_NO_STEP;
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
                                                candidate_parent,
                                                candidate_origin,
                                                witness_proof)) {
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
    if (order_pos == 0) {
        /* Outermost entry for this rule application: start a fresh witness. */
        result->witness_filled_mask = 0u;
    }
    if (order_pos == join_order_count) {
        maelys_datalog_fact_t fact;
        memset(&fact, 0, sizeof(fact));
        fact.predicate_id = rule->head.predicate_id;
        fact.arity = rule->head.arity;
        for (size_t i = 0; i < fact.arity; i++) {
            if (!solve_once_instantiate_term(bindings, &rule->head.terms[i], &fact.terms[i])) return 1;
        }
        return solve_once_append_idb_merge(result, &fact, rule->rule_id, depth, parent_proof_index, rule);
    }

    if (!join_order || order_pos >= join_order_count) return 0;
    const size_t literal_index = join_order[order_pos];
    if (literal_index >= rule->body_count) return 0;
    const maelys_datalog_literal_t *literal = &rule->body[literal_index];
    if (literal->kind == MAELYS_DATALOG_LITERAL_COMPARISON) {
        maelys_datalog_term_t cmp_lhs;
        maelys_datalog_term_t cmp_rhs;
        memset(&cmp_lhs, 0, sizeof(cmp_lhs));
        memset(&cmp_rhs, 0, sizeof(cmp_rhs));
        int comparison =
            solve_once_evaluate_comparison_literal(result, rule, literal, bindings, &cmp_lhs, &cmp_rhs);
        if (comparison == 0) return 0;
        if (comparison == 1) return 1;
        witness_record_comparison(result, literal_index, literal->op, &cmp_lhs, &cmp_rhs);
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
    if (literal->kind == MAELYS_DATALOG_LITERAL_FILTER) {
        maelys_datalog_term_t value;
        memset(&value, 0, sizeof(value));
        const int filter = solve_once_evaluate_filter_literal(
            result, ruleset, literal, bindings, &value);
        if (filter == 0) return 0;
        if (filter == 1) return 1;
        witness_record_filter(result, literal_index, literal, &value);
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
        maelys_datalog_fact_t neg_ground;
        maelys_datalog_explanation_origin_t neg_origin =
            MAELYS_DATALOG_EXPLANATION_ORIGIN_NOT_APPLICABLE;
        memset(&neg_ground, 0, sizeof(neg_ground));
        if (!solve_negated_literal(result, ruleset, literal, bindings, &neg_ground, &neg_origin)) return 1;
        witness_record_negation(result, literal_index, neg_origin, &neg_ground);
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
                                                  parent_proof_index,
                                                  MAELYS_DATALOG_EXPLANATION_ORIGIN_IDB,
                                                  &result->idb_proof_index[result->idb_delta_begin]);
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
                                            parent_proof_index,
                                            MAELYS_DATALOG_EXPLANATION_ORIGIN_POLICY_FACT,
                                            NULL)) {
        return 0;
    }
    if (def->kind_flags & MAELYS_DATALOG_PRED_KIND_EDB) {
        const maelys_datalog_fact_t *edb_facts = NULL;
        size_t edb_count = 0;
        solve_once_edb_slice(result, literal->atom.predicate_id, &edb_facts, &edb_count);
        if (!solve_once_scan_candidates_ordered(ruleset,
                                                result,
                                                rule,
                                                order_pos,
                                                join_order,
                                                join_order_count,
                                                bindings,
                                                edb_facts,
                                                edb_count,
                                                delta_literal_index,
                                                depth,
                                                NULL,
                                                parent_proof_index,
                                                MAELYS_DATALOG_EXPLANATION_ORIGIN_EDB,
                                                NULL)) {
            return 0;
        }
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
                                                parent_proof_index,
                                                MAELYS_DATALOG_EXPLANATION_ORIGIN_IDB,
                                                &result->idb_proof_index[begin])) {
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
    maelys_datalog_solve_diagnostic_t *out_diag,
    int full_scan_reference) {
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
    if (!ruleset_filters_structurally_valid(ruleset)) {
        solve_once_diag_base(out_diag,
                             MAELYS_DATALOG_SOLVE_DIAG_FILTER_ERROR,
                             MAELYS_ERR_INVALID_STATE,
                             MAELYS_DATALOG_DENY_FILTER_ERROR);
        return MAELYS_ERR_INVALID_STATE;
    }
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
#ifdef MAELYS_TESTING
    result->edb_full_scan_reference = full_scan_reference;
#else
    (void)full_scan_reference;
#endif
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
    maelys_result_t range_rc = solve_once_build_edb_ranges(result);
    if (range_rc != MAELYS_OK) {
        result->failed = 1;
        solve_once_diag_base(out_diag,
                             MAELYS_DATALOG_SOLVE_DIAG_INVALID_STATE,
                             range_rc,
                             MAELYS_DATALOG_DENY_NONE);
        maelys_datalog_solve_result_free(result);
        return range_rc;
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
    int use_static_join_order,
    int full_scan_reference) {
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
    if (!ruleset_filters_structurally_valid(ruleset)) {
        solve_once_diag_base(out_diag,
                             MAELYS_DATALOG_SOLVE_DIAG_FILTER_ERROR,
                             MAELYS_ERR_INVALID_STATE,
                             MAELYS_DATALOG_DENY_FILTER_ERROR);
        return MAELYS_ERR_INVALID_STATE;
    }
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
#ifdef MAELYS_TESTING
    result->edb_full_scan_reference = full_scan_reference;
#else
    (void)full_scan_reference;
#endif
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
    maelys_result_t range_rc = solve_once_build_edb_ranges(result);
    if (range_rc != MAELYS_OK) {
        result->failed = 1;
        solve_once_diag_base(out_diag,
                             MAELYS_DATALOG_SOLVE_DIAG_INVALID_STATE,
                             range_rc,
                             MAELYS_DATALOG_DENY_NONE);
        maelys_datalog_solve_result_free(result);
        return range_rc;
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
        return solve_stratified_path(ruleset, edb, out_result, out_diag, 0);
    }
    return maelys_datalog_solve_once_run(ruleset, edb, out_result, out_diag, 1, 0);
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
    return maelys_datalog_solve_once_run(ruleset, edb, out_result, out_diag, 0, 0);
}

maelys_result_t maelys_datalog_test_solve_once_full_scan(
    const maelys_datalog_ruleset_t *ruleset,
    const maelys_datalog_edb_t *edb,
    maelys_datalog_solve_result_t **out_result,
    maelys_datalog_solve_diagnostic_t *out_diag) {
    if (ruleset && ruleset->negation_supported) {
        return solve_stratified_path(ruleset, edb, out_result, out_diag, 1);
    }
    return maelys_datalog_solve_once_run(ruleset, edb, out_result, out_diag, 1, 1);
}

maelys_result_t maelys_datalog_test_solve_once_legacy_full_scan(
    const maelys_datalog_ruleset_t *ruleset,
    const maelys_datalog_edb_t *edb,
    maelys_datalog_solve_result_t **out_result,
    maelys_datalog_solve_diagnostic_t *out_diag) {
    return maelys_datalog_solve_once_run(ruleset, edb, out_result, out_diag, 0, 1);
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

maelys_result_t maelys_datalog_test_solve_result_edb_slice(
    const maelys_datalog_solve_result_t *result,
    maelys_datalog_predicate_id_t predicate_id,
    const maelys_datalog_fact_t **out_facts,
    size_t *out_count) {
    if (!result || !out_facts || !out_count) return MAELYS_ERR_INVALID_ARGUMENT;
    solve_once_edb_slice(result, predicate_id, out_facts, out_count);
    return MAELYS_OK;
}

maelys_result_t maelys_datalog_test_solve_result_edb_range_stats(
    const maelys_datalog_solve_result_t *result,
    size_t *out_sum,
    int *out_ascending,
    int *out_non_overlapping) {
    if (!result || !out_sum || !out_ascending || !out_non_overlapping) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    size_t sum = 0;
    size_t previous_end = 0;
    int have_previous = 0;
    int ascending = 1;
    int non_overlapping = 1;
    for (size_t i = 0; i < MAELYS_DATALOG_MAX_PREDICATES; i++) {
        const maelys_datalog_pred_range_t range = result->edb_ranges[i];
        if (range.count == 0) continue;
        const size_t begin = range.begin;
        const size_t end = begin + range.count;
        if (end > result->edb_snapshot.count) {
            ascending = 0;
            non_overlapping = 0;
        }
        if (have_previous && begin < previous_end) {
            ascending = 0;
            non_overlapping = 0;
        }
        sum += range.count;
        previous_end = end;
        have_previous = 1;
    }
    *out_sum = sum;
    *out_ascending = ascending;
    *out_non_overlapping = non_overlapping;
    return MAELYS_OK;
}

maelys_result_t maelys_datalog_test_edb_slice_null_base(
    maelys_datalog_predicate_id_t predicate_id,
    const maelys_datalog_fact_t **out_facts,
    size_t *out_count) {
    if (!out_facts || !out_count) return MAELYS_ERR_INVALID_ARGUMENT;
    maelys_datalog_solve_result_t result;
    memset(&result, 0, sizeof(result));
    result.edb_snapshot.facts = NULL;
    result.edb_snapshot.count = 0;
    result.edb_snapshot.sorted = 1;
    solve_once_edb_slice(&result, predicate_id, out_facts, out_count);
    return MAELYS_OK;
}

maelys_result_t maelys_datalog_test_bindings_bitmask_probe(
    size_t *out_size,
    uint32_t *out_initial_mask,
    int *out_var0_bound,
    int *out_var31_bound,
    int *out_var30_bound,
    int *out_equal_rebind,
    int *out_different_rebind) {
    if (!out_size || !out_initial_mask || !out_var0_bound || !out_var31_bound ||
        !out_var30_bound || !out_equal_rebind || !out_different_rebind) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    solve_once_bindings_t bindings;
    memset(&bindings, 0, sizeof(bindings));
    *out_size = sizeof(bindings);
    *out_initial_mask = bindings.bound_mask;

    maelys_datalog_term_t var0 = {.kind = MAELYS_DATALOG_TERM_VAR};
    var0.as.variable = 0;
    maelys_datalog_term_t var31 = {.kind = MAELYS_DATALOG_TERM_VAR};
    var31.as.variable = 31;
    maelys_datalog_term_t value0 = {.kind = MAELYS_DATALOG_TERM_INT};
    value0.as.integer = 7;
    maelys_datalog_term_t value0_same = value0;
    maelys_datalog_term_t value0_different = {.kind = MAELYS_DATALOG_TERM_INT};
    value0_different.as.integer = 8;
    maelys_datalog_term_t value31 = {.kind = MAELYS_DATALOG_TERM_SYMBOL};
    value31.as.symbol = 31;

    if (!solve_once_bind_or_match(&bindings, &var0, &value0)) return MAELYS_ERR_INVALID_STATE;
    if (!solve_once_bind_or_match(&bindings, &var31, &value31)) return MAELYS_ERR_INVALID_STATE;
    *out_var0_bound = solve_once_bindings_is_bound(&bindings, 0);
    *out_var31_bound = solve_once_bindings_is_bound(&bindings, 31);
    *out_var30_bound = solve_once_bindings_is_bound(&bindings, 30);
    *out_equal_rebind = solve_once_bind_or_match(&bindings, &var0, &value0_same);
    *out_different_rebind = solve_once_bind_or_match(&bindings, &var0, &value0_different);
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

/* Depth-first emission of the witness DAG rooted at a proof node.
 *
 * Emits ancestors before descendants, deduplicates shared parent nodes via
 * local_step[], detects cycles/invalid indices via on_stack[], and remaps each
 * IDB premise's parent to a local step index. Returns 1 on a complete subtree,
 * 0 on any incompleteness (missing witness, dangling parent, capacity, cycle),
 * which the caller turns into an atomically empty, truncated explanation.
 *
 * Recursion is bounded by MAELYS_DATALOG_MAX_PROOF_NODES (on_stack prevents
 * revisiting any node already on the current path; local_step short-circuits
 * already-emitted nodes). */
static int explain_visit_node(const maelys_datalog_solve_result_t *result,
                              uint16_t node_idx,
                              maelys_datalog_explanation_t *out,
                              uint16_t *local_step,
                              uint8_t *on_stack,
                              size_t recursion_depth) {
    if (node_idx >= result->proof.node_count) return 0;
    if (local_step[node_idx] != (uint16_t)MAELYS_DATALOG_EXPLANATION_NO_STEP) return 1;
    if (on_stack[node_idx]) return 0;
    if (recursion_depth > MAELYS_DATALOG_MAX_PROOF_NODES) return 0;
    if (!result->node_has_premises[node_idx]) return 0;

    const uint16_t begin = result->node_premise_begin[node_idx];
    const uint16_t count = result->node_premise_count[node_idx];
    if ((size_t)begin + (size_t)count > (size_t)result->premise_pool_count) return 0;

    on_stack[node_idx] = 1u;
    /* Ancestors first: emit every IDB premise parent before this step. */
    for (uint16_t i = 0; i < count; i++) {
        const maelys_datalog_explanation_premise_t *p = &result->premise_pool[begin + i];
        if (p->kind == (uint8_t)MAELYS_DATALOG_EXPLANATION_PREMISE_POSITIVE_FACT &&
            p->origin == (uint8_t)MAELYS_DATALOG_EXPLANATION_ORIGIN_IDB) {
            if (p->parent_step == (uint16_t)MAELYS_DATALOG_EXPLANATION_NO_STEP) {
                on_stack[node_idx] = 0u;
                return 0;
            }
            if (!explain_visit_node(result, p->parent_step, out, local_step, on_stack,
                                    recursion_depth + 1u)) {
                on_stack[node_idx] = 0u;
                return 0;
            }
        }
    }

    if (out->step_count >= MAELYS_DATALOG_MAX_EXPLANATION_STEPS ||
        (size_t)out->premise_count + (size_t)count > (size_t)MAELYS_DATALOG_MAX_EXPLANATION_PREMISES) {
        on_stack[node_idx] = 0u;
        return 0;
    }

    const uint16_t premise_begin = out->premise_count;
    for (uint16_t i = 0; i < count; i++) {
        maelys_datalog_explanation_premise_t copy;
        explanation_copy_premise(&copy, &result->premise_pool[begin + i]);
        if (copy.kind == (uint8_t)MAELYS_DATALOG_EXPLANATION_PREMISE_POSITIVE_FACT &&
            copy.origin == (uint8_t)MAELYS_DATALOG_EXPLANATION_ORIGIN_IDB) {
            const uint16_t raw = copy.parent_step;
            if (raw >= result->proof.node_count) { on_stack[node_idx] = 0u; return 0; }
            const uint16_t mapped = local_step[raw];
            if (mapped == (uint16_t)MAELYS_DATALOG_EXPLANATION_NO_STEP) {
                on_stack[node_idx] = 0u;
                return 0;
            }
            copy.parent_step = mapped;
        } else {
            copy.parent_step = (uint16_t)MAELYS_DATALOG_EXPLANATION_NO_STEP;
        }
        explanation_copy_premise(&out->premises[out->premise_count], &copy);
        out->premise_count++;
    }

    maelys_datalog_explanation_step_t *step = &out->steps[out->step_count];
    memset(step, 0, sizeof(*step));
    step->rule_id = result->proof.nodes[node_idx].rule_id;
    explanation_copy_fact(&step->derived_fact,
                          &result->proof.nodes[node_idx].derived_fact);
    step->premise_begin = premise_begin;
    step->premise_count = count;
    local_step[node_idx] = out->step_count;
    out->step_count++;
    on_stack[node_idx] = 0u;
    return 1;
}

maelys_result_t maelys_datalog_explain_solved_fact(
    const maelys_datalog_solve_result_t *result,
    const maelys_datalog_fact_t *queried_fact,
    maelys_datalog_explanation_t *out_explanation) {
    if (!result || !queried_fact || !out_explanation) return MAELYS_ERR_INVALID_ARGUMENT;
    if (!result->finalized || result->failed || !result->ruleset || !result->ruleset->loaded) {
        return MAELYS_ERR_INVALID_STATE;
    }
    if (!datalog_fact_structurally_valid(&result->ruleset->registry, queried_fact)) {
        return MAELYS_ERR_INVALID_FIELD;
    }

    /* All argument/state/field validation done; the output may now be touched. */
    memset(out_explanation, 0, sizeof(*out_explanation));

    /* Presence is defined by membership in the finalized IDB set. */
    if (!maelys_datalog_fact_set_contains(&result->idb_final, queried_fact)) {
        return MAELYS_OK; /* found=0, truncated=0, empty */
    }
    out_explanation->found = 1u;

    /* Canonical witness: the first non-deny proof node deriving this fact — the
     * same first-witness authority the historic proof tree already uses. */
    size_t canonical = result->proof.node_count;
    for (size_t n = 0; n < result->proof.node_count; n++) {
        const maelys_datalog_proof_node_t *node = &result->proof.nodes[n];
        if (node->deny_reason != MAELYS_DATALOG_DENY_NONE) continue;
        if (maelys_datalog_fact_equals(&node->derived_fact, queried_fact)) {
            canonical = n;
            break;
        }
    }
    if (canonical >= result->proof.node_count) {
        out_explanation->truncated = 1u; /* present but no usable proof node */
        return MAELYS_OK;
    }

    uint16_t local_step[MAELYS_DATALOG_MAX_PROOF_NODES];
    uint8_t on_stack[MAELYS_DATALOG_MAX_PROOF_NODES];
    for (size_t i = 0; i < MAELYS_DATALOG_MAX_PROOF_NODES; i++) {
        local_step[i] = (uint16_t)MAELYS_DATALOG_EXPLANATION_NO_STEP;
        on_stack[i] = 0u;
    }

    if (!explain_visit_node(result, (uint16_t)canonical, out_explanation,
                            local_step, on_stack, 0u)) {
        /* Incomplete provenance -> atomically empty, truncated (§3.3, §3.6). */
        memset(out_explanation, 0, sizeof(*out_explanation));
        out_explanation->found = 1u;
        out_explanation->truncated = 1u;
        return MAELYS_OK;
    }

    out_explanation->truncated = 0u;
    return MAELYS_OK;
}

maelys_result_t maelys_datalog_solve_result_derived_fact_count(
    const maelys_datalog_solve_result_t *result,
    size_t *out_count) {
    if (!result || !out_count) return MAELYS_ERR_INVALID_ARGUMENT;
    if (!result->finalized || result->failed) return MAELYS_ERR_INVALID_STATE;
    *out_count = result->idb_current_end;
    return MAELYS_OK;
}

maelys_result_t maelys_datalog_solve_result_filter_statistics(
    const maelys_datalog_solve_result_t *result,
    maelys_datalog_filter_statistics_t *out_statistics) {
    if (!result || !out_statistics) return MAELYS_ERR_INVALID_ARGUMENT;
    if (!result->finalized || result->failed) return MAELYS_ERR_INVALID_STATE;
    *out_statistics = result->filter_statistics;
    return MAELYS_OK;
}

maelys_result_t maelys_datalog_solve_result_enumerate_predicate_facts(
    const maelys_datalog_solve_result_t *result,
    const char *predicate,
    size_t arity,
    maelys_datalog_fact_t *out_facts,
    size_t out_capacity,
    size_t *out_count) {
    if (!result || !predicate || (!out_facts && out_capacity > 0u) || !out_count) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    *out_count = 0u;
    if (arity > MAELYS_DATALOG_MAX_ARITY) return MAELYS_ERR_INVALID_FIELD;
    if (!result->finalized || result->failed || !result->ruleset || !result->ruleset->loaded) {
        return MAELYS_ERR_INVALID_STATE;
    }
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

    size_t found = 0u;
    for (size_t i = 0u; i < result->idb_current_end; i++) {
        if (result->idb_facts[i].predicate_id != pid) continue;
        if (found < out_capacity) {
            out_facts[found] = result->idb_facts[i];
        }
        found++;
    }
    *out_count = found;
    return MAELYS_OK;
}

void maelys_datalog_solve_result_free(maelys_datalog_solve_result_t *result) {
    if (!result) return;
    if (result->release) {
        result->release(result->release_owner, result);
    }
    memset(result, 0, sizeof(*result));
    free(result);
}

void maelys_datalog_solve_result_set_release(
    maelys_datalog_solve_result_t *result,
    void *owner,
    maelys_datalog_solve_result_release_fn release) {
    if (!result) return;
    result->release_owner = owner;
    result->release = release;
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

static maelys_result_t validate_solved_query_predicate(
    const maelys_datalog_solve_result_t *result,
    const char *predicate,
    size_t arity,
    maelys_datalog_predicate_id_t *out_pid) {
    if (result->ruleset->enforces_query_whitelist &&
        !query_whitelist_contains(result->ruleset, predicate, arity)) {
        return MAELYS_ERR_FORBIDDEN;
    }

    maelys_datalog_predicate_id_t pid = 0;
    if (!maelys_datalog_predicate_registry_find(
            &result->ruleset->registry, predicate, arity, &pid)) {
        return MAELYS_ERR_INVALID_FIELD;
    }
    const maelys_datalog_predicate_def_t *def =
        maelys_datalog_predicate_registry_get(&result->ruleset->registry, pid);
    if (!def || !(def->kind_flags & MAELYS_DATALOG_PRED_KIND_QUERY)) {
        return MAELYS_ERR_INVALID_FIELD;
    }
    if (out_pid) *out_pid = pid;
    return MAELYS_OK;
}

maelys_result_t maelys_datalog_validate_solved_ground_query(
    const maelys_datalog_solve_result_t *result,
    const char *predicate,
    size_t arity) {
    if (!result || !predicate) return MAELYS_ERR_INVALID_ARGUMENT;
    if (arity > MAELYS_DATALOG_MAX_ARITY) return MAELYS_ERR_INVALID_FIELD;
    if (!result->finalized || result->failed || !result->ruleset || !result->ruleset->loaded) {
        return MAELYS_ERR_INVALID_STATE;
    }
    return validate_solved_query_predicate(result, predicate, arity, NULL);
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

    maelys_datalog_predicate_id_t pid = 0;
    maelys_result_t rc =
        validate_solved_query_predicate(result, predicate, arity, &pid);
    if (rc != MAELYS_OK) return rc;
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

/* -------------------------------------------------------------------------
 * Experimental bounded Why-false exploration.
 *
 * This deliberately uses a stable, lowest-lexical-safe literal order instead
 * of build_static_join_order(): planner heuristics are not public explanation
 * identity. The evaluator only reads the finalized solve state.
 * ------------------------------------------------------------------------- */

typedef struct {
    const maelys_datalog_fact_t *fact;
    maelys_datalog_explanation_origin_t origin;
} why_false_candidate_t;

typedef struct {
    solve_once_bindings_t bindings;
    maelys_datalog_why_false_support_t
        supports[MAELYS_DATALOG_MAX_WHY_FALSE_SUPPORTS];
    uint16_t support_count;
} why_false_branch_t;

typedef struct {
    const maelys_datalog_rule_t *rule;
    maelys_datalog_fact_t target;
    size_t depth;
    maelys_datalog_fact_t path[MAELYS_DATALOG_MAX_PROOF_DEPTH + 1u];
} why_false_rule_task_t;

typedef struct {
    const maelys_datalog_solve_result_t *result;
    const maelys_datalog_why_false_limits_t *limits;
    maelys_datalog_why_false_explanation_t *out;
    maelys_datalog_fact_t path[MAELYS_DATALOG_MAX_PROOF_DEPTH + 1u];
    uint16_t symbol_rank[MAELYS_DATALOG_MAX_SYMBOLS + 1u];
    why_false_rule_task_t *frontier;
    size_t frontier_count;
    size_t frontier_capacity;
    int frontier_truncated;
    maelys_result_t fatal_error;
} why_false_context_t;

static maelys_result_t why_false_build_symbol_ranks(
    why_false_context_t *context) {
    const maelys_datalog_symbol_table_t *symbols =
        &context->result->ruleset->symbols;
    if (symbols->count > MAELYS_DATALOG_MAX_SYMBOLS ||
        symbols->used > sizeof(symbols->storage)) {
        return MAELYS_ERR_INVALID_STATE;
    }
    for (size_t index = 0u; index < symbols->count; index++) {
        const size_t offset = (size_t)symbols->entries[index].offset;
        const size_t length = (size_t)symbols->entries[index].len;
        if (offset >= symbols->used || length >= symbols->used - offset ||
            symbols->storage[offset + length] != '\0' ||
            memchr(symbols->storage + offset, '\0', length) != NULL) {
            return MAELYS_ERR_INVALID_STATE;
        }
    }
    for (maelys_datalog_symbol_id_t id = 1u;
         id <= symbols->count;
         id++) {
        const char *text = maelys_datalog_symbol_text(symbols, id);
        if (!text) return MAELYS_ERR_INVALID_STATE;
        uint16_t rank = 1u;
        for (maelys_datalog_symbol_id_t other = 1u;
             other <= symbols->count;
             other++) {
            if (other == id) continue;
            const char *other_text = maelys_datalog_symbol_text(symbols, other);
            if (!other_text) return MAELYS_ERR_INVALID_STATE;
            const int comparison = strcmp(other_text, text);
            if (comparison == 0) return MAELYS_ERR_INVALID_STATE;
            if (comparison < 0) rank++;
        }
        context->symbol_rank[id] = rank;
    }
    return MAELYS_OK;
}

static int why_false_term_symbol_resolves(
    const why_false_context_t *context,
    const maelys_datalog_term_t *term) {
    if (term->kind != MAELYS_DATALOG_TERM_SYMBOL) return 1;
    const maelys_datalog_symbol_id_t id = term->as.symbol;
    return maelys_datalog_symbol_id_is_valid(
               &context->result->ruleset->symbols, id) &&
           context->symbol_rank[id] != 0u;
}

static int why_false_fact_symbols_resolve(
    const why_false_context_t *context,
    const maelys_datalog_fact_t *fact) {
    if (!fact || fact->arity > MAELYS_DATALOG_MAX_TERMS) return 0;
    for (size_t term = 0u; term < fact->arity; term++) {
        if (!why_false_term_symbol_resolves(context, &fact->terms[term])) {
            return 0;
        }
    }
    return 1;
}

static maelys_result_t why_false_validate_result_symbols(
    const why_false_context_t *context,
    const maelys_datalog_fact_t *queried_fact) {
    const maelys_datalog_ruleset_t *ruleset = context->result->ruleset;
    if (!why_false_fact_symbols_resolve(context, queried_fact)) {
        return MAELYS_ERR_INVALID_STATE;
    }
    for (size_t fact = 0u; fact < ruleset->fact_count; fact++) {
        if (!why_false_fact_symbols_resolve(context, &ruleset->facts[fact])) {
            return MAELYS_ERR_INVALID_STATE;
        }
    }
    for (size_t fact = 0u; fact < context->result->edb_snapshot.count; fact++) {
        if (!why_false_fact_symbols_resolve(
                context, &context->result->edb_snapshot.facts[fact])) {
            return MAELYS_ERR_INVALID_STATE;
        }
    }
    for (size_t fact = 0u; fact < context->result->idb_final.count; fact++) {
        if (!why_false_fact_symbols_resolve(
                context, &context->result->idb_final.facts[fact])) {
            return MAELYS_ERR_INVALID_STATE;
        }
    }
    for (size_t rule_index = 0u; rule_index < ruleset->rule_count; rule_index++) {
        const maelys_datalog_rule_t *rule = &ruleset->rules[rule_index];
        if (!why_false_fact_symbols_resolve(context, &rule->head)) {
            return MAELYS_ERR_INVALID_STATE;
        }
        for (size_t body = 0u; body < rule->body_count; body++) {
            const maelys_datalog_literal_t *literal = &rule->body[body];
            if ((literal->kind == MAELYS_DATALOG_LITERAL_ATOM ||
                 literal->kind == MAELYS_DATALOG_LITERAL_NEGATED_ATOM) &&
                !why_false_fact_symbols_resolve(context, &literal->atom)) {
                return MAELYS_ERR_INVALID_STATE;
            }
            if (literal->kind == MAELYS_DATALOG_LITERAL_COMPARISON &&
                (!why_false_term_symbol_resolves(context, &literal->lhs) ||
                 !why_false_term_symbol_resolves(context, &literal->rhs))) {
                return MAELYS_ERR_INVALID_STATE;
            }
            if (literal->kind == MAELYS_DATALOG_LITERAL_FILTER) {
                const maelys_datalog_filter_program_t *program = NULL;
                const maelys_datalog_filter_definition_t *definition = NULL;
                const unsigned char *pattern = NULL;
                if (!why_false_term_symbol_resolves(
                        context, &literal->filter_value) ||
                    !filter_program_resolve(ruleset,
                                            literal,
                                            &program,
                                            &definition,
                                            &pattern)) {
                    return MAELYS_ERR_INVALID_STATE;
                }
                (void)program;
                (void)definition;
                (void)pattern;
            }
        }
        for (size_t node = 0u; node < rule->expr_node_count; node++) {
            if (!why_false_term_symbol_resolves(
                    context, &rule->expr_nodes[node].term)) {
                return MAELYS_ERR_INVALID_STATE;
            }
        }
    }
    return MAELYS_OK;
}

static int why_false_term_cmp(
    why_false_context_t *context,
    const maelys_datalog_term_t *lhs,
    const maelys_datalog_term_t *rhs) {
    if (lhs->kind != rhs->kind) return (int)lhs->kind - (int)rhs->kind;
    switch (lhs->kind) {
        case MAELYS_DATALOG_TERM_SYMBOL: {
            const maelys_datalog_symbol_table_t *symbols =
                &context->result->ruleset->symbols;
            if (!maelys_datalog_symbol_id_is_valid(symbols, lhs->as.symbol) ||
                !maelys_datalog_symbol_id_is_valid(symbols, rhs->as.symbol) ||
                context->symbol_rank[lhs->as.symbol] == 0u ||
                context->symbol_rank[rhs->as.symbol] == 0u) {
                context->fatal_error = MAELYS_ERR_INVALID_STATE;
                return 0;
            }
            if (context->symbol_rank[lhs->as.symbol] <
                context->symbol_rank[rhs->as.symbol]) return -1;
            if (context->symbol_rank[lhs->as.symbol] >
                context->symbol_rank[rhs->as.symbol]) return 1;
            return 0;
        }
        case MAELYS_DATALOG_TERM_INT:
            if (lhs->as.integer < rhs->as.integer) return -1;
            if (lhs->as.integer > rhs->as.integer) return 1;
            return 0;
        case MAELYS_DATALOG_TERM_BOOL:
            return lhs->as.boolean - rhs->as.boolean;
        case MAELYS_DATALOG_TERM_VAR:
            if (lhs->as.variable < rhs->as.variable) return -1;
            if (lhs->as.variable > rhs->as.variable) return 1;
            return 0;
        default:
            context->fatal_error = MAELYS_ERR_INVALID_STATE;
            return 0;
    }
}

static int why_false_fact_cmp(
    why_false_context_t *context,
    const maelys_datalog_fact_t *left,
    const maelys_datalog_fact_t *right) {
    if (left->predicate_id < right->predicate_id) return -1;
    if (left->predicate_id > right->predicate_id) return 1;
    if (left->arity < right->arity) return -1;
    if (left->arity > right->arity) return 1;
    for (size_t term = 0u; term < left->arity; term++) {
        const int value_cmp = why_false_term_cmp(
            context, &left->terms[term], &right->terms[term]);
        if (value_cmp != 0) return value_cmp;
        if (context->fatal_error != MAELYS_OK) return 0;
    }
    return 0;
}

static int why_false_candidate_cmp(
    why_false_context_t *context,
    const why_false_candidate_t *left,
    const why_false_candidate_t *right) {
    const int fact_cmp = why_false_fact_cmp(context, left->fact, right->fact);
    if (fact_cmp != 0) return fact_cmp;
    return (int)left->origin - (int)right->origin;
}

static int why_false_diagnostic_cmp(
    why_false_context_t *context,
    const maelys_datalog_why_false_diagnostic_t *left,
    const maelys_datalog_why_false_diagnostic_t *right) {
    if (left->rule_id < right->rule_id) return -1;
    if (left->rule_id > right->rule_id) return 1;
    for (size_t variable = 0u;
         variable < MAELYS_DATALOG_MAX_RULE_VARIABLES;
         variable++) {
        const uint32_t bit = (uint32_t)1u << variable;
        const int left_bound = (left->bound_variable_mask & bit) != 0u;
        const int right_bound = (right->bound_variable_mask & bit) != 0u;
        if (left_bound != right_bound) return left_bound ? -1 : 1;
        if (!left_bound) continue;
        const int value_cmp = why_false_term_cmp(
            context,
            &left->substitution[variable],
            &right->substitution[variable]);
        if (value_cmp != 0) return value_cmp;
        if (context->fatal_error != MAELYS_OK) return 0;
    }
    if (left->obstacle.body_index < right->obstacle.body_index) return -1;
    if (left->obstacle.body_index > right->obstacle.body_index) return 1;
    if (left->obstacle.kind < right->obstacle.kind) return -1;
    if (left->obstacle.kind > right->obstacle.kind) return 1;
    if (left->obstacle.kind ==
        MAELYS_DATALOG_WHY_FALSE_OBSTACLE_FILTER_FALSE) {
        if (left->obstacle.filter_kind < right->obstacle.filter_kind) return -1;
        if (left->obstacle.filter_kind > right->obstacle.filter_kind) return 1;
        const int value_cmp = why_false_term_cmp(
            context,
            &left->obstacle.filter_value,
            &right->obstacle.filter_value);
        if (value_cmp != 0) return value_cmp;
        const maelys_datalog_ruleset_t *ruleset = context->result->ruleset;
        if (left->obstacle.filter_program_index >= ruleset->filter_program_count ||
            right->obstacle.filter_program_index >= ruleset->filter_program_count) {
            context->fatal_error = MAELYS_ERR_INVALID_STATE;
            return 0;
        }
        const maelys_datalog_filter_program_t *left_program =
            &ruleset->filter_programs[left->obstacle.filter_program_index];
        const maelys_datalog_filter_program_t *right_program =
            &ruleset->filter_programs[right->obstacle.filter_program_index];
        if (left_program->pattern_offset > ruleset->filter_pattern_pool_used ||
            left_program->pattern_length > ruleset->filter_pattern_pool_used -
                                               left_program->pattern_offset ||
            right_program->pattern_offset > ruleset->filter_pattern_pool_used ||
            right_program->pattern_length > ruleset->filter_pattern_pool_used -
                                                right_program->pattern_offset) {
            context->fatal_error = MAELYS_ERR_INVALID_STATE;
            return 0;
        }
        const size_t common = left_program->pattern_length < right_program->pattern_length
            ? left_program->pattern_length
            : right_program->pattern_length;
        const int pattern_cmp = memcmp(
            ruleset->filter_pattern_pool + left_program->pattern_offset,
            ruleset->filter_pattern_pool + right_program->pattern_offset,
            common);
        if (pattern_cmp != 0) return pattern_cmp;
        if (left_program->pattern_length < right_program->pattern_length) return -1;
        if (left_program->pattern_length > right_program->pattern_length) return 1;
    }
    const int target_cmp = why_false_fact_cmp(
        context, &left->target_fact, &right->target_fact);
    if (target_cmp != 0) return target_cmp;
    if (context->fatal_error != MAELYS_OK) return 0;
    if (left->depth < right->depth) return -1;
    if (left->depth > right->depth) return 1;
    return 0;
}

static int why_false_limits_valid(
    const maelys_datalog_why_false_limits_t *limits) {
    return limits &&
           limits->max_candidate_rules > 0u &&
           limits->max_candidate_rules <= MAELYS_DATALOG_MAX_RULES &&
           limits->max_substitutions_per_rule > 0u &&
           limits->max_substitutions_per_rule <=
               MAELYS_DATALOG_MAX_WHY_FALSE_SUBSTITUTIONS_PER_RULE &&
           limits->max_depth > 0u &&
           limits->max_depth <= MAELYS_DATALOG_MAX_PROOF_DEPTH &&
           limits->max_diagnostics > 0u &&
           limits->max_diagnostics <=
               MAELYS_DATALOG_MAX_WHY_FALSE_DIAGNOSTICS;
}

static maelys_datalog_explanation_origin_t why_false_fact_origin(
    const maelys_datalog_solve_result_t *result,
    const maelys_datalog_fact_t *fact) {
    if (solve_once_fact_in_slice(
            result->ruleset->facts, result->ruleset->fact_count, fact)) {
        return MAELYS_DATALOG_EXPLANATION_ORIGIN_POLICY_FACT;
    }
    if (maelys_datalog_fact_set_contains(&result->edb_snapshot, fact)) {
        return MAELYS_DATALOG_EXPLANATION_ORIGIN_EDB;
    }
    if (maelys_datalog_fact_set_contains(&result->idb_final, fact)) {
        return MAELYS_DATALOG_EXPLANATION_ORIGIN_IDB;
    }
    return MAELYS_DATALOG_EXPLANATION_ORIGIN_NOT_APPLICABLE;
}

static int why_false_unify_head(const maelys_datalog_rule_t *rule,
                                const maelys_datalog_fact_t *target,
                                solve_once_bindings_t *out_bindings) {
    if (!rule || !target || !out_bindings ||
        rule->head.predicate_id != target->predicate_id ||
        rule->head.arity != target->arity) {
        return 0;
    }
    memset(out_bindings, 0, sizeof(*out_bindings));
    for (size_t term = 0u; term < target->arity; term++) {
        if (!solve_once_bind_or_match(
                out_bindings, &rule->head.terms[term], &target->terms[term])) {
            return 0;
        }
    }
    return 1;
}

static int why_false_rule_task_cmp(
    why_false_context_t *context,
    const why_false_rule_task_t *left,
    const why_false_rule_task_t *right) {
    if (left->rule->rule_id < right->rule->rule_id) return -1;
    if (left->rule->rule_id > right->rule->rule_id) return 1;
    const int target_cmp = why_false_fact_cmp(
        context, &left->target, &right->target);
    if (target_cmp != 0) return target_cmp;
    if (context->fatal_error != MAELYS_OK) return 0;
    if (left->depth < right->depth) return -1;
    if (left->depth > right->depth) return 1;
    for (size_t depth = 0u; depth <= left->depth; depth++) {
        const int path_cmp = why_false_fact_cmp(
            context, &left->path[depth], &right->path[depth]);
        if (path_cmp != 0) return path_cmp;
        if (context->fatal_error != MAELYS_OK) return 0;
    }
    return 0;
}

static void why_false_retain_rule_task(
    why_false_context_t *context,
    const why_false_rule_task_t *task) {
    for (size_t index = 0u; index < context->frontier_count; index++) {
        if (why_false_rule_task_cmp(
                context, task, &context->frontier[index]) == 0) {
            return;
        }
        if (context->fatal_error != MAELYS_OK) return;
    }
    if (context->frontier_count < context->frontier_capacity) {
        context->frontier[context->frontier_count++] = *task;
        return;
    }
    context->frontier_truncated = 1;
    size_t largest = 0u;
    for (size_t index = 1u; index < context->frontier_count; index++) {
        if (why_false_rule_task_cmp(
                context,
                &context->frontier[largest],
                &context->frontier[index]) < 0) {
            largest = index;
        }
        if (context->fatal_error != MAELYS_OK) return;
    }
    if (why_false_rule_task_cmp(
            context, task, &context->frontier[largest]) < 0) {
        context->frontier[largest] = *task;
    }
}

static void why_false_enqueue_fact(why_false_context_t *context,
                                   const maelys_datalog_fact_t *target,
                                   size_t depth) {
    if (context->fatal_error != MAELYS_OK) return;
    if (depth >= context->limits->max_depth ||
        depth > MAELYS_DATALOG_MAX_PROOF_DEPTH) {
        context->out->limit_hits |=
            (uint8_t)MAELYS_DATALOG_WHY_FALSE_LIMIT_DEPTH;
        return;
    }
    const maelys_datalog_ruleset_t *ruleset = context->result->ruleset;
    size_t matching_rules = 0u;
    for (size_t rule_index = 0u;
         rule_index < ruleset->rule_count;
         rule_index++) {
        solve_once_bindings_t bindings;
        if (!why_false_unify_head(
                &ruleset->rules[rule_index], target, &bindings)) {
            continue;
        }
        why_false_rule_task_t task;
        memset(&task, 0, sizeof(task));
        task.rule = &ruleset->rules[rule_index];
        task.target = *target;
        task.depth = depth;
        for (size_t path_index = 0u; path_index < depth; path_index++) {
            task.path[path_index] = context->path[path_index];
        }
        task.path[depth] = *target;
        why_false_retain_rule_task(context, &task);
        if (context->fatal_error != MAELYS_OK) return;
        matching_rules++;
    }
    if (depth == 0u && matching_rules == 0u) {
        context->out->summary =
            (uint8_t)MAELYS_DATALOG_WHY_FALSE_SUMMARY_NO_CANDIDATE_RULE;
    }
}

static int why_false_pop_rule_task(why_false_context_t *context,
                                   why_false_rule_task_t *out_task) {
    if (context->frontier_count == 0u) return 0;
    size_t smallest = 0u;
    for (size_t index = 1u; index < context->frontier_count; index++) {
        if (why_false_rule_task_cmp(
                context,
                &context->frontier[index],
                &context->frontier[smallest]) < 0) {
            smallest = index;
        }
        if (context->fatal_error != MAELYS_OK) return 0;
    }
    *out_task = context->frontier[smallest];
    context->frontier_count--;
    if (smallest != context->frontier_count) {
        context->frontier[smallest] =
            context->frontier[context->frontier_count];
    }
    return 1;
}

static maelys_result_t why_false_build_literal_order(
    const maelys_datalog_rule_t *rule,
    uint32_t initial_bound_mask,
    uint8_t out_order[MAELYS_DATALOG_MAX_BODY_LITERALS]) {
    if (!rule || !out_order ||
        rule->body_count > MAELYS_DATALOG_MAX_BODY_LITERALS) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    uint64_t planned_mask = 0u;
    uint64_t bound_mask = initial_bound_mask;
    for (size_t position = 0u; position < rule->body_count; position++) {
        int selected = -1;
        for (uint8_t index = 0u; index < rule->body_count; index++) {
            if ((planned_mask & ((uint64_t)1u << index)) != 0u) continue;
            if (!literal_safe_with_bound_vars(rule, index, bound_mask)) continue;
            selected = (int)index;
            break;
        }
        if (selected < 0) return MAELYS_ERR_INVALID_FIELD;
        const uint8_t index = (uint8_t)selected;
        out_order[position] = index;
        planned_mask |= (uint64_t)1u << index;
        bound_mask |= literal_var_mask(&rule->body[index]);
    }
    return MAELYS_OK;
}

static void why_false_fill_pattern(
    const maelys_datalog_literal_t *literal,
    const solve_once_bindings_t *bindings,
    maelys_datalog_why_false_pattern_t *out_pattern) {
    memset(out_pattern, 0, sizeof(*out_pattern));
    out_pattern->predicate_id = literal->atom.predicate_id;
    out_pattern->arity = literal->atom.arity;
    for (size_t term = 0u; term < literal->atom.arity; term++) {
        const maelys_datalog_term_t *source = &literal->atom.terms[term];
        if (solve_once_instantiate_term(
                bindings, source, &out_pattern->terms[term])) {
            continue;
        }
        out_pattern->terms[term] = *source;
        out_pattern->unbound_term_mask |= (uint8_t)1u << term;
    }
}

static int why_false_pattern_is_ground(
    const maelys_datalog_why_false_pattern_t *pattern) {
    return pattern && pattern->unbound_term_mask == 0u;
}

static void why_false_pattern_as_fact(
    const maelys_datalog_why_false_pattern_t *pattern,
    maelys_datalog_fact_t *out_fact) {
    memset(out_fact, 0, sizeof(*out_fact));
    out_fact->predicate_id = pattern->predicate_id;
    out_fact->arity = pattern->arity;
    for (size_t term = 0u; term < pattern->arity; term++) {
        out_fact->terms[term] = pattern->terms[term];
    }
}

static void why_false_make_diagnostic(
    const maelys_datalog_fact_t *target,
    const maelys_datalog_rule_t *rule,
    const why_false_branch_t *branch,
    size_t depth,
    maelys_datalog_why_false_diagnostic_t *out_diagnostic) {
    memset(out_diagnostic, 0, sizeof(*out_diagnostic));
    out_diagnostic->rule_id = rule->rule_id;
    out_diagnostic->target_fact = *target;
    out_diagnostic->bound_variable_mask = branch->bindings.bound_mask;
    for (size_t variable = 0u;
         variable < MAELYS_DATALOG_MAX_RULE_VARIABLES;
         variable++) {
        if ((branch->bindings.bound_mask & ((uint32_t)1u << variable)) != 0u) {
            out_diagnostic->substitution[variable] =
                branch->bindings.value[variable];
        }
    }
    out_diagnostic->support_count = branch->support_count;
    for (size_t support = 0u; support < branch->support_count; support++) {
        out_diagnostic->supports[support] = branch->supports[support];
    }
    out_diagnostic->depth = (uint8_t)depth;
    out_diagnostic->obstacle.origin =
        (uint8_t)MAELYS_DATALOG_EXPLANATION_ORIGIN_NOT_APPLICABLE;
}

static void why_false_retain_diagnostic(
    why_false_context_t *context,
    const maelys_datalog_why_false_diagnostic_t *diagnostic) {
    const size_t capacity = context->limits->max_diagnostics;
    if (context->out->diagnostic_count < capacity) {
        context->out->diagnostics[context->out->diagnostic_count++] = *diagnostic;
        return;
    }
    context->out->limit_hits |=
        (uint8_t)MAELYS_DATALOG_WHY_FALSE_LIMIT_DIAGNOSTICS;
    size_t largest = 0u;
    for (size_t i = 1u; i < capacity; i++) {
        if (why_false_diagnostic_cmp(
                context,
                &context->out->diagnostics[largest],
                &context->out->diagnostics[i]) < 0) {
            largest = i;
        }
        if (context->fatal_error != MAELYS_OK) return;
    }
    if (why_false_diagnostic_cmp(
            context,
            diagnostic,
            &context->out->diagnostics[largest]) < 0) {
        context->out->diagnostics[largest] = *diagnostic;
    }
}

static maelys_datalog_compare_result_t why_false_evaluate_comparison_literal(
    const maelys_datalog_rule_t *rule,
    const maelys_datalog_literal_t *literal,
    const solve_once_bindings_t *bindings,
    maelys_datalog_term_t *out_lhs,
    maelys_datalog_term_t *out_rhs) {
    maelys_datalog_term_t lhs;
    maelys_datalog_term_t rhs;
    memset(&lhs, 0, sizeof(lhs));
    memset(&rhs, 0, sizeof(rhs));
    if (literal->has_arith_expr) {
        long long lhs_value = 0;
        long long rhs_value = 0;
        maelys_datalog_term_kind_t lhs_kind = 0;
        maelys_datalog_term_kind_t rhs_kind = 0;
        maelys_datalog_compare_result_t rc = solve_once_eval_arith_expr(
            rule, literal->lhs_expr_root, bindings, &lhs_value, &lhs_kind);
        if (rc != MAELYS_DATALOG_COMPARE_TRUE) return rc;
        rc = solve_once_eval_arith_expr(
            rule, literal->rhs_expr_root, bindings, &rhs_value, &rhs_kind);
        if (rc != MAELYS_DATALOG_COMPARE_TRUE) return rc;
        lhs.kind = MAELYS_DATALOG_TERM_INT;
        lhs.as.integer = lhs_value;
        rhs.kind = MAELYS_DATALOG_TERM_INT;
        rhs.as.integer = rhs_value;
    } else {
        maelys_datalog_compare_result_t rc =
            solve_once_instantiate_comparison_term(bindings, &literal->lhs, &lhs);
        if (rc != MAELYS_DATALOG_COMPARE_TRUE) return rc;
        rc = solve_once_instantiate_comparison_term(
            bindings, &literal->rhs, &rhs);
        if (rc != MAELYS_DATALOG_COMPARE_TRUE) return rc;
    }
    if (out_lhs) *out_lhs = lhs;
    if (out_rhs) *out_rhs = rhs;
    return solve_once_evaluate_comparison(&lhs, literal->op, &rhs);
}

/* Returns 0 on invalid state, 1 on a normal false result, 2 on true, and 3
 * when the independent Why-false filter-cost budget prevented evaluation. */
static int why_false_evaluate_filter_literal(
    why_false_context_t *context,
    const maelys_datalog_literal_t *literal,
    const solve_once_bindings_t *bindings,
    maelys_datalog_term_t *out_value) {
    const maelys_datalog_ruleset_t *ruleset = context->result->ruleset;
    const maelys_datalog_filter_program_t *program = NULL;
    const maelys_datalog_filter_definition_t *definition = NULL;
    const unsigned char *pattern = NULL;
    if (!filter_program_resolve(
            ruleset, literal, &program, &definition, &pattern)) return 0;
    (void)definition;
    maelys_datalog_term_t value;
    memset(&value, 0, sizeof(value));
    if (solve_once_instantiate_comparison_term(
            bindings, &literal->filter_value, &value) !=
            MAELYS_DATALOG_COMPARE_TRUE ||
        value.kind != MAELYS_DATALOG_TERM_SYMBOL) return 0;
    const unsigned char *value_bytes = NULL;
    size_t value_length = 0u;
    if (!filter_symbol_bytes_resolve(&ruleset->symbols,
                                     value.as.symbol,
                                     &value_bytes,
                                     &value_length)) return 0;
    size_t cost = 0u;
    if (maelys_datalog_filter_cost(
            (maelys_datalog_filter_kind_t)program->kind,
            value_length,
            program->pattern_length,
            &cost) != MAELYS_OK) return 0;
    if (context->out->filter_cost_units >
            MAELYS_DATALOG_MAX_WHY_FALSE_FILTER_COST_UNITS ||
        cost > MAELYS_DATALOG_MAX_WHY_FALSE_FILTER_COST_UNITS -
                   context->out->filter_cost_units) {
        context->out->limit_hits |=
            (uint8_t)MAELYS_DATALOG_WHY_FALSE_LIMIT_FILTER_COST;
        return 3;
    }
    context->out->filter_cost_units += cost;
    int matched = 0;
    if (maelys_datalog_filter_evaluate(
            (maelys_datalog_filter_kind_t)program->kind,
            value_bytes,
            value_length,
            pattern,
            program->pattern_length,
            &matched) != MAELYS_OK) return 0;
    if (out_value) explanation_copy_term(out_value, &value);
    return matched ? 2 : 1;
}

static why_false_candidate_t *why_false_collect_candidates(
    why_false_context_t *context,
    const maelys_datalog_literal_t *literal,
    size_t *out_count) {
    *out_count = 0u;
    const maelys_datalog_predicate_def_t *def =
        maelys_datalog_predicate_registry_get(
            &context->result->ruleset->registry, literal->atom.predicate_id);
    if (!def) return NULL;
    const size_t capacity = context->result->ruleset->fact_count +
                            context->result->edb_snapshot.count +
                            context->result->idb_final.count;
    why_false_candidate_t *candidates =
        capacity > 0u ? calloc(capacity, sizeof(*candidates)) : NULL;
    if (capacity > 0u && !candidates) return NULL;
    size_t count = 0u;
    if ((def->kind_flags & MAELYS_DATALOG_PRED_KIND_POLICY_FACT) != 0u) {
        for (size_t i = 0u; i < context->result->ruleset->fact_count; i++) {
            const maelys_datalog_fact_t *fact =
                &context->result->ruleset->facts[i];
            if (fact->predicate_id != literal->atom.predicate_id ||
                fact->arity != literal->atom.arity) {
                continue;
            }
            candidates[count++] = (why_false_candidate_t){
                fact, MAELYS_DATALOG_EXPLANATION_ORIGIN_POLICY_FACT};
        }
    }
    if ((def->kind_flags & MAELYS_DATALOG_PRED_KIND_EDB) != 0u) {
        for (size_t i = 0u; i < context->result->edb_snapshot.count; i++) {
            const maelys_datalog_fact_t *fact =
                &context->result->edb_snapshot.facts[i];
            if (fact->predicate_id != literal->atom.predicate_id ||
                fact->arity != literal->atom.arity) {
                continue;
            }
            candidates[count++] = (why_false_candidate_t){
                fact, MAELYS_DATALOG_EXPLANATION_ORIGIN_EDB};
        }
    }
    if ((def->kind_flags & MAELYS_DATALOG_PRED_KIND_IDB) != 0u) {
        for (size_t i = 0u; i < context->result->idb_final.count; i++) {
            const maelys_datalog_fact_t *fact =
                &context->result->idb_final.facts[i];
            if (fact->predicate_id != literal->atom.predicate_id ||
                fact->arity != literal->atom.arity) {
                continue;
            }
            candidates[count++] = (why_false_candidate_t){
                fact, MAELYS_DATALOG_EXPLANATION_ORIGIN_IDB};
        }
    }
    for (size_t i = 1u; i < count; i++) {
        const why_false_candidate_t candidate = candidates[i];
        size_t insertion = i;
        while (insertion > 0u &&
               why_false_candidate_cmp(
                   context, &candidate, &candidates[insertion - 1u]) < 0) {
            candidates[insertion] = candidates[insertion - 1u];
            insertion--;
        }
        if (context->fatal_error != MAELYS_OK) {
            free(candidates);
            return NULL;
        }
        candidates[insertion] = candidate;
    }
    *out_count = count;
    return candidates;
}

static int why_false_path_contains(const why_false_context_t *context,
                                   const maelys_datalog_fact_t *fact,
                                   size_t depth) {
    for (size_t i = 0u; i <= depth; i++) {
        if (maelys_datalog_fact_equals(&context->path[i], fact)) return 1;
    }
    return 0;
}

static int why_false_explore_body(
    why_false_context_t *context,
    const maelys_datalog_fact_t *target,
    const maelys_datalog_rule_t *rule,
    const uint8_t order[MAELYS_DATALOG_MAX_BODY_LITERALS],
    size_t order_position,
    why_false_branch_t *branch,
    size_t depth,
    size_t *rule_substitution_count) {
    if (context->fatal_error != MAELYS_OK) return 0;
    if (order_position == rule->body_count) {
        /* A complete body would derive target. Its absence from a successful
         * final solve means this diagnostic evaluator diverged from the solver. */
        context->fatal_error = MAELYS_ERR_INVALID_STATE;
        return 0;
    }
    const size_t body_index = order[order_position];
    const maelys_datalog_literal_t *literal = &rule->body[body_index];
    if (literal->kind == MAELYS_DATALOG_LITERAL_FILTER) {
        maelys_datalog_term_t value;
        memset(&value, 0, sizeof(value));
        const int filter = why_false_evaluate_filter_literal(
            context, literal, &branch->bindings, &value);
        if (filter == 0) {
            context->fatal_error = MAELYS_ERR_INVALID_STATE;
            return 0;
        }
        if (filter == 3) return 1;
        if (filter == 2) {
            return why_false_explore_body(context,
                                          target,
                                          rule,
                                          order,
                                          order_position + 1u,
                                          branch,
                                          depth,
                                          rule_substitution_count);
        }
        maelys_datalog_why_false_diagnostic_t diagnostic;
        why_false_make_diagnostic(target, rule, branch, depth, &diagnostic);
        diagnostic.obstacle.kind =
            (uint8_t)MAELYS_DATALOG_WHY_FALSE_OBSTACLE_FILTER_FALSE;
        diagnostic.obstacle.body_index = (uint16_t)body_index;
        diagnostic.obstacle.filter_kind = literal->filter_kind;
        diagnostic.obstacle.filter_program_index =
            literal->filter_program_index;
        explanation_copy_term(&diagnostic.obstacle.filter_value, &value);
        why_false_retain_diagnostic(context, &diagnostic);
        return 1;
    }
    if (literal->kind == MAELYS_DATALOG_LITERAL_COMPARISON) {
        maelys_datalog_term_t lhs;
        maelys_datalog_term_t rhs;
        const maelys_datalog_compare_result_t comparison =
            why_false_evaluate_comparison_literal(
                rule, literal, &branch->bindings, &lhs, &rhs);
        if (comparison == MAELYS_DATALOG_COMPARE_TRUE) {
            return why_false_explore_body(context,
                                          target,
                                          rule,
                                          order,
                                          order_position + 1u,
                                          branch,
                                          depth,
                                          rule_substitution_count);
        }
        if (comparison != MAELYS_DATALOG_COMPARE_FALSE) {
            context->fatal_error = MAELYS_ERR_INVALID_STATE;
            return 0;
        }
        maelys_datalog_why_false_diagnostic_t diagnostic;
        why_false_make_diagnostic(target, rule, branch, depth, &diagnostic);
        diagnostic.obstacle.kind =
            (uint8_t)MAELYS_DATALOG_WHY_FALSE_OBSTACLE_COMPARISON_FALSE;
        diagnostic.obstacle.body_index = (uint16_t)body_index;
        diagnostic.obstacle.op = (uint8_t)literal->op;
        diagnostic.obstacle.lhs = lhs;
        diagnostic.obstacle.rhs = rhs;
        why_false_retain_diagnostic(context, &diagnostic);
        return 1;
    }
    if (literal->kind == MAELYS_DATALOG_LITERAL_NEGATED_ATOM) {
        maelys_datalog_fact_t ground;
        memset(&ground, 0, sizeof(ground));
        ground.predicate_id = literal->atom.predicate_id;
        ground.arity = literal->atom.arity;
        for (size_t term = 0u; term < ground.arity; term++) {
            if (!solve_once_instantiate_term(
                    &branch->bindings,
                    &literal->atom.terms[term],
                    &ground.terms[term])) {
                context->fatal_error = MAELYS_ERR_INVALID_STATE;
                return 0;
            }
        }
        const maelys_datalog_explanation_origin_t origin =
            why_false_fact_origin(context->result, &ground);
        if (origin == MAELYS_DATALOG_EXPLANATION_ORIGIN_NOT_APPLICABLE) {
            return why_false_explore_body(context,
                                          target,
                                          rule,
                                          order,
                                          order_position + 1u,
                                          branch,
                                          depth,
                                          rule_substitution_count);
        }
        maelys_datalog_why_false_diagnostic_t diagnostic;
        why_false_make_diagnostic(target, rule, branch, depth, &diagnostic);
        diagnostic.obstacle.kind =
            (uint8_t)MAELYS_DATALOG_WHY_FALSE_OBSTACLE_NEGATIVE_CONTRADICTED;
        diagnostic.obstacle.origin = (uint8_t)origin;
        diagnostic.obstacle.body_index = (uint16_t)body_index;
        diagnostic.obstacle.pattern.predicate_id = ground.predicate_id;
        diagnostic.obstacle.pattern.arity = ground.arity;
        for (size_t term = 0u; term < ground.arity; term++) {
            diagnostic.obstacle.pattern.terms[term] = ground.terms[term];
        }
        why_false_retain_diagnostic(context, &diagnostic);
        return 1;
    }
    if (literal->kind != MAELYS_DATALOG_LITERAL_ATOM) {
        context->fatal_error = MAELYS_ERR_INVALID_STATE;
        return 0;
    }

    size_t candidate_count = 0u;
    why_false_candidate_t *candidates =
        why_false_collect_candidates(context, literal, &candidate_count);
    if (context->fatal_error != MAELYS_OK) return 0;
    const size_t possible_capacity = context->result->ruleset->fact_count +
                                     context->result->edb_snapshot.count +
                                     context->result->idb_final.count;
    if (!candidates && possible_capacity > 0u) {
        context->fatal_error = MAELYS_ERR_INTERNAL;
        return 0;
    }
    size_t matching_count = 0u;
    for (size_t candidate_index = 0u;
         candidate_index < candidate_count;
         candidate_index++) {
        if (candidate_index > 0u &&
            candidates[candidate_index].origin ==
                candidates[candidate_index - 1u].origin &&
            maelys_datalog_fact_equals(candidates[candidate_index].fact,
                                       candidates[candidate_index - 1u].fact)) {
            continue;
        }
        why_false_branch_t next = *branch;
        int matched = 1;
        for (size_t term = 0u; term < literal->atom.arity; term++) {
            if (!solve_once_bind_or_match(&next.bindings,
                                          &literal->atom.terms[term],
                                          &candidates[candidate_index].fact->terms[term])) {
                matched = 0;
                break;
            }
        }
        if (!matched) continue;
        matching_count++;
        if (*rule_substitution_count >=
            context->limits->max_substitutions_per_rule) {
            context->out->limit_hits |=
                (uint8_t)MAELYS_DATALOG_WHY_FALSE_LIMIT_SUBSTITUTIONS;
            continue;
        }
        (*rule_substitution_count)++;
        context->out->substitution_count++;
        if (next.support_count >= MAELYS_DATALOG_MAX_WHY_FALSE_SUPPORTS) {
            free(candidates);
            context->fatal_error = MAELYS_ERR_INVALID_STATE;
            return 0;
        }
        maelys_datalog_why_false_support_t *support =
            &next.supports[next.support_count++];
        memset(support, 0, sizeof(*support));
        support->body_index = (uint16_t)body_index;
        support->origin = (uint8_t)candidates[candidate_index].origin;
        support->fact = *candidates[candidate_index].fact;
        if (!why_false_explore_body(context,
                                    target,
                                    rule,
                                    order,
                                    order_position + 1u,
                                    &next,
                                    depth,
                                    rule_substitution_count)) {
            if (context->fatal_error != MAELYS_OK) {
                free(candidates);
                return 0;
            }
        }
    }
    free(candidates);
    if (matching_count > 0u) return 1;

    maelys_datalog_why_false_diagnostic_t diagnostic;
    why_false_make_diagnostic(target, rule, branch, depth, &diagnostic);
    diagnostic.obstacle.kind =
        (uint8_t)MAELYS_DATALOG_WHY_FALSE_OBSTACLE_POSITIVE_NO_MATCH;
    diagnostic.obstacle.body_index = (uint16_t)body_index;
    why_false_fill_pattern(
        literal, &branch->bindings, &diagnostic.obstacle.pattern);

    const maelys_datalog_predicate_def_t *def =
        maelys_datalog_predicate_registry_get(
            &context->result->ruleset->registry, literal->atom.predicate_id);
    if (def && (def->kind_flags & MAELYS_DATALOG_PRED_KIND_IDB) != 0u &&
        why_false_pattern_is_ground(&diagnostic.obstacle.pattern)) {
        maelys_datalog_fact_t missing;
        why_false_pattern_as_fact(&diagnostic.obstacle.pattern, &missing);
        if (why_false_path_contains(context, &missing, depth)) {
            diagnostic.obstacle.kind = (uint8_t)
                MAELYS_DATALOG_WHY_FALSE_OBSTACLE_RECURSIVE_NO_BASE_SUPPORT;
        } else if (depth + 1u >= context->limits->max_depth) {
            context->out->limit_hits |=
                (uint8_t)MAELYS_DATALOG_WHY_FALSE_LIMIT_DEPTH;
        } else {
            why_false_enqueue_fact(context, &missing, depth + 1u);
        }
    }
    why_false_retain_diagnostic(context, &diagnostic);
    return 1;
}

static void why_false_explore_frontier(
    why_false_context_t *context,
    const maelys_datalog_fact_t *root_target) {
    why_false_enqueue_fact(context, root_target, 0u);
    while (context->fatal_error == MAELYS_OK &&
           context->out->candidate_rule_count <
               context->limits->max_candidate_rules) {
        why_false_rule_task_t task;
        memset(&task, 0, sizeof(task));
        if (!why_false_pop_rule_task(context, &task)) break;
        for (size_t depth = 0u; depth <= task.depth; depth++) {
            context->path[depth] = task.path[depth];
        }
        why_false_branch_t branch;
        memset(&branch, 0, sizeof(branch));
        if (!why_false_unify_head(
                task.rule, &task.target, &branch.bindings)) {
            context->fatal_error = MAELYS_ERR_INVALID_STATE;
            return;
        }
        context->out->candidate_rule_count++;
        uint8_t order[MAELYS_DATALOG_MAX_BODY_LITERALS];
        memset(order, 0, sizeof(order));
        const maelys_result_t order_rc = why_false_build_literal_order(
            task.rule, branch.bindings.bound_mask, order);
        if (order_rc != MAELYS_OK) {
            context->fatal_error = order_rc;
            return;
        }
        size_t rule_substitution_count = 0u;
        (void)why_false_explore_body(context,
                                     &task.target,
                                     task.rule,
                                     order,
                                     0u,
                                     &branch,
                                     task.depth,
                                     &rule_substitution_count);
        if (context->fatal_error != MAELYS_OK) return;
    }
    if (context->frontier_count > 0u || context->frontier_truncated) {
        context->out->limit_hits |=
            (uint8_t)MAELYS_DATALOG_WHY_FALSE_LIMIT_CANDIDATE_RULES;
    }
}

maelys_result_t maelys_datalog_explain_absent_solved_fact(
    const maelys_datalog_solve_result_t *result,
    const maelys_datalog_fact_t *queried_fact,
    const maelys_datalog_why_false_limits_t *limits,
    maelys_datalog_why_false_explanation_t *out_explanation) {
    if (!result || !queried_fact || !limits || !out_explanation) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    if (!why_false_limits_valid(limits)) return MAELYS_ERR_INVALID_FIELD;
    if (!result->finalized || result->failed || !result->ruleset ||
        !result->ruleset->loaded) {
        return MAELYS_ERR_INVALID_STATE;
    }
    if (!datalog_fact_structurally_valid(
            &result->ruleset->registry, queried_fact)) {
        return MAELYS_ERR_INVALID_FIELD;
    }
    const maelys_datalog_predicate_def_t *def =
        maelys_datalog_predicate_registry_get(
            &result->ruleset->registry, queried_fact->predicate_id);
    if (!def) return MAELYS_ERR_INVALID_FIELD;
    maelys_datalog_predicate_id_t query_pid = 0u;
    maelys_result_t rc = validate_solved_query_predicate(
        result, def->name, queried_fact->arity, &query_pid);
    if (rc != MAELYS_OK) return rc;
    if (query_pid != queried_fact->predicate_id) {
        return MAELYS_ERR_INVALID_FIELD;
    }

    maelys_datalog_why_false_explanation_t *work =
        calloc(1u, sizeof(*work));
    if (!work) return MAELYS_ERR_INTERNAL;
    work->query = *queried_fact;

    why_false_context_t context;
    memset(&context, 0, sizeof(context));
    context.result = result;
    context.limits = limits;
    context.out = work;
    rc = why_false_build_symbol_ranks(&context);
    if (rc == MAELYS_OK) {
        rc = why_false_validate_result_symbols(&context, queried_fact);
    }
    if (rc != MAELYS_OK) {
        memset(work, 0, sizeof(*work));
        free(work);
        return rc;
    }

    const maelys_datalog_explanation_origin_t present_origin =
        why_false_fact_origin(result, queried_fact);
    if (present_origin !=
        MAELYS_DATALOG_EXPLANATION_ORIGIN_NOT_APPLICABLE) {
        work->status =
            (uint8_t)MAELYS_DATALOG_WHY_FALSE_STATUS_NOT_APPLICABLE;
        work->query_origin = (uint8_t)present_origin;
        *out_explanation = *work;
        free(work);
        return MAELYS_OK;
    }

    context.frontier_capacity = limits->max_candidate_rules;
    context.frontier = calloc(
        context.frontier_capacity, sizeof(*context.frontier));
    if (!context.frontier) {
        memset(work, 0, sizeof(*work));
        free(work);
        return MAELYS_ERR_INTERNAL;
    }
    why_false_explore_frontier(&context, queried_fact);
    if (context.fatal_error != MAELYS_OK) {
        rc = context.fatal_error;
        free(context.frontier);
        memset(work, 0, sizeof(*work));
        free(work);
        return rc;
    }
    for (size_t i = 1u; i < work->diagnostic_count; i++) {
        const maelys_datalog_why_false_diagnostic_t diagnostic =
            work->diagnostics[i];
        size_t insertion = i;
        while (insertion > 0u &&
               why_false_diagnostic_cmp(
                   &context,
                   &diagnostic,
                   &work->diagnostics[insertion - 1u]) < 0) {
            work->diagnostics[insertion] = work->diagnostics[insertion - 1u];
            insertion--;
        }
        if (context.fatal_error != MAELYS_OK) {
            rc = context.fatal_error;
            free(context.frontier);
            memset(work, 0, sizeof(*work));
            free(work);
            return rc;
        }
        work->diagnostics[insertion] = diagnostic;
    }
    work->status = work->limit_hits != 0u
        ? (uint8_t)MAELYS_DATALOG_WHY_FALSE_STATUS_TRUNCATED
        : (uint8_t)MAELYS_DATALOG_WHY_FALSE_STATUS_COMPLETE;
    *out_explanation = *work;
    free(context.frontier);
    memset(work, 0, sizeof(*work));
    free(work);
    return MAELYS_OK;
}
