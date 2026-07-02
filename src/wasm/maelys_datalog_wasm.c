#include "src/wasm/maelys_datalog_wasm.h"

#include "src/core/maelys_datalog_domain_registry.h"
#include "src/core/maelys_datalog_diagnostic.h"
#include "src/core/maelys_datalog_edb.h"
#include "src/core/maelys_datalog_solver.h"
#include "src/core/maelys_datalog_symbol_table.h"
#include "src/core/maelys_datalog_types.h"
#include "src/manifest/maelys_datalog_manifest.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define MAELYS_DATALOG_WASM_VALID_KIND_MASK \
    (MAELYS_DATALOG_PRED_KIND_EDB | MAELYS_DATALOG_PRED_KIND_IDB | \
     MAELYS_DATALOG_PRED_KIND_QUERY | MAELYS_DATALOG_PRED_KIND_POLICY_FACT)

/* WASM boundary only: caps packed-buffer scans; not a core string-pool rule. */
#define MAELYS_DATALOG_WASM_PACKED_STRING_BYTES_MAX MAELYS_DATALOG_STRING_POOL_BYTES

#define MAELYS_DATALOG_WASM_BUILD_LIMITS_COUNT 10u

#if defined(__EMSCRIPTEN__) && SIZE_MAX > UINT32_MAX
#  error "maelys_datalog_wasm_get_build_limits assumes a 32-bit size_t WASM ABI"
#endif

typedef enum {
    WASM_EDB_STATE_EMPTY = 0,
    WASM_EDB_STATE_OPEN = 1,
    WASM_EDB_STATE_SOLVED = 2,
} wasm_edb_state_t;

static char s_domain_name[MAELYS_DATALOG_INLINE_MAX_DOMAIN_LEN + 1u];
static maelys_datalog_predicate_def_t s_predicates[MAELYS_DATALOG_MAX_PREDICATES];
static size_t s_pred_count;
static int s_building;
static int s_has_committed;
static maelys_datalog_policy_set_t s_policy_set;
static maelys_datalog_diagnostic_t s_last_diag;
static maelys_datalog_edb_t s_edb;
static maelys_datalog_fact_t s_fact_pool[MAELYS_DATALOG_MAX_EDB_FACTS];
static maelys_datalog_symbol_id_t s_id_scratch[MAELYS_DATALOG_MAX_EDB_FACTS * 2u];
static maelys_datalog_solve_result_t *s_solve_result = NULL;
static wasm_edb_state_t s_edb_state = WASM_EDB_STATE_EMPTY;

/* Flat uint32_t ABI for JS build limits.
 * Slots mirror maelys_datalog_build_limits_t declaration order:
 *   0 max_symbols
 *   1 string_pool_bytes
 *   2 max_predicates
 *   3 max_rules
 *   4 max_arity
 *   5 max_body_literals
 *   6 max_depth
 *   7 max_edb_facts
 *   8 max_idb_facts
 *   9 max_facts_per_pred
 */
void maelys_datalog_wasm_get_build_limits(uint32_t *out) {
    if (!out) return;
    maelys_datalog_build_limits_t limits;
    maelys_datalog_get_build_limits(&limits);
    out[0] = (uint32_t)limits.max_symbols;
    out[1] = (uint32_t)limits.string_pool_bytes;
    out[2] = (uint32_t)limits.max_predicates;
    out[3] = (uint32_t)limits.max_rules;
    out[4] = (uint32_t)limits.max_arity;
    out[5] = (uint32_t)limits.max_body_literals;
    out[6] = (uint32_t)limits.max_depth;
    out[7] = (uint32_t)limits.max_edb_facts;
    out[8] = (uint32_t)limits.max_idb_facts;
    out[9] = (uint32_t)limits.max_facts_per_pred;
}

static void builder_clear_current(void) {
    memset(s_domain_name, 0, sizeof(s_domain_name));
    memset(s_predicates, 0, sizeof(s_predicates));
    s_pred_count = 0u;
    s_building = 0;
}

static void free_solve_result_only(void) {
    if (!s_solve_result) return;
    maelys_datalog_solve_result_free(s_solve_result);
    s_solve_result = NULL;
}

static void reset_edb_runtime_state(void) {
    free_solve_result_only();
    maelys_datalog_edb_clear(&s_edb);
    s_edb_state = WASM_EDB_STATE_EMPTY;
}

static maelys_result_t copy_bounded(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0u || !src) return MAELYS_ERR_INVALID_ARGUMENT;
    size_t max = dst_size - 1u;
    size_t len = strnlen(src, max + 1u);
    if (len == 0u) return MAELYS_ERR_INVALID_ARGUMENT;
    if (len > max) return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    memcpy(dst, src, len);
    dst[len] = '\0';
    return MAELYS_OK;
}

static int lookup_symbol_readonly(const char *text, maelys_datalog_symbol_id_t *out_id) {
    if (!text || !text[0] || !out_id) return -1;
    const maelys_datalog_symbol_table_t *tbl = &s_policy_set.policies[0].symbols;
    size_t len = strnlen(text, MAELYS_DATALOG_MAX_STRING_BYTES + 1u);
    if (len == 0u) return -1;
    if (len > MAELYS_DATALOG_MAX_STRING_BYTES) return -1;
    for (maelys_datalog_symbol_id_t id = 1u;
         id <= (maelys_datalog_symbol_id_t)tbl->count;
         id++) {
        const char *entry = maelys_datalog_symbol_text(tbl, id);
        if (entry && strncmp(entry, text, len) == 0 && entry[len] == '\0') {
            *out_id = id;
            return 1;
        }
    }
    return 0;
}

static maelys_result_t wasm_validate_symbol_id(int32_t symbol_id_from_js,
                                               maelys_datalog_symbol_id_t *out) {
    if (!out) return MAELYS_ERR_INVALID_ARGUMENT;
    if (symbol_id_from_js <= 0) return MAELYS_ERR_INVALID_ARGUMENT;

    maelys_datalog_symbol_id_t candidate =
        (maelys_datalog_symbol_id_t)symbol_id_from_js;
    if (!maelys_datalog_symbol_id_is_valid(&s_policy_set.policies[0].symbols, candidate)) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }

    *out = candidate;
    return MAELYS_OK;
}

static maelys_result_t wasm_validate_symbol_id_array(const int32_t *ids,
                                                     size_t id_count,
                                                     maelys_datalog_symbol_id_t *out) {
    if (!ids || !out) return MAELYS_ERR_INVALID_ARGUMENT;
    for (size_t i = 0; i < id_count; i++) {
        maelys_result_t rc = wasm_validate_symbol_id(ids[i], &out[i]);
        if (rc != MAELYS_OK) return rc;
    }
    return MAELYS_OK;
}

static maelys_result_t wasm_unpack_packed_strings(const char *packed,
                                                  int32_t byte_len,
                                                  size_t expected_count,
                                                  const char **out) {
    if (!packed || !out) return MAELYS_ERR_INVALID_ARGUMENT;
    if (byte_len <= 0) return MAELYS_ERR_INVALID_ARGUMENT;
    if ((uint32_t)byte_len > MAELYS_DATALOG_WASM_PACKED_STRING_BYTES_MAX) {
        return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    }

    const char *cursor = packed;
    const char *end = packed + (size_t)byte_len;
    for (size_t i = 0; i < expected_count; i++) {
        if (cursor >= end) return MAELYS_ERR_INVALID_ARGUMENT;
        size_t remaining = (size_t)(end - cursor);
        size_t cap = remaining;
        if (cap > (size_t)MAELYS_DATALOG_MAX_STRING_BYTES + 1u) {
            cap = (size_t)MAELYS_DATALOG_MAX_STRING_BYTES + 1u;
        }
        size_t len = strnlen(cursor, cap);
        if (len == cap) {
            if (cap <= remaining && cap > MAELYS_DATALOG_MAX_STRING_BYTES) {
                return MAELYS_ERR_PAYLOAD_TOO_LARGE;
            }
            return MAELYS_ERR_INVALID_ARGUMENT;
        }
        out[i] = cursor;
        cursor += len + 1u;
    }
    if (cursor != end) return MAELYS_ERR_INVALID_ARGUMENT;
    return MAELYS_OK;
}

maelys_result_t maelys_datalog_wasm_domain_begin(const char *name) {
    if (s_building || s_has_committed) return MAELYS_ERR_INVALID_STATE;
    reset_edb_runtime_state();
    maelys_result_t rc = copy_bounded(s_domain_name, sizeof(s_domain_name), name);
    if (rc != MAELYS_OK) {
        builder_clear_current();
        return rc;
    }
    s_building = 1;
    s_pred_count = 0u;
    memset(s_predicates, 0, sizeof(s_predicates));
    return MAELYS_OK;
}

maelys_result_t maelys_datalog_wasm_domain_add_predicate(const char *name,
                                                         unsigned arity,
                                                         unsigned kind_flags) {
    if (!s_building) return MAELYS_ERR_INVALID_STATE;
    if (kind_flags == 0u) return MAELYS_ERR_INVALID_ARGUMENT;
    if ((kind_flags & ~MAELYS_DATALOG_WASM_VALID_KIND_MASK) != 0u) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    if (s_pred_count >= MAELYS_DATALOG_MAX_PREDICATES) {
        return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    }
    maelys_result_t rc = copy_bounded(s_predicates[s_pred_count].name,
                                      sizeof(s_predicates[s_pred_count].name),
                                      name);
    if (rc != MAELYS_OK) return rc;
    s_predicates[s_pred_count].arity = arity;
    s_predicates[s_pred_count].kind_flags = kind_flags;
    s_pred_count++;
    return MAELYS_OK;
}

maelys_result_t maelys_datalog_wasm_domain_commit(void) {
    if (!s_building) return MAELYS_ERR_INVALID_STATE;
    reset_edb_runtime_state();
    if (s_pred_count == 0u) {
        builder_clear_current();
        s_has_committed = 0;
        return MAELYS_ERR_INVALID_ARGUMENT;
    }

    maelys_datalog_domain_def_t def = {
        .domain_name = s_domain_name,
        .predicates = s_predicates,
        .predicate_count = s_pred_count,
        .description = "WASM dynamic domain",
        .install_predicates = NULL,
    };
    maelys_result_t rc = maelys_datalog_domain_registry_register(&def);
    if (rc != MAELYS_OK) {
        builder_clear_current();
        s_has_committed = 0;
        return rc;
    }

    s_building = 0;
    s_has_committed = 1;
    return MAELYS_OK;
}

maelys_result_t maelys_datalog_wasm_domain_abort(void) {
    reset_edb_runtime_state();
    if (s_building) builder_clear_current();
    return MAELYS_OK;
}

maelys_result_t maelys_datalog_wasm_edb_begin(void) {
    if (s_policy_set.policy_count != 1u || !s_policy_set.policies[0].loaded) {
        return MAELYS_ERR_INVALID_STATE;
    }
    reset_edb_runtime_state();
    maelys_result_t rc = maelys_datalog_edb_init(&s_edb,
                                                  s_fact_pool,
                                                  MAELYS_DATALOG_MAX_EDB_FACTS,
                                                  &s_policy_set.policies[0].symbols,
                                                  &s_policy_set.policies[0].registry);
    if (rc != MAELYS_OK) {
        s_edb_state = WASM_EDB_STATE_EMPTY;
        return rc;
    }
    s_edb_state = WASM_EDB_STATE_OPEN;
    return MAELYS_OK;
}

maelys_result_t maelys_datalog_wasm_edb_intern_runtime_symbol(const char *text,
                                                              int32_t *out_id) {
    if (s_edb_state != WASM_EDB_STATE_OPEN) return MAELYS_ERR_INVALID_STATE;
    if (!text || !out_id) return MAELYS_ERR_INVALID_ARGUMENT;

    maelys_datalog_symbol_id_t core_id;
    maelys_result_t rc = maelys_datalog_edb_intern_runtime_symbol(&s_edb, text, &core_id);
    if (rc != MAELYS_OK) return rc;
    *out_id = (int32_t)core_id;
    return MAELYS_OK;
}

maelys_result_t maelys_datalog_wasm_edb_add_symbol_id_fact(const char *predicate,
                                                           int32_t symbol_id_from_js) {
    if (s_edb_state != WASM_EDB_STATE_OPEN) return MAELYS_ERR_INVALID_STATE;
    if (!predicate) return MAELYS_ERR_INVALID_ARGUMENT;

    maelys_datalog_symbol_id_t symbol_id;
    maelys_result_t rc = wasm_validate_symbol_id(symbol_id_from_js, &symbol_id);
    if (rc != MAELYS_OK) return rc;
    return maelys_datalog_edb_add_symbol_id_fact(&s_edb, predicate, symbol_id);
}

maelys_result_t maelys_datalog_wasm_edb_add_symbol_ids_fact(const char *predicate,
                                                            int32_t left_from_js,
                                                            int32_t right_from_js) {
    if (s_edb_state != WASM_EDB_STATE_OPEN) return MAELYS_ERR_INVALID_STATE;
    if (!predicate) return MAELYS_ERR_INVALID_ARGUMENT;

    maelys_datalog_symbol_id_t left;
    maelys_datalog_symbol_id_t right;
    maelys_result_t rc = wasm_validate_symbol_id(left_from_js, &left);
    if (rc != MAELYS_OK) return rc;
    rc = wasm_validate_symbol_id(right_from_js, &right);
    if (rc != MAELYS_OK) return rc;
    return maelys_datalog_edb_add_symbol_ids_fact(&s_edb, predicate, left, right);
}

maelys_result_t maelys_datalog_wasm_edb_add_symbol_id_facts(const char *predicate,
                                                            const int32_t *ids,
                                                            int32_t count) {
    if (s_edb_state != WASM_EDB_STATE_OPEN) return MAELYS_ERR_INVALID_STATE;
    if (!predicate) return MAELYS_ERR_INVALID_ARGUMENT;
    if (count < 0) return MAELYS_ERR_INVALID_ARGUMENT;
    if (count == 0) return MAELYS_OK;
    if (!ids) return MAELYS_ERR_INVALID_ARGUMENT;
    if ((uint32_t)count > MAELYS_DATALOG_MAX_EDB_FACTS) {
        return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    }

    const size_t value_count = (size_t)count;
    maelys_result_t rc = wasm_validate_symbol_id_array(ids, value_count, s_id_scratch);
    if (rc != MAELYS_OK) return rc;
    return maelys_datalog_edb_add_symbol_id_facts(&s_edb,
                                                  predicate,
                                                  s_id_scratch,
                                                  value_count);
}

maelys_result_t maelys_datalog_wasm_edb_add_symbol_ids_facts(const char *predicate,
                                                             const int32_t *pairs,
                                                             int32_t pair_count) {
    if (s_edb_state != WASM_EDB_STATE_OPEN) return MAELYS_ERR_INVALID_STATE;
    if (!predicate) return MAELYS_ERR_INVALID_ARGUMENT;
    if (pair_count < 0) return MAELYS_ERR_INVALID_ARGUMENT;
    if (pair_count == 0) return MAELYS_OK;
    if (!pairs) return MAELYS_ERR_INVALID_ARGUMENT;
    if ((uint32_t)pair_count > MAELYS_DATALOG_MAX_EDB_FACTS) {
        return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    }

    const size_t fact_count = (size_t)pair_count;
    const size_t flat_len = fact_count * 2u;
    maelys_result_t rc = wasm_validate_symbol_id_array(pairs, flat_len, s_id_scratch);
    if (rc != MAELYS_OK) return rc;
    return maelys_datalog_edb_add_symbol_ids_facts(&s_edb,
                                                   predicate,
                                                   s_id_scratch,
                                                   fact_count);
}

maelys_result_t maelys_datalog_wasm_edb_add_runtime_symbol_facts(const char *predicate,
                                                                 const char *packed,
                                                                 int32_t byte_len,
                                                                 int32_t value_count) {
    if (s_edb_state != WASM_EDB_STATE_OPEN) return MAELYS_ERR_INVALID_STATE;
    if (!predicate) return MAELYS_ERR_INVALID_ARGUMENT;
    if (value_count < 0) return MAELYS_ERR_INVALID_ARGUMENT;
    if (value_count == 0) return MAELYS_OK;
    if (!packed) return MAELYS_ERR_INVALID_ARGUMENT;
    if ((uint32_t)value_count > MAELYS_DATALOG_MAX_EDB_FACTS) {
        return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    }

    const size_t count = (size_t)value_count;
    const char *values[MAELYS_DATALOG_MAX_EDB_FACTS];
    maelys_result_t rc = wasm_unpack_packed_strings(packed, byte_len, count, values);
    if (rc != MAELYS_OK) return rc;
    return maelys_datalog_edb_add_runtime_symbol_facts(&s_edb, predicate, values, count);
}

maelys_result_t maelys_datalog_wasm_edb_add_runtime_symbol_pair_facts(const char *predicate,
                                                                      const char *packed,
                                                                      int32_t byte_len,
                                                                      int32_t pair_count) {
    if (s_edb_state != WASM_EDB_STATE_OPEN) return MAELYS_ERR_INVALID_STATE;
    if (!predicate) return MAELYS_ERR_INVALID_ARGUMENT;
    if (pair_count < 0) return MAELYS_ERR_INVALID_ARGUMENT;
    if (pair_count == 0) return MAELYS_OK;
    if (!packed) return MAELYS_ERR_INVALID_ARGUMENT;
    if ((uint32_t)pair_count > MAELYS_DATALOG_MAX_EDB_FACTS) {
        return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    }

    const size_t count = (size_t)pair_count;
    const size_t symbol_count = count * 2u;
    const char *values[2u * MAELYS_DATALOG_MAX_EDB_FACTS];
    maelys_result_t rc = wasm_unpack_packed_strings(packed, byte_len, symbol_count, values);
    if (rc != MAELYS_OK) return rc;
    return maelys_datalog_edb_add_runtime_symbol_pair_facts(&s_edb, predicate, values, count);
}

maelys_result_t maelys_datalog_wasm_edb_add_symbol(const char *pred, const char *arg0) {
    if (s_edb_state != WASM_EDB_STATE_OPEN) return MAELYS_ERR_INVALID_STATE;
    char pred_buf[MAELYS_DATALOG_MAX_STRING_BYTES + 1u];
    char arg0_buf[MAELYS_DATALOG_MAX_STRING_BYTES + 1u];
    maelys_result_t rc = copy_bounded(pred_buf, sizeof(pred_buf), pred);
    if (rc != MAELYS_OK) return rc;
    rc = copy_bounded(arg0_buf, sizeof(arg0_buf), arg0);
    if (rc != MAELYS_OK) return rc;

    int32_t id0;
    rc = maelys_datalog_wasm_edb_intern_runtime_symbol(arg0_buf, &id0);
    if (rc != MAELYS_OK) return rc;
    return maelys_datalog_wasm_edb_add_symbol_id_fact(pred_buf, id0);
}

maelys_result_t maelys_datalog_wasm_edb_add_symbol2(const char *pred,
                                                    const char *arg0,
                                                    const char *arg1) {
    if (s_edb_state != WASM_EDB_STATE_OPEN) return MAELYS_ERR_INVALID_STATE;
    char pred_buf[MAELYS_DATALOG_MAX_STRING_BYTES + 1u];
    char arg0_buf[MAELYS_DATALOG_MAX_STRING_BYTES + 1u];
    char arg1_buf[MAELYS_DATALOG_MAX_STRING_BYTES + 1u];
    maelys_result_t rc = copy_bounded(pred_buf, sizeof(pred_buf), pred);
    if (rc != MAELYS_OK) return rc;
    rc = copy_bounded(arg0_buf, sizeof(arg0_buf), arg0);
    if (rc != MAELYS_OK) return rc;
    rc = copy_bounded(arg1_buf, sizeof(arg1_buf), arg1);
    if (rc != MAELYS_OK) return rc;

    int32_t id0;
    int32_t id1;
    rc = maelys_datalog_wasm_edb_intern_runtime_symbol(arg0_buf, &id0);
    if (rc != MAELYS_OK) return rc;
    rc = maelys_datalog_wasm_edb_intern_runtime_symbol(arg1_buf, &id1);
    if (rc != MAELYS_OK) return rc;

    return maelys_datalog_wasm_edb_add_symbol_ids_fact(pred_buf, id0, id1);
}

maelys_result_t maelys_datalog_wasm_solve(void) {
    if (s_edb_state != WASM_EDB_STATE_OPEN) return MAELYS_ERR_INVALID_STATE;
    if (s_policy_set.policy_count != 1u || !s_policy_set.policies[0].loaded) {
        return MAELYS_ERR_INVALID_STATE;
    }
    free_solve_result_only();
    maelys_result_t rc = maelys_datalog_edb_finalize(&s_edb);
    if (rc != MAELYS_OK) {
        maelys_datalog_edb_clear(&s_edb);
        s_edb_state = WASM_EDB_STATE_EMPTY;
        return rc;
    }
    rc = maelys_datalog_solve_once(&s_policy_set.policies[0], &s_edb, &s_solve_result);
    if (rc != MAELYS_OK) {
        free_solve_result_only();
        maelys_datalog_edb_clear(&s_edb);
        s_edb_state = WASM_EDB_STATE_EMPTY;
        return rc;
    }
    s_edb_state = WASM_EDB_STATE_SOLVED;
    return MAELYS_OK;
}

int maelys_datalog_wasm_query_symbol(const char *pred, const char *arg0) {
    if (s_edb_state != WASM_EDB_STATE_SOLVED || !s_solve_result) return -1;
    char pred_buf[MAELYS_DATALOG_MAX_STRING_BYTES + 1u];
    char arg0_buf[MAELYS_DATALOG_MAX_STRING_BYTES + 1u];
    if (copy_bounded(pred_buf, sizeof(pred_buf), pred) != MAELYS_OK) return -1;
    if (copy_bounded(arg0_buf, sizeof(arg0_buf), arg0) != MAELYS_OK) return -1;

    maelys_datalog_symbol_id_t id0;
    int found = lookup_symbol_readonly(arg0_buf, &id0);
    if (found < 0) return -1;
    if (found == 0) return 0;

    maelys_datalog_term_t term = {.kind = MAELYS_DATALOG_TERM_SYMBOL};
    term.as.symbol = id0;
    bool present = false;
    maelys_result_t rc = maelys_datalog_query_solved_ground_fact(s_solve_result,
                                                                  pred_buf,
                                                                  &term,
                                                                  1u,
                                                                  &present);
    if (rc != MAELYS_OK) return -1;
    return present ? 1 : 0;
}

int maelys_datalog_wasm_query_symbol2(const char *pred,
                                      const char *arg0,
                                      const char *arg1) {
    if (s_edb_state != WASM_EDB_STATE_SOLVED || !s_solve_result) return -1;
    char pred_buf[MAELYS_DATALOG_MAX_STRING_BYTES + 1u];
    char arg0_buf[MAELYS_DATALOG_MAX_STRING_BYTES + 1u];
    char arg1_buf[MAELYS_DATALOG_MAX_STRING_BYTES + 1u];
    if (copy_bounded(pred_buf, sizeof(pred_buf), pred) != MAELYS_OK) return -1;
    if (copy_bounded(arg0_buf, sizeof(arg0_buf), arg0) != MAELYS_OK) return -1;
    if (copy_bounded(arg1_buf, sizeof(arg1_buf), arg1) != MAELYS_OK) return -1;

    maelys_datalog_symbol_id_t id0;
    maelys_datalog_symbol_id_t id1;
    int found0 = lookup_symbol_readonly(arg0_buf, &id0);
    int found1 = lookup_symbol_readonly(arg1_buf, &id1);
    if (found0 < 0 || found1 < 0) return -1;
    if (found0 == 0 || found1 == 0) return 0;

    maelys_datalog_term_t terms[2];
    terms[0].kind = MAELYS_DATALOG_TERM_SYMBOL;
    terms[0].as.symbol = id0;
    terms[1].kind = MAELYS_DATALOG_TERM_SYMBOL;
    terms[1].as.symbol = id1;
    bool present = false;
    maelys_result_t rc = maelys_datalog_query_solved_ground_fact(s_solve_result,
                                                                  pred_buf,
                                                                  terms,
                                                                  2u,
                                                                  &present);
    if (rc != MAELYS_OK) return -1;
    return present ? 1 : 0;
}

int32_t maelys_datalog_wasm_derived_fact_count(void) {
    if (s_edb_state != WASM_EDB_STATE_SOLVED || !s_solve_result) return -1;
    size_t count = 0u;
    maelys_result_t rc =
        maelys_datalog_solve_result_derived_fact_count(s_solve_result, &count);
    if (rc != MAELYS_OK) return -1;
    /* Current profiles keep derived fact counts well below INT32_MAX
     * (MAX_IDB_FACTS = 1024/2048, vs INT32_MAX = 2147483647). solver.c keeps
     * idb_current_end <= idb_merge_end <= MAELYS_DATALOG_MAX_IDB_FACTS through
     * assertions plus the runtime IDB_OVERFLOW guard, which fails the solve
     * before publication. Therefore -1 remains an unambiguous sentinel. */
    return (int32_t)count;
}

void maelys_datalog_wasm_solve_result_free(void) {
    free_solve_result_only();
    maelys_datalog_edb_clear(&s_edb);
    s_edb_state = WASM_EDB_STATE_EMPTY;
}

maelys_result_t maelys_datalog_wasm_load_ruleset(const char *domain_name,
                                                const char *policy_id,
                                                const char *src,
                                                size_t src_len) {
    char domain_buf[MAELYS_DATALOG_INLINE_MAX_DOMAIN_LEN + 1u];
    char policy_buf[MAELYS_DATALOG_INLINE_MAX_POLICY_ID_LEN + 1u];
    reset_edb_runtime_state();
    maelys_datalog_policy_set_clear(&s_policy_set);
    maelys_datalog_diagnostic_clear(&s_last_diag);

    maelys_result_t rc = copy_bounded(domain_buf, sizeof(domain_buf), domain_name);
    if (rc != MAELYS_OK) return rc;
    rc = copy_bounded(policy_buf, sizeof(policy_buf), policy_id);
    if (rc != MAELYS_OK) return rc;

    rc = maelys_datalog_load_policy_inline(domain_buf,
                                           policy_buf,
                                           src,
                                           src_len,
                                           0u,
                                           &s_policy_set,
                                           &s_last_diag);
    if (rc != MAELYS_OK) {
        maelys_datalog_policy_set_clear(&s_policy_set);
        return rc;
    }
    s_policy_set.enforces_query_whitelist = 0;
    s_policy_set.query_whitelist_count = 0u;
    if (s_policy_set.policy_count == 1u) {
        s_policy_set.policies[0].enforces_query_whitelist = 0;
        s_policy_set.policies[0].query_whitelist_count = 0u;
    }
    return MAELYS_OK;
}

uintptr_t maelys_datalog_wasm_ruleset_ptr(void) {
    if (s_policy_set.policy_count != 1u || !s_policy_set.policies[0].loaded) return (uintptr_t)0u;
    return (uintptr_t)&s_policy_set.policies[0];
}

const char *maelys_datalog_wasm_last_diag_message(void) {
    return s_last_diag.message[0] ? s_last_diag.message : "";
}

const char *maelys_datalog_wasm_last_diag_hint(void) {
    return s_last_diag.hint[0] ? s_last_diag.hint : "";
}

int maelys_datalog_wasm_last_diag_code(void) {
    return (int)s_last_diag.code;
}
