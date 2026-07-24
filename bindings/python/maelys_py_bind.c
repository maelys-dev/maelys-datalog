#include "bindings/python/maelys_py_bind.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "common/maelys_errors.h"
#include "src/core/maelys_datalog_diagnostic.h"
#include "src/core/maelys_datalog_domain_registry.h"
#include "src/core/maelys_datalog_edb.h"
#include "src/core/maelys_datalog_predicate_registry.h"
#include "src/core/maelys_datalog_ruleset.h"
#include "src/core/maelys_datalog_solver.h"
#include "src/core/maelys_datalog_symbol_table.h"
#include "src/manifest/maelys_datalog_manifest.h"

_Static_assert(sizeof(((maelys_py_term_t *)0)->kind) == sizeof(int32_t),
               "maelys_py_term_t.kind must stay int32_t");
_Static_assert(sizeof(((maelys_py_term_t *)0)->value) == sizeof(int64_t),
               "maelys_py_term_t.value must stay int64_t");
_Static_assert(offsetof(maelys_py_term_t, kind) < offsetof(maelys_py_term_t, value),
               "maelys_py_term_t field order changed");
_Static_assert(sizeof(((maelys_py_predicate_def_t *)0)->arity) == sizeof(size_t),
               "maelys_py_predicate_def_t.arity must stay size_t");

struct maelys_py_engine {
    maelys_datalog_build_limits_t limits;
    maelys_datalog_diagnostic_t last_diag;
};

struct maelys_py_ruleset {
    maelys_datalog_policy_set_t policy_set;
    maelys_datalog_ruleset_t *ruleset;
};

struct maelys_py_edb {
    maelys_py_ruleset_t *ruleset;
    maelys_datalog_edb_t edb;
    maelys_datalog_fact_t fact_pool[MAELYS_DATALOG_MAX_EDB_FACTS];
    int finalized;
};

struct maelys_py_result {
    maelys_py_ruleset_t *ruleset;
    maelys_datalog_solve_result_t *result;
};

#define MAELYS_PY_MAX_DOMAINS MAELYS_DATALOG_MAX_REGISTERED_DOMAINS

typedef struct {
    char domain_name[MAELYS_DATALOG_INLINE_MAX_DOMAIN_LEN + 1u];
    maelys_datalog_predicate_def_t predicates[MAELYS_DATALOG_MAX_PREDICATES];
    size_t predicate_count;
    maelys_datalog_domain_def_t def;
} maelys_py_domain_slot_t;

/* No static scratch for transient enumeration buffers: callers own those
 * buffers. Durable static storage is required for registered domain
 * definitions because the native domain registry is process-wide, unbounded in
 * lifetime, and stores raw pointers rather than deep-copying. */
static maelys_py_domain_slot_t s_py_domains[MAELYS_PY_MAX_DOMAINS];
static size_t s_py_domain_count = 0u;

static size_t bounded_strlen(const char *s, size_t max_len_plus_one) {
    if (!s) return 0u;
    size_t n = 0u;
    while (n < max_len_plus_one && s[n]) n++;
    return n;
}

static int copy_cstr_bounded(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0u || !src || !*src) return 0;
    size_t len = bounded_strlen(src, dst_size);
    if (len >= dst_size) return 0;
    memcpy(dst, src, len + 1u);
    return 1;
}

static int py_predicates_equal(const maelys_datalog_domain_def_t *domain,
                               const maelys_py_predicate_def_t *predicates,
                               size_t predicate_count) {
    if (!domain || !domain->predicates || !predicates ||
        domain->predicate_count != predicate_count) {
        return 0;
    }
    for (size_t i = 0u; i < predicate_count; i++) {
        const maelys_datalog_predicate_def_t *a = &domain->predicates[i];
        const maelys_py_predicate_def_t *b = &predicates[i];
        if (!b->name || strcmp(a->name, b->name) != 0 ||
            a->arity != b->arity ||
            a->kind_flags != b->kind_flags) {
            return 0;
        }
    }
    return 1;
}

static maelys_result_t py_term_to_native(const maelys_py_term_t *src,
                                         maelys_datalog_term_t *dst) {
    if (!src || !dst) return MAELYS_ERR_INVALID_ARGUMENT;
    memset(dst, 0, sizeof(*dst));
    if (src->kind == (int32_t)MAELYS_DATALOG_TERM_SYMBOL) {
        if (src->value <= 0 || src->value > UINT32_MAX) return MAELYS_ERR_INVALID_FIELD;
        dst->kind = MAELYS_DATALOG_TERM_SYMBOL;
        dst->as.symbol = (maelys_datalog_symbol_id_t)src->value;
        return MAELYS_OK;
    }
    if (src->kind == (int32_t)MAELYS_DATALOG_TERM_INT) {
        dst->kind = MAELYS_DATALOG_TERM_INT;
        dst->as.integer = (long long)src->value;
        return MAELYS_OK;
    }
    if (src->kind == (int32_t)MAELYS_DATALOG_TERM_BOOL) {
        if (src->value != 0 && src->value != 1) return MAELYS_ERR_INVALID_FIELD;
        dst->kind = MAELYS_DATALOG_TERM_BOOL;
        dst->as.boolean = (int)src->value;
        return MAELYS_OK;
    }
    return MAELYS_ERR_INVALID_FIELD;
}

static void native_term_to_py(const maelys_datalog_term_t *src,
                              maelys_py_term_t *dst) {
    memset(dst, 0, sizeof(*dst));
    dst->kind = (int32_t)src->kind;
    if (src->kind == MAELYS_DATALOG_TERM_SYMBOL) {
        dst->value = (int64_t)src->as.symbol;
    } else if (src->kind == MAELYS_DATALOG_TERM_INT) {
        dst->value = (int64_t)src->as.integer;
    } else if (src->kind == MAELYS_DATALOG_TERM_BOOL) {
        dst->value = (int64_t)src->as.boolean;
    } else if (src->kind == MAELYS_DATALOG_TERM_VAR) {
        dst->value = (int64_t)src->as.variable;
    }
}

maelys_py_engine_t *maelys_py_engine_new(void) {
    maelys_py_engine_t *engine = (maelys_py_engine_t *)calloc(1u, sizeof(*engine));
    if (!engine) return NULL;
    maelys_datalog_get_build_limits(&engine->limits);
    maelys_datalog_diagnostic_clear(&engine->last_diag);
    return engine;
}

void maelys_py_engine_free(maelys_py_engine_t *engine) {
    free(engine);
}

int maelys_py_get_build_limits(maelys_datalog_build_limits_t *out) {
    if (!out) return (int)MAELYS_ERR_INVALID_ARGUMENT;
    maelys_datalog_get_build_limits(out);
    return (int)MAELYS_OK;
}

void maelys_py_get_abi_layout(maelys_py_abi_layout_t *out) {
    if (!out) return;
    out->term_size = sizeof(maelys_py_term_t);
    out->term_kind_offset = offsetof(maelys_py_term_t, kind);
    out->term_value_offset = offsetof(maelys_py_term_t, value);
    out->predicate_def_size = sizeof(maelys_py_predicate_def_t);
    out->predicate_def_name_offset = offsetof(maelys_py_predicate_def_t, name);
    out->predicate_def_arity_offset = offsetof(maelys_py_predicate_def_t, arity);
    out->predicate_def_kind_flags_offset = offsetof(maelys_py_predicate_def_t, kind_flags);
}

void maelys_py_get_abi_constants(maelys_py_abi_constants_t *out) {
    if (!out) return;
    out->term_symbol = (int32_t)MAELYS_DATALOG_TERM_SYMBOL;
    out->term_int = (int32_t)MAELYS_DATALOG_TERM_INT;
    out->term_bool = (int32_t)MAELYS_DATALOG_TERM_BOOL;
    out->term_var = (int32_t)MAELYS_DATALOG_TERM_VAR;
    out->pred_edb = (unsigned)MAELYS_DATALOG_PRED_KIND_EDB;
    out->pred_idb = (unsigned)MAELYS_DATALOG_PRED_KIND_IDB;
    out->pred_query = (unsigned)MAELYS_DATALOG_PRED_KIND_QUERY;
    out->pred_policy_fact = (unsigned)MAELYS_DATALOG_PRED_KIND_POLICY_FACT;
    out->ok = (int)MAELYS_OK;
    out->err_invalid_argument = (int)MAELYS_ERR_INVALID_ARGUMENT;
    out->err_invalid_field = (int)MAELYS_ERR_INVALID_FIELD;
    out->err_invalid_state = (int)MAELYS_ERR_INVALID_STATE;
    out->err_payload_too_large = (int)MAELYS_ERR_PAYLOAD_TOO_LARGE;
    out->err_forbidden = (int)MAELYS_ERR_FORBIDDEN;
    out->err_unsupported = (int)MAELYS_ERR_UNSUPPORTED;
}

int maelys_py_find_domain(const char *domain_name,
                          maelys_py_predicate_def_t *out_predicates,
                          size_t out_capacity,
                          size_t *out_count,
                          int *out_found,
                          int *out_inspectable) {
    if (!domain_name || !out_count || !out_found || !out_inspectable ||
        (!out_predicates && out_capacity > 0u)) {
        return (int)MAELYS_ERR_INVALID_ARGUMENT;
    }
    *out_count = 0u;
    *out_found = 0;
    *out_inspectable = 0;
    const maelys_datalog_domain_def_t *domain =
        maelys_datalog_domain_registry_find(domain_name);
    if (!domain) return (int)MAELYS_OK;
    *out_found = 1;
    if (domain->install_predicates) {
        *out_inspectable = 0;
        return (int)MAELYS_OK;
    }
    if (!domain->predicates) return (int)MAELYS_ERR_INVALID_STATE;
    *out_inspectable = 1;
    *out_count = domain->predicate_count;
    size_t n = domain->predicate_count < out_capacity ? domain->predicate_count : out_capacity;
    for (size_t i = 0u; i < n; i++) {
        out_predicates[i].name = domain->predicates[i].name;
        out_predicates[i].arity = domain->predicates[i].arity;
        out_predicates[i].kind_flags = domain->predicates[i].kind_flags;
    }
    return (int)MAELYS_OK;
}

int maelys_py_register_domain(const char *domain_name,
                              const maelys_py_predicate_def_t *predicates,
                              size_t predicate_count) {
    if (!domain_name || !predicates || predicate_count == 0u ||
        predicate_count > MAELYS_DATALOG_MAX_PREDICATES) {
        return (int)MAELYS_ERR_INVALID_ARGUMENT;
    }
    const maelys_datalog_domain_def_t *existing =
        maelys_datalog_domain_registry_find(domain_name);
    if (existing) {
        if (existing->install_predicates) return (int)MAELYS_ERR_UNSUPPORTED;
        return py_predicates_equal(existing, predicates, predicate_count)
                   ? (int)MAELYS_OK
                   : (int)MAELYS_ERR_INVALID_FIELD;
    }
    if (s_py_domain_count >= MAELYS_PY_MAX_DOMAINS) {
        return (int)MAELYS_ERR_PAYLOAD_TOO_LARGE;
    }

    maelys_py_domain_slot_t *slot = &s_py_domains[s_py_domain_count];
    memset(slot, 0, sizeof(*slot));
    if (!copy_cstr_bounded(slot->domain_name, sizeof(slot->domain_name), domain_name)) {
        return (int)MAELYS_ERR_INVALID_FIELD;
    }
    for (size_t i = 0u; i < predicate_count; i++) {
        if (!copy_cstr_bounded(slot->predicates[i].name,
                               sizeof(slot->predicates[i].name),
                               predicates[i].name)) {
            return (int)MAELYS_ERR_INVALID_FIELD;
        }
        slot->predicates[i].arity = predicates[i].arity;
        slot->predicates[i].kind_flags = predicates[i].kind_flags;
    }
    slot->predicate_count = predicate_count;
    slot->def.domain_name = slot->domain_name;
    slot->def.predicates = slot->predicates;
    slot->def.predicate_count = slot->predicate_count;
    slot->def.description = NULL;
    slot->def.install_predicates = NULL;

    maelys_result_t rc = maelys_datalog_domain_registry_register(&slot->def);
    if (rc == MAELYS_OK) s_py_domain_count++;
    return (int)rc;
}

int maelys_py_load_inline_ruleset(maelys_py_engine_t *engine,
                                  const char *domain_name,
                                  const char *ruleset_id,
                                  const char *source,
                                  size_t source_len,
                                  maelys_py_ruleset_t **out_ruleset) {
    if (!engine || !domain_name || !ruleset_id || !source || source_len == 0u || !out_ruleset) {
        return (int)MAELYS_ERR_INVALID_ARGUMENT;
    }
    *out_ruleset = NULL;
    maelys_datalog_diagnostic_clear(&engine->last_diag);
    maelys_py_ruleset_t *ruleset = (maelys_py_ruleset_t *)calloc(1u, sizeof(*ruleset));
    if (!ruleset) return (int)MAELYS_ERR_INTERNAL;
    maelys_result_t rc = maelys_datalog_load_policy_inline(domain_name,
                                                           ruleset_id,
                                                           source,
                                                           source_len,
                                                           0u,
                                                           &ruleset->policy_set,
                                                           &engine->last_diag);
    if (rc != MAELYS_OK) {
        free(ruleset);
        return (int)rc;
    }
    if (ruleset->policy_set.policy_count != 1u) {
        maelys_datalog_policy_set_clear(&ruleset->policy_set);
        free(ruleset);
        return (int)MAELYS_ERR_INVALID_STATE;
    }
    ruleset->ruleset = &ruleset->policy_set.policies[0];
    *out_ruleset = ruleset;
    return (int)MAELYS_OK;
}

const char *maelys_py_last_diag_message(maelys_py_engine_t *engine) {
    if (!engine) return "";
    return engine->last_diag.message[0] ? engine->last_diag.message : "";
}

const char *maelys_py_last_diag_hint(maelys_py_engine_t *engine) {
    if (!engine) return "";
    return engine->last_diag.hint[0] ? engine->last_diag.hint : "";
}

void maelys_py_ruleset_free(maelys_py_ruleset_t *ruleset) {
    if (!ruleset) return;
    maelys_datalog_policy_set_clear(&ruleset->policy_set);
    free(ruleset);
}

maelys_py_edb_t *maelys_py_edb_new(maelys_py_ruleset_t *ruleset) {
    if (!ruleset || !ruleset->ruleset) return NULL;
    maelys_py_edb_t *edb = (maelys_py_edb_t *)calloc(1u, sizeof(*edb));
    if (!edb) return NULL;
    edb->ruleset = ruleset;
    maelys_result_t rc = maelys_datalog_edb_init(&edb->edb,
                                                 edb->fact_pool,
                                                 MAELYS_DATALOG_MAX_EDB_FACTS,
                                                 &ruleset->ruleset->symbols,
                                                 &ruleset->ruleset->registry);
    if (rc != MAELYS_OK) {
        free(edb);
        return NULL;
    }
    return edb;
}

void maelys_py_edb_free(maelys_py_edb_t *edb) {
    if (!edb) return;
    maelys_datalog_edb_clear(&edb->edb);
    free(edb);
}

int maelys_py_edb_add_fact(maelys_py_edb_t *edb,
                           const char *predicate,
                           const maelys_py_term_t *terms,
                           size_t arity) {
    if (!edb || !predicate || (!terms && arity > 0u) || arity > MAELYS_DATALOG_MAX_ARITY) {
        return (int)MAELYS_ERR_INVALID_ARGUMENT;
    }
    if (edb->finalized) return (int)MAELYS_ERR_INVALID_STATE;
    maelys_datalog_term_t native_terms[MAELYS_DATALOG_MAX_ARITY];
    for (size_t i = 0u; i < arity; i++) {
        maelys_result_t rc = py_term_to_native(&terms[i], &native_terms[i]);
        if (rc != MAELYS_OK) return (int)rc;
    }
    return (int)maelys_datalog_edb_add_fact(&edb->edb, predicate, native_terms, arity);
}

int maelys_py_intern_symbol(maelys_py_ruleset_t *ruleset,
                            const char *text,
                            uint32_t *out_symbol_id) {
    if (!ruleset || !ruleset->ruleset || !text || !out_symbol_id) {
        return (int)MAELYS_ERR_INVALID_ARGUMENT;
    }
    maelys_datalog_symbol_id_t id = 0;
    maelys_result_t rc = maelys_datalog_symbol_intern(&ruleset->ruleset->symbols,
                                                      text,
                                                      strlen(text),
                                                      &id);
    if (rc != MAELYS_OK) return (int)rc;
    *out_symbol_id = (uint32_t)id;
    return (int)MAELYS_OK;
}

int maelys_py_symbol_lookup_readonly(maelys_py_ruleset_t *ruleset,
                                     const char *text,
                                     size_t len,
                                     uint32_t *out_symbol_id,
                                     int *out_found) {
    if (!out_symbol_id || !out_found) {
        return (int)MAELYS_ERR_INVALID_ARGUMENT;
    }
    *out_symbol_id = (uint32_t)MAELYS_DATALOG_SYMBOL_ID_INVALID;
    *out_found = 0;
    if (!ruleset || !ruleset->ruleset || !text) {
        return (int)MAELYS_ERR_INVALID_ARGUMENT;
    }
    maelys_datalog_symbol_id_t id = MAELYS_DATALOG_SYMBOL_ID_INVALID;
    int found = 0;
    maelys_result_t rc = maelys_datalog_symbol_lookup_readonly(
        &ruleset->ruleset->symbols, text, len, &id, &found);
    if (rc != MAELYS_OK) return (int)rc;
    *out_symbol_id = (uint32_t)id;
    *out_found = found;
    return (int)MAELYS_OK;
}

int maelys_py_symbol_id_is_valid(maelys_py_ruleset_t *ruleset,
                                 uint32_t symbol_id,
                                 int *out_valid) {
    if (!out_valid) {
        return (int)MAELYS_ERR_INVALID_ARGUMENT;
    }
    *out_valid = 0;
    if (!ruleset || !ruleset->ruleset) {
        return (int)MAELYS_ERR_INVALID_ARGUMENT;
    }
    *out_valid = maelys_datalog_symbol_id_is_valid(
        &ruleset->ruleset->symbols, (maelys_datalog_symbol_id_t)symbol_id);
    return (int)MAELYS_OK;
}

const char *maelys_py_symbol_text(maelys_py_ruleset_t *ruleset,
                                  uint32_t symbol_id) {
    if (!ruleset || !ruleset->ruleset) return NULL;
    return maelys_datalog_symbol_text(&ruleset->ruleset->symbols,
                                      (maelys_datalog_symbol_id_t)symbol_id);
}

int maelys_py_solve(maelys_py_ruleset_t *ruleset,
                    maelys_py_edb_t *edb,
                    maelys_py_result_t **out_result) {
    if (!ruleset || !ruleset->ruleset || !edb || !out_result) {
        return (int)MAELYS_ERR_INVALID_ARGUMENT;
    }
    *out_result = NULL;
    if (!edb->finalized) {
        maelys_result_t rc = maelys_datalog_edb_finalize(&edb->edb);
        if (rc != MAELYS_OK) return (int)rc;
        edb->finalized = 1;
    }
    maelys_py_result_t *result = (maelys_py_result_t *)calloc(1u, sizeof(*result));
    if (!result) return (int)MAELYS_ERR_INTERNAL;
    maelys_result_t rc = maelys_datalog_solve_once(ruleset->ruleset,
                                                   &edb->edb,
                                                   &result->result);
    if (rc != MAELYS_OK) {
        free(result);
        return (int)rc;
    }
    result->ruleset = ruleset;
    *out_result = result;
    return (int)MAELYS_OK;
}

void maelys_py_result_free(maelys_py_result_t *result) {
    if (!result) return;
    maelys_datalog_solve_result_free(result->result);
    free(result);
}

int maelys_py_result_derived_fact_count(maelys_py_result_t *result,
                                        size_t *out_count) {
    if (!result || !result->result || !out_count) return (int)MAELYS_ERR_INVALID_ARGUMENT;
    return (int)maelys_datalog_solve_result_derived_fact_count(result->result, out_count);
}

int maelys_py_result_validate_query_predicate(maelys_py_result_t *result,
                                              const char *predicate,
                                              size_t arity) {
    if (!result || !result->ruleset || !result->result || !predicate) {
        return (int)MAELYS_ERR_INVALID_ARGUMENT;
    }
    return (int)maelys_datalog_validate_solved_ground_query(
        result->result, predicate, arity);
}

int maelys_py_result_contains_fact(maelys_py_result_t *result,
                                   const char *predicate,
                                   const maelys_py_term_t *terms,
                                   size_t arity,
                                   int *out_present) {
    if (!out_present) {
        return (int)MAELYS_ERR_INVALID_ARGUMENT;
    }
    *out_present = 0;
    if (!result || !result->ruleset || !result->result || !predicate ||
        (!terms && arity > 0u)) {
        return (int)MAELYS_ERR_INVALID_ARGUMENT;
    }
    if (arity > MAELYS_DATALOG_MAX_ARITY) {
        return (int)MAELYS_ERR_INVALID_FIELD;
    }
    maelys_datalog_term_t native_terms[MAELYS_DATALOG_MAX_ARITY];
    for (size_t i = 0u; i < arity; i++) {
        maelys_result_t rc = py_term_to_native(&terms[i], &native_terms[i]);
        if (rc != MAELYS_OK) return (int)rc;
    }
    bool present = false;
    maelys_result_t rc = maelys_datalog_query_solved_ground_fact(
        result->result,
        predicate,
        arity > 0u ? native_terms : NULL,
        arity,
        &present);
    if (rc != MAELYS_OK) return (int)rc;
    *out_present = present ? 1 : 0;
    return (int)MAELYS_OK;
}

int maelys_py_result_enumerate_predicate_facts(maelys_py_result_t *result,
                                               const char *predicate,
                                               size_t arity,
                                               maelys_py_term_t *out_terms,
                                               size_t out_capacity,
                                               size_t *out_count) {
    if (!result || !result->result || !predicate || !out_count ||
        (!out_terms && out_capacity > 0u) || arity > MAELYS_DATALOG_MAX_ARITY) {
        return (int)MAELYS_ERR_INVALID_ARGUMENT;
    }
    if (out_capacity == 0u) {
        return (int)maelys_datalog_solve_result_enumerate_predicate_facts(
            result->result, predicate, arity, NULL, 0u, out_count);
    }
    maelys_datalog_fact_t *facts =
        (maelys_datalog_fact_t *)calloc(out_capacity, sizeof(*facts));
    if (!facts) return (int)MAELYS_ERR_INTERNAL;
    maelys_result_t rc = maelys_datalog_solve_result_enumerate_predicate_facts(
        result->result, predicate, arity, facts, out_capacity, out_count);
    if (rc != MAELYS_OK) {
        free(facts);
        return (int)rc;
    }
    size_t copied = *out_count < out_capacity ? *out_count : out_capacity;
    for (size_t i = 0u; i < copied; i++) {
        for (size_t j = 0u; j < arity; j++) {
            native_term_to_py(&facts[i].terms[j], &out_terms[i * arity + j]);
        }
    }
    free(facts);
    return (int)MAELYS_OK;
}
