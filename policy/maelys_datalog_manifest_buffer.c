#include "policy/maelys_datalog_manifest.h"

#include "common/maelys_sha256.h"
#include "common/maelys_utf8.h"
#include "policy/maelys_datalog_diagnostic.h"
#include "policy/maelys_datalog_domain_registry.h"
#include "policy/maelys_datalog_parser.h"
#include "policy/maelys_datalog_predicate_registry.h"
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

static maelys_result_t add_decl_array(maelys_datalog_ruleset_t *ruleset,
                                      yyjson_val *arr,
                                      unsigned kind,
                                      maelys_datalog_diagnostic_t *diag,
                                      const char *manifest_path,
                                      const char *field_name) {
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
        maelys_result_t rc = maelys_datalog_predicate_registry_add_manifest_predicate(
            &ruleset->registry, name, arity, kind);
        if (rc != MAELYS_OK) {
            manifest_diag(diag,
                          rc == MAELYS_ERR_INVALID_STATE
                              ? MAELYS_DATALOG_DIAG_REGISTRY_MUTATION_AFTER_FREEZE
                              : MAELYS_DATALOG_DIAG_REGISTRY_CONFLICT,
                          manifest_path,
                          "predicate registry declaration rejected",
                          "check duplicate predicates, arity, and predicate kind");
            maelys_datalog_diagnostic_set_predicate(diag, name, arity);
            return rc;
        }
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
    if (!maelys_utf8_validate((const unsigned char *)src_entry->src, src_entry->src_len)) {
        manifest_diag(diag,
                      MAELYS_DATALOG_DIAG_LEXER_INVALID_UTF8,
                      policy_label,
                      "invalid UTF-8 in policy text",
                      "ensure the policy text is valid UTF-8");
        return MAELYS_ERR_INVALID_FIELD;
    }
    if (set->policy_count >= sizeof(set->policies) / sizeof(set->policies[0])) {
        return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    }

    maelys_datalog_ruleset_t tmp;
    memset(&tmp, 0, sizeof(tmp));
    maelys_result_t rc = maelys_datalog_ruleset_init(&tmp, policy_id, domain, sha, test_only);
    if (rc == MAELYS_OK) rc = maelys_datalog_domain_registry_install(domain, &tmp.registry);
    if (rc != MAELYS_OK) {
        manifest_diag(diag,
                      MAELYS_DATALOG_DIAG_REGISTRY_CONFLICT,
                      manifest_label,
                      "domain predicate registry install failed",
                      "check domain predicate declarations");
    }
    if (rc == MAELYS_OK) {
        rc = add_decl_array(&tmp,
                            yyjson_obj_get(entry, "idb_predicates"),
                            MAELYS_DATALOG_PRED_KIND_IDB,
                            diag,
                            manifest_label,
                            "idb_predicates");
    }
    if (rc == MAELYS_OK) {
        rc = add_decl_array(&tmp,
                            yyjson_obj_get(entry, "queries"),
                            MAELYS_DATALOG_PRED_KIND_QUERY | MAELYS_DATALOG_PRED_KIND_IDB,
                            diag,
                            manifest_label,
                            "queries");
    }
    if (rc == MAELYS_OK) rc = maelys_datalog_predicate_registry_freeze(&tmp.registry);
    if (rc == MAELYS_OK) {
        rc = maelys_datalog_parse_ruleset_ex(&tmp,
                                             src_entry->src,
                                             src_entry->src_len,
                                             policy_label,
                                             diag);
    }
    if (rc != MAELYS_OK) return rc;
    if (maelys_datalog_ruleset_has_allow_all(&tmp) && !test_only) return MAELYS_ERR_FORBIDDEN;
    set->policies[set->policy_count++] = tmp;
    return MAELYS_OK;
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

    yyjson_doc *doc = yyjson_read(manifest_json ? manifest_json : "", manifest_json_len, 0);
    yyjson_val *root = doc ? yyjson_doc_get_root(doc) : NULL;
    if (!root) {
        if (doc) yyjson_doc_free(doc);
        manifest_diag(out_diag,
                      MAELYS_DATALOG_DIAG_MANIFEST_INVALID_JSON,
                      "<manifest-buffer>",
                      "invalid manifest JSON",
                      "fix manifest JSON syntax");
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

void maelys_datalog_policy_set_clear(maelys_datalog_policy_set_t *set) {
    if (!set) return;
    memset(set, 0, sizeof(*set));
}
