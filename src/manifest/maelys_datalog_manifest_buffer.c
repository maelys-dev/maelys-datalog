#include "src/manifest/maelys_datalog_manifest.h"

#include "common/maelys_sha256.h"
#include "common/maelys_utf8.h"
#include "src/core/maelys_datalog_diagnostic.h"
#include "src/core/maelys_datalog_domain_registry.h"
#include "src/core/maelys_datalog_parser.h"
#include "src/core/maelys_datalog_predicate_registry.h"
#include "vendor/yyjson/yyjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int has_only_keys(yyjson_val *obj, const char *const *keys) {
    if (!yyjson_is_obj(obj)) return 0;
    yyjson_obj_iter iter = yyjson_obj_iter_with(obj);
    yyjson_val *key_val;
    while ((key_val = yyjson_obj_iter_next(&iter))) {
        const char *key = yyjson_get_str(key_val);
        yyjson_val *value = yyjson_obj_iter_get_val(key_val);
        (void)value;
        int ok = 0;
        for (size_t i = 0; keys[i]; i++) {
            if (strcmp(key, keys[i]) == 0) { ok = 1; break; }
        }
        if (!ok) return 0;
    }
    return 1;
}

static const char *first_unknown_key(yyjson_val *obj, const char *const *keys) {
    if (!yyjson_is_obj(obj)) return NULL;
    yyjson_obj_iter iter = yyjson_obj_iter_with(obj);
    yyjson_val *key_val;
    while ((key_val = yyjson_obj_iter_next(&iter))) {
        const char *key = yyjson_get_str(key_val);
        yyjson_val *value = yyjson_obj_iter_get_val(key_val);
        (void)value;
        int ok = 0;
        for (size_t i = 0; keys[i]; i++) {
            if (strcmp(key, keys[i]) == 0) { ok = 1; break; }
        }
        if (!ok) return key;
    }
    return NULL;
}

static void manifest_diag(maelys_datalog_diagnostic_t *diag,
                          maelys_datalog_diag_code_t code,
                          const char *file,
                          const char *message,
                          const char *hint) {
    maelys_datalog_diagnostic_set(diag, code, "manifest", file, 0, 0, message, hint);
}

static int required_string(yyjson_val *obj, const char *key) {
    return yyjson_is_str(yyjson_obj_get(obj, key));
}

static int required_bool(yyjson_val *obj, const char *key) {
    yyjson_val *v = yyjson_obj_get(obj, key);
    return yyjson_is_true(v) || yyjson_is_false(v);
}

static int required_array(yyjson_val *obj, const char *key) {
    return yyjson_is_arr(yyjson_obj_get(obj, key));
}

typedef struct {
    const char *name;
    size_t arity;
    unsigned kind_flags;
} maelys_datalog_policy_load_predicate_decl_t;

typedef struct {
    const char *policy_id;
    const char *domain;
    const char *src;
    size_t src_len;
    const char *sha256;
    int test_only;
    const maelys_datalog_policy_load_predicate_decl_t *idb_predicates;
    size_t idb_predicate_count;
    const maelys_datalog_policy_load_predicate_decl_t *queries;
    size_t query_count;
    int enforces_query_whitelist;
    const char *diagnostic_file_label;
} maelys_datalog_policy_load_spec_t;

static maelys_result_t collect_decl_array(
    yyjson_val *arr,
    unsigned kind,
    maelys_datalog_policy_load_predicate_decl_t *decls,
    size_t decl_cap,
    size_t *out_count,
    maelys_datalog_diagnostic_t *diag,
    const char *manifest_path,
    const char *field_name) {
    if (!out_count) return MAELYS_ERR_INVALID_ARGUMENT;
    *out_count = 0u;
    if (!arr) return MAELYS_OK;
    if (!yyjson_is_arr(arr)) {
        manifest_diag(diag,
                      MAELYS_DATALOG_DIAG_MANIFEST_INVALID_FIELD,
                      manifest_path,
                      "invalid manifest field",
                      "predicate declarations must be arrays");
        if (diag && field_name) snprintf(diag->field, sizeof(diag->field), "%s", field_name);
        return MAELYS_ERR_INVALID_FIELD;
    }
    static const char *const keys[] = {"name", "arity", NULL};
    yyjson_arr_iter iter = yyjson_arr_iter_with(arr);
    yyjson_val *it;
    while ((it = yyjson_arr_iter_next(&iter))) {
        if (*out_count >= decl_cap) return MAELYS_ERR_PAYLOAD_TOO_LARGE;
        if (!has_only_keys(it, keys) || !required_string(it, "name") ||
            !yyjson_is_int(yyjson_obj_get(it, "arity"))) {
            manifest_diag(diag,
                          MAELYS_DATALOG_DIAG_MANIFEST_INVALID_FIELD,
                          manifest_path,
                          "invalid predicate declaration",
                          "predicate declarations require name and arity");
            if (diag && field_name) snprintf(diag->field, sizeof(diag->field), "%s", field_name);
            return MAELYS_ERR_INVALID_FIELD;
        }
        const char *name = yyjson_get_str(yyjson_obj_get(it, "name"));
        size_t arity = (size_t)yyjson_get_int(yyjson_obj_get(it, "arity"));
        decls[*out_count].name = name;
        decls[*out_count].arity = arity;
        decls[*out_count].kind_flags = kind;
        (*out_count)++;
    }
    return MAELYS_OK;
}

static maelys_result_t append_policy_set_query_whitelist(
    maelys_datalog_policy_set_t *set,
    const maelys_datalog_query_whitelist_entry_t *entry) {
    if (!set || !entry) return MAELYS_ERR_INVALID_ARGUMENT;
    for (size_t i = 0u; i < set->query_whitelist_count; i++) {
        if (set->query_whitelist[i].arity == entry->arity &&
            strcmp(set->query_whitelist[i].name, entry->name) == 0) {
            return MAELYS_OK;
        }
    }
    if (set->query_whitelist_count >= MAELYS_DATALOG_MAX_QUERY_WHITELIST) {
        return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    }
    set->query_whitelist[set->query_whitelist_count++] = *entry;
    return MAELYS_OK;
}

static maelys_result_t validate_and_copy_query_whitelist(
    maelys_datalog_ruleset_t *ruleset,
    const maelys_datalog_policy_load_predicate_decl_t *decls,
    size_t decl_count,
    maelys_datalog_policy_set_t *set,
    maelys_datalog_diagnostic_t *diag,
    const char *manifest_path) {
    if (!ruleset || !set) return MAELYS_ERR_INVALID_ARGUMENT;
    for (size_t i = 0u; i < decl_count; i++) {
        const char *name = decls[i].name;
        size_t arity = decls[i].arity;
        maelys_datalog_predicate_id_t pid = 0;
        if (!maelys_datalog_predicate_registry_find(&ruleset->registry, name, arity, &pid)) {
            manifest_diag(diag,
                          MAELYS_DATALOG_DIAG_REGISTRY_CONFLICT,
                          manifest_path,
                          "query whitelist predicate rejected",
                          "queries must reference domain QUERY predicates with matching arity");
            maelys_datalog_diagnostic_set_predicate(diag, name, arity);
            return MAELYS_ERR_INVALID_FIELD;
        }
        const maelys_datalog_predicate_def_t *def =
            maelys_datalog_predicate_registry_get(&ruleset->registry, pid);
        if (!def || !(def->kind_flags & MAELYS_DATALOG_PRED_KIND_QUERY)) {
            manifest_diag(diag,
                          MAELYS_DATALOG_DIAG_REGISTRY_CONFLICT,
                          manifest_path,
                          "query whitelist predicate is not query-capable",
                          "queries may only expose predicates marked QUERY by the domain");
            maelys_datalog_diagnostic_set_predicate(diag, name, arity);
            return MAELYS_ERR_INVALID_FIELD;
        }
        if (ruleset->query_whitelist_count >= MAELYS_DATALOG_MAX_QUERY_WHITELIST) {
            return MAELYS_ERR_PAYLOAD_TOO_LARGE;
        }
        maelys_datalog_query_whitelist_entry_t entry;
        memset(&entry, 0, sizeof(entry));
        snprintf(entry.name, sizeof(entry.name), "%s", def->name);
        entry.arity = def->arity;
        ruleset->query_whitelist[ruleset->query_whitelist_count++] = entry;
        maelys_result_t rc = append_policy_set_query_whitelist(set, &entry);
        if (rc != MAELYS_OK) return rc;
    }
    return MAELYS_OK;
}

static const maelys_datalog_policy_bundle_entry_t *find_bundle(
    const maelys_datalog_policy_bundle_entry_t *bundle,
    size_t bundle_count,
    const char *policy_id) {
    if (!bundle || !policy_id) return NULL;
    for (size_t i = 0; i < bundle_count; i++) {
        if (bundle[i].policy_id && strcmp(bundle[i].policy_id, policy_id) == 0) {
            return &bundle[i];
        }
    }
    return NULL;
}

static maelys_result_t maelys_datalog_policy_load_from_spec(
    const maelys_datalog_policy_load_spec_t *spec,
    maelys_datalog_policy_set_t *set,
    maelys_datalog_diagnostic_t *diag) {
    if (!spec || !spec->policy_id || !spec->domain || !spec->src || !spec->sha256 || !set) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    const char *label = spec->diagnostic_file_label ? spec->diagnostic_file_label : spec->policy_id;
    if (!maelys_datalog_domain_registry_find(spec->domain)) {
        manifest_diag(diag,
                      MAELYS_DATALOG_DIAG_MANIFEST_UNKNOWN_DOMAIN,
                      label,
                      "unknown policy domain",
                      "install a domain registry or disable the policy");
        if (diag) snprintf(diag->domain, sizeof(diag->domain), "%s", spec->domain);
        return MAELYS_ERR_UNSUPPORTED;
    }
    if (!maelys_utf8_validate((const unsigned char *)spec->src, spec->src_len)) {
        manifest_diag(diag,
                      MAELYS_DATALOG_DIAG_LEXER_INVALID_UTF8,
                      label,
                      "invalid UTF-8 in policy text",
                      "ensure the policy text is valid UTF-8");
        return MAELYS_ERR_INVALID_FIELD;
    }
    if (set->policy_count >= sizeof(set->policies) / sizeof(set->policies[0])) {
        return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    }

    maelys_datalog_ruleset_t *tmp =
        (maelys_datalog_ruleset_t *)calloc(1u, sizeof(*tmp));
    if (!tmp) return MAELYS_ERR_INTERNAL;

    maelys_result_t rc = maelys_datalog_ruleset_init(
        tmp, spec->policy_id, spec->domain, spec->sha256, spec->test_only);
    if (rc == MAELYS_OK) {
        rc = maelys_datalog_domain_registry_install(spec->domain, &tmp->registry);
    }
    if (rc != MAELYS_OK) {
        manifest_diag(diag,
                      MAELYS_DATALOG_DIAG_REGISTRY_CONFLICT,
                      label,
                      "domain predicate registry install failed",
                      "check domain predicate declarations");
    }
    if (rc == MAELYS_OK) {
        (void)spec->idb_predicates;
        (void)spec->idb_predicate_count;
        tmp->enforces_query_whitelist = spec->enforces_query_whitelist ? 1 : 0;
        if (tmp->enforces_query_whitelist) {
            set->enforces_query_whitelist = 1;
            rc = validate_and_copy_query_whitelist(tmp,
                                                   spec->queries,
                                                   spec->query_count,
                                                   set,
                                                   diag,
                                                   label);
        }
    }
    if (rc == MAELYS_OK) {
        rc = maelys_datalog_predicate_registry_freeze(&tmp->registry);
    }
    if (rc == MAELYS_OK) {
        rc = maelys_datalog_parse_ruleset_ex(tmp,
                                             spec->src,
                                             spec->src_len,
                                             label,
                                             diag);
    }
    if (rc == MAELYS_OK &&
        maelys_datalog_ruleset_has_allow_all(tmp) &&
        !spec->test_only) {
        rc = MAELYS_ERR_FORBIDDEN;
    }
    if (rc == MAELYS_OK) {
        set->policies[set->policy_count++] = *tmp;
    }
    free(tmp);
    return rc;
}

static maelys_result_t load_policy_entry_from_bundle(
    yyjson_val *entry,
    const char *manifest_label,
    const maelys_datalog_policy_bundle_entry_t *bundle,
    size_t bundle_count,
    unsigned flags,
    maelys_datalog_policy_set_t *set,
    maelys_datalog_diagnostic_t *diag) {
    static const char *const keys[] = {
        "policy_id", "domain", "file", "sha256", "mode", "enabled", "description",
        "idb_predicates", "queries", NULL
    };
    if (!has_only_keys(entry, keys)) {
        manifest_diag(diag,
                      MAELYS_DATALOG_DIAG_MANIFEST_UNKNOWN_FIELD,
                      manifest_label,
                      "unknown policy entry field",
                      "remove unsupported fields from the policy entry");
        const char *unknown = first_unknown_key(entry, keys);
        if (diag && unknown) snprintf(diag->field, sizeof(diag->field), "%s", unknown);
        return MAELYS_ERR_INVALID_FIELD;
    }
    const char *required[] = {"policy_id", "domain", "file", "sha256", "mode", "description", NULL};
    for (size_t i = 0; required[i]; i++) {
        if (!required_string(entry, required[i])) {
            manifest_diag(diag,
                          MAELYS_DATALOG_DIAG_MANIFEST_INVALID_FIELD,
                          manifest_label,
                          "invalid required policy field",
                          "provide all required policy fields with the expected type");
            if (diag) snprintf(diag->field, sizeof(diag->field), "%s", required[i]);
            return MAELYS_ERR_INVALID_FIELD;
        }
    }
    if (!required_bool(entry, "enabled")) {
        manifest_diag(diag,
                      MAELYS_DATALOG_DIAG_MANIFEST_INVALID_FIELD,
                      manifest_label,
                      "invalid enabled field",
                      "enabled must be a boolean");
        if (diag) snprintf(diag->field, sizeof(diag->field), "%s", "enabled");
        return MAELYS_ERR_INVALID_FIELD;
    }
    int enabled = yyjson_is_true(yyjson_obj_get(entry, "enabled"));
    if (!enabled) return MAELYS_OK;

    const char *policy_id = yyjson_get_str(yyjson_obj_get(entry, "policy_id"));
    const char *domain = yyjson_get_str(yyjson_obj_get(entry, "domain"));
    const char *policy_label = policy_id ? policy_id : manifest_label;
    if (!maelys_datalog_domain_registry_find(domain)) {
        manifest_diag(diag,
                      MAELYS_DATALOG_DIAG_MANIFEST_UNKNOWN_DOMAIN,
                      manifest_label,
                      "unknown policy domain",
                      "install a domain registry or disable the policy");
        if (diag && domain) snprintf(diag->domain, sizeof(diag->domain), "%s", domain);
        return MAELYS_ERR_UNSUPPORTED;
    }

    const char *mode = yyjson_get_str(yyjson_obj_get(entry, "mode"));
    int test_only = strcmp(mode, "test_only") == 0;
    if (test_only && !(flags & MAELYS_DATALOG_MANIFEST_ALLOW_TEST_ONLY)) {
        manifest_diag(diag,
                      MAELYS_DATALOG_DIAG_MANIFEST_TEST_ONLY_REJECTED,
                      manifest_label,
                      "test-only policy rejected in production mode",
                      "load with test-only permission only for test manifests");
        return MAELYS_ERR_FORBIDDEN;
    }
    if (!test_only && strcmp(mode, "enforce") != 0 && strcmp(mode, "shadow") != 0) {
        manifest_diag(diag,
                      MAELYS_DATALOG_DIAG_MANIFEST_INVALID_FIELD,
                      manifest_label,
                      "invalid policy mode",
                      "mode must be enforce, shadow, or test_only");
        if (diag) snprintf(diag->field, sizeof(diag->field), "%s", "mode");
        return MAELYS_ERR_INVALID_FIELD;
    }

    const char *sha = yyjson_get_str(yyjson_obj_get(entry, "sha256"));
    if (!maelys_sha256_hex_is_lowercase(sha)) {
        manifest_diag(diag,
                      MAELYS_DATALOG_DIAG_MANIFEST_INVALID_FIELD,
                      manifest_label,
                      "invalid policy SHA-256 field",
                      "sha256 must be lowercase hexadecimal");
        if (diag) snprintf(diag->field, sizeof(diag->field), "%s", "sha256");
        return MAELYS_ERR_INVALID_FIELD;
    }

    const maelys_datalog_policy_bundle_entry_t *src_entry =
        find_bundle(bundle, bundle_count, policy_id);
    if (!src_entry || !src_entry->src) {
        manifest_diag(diag,
                      MAELYS_DATALOG_DIAG_MANIFEST_POLICY_NOT_FOUND,
                      policy_label,
                      "policy source not found",
                      "provide a bundle entry matching policy_id");
        return MAELYS_ERR_NOT_FOUND;
    }

    char actual[65];
    if (maelys_sha256_hex((const unsigned char *)src_entry->src, src_entry->src_len, actual) != 0) {
        return MAELYS_ERR_INTERNAL;
    }
    if (strcmp(actual, sha) != 0) {
        manifest_diag(diag,
                      MAELYS_DATALOG_DIAG_MANIFEST_SHA_MISMATCH,
                      policy_label,
                      "policy SHA mismatch",
                      "update manifest sha256 after policy edit");
        return MAELYS_ERR_INVALID_FIELD;
    }

    maelys_datalog_policy_load_predicate_decl_t idb_predicates[MAELYS_DATALOG_MAX_PREDICATES];
    maelys_datalog_policy_load_predicate_decl_t queries[MAELYS_DATALOG_MAX_PREDICATES];
    size_t idb_predicate_count = 0u;
    size_t query_count = 0u;
    maelys_result_t rc = collect_decl_array(yyjson_obj_get(entry, "idb_predicates"),
                                            MAELYS_DATALOG_PRED_KIND_IDB,
                                            idb_predicates,
                                            sizeof(idb_predicates) / sizeof(idb_predicates[0]),
                                            &idb_predicate_count,
                                            diag,
                                            manifest_label,
                                            "idb_predicates");
    if (rc != MAELYS_OK) return rc;
    rc = collect_decl_array(yyjson_obj_get(entry, "queries"),
                            MAELYS_DATALOG_PRED_KIND_QUERY | MAELYS_DATALOG_PRED_KIND_IDB,
                            queries,
                            MAELYS_DATALOG_MAX_QUERY_WHITELIST,
                            &query_count,
                            diag,
                            manifest_label,
                            "queries");
    if (rc != MAELYS_OK) return rc;

    maelys_datalog_policy_load_spec_t spec = {
        .policy_id = policy_id,
        .domain = domain,
        .src = src_entry->src,
        .src_len = src_entry->src_len,
        .sha256 = sha,
        .test_only = test_only,
        .idb_predicates = idb_predicates,
        .idb_predicate_count = idb_predicate_count,
        .queries = queries,
        .query_count = query_count,
        .enforces_query_whitelist = 1,
        .diagnostic_file_label = policy_label,
    };
    return maelys_datalog_policy_load_from_spec(&spec, set, diag);
}

maelys_result_t maelys_datalog_manifest_load_from_text(
    const char *manifest_json,
    size_t manifest_json_len,
    const maelys_datalog_policy_bundle_entry_t *bundle,
    size_t bundle_count,
    unsigned flags,
    maelys_datalog_policy_set_t *out_set,
    maelys_datalog_diagnostic_t *out_diag) {
    if ((!manifest_json && manifest_json_len > 0u) || !out_set) return MAELYS_ERR_INVALID_ARGUMENT;
    if (bundle_count > 0u && !bundle) return MAELYS_ERR_INVALID_ARGUMENT;
    if (out_diag) maelys_datalog_diagnostic_clear(out_diag);
    memset(out_set, 0, sizeof(*out_set));
    out_set->enforces_query_whitelist = 1;

    yyjson_doc *doc = yyjson_read(manifest_json ? manifest_json : "", manifest_json_len, 0);
    yyjson_val *root = doc ? yyjson_doc_get_root(doc) : NULL;
    if (!root) {
        if (doc) yyjson_doc_free(doc);
        manifest_diag(out_diag,
                      MAELYS_DATALOG_DIAG_MANIFEST_INVALID_JSON,
                      "<manifest-buffer>",
                      "invalid manifest JSON",
                      "fix manifest JSON syntax");
        maelys_datalog_policy_set_clear(out_set);
        return MAELYS_ERR_INVALID_FIELD;
    }

    static const char *const keys[] = {
        "policy_set_id", "policy_set_version", "manifest_version", "policies",
        "capabilities", "default_profile", "strict_loading", "fail_closed", "created_for", NULL
    };
    maelys_result_t rc = MAELYS_OK;
    if (!has_only_keys(root, keys) ||
        !required_string(root, "policy_set_id") ||
        !required_string(root, "policy_set_version") ||
        !required_string(root, "manifest_version") ||
        !required_string(root, "default_profile") ||
        !required_string(root, "created_for") ||
        !required_array(root, "policies") ||
        !required_array(root, "capabilities") ||
        !required_bool(root, "strict_loading") ||
        !required_bool(root, "fail_closed")) {
        rc = MAELYS_ERR_INVALID_FIELD;
        const char *unknown = first_unknown_key(root, keys);
        manifest_diag(out_diag,
                      unknown
                          ? MAELYS_DATALOG_DIAG_MANIFEST_UNKNOWN_FIELD
                          : MAELYS_DATALOG_DIAG_MANIFEST_INVALID_FIELD,
                      "<manifest-buffer>",
                      unknown ? "unknown manifest field" : "invalid manifest field",
                      "provide the required manifest fields with accepted names and types");
        if (out_diag && unknown) snprintf(out_diag->field, sizeof(out_diag->field), "%s", unknown);
    }
    if (rc == MAELYS_OK) {
        yyjson_val *policies = yyjson_obj_get(root, "policies");
        yyjson_arr_iter iter = yyjson_arr_iter_with(policies);
        yyjson_val *entry;
        while ((entry = yyjson_arr_iter_next(&iter))) {
            rc = load_policy_entry_from_bundle(entry,
                                               "<manifest-buffer>",
                                               bundle,
                                               bundle_count,
                                               flags,
                                               out_set,
                                               out_diag);
            if (rc != MAELYS_OK) break;
        }
    }
    yyjson_doc_free(doc);
    if (rc != MAELYS_OK) maelys_datalog_policy_set_clear(out_set);
    return rc;
}

static size_t bounded_cstr_len(const char *value, size_t max_plus_one) {
    for (size_t i = 0u; i < max_plus_one; i++) {
        if (value[i] == '\0') return i;
    }
    return max_plus_one;
}

static maelys_result_t validate_inline_identity(const char *value, size_t max_len) {
    if (!value) return MAELYS_ERR_INVALID_ARGUMENT;
    size_t len = bounded_cstr_len(value, max_len + 1u);
    if (len == 0u) return MAELYS_ERR_INVALID_ARGUMENT;
    if (len > max_len) return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    return MAELYS_OK;
}

maelys_result_t maelys_datalog_load_policy_inline(
    const char *domain,
    const char *policy_id,
    const char *src,
    size_t src_len,
    unsigned flags,
    maelys_datalog_policy_set_t *out_set,
    maelys_datalog_diagnostic_t *out_diag) {
    if (!out_set) return MAELYS_ERR_INVALID_ARGUMENT;
    memset(out_set, 0, sizeof(*out_set));
    if (out_diag) maelys_datalog_diagnostic_clear(out_diag);
    if (flags != 0u) return MAELYS_ERR_INVALID_ARGUMENT;

    maelys_result_t rc = validate_inline_identity(domain, MAELYS_DATALOG_INLINE_MAX_DOMAIN_LEN);
    if (rc != MAELYS_OK) return rc;
    rc = validate_inline_identity(policy_id, MAELYS_DATALOG_INLINE_MAX_POLICY_ID_LEN);
    if (rc != MAELYS_OK) return rc;
    if (!src || src_len == 0u) return MAELYS_ERR_INVALID_ARGUMENT;

    char sha[65];
    if (maelys_sha256_hex((const unsigned char *)src, src_len, sha) != 0) {
        return MAELYS_ERR_INTERNAL;
    }

    maelys_datalog_policy_load_spec_t spec = {
        .policy_id = policy_id,
        .domain = domain,
        .src = src,
        .src_len = src_len,
        .sha256 = sha,
        .test_only = 0,
        .idb_predicates = NULL,
        .idb_predicate_count = 0u,
        .queries = NULL,
        .query_count = 0u,
        .enforces_query_whitelist = 0,
        .diagnostic_file_label = "inline",
    };
    rc = maelys_datalog_policy_load_from_spec(&spec, out_set, out_diag);
    if (rc != MAELYS_OK) maelys_datalog_policy_set_clear(out_set);
    return rc;
}

maelys_result_t maelys_datalog_load_policy_inline_with_static_domain(
    const maelys_datalog_predicate_def_t *predicates,
    size_t predicate_count,
    const char *domain_name,
    const char *policy_id,
    const char *src,
    size_t src_len,
    unsigned flags,
    maelys_datalog_policy_set_t *out_set,
    maelys_datalog_diagnostic_t *out_diag) {
    if (!out_set) return MAELYS_ERR_INVALID_ARGUMENT;
    memset(out_set, 0, sizeof(*out_set));
    if (out_diag) maelys_datalog_diagnostic_clear(out_diag);

    maelys_result_t rc = validate_inline_identity(domain_name, MAELYS_DATALOG_INLINE_MAX_DOMAIN_LEN);
    if (rc != MAELYS_OK) return rc;

    maelys_datalog_domain_def_t def = {
        .domain_name = domain_name,
        .predicates = predicates,
        .predicate_count = predicate_count,
        .description = NULL,
        .install_predicates = NULL,
    };
    rc = maelys_datalog_domain_registry_register(&def);
    if (rc != MAELYS_OK) return rc;

    return maelys_datalog_load_policy_inline(domain_name,
                                             policy_id,
                                             src,
                                             src_len,
                                             flags,
                                             out_set,
                                             out_diag);
}

void maelys_datalog_policy_set_clear(maelys_datalog_policy_set_t *set) {
    if (!set) return;
    memset(set, 0, sizeof(*set));
}
