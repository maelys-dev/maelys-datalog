#include "src/wasm/maelys_datalog_wasm.h"

#include "src/core/maelys_datalog_domain_registry.h"
#include "src/core/maelys_datalog_diagnostic.h"
#include "src/manifest/maelys_datalog_manifest.h"

#include <string.h>

#define MAELYS_DATALOG_WASM_VALID_KIND_MASK \
    (MAELYS_DATALOG_PRED_KIND_EDB | MAELYS_DATALOG_PRED_KIND_IDB | \
     MAELYS_DATALOG_PRED_KIND_QUERY | MAELYS_DATALOG_PRED_KIND_POLICY_FACT)

static char s_domain_name[MAELYS_DATALOG_INLINE_MAX_DOMAIN_LEN + 1u];
static maelys_datalog_predicate_def_t s_predicates[MAELYS_DATALOG_MAX_PREDICATES];
static size_t s_pred_count;
static int s_building;
static int s_has_committed;
static maelys_datalog_policy_set_t s_policy_set;
static maelys_datalog_diagnostic_t s_last_diag;

static void builder_clear_current(void) {
    memset(s_domain_name, 0, sizeof(s_domain_name));
    memset(s_predicates, 0, sizeof(s_predicates));
    s_pred_count = 0u;
    s_building = 0;
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

maelys_result_t maelys_datalog_wasm_domain_begin(const char *name) {
    if (s_building || s_has_committed) return MAELYS_ERR_INVALID_STATE;
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
    if (s_building) builder_clear_current();
    return MAELYS_OK;
}

maelys_result_t maelys_datalog_wasm_load_policy(const char *domain_name,
                                                const char *policy_id,
                                                const char *src,
                                                size_t src_len) {
    maelys_datalog_policy_set_clear(&s_policy_set);
    maelys_datalog_diagnostic_clear(&s_last_diag);
    maelys_result_t rc = maelys_datalog_load_policy_inline(domain_name,
                                                           policy_id,
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

int maelys_datalog_wasm_last_diag_code(void) {
    return (int)s_last_diag.code;
}
