#include "policy/maelys_datalog_manifest.h"

#include "vendor/yyjson/yyjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void manifest_diag(maelys_datalog_diagnostic_t *diag,
                          maelys_datalog_diag_code_t code,
                          const char *file,
                          const char *message,
                          const char *hint) {
    maelys_datalog_diagnostic_set(diag, code, "manifest", file, 0, 0, message, hint);
}

static int safe_relative_path(const char *path) {
    if (!path || !*path || path[0] == '/') return 0;
    if (strstr(path, "..")) return 0;
    if (strchr(path, '\\')) return 0;
    return 1;
}

static void dirname_of(const char *path, char *out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    const char *slash = path ? strrchr(path, '/') : NULL;
    if (!slash) {
        snprintf(out, cap, ".");
        return;
    }
    size_t len = (size_t)(slash - path);
    if (len >= cap) len = cap - 1u;
    memcpy(out, path, len);
    out[len] = '\0';
}

static int join_path(const char *dir, const char *rel, char *out, size_t cap) {
    if (!dir || !rel || !out || cap == 0 || !safe_relative_path(rel)) return 0;
    int n = snprintf(out, cap, "%s/%s", dir, rel);
    return n > 0 && (size_t)n < cap;
}

static maelys_result_t read_file(const char *path, char **out, size_t *out_len) {
    if (!path || !out || !out_len) return MAELYS_ERR_INVALID_ARGUMENT;
    *out = NULL;
    *out_len = 0;
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0) return MAELYS_ERR_NOT_FOUND;
    FILE *f = fopen(path, "rb");
    if (!f) return MAELYS_ERR_NOT_FOUND;
    size_t len = (size_t)st.st_size;
    char *buf = (char *)malloc(len + 1u);
    if (!buf) {
        fclose(f);
        return MAELYS_ERR_INTERNAL;
    }
    size_t n = fread(buf, 1, len, f);
    int err = ferror(f);
    fclose(f);
    if (err || n != len) {
        free(buf);
        return MAELYS_ERR_IO;
    }
    buf[len] = '\0';
    *out = buf;
    *out_len = len;
    return MAELYS_OK;
}

static char *copy_json_string(yyjson_val *val) {
    const char *src = yyjson_get_str(val);
    if (!src) return NULL;
    size_t len = strlen(src);
    char *copy = (char *)malloc(len + 1u);
    if (!copy) return NULL;
    memcpy(copy, src, len + 1u);
    return copy;
}

static void free_bundle(maelys_datalog_policy_bundle_entry_t *bundle,
                        char **src_buffers,
                        size_t bundle_count) {
    if (bundle) {
        for (size_t i = 0u; i < bundle_count; i++) free((char *)bundle[i].policy_id);
    }
    if (src_buffers) {
        for (size_t i = 0u; i < bundle_count; i++) free(src_buffers[i]);
    }
    free(src_buffers);
    free(bundle);
}

static maelys_result_t build_bundle_from_manifest(
    const char *manifest_path,
    const char *manifest_dir,
    const char *manifest_buf,
    size_t manifest_len,
    maelys_datalog_policy_bundle_entry_t **out_bundle,
    char ***out_src_buffers,
    size_t *out_bundle_count,
    maelys_datalog_diagnostic_t *out_diag) {
    *out_bundle = NULL;
    *out_src_buffers = NULL;
    *out_bundle_count = 0u;

    yyjson_doc *doc = yyjson_read(manifest_buf, manifest_len, 0);
    yyjson_val *root = doc ? yyjson_doc_get_root(doc) : NULL;
    if (!root || !yyjson_is_obj(root)) {
        if (doc) yyjson_doc_free(doc);
        manifest_diag(out_diag,
                      MAELYS_DATALOG_DIAG_MANIFEST_INVALID_JSON,
                      manifest_path,
                      "invalid manifest JSON",
                      "fix manifest JSON syntax");
        return MAELYS_ERR_INVALID_FIELD;
    }

    yyjson_val *policies = yyjson_obj_get(root, "policies");
    if (!yyjson_is_arr(policies)) {
        yyjson_doc_free(doc);
        manifest_diag(out_diag,
                      MAELYS_DATALOG_DIAG_MANIFEST_INVALID_FIELD,
                      manifest_path,
                      "invalid manifest field",
                      "policies must be an array");
        if (out_diag) snprintf(out_diag->field, sizeof(out_diag->field), "%s", "policies");
        return MAELYS_ERR_INVALID_FIELD;
    }

    size_t policy_cap = yyjson_get_len(policies);
    maelys_datalog_policy_bundle_entry_t *bundle =
        (maelys_datalog_policy_bundle_entry_t *)calloc(policy_cap ? policy_cap : 1u, sizeof(*bundle));
    char **src_buffers = (char **)calloc(policy_cap ? policy_cap : 1u, sizeof(*src_buffers));
    if (!bundle || !src_buffers) {
        yyjson_doc_free(doc);
        free(bundle);
        free(src_buffers);
        return MAELYS_ERR_INTERNAL;
    }

    size_t bundle_count = 0u;
    yyjson_arr_iter iter = yyjson_arr_iter_with(policies);
    yyjson_val *entry;
    while ((entry = yyjson_arr_iter_next(&iter))) {
        yyjson_val *enabled = yyjson_obj_get(entry, "enabled");
        if (!yyjson_is_true(enabled)) continue;

        yyjson_val *policy_id_val = yyjson_obj_get(entry, "policy_id");
        yyjson_val *file_val = yyjson_obj_get(entry, "file");
        if (!yyjson_is_str(policy_id_val) || !yyjson_is_str(file_val)) {
            yyjson_doc_free(doc);
            free_bundle(bundle, src_buffers, bundle_count);
            manifest_diag(out_diag,
                          MAELYS_DATALOG_DIAG_MANIFEST_INVALID_FIELD,
                          manifest_path,
                          "invalid required policy field",
                          "enabled policies require policy_id and file strings");
            return MAELYS_ERR_INVALID_FIELD;
        }

        char path[512];
        const char *rel = yyjson_get_str(file_val);
        if (!join_path(manifest_dir, rel, path, sizeof(path))) {
            yyjson_doc_free(doc);
            free_bundle(bundle, src_buffers, bundle_count);
            manifest_diag(out_diag,
                          MAELYS_DATALOG_DIAG_MANIFEST_INVALID_FIELD,
                          manifest_path,
                          "invalid policy file path",
                          "policy files must use safe relative paths");
            if (out_diag) snprintf(out_diag->field, sizeof(out_diag->field), "%s", "file");
            return MAELYS_ERR_INVALID_FIELD;
        }

        char *src = NULL;
        size_t src_len = 0u;
        maelys_result_t rc = read_file(path, &src, &src_len);
        if (rc != MAELYS_OK) {
            yyjson_doc_free(doc);
            free_bundle(bundle, src_buffers, bundle_count);
            manifest_diag(out_diag,
                          MAELYS_DATALOG_DIAG_MANIFEST_POLICY_NOT_FOUND,
                          path,
                          "policy file not found",
                          "ensure the manifest policy file path exists");
            return rc;
        }

        char *policy_id = copy_json_string(policy_id_val);
        if (!policy_id) {
            free(src);
            yyjson_doc_free(doc);
            free_bundle(bundle, src_buffers, bundle_count);
            return MAELYS_ERR_INTERNAL;
        }

        src_buffers[bundle_count] = src;
        bundle[bundle_count].policy_id = policy_id;
        bundle[bundle_count].src = src;
        bundle[bundle_count].src_len = src_len;
        bundle_count++;
    }

    yyjson_doc_free(doc);
    *out_bundle = bundle;
    *out_src_buffers = src_buffers;
    *out_bundle_count = bundle_count;
    return MAELYS_OK;
}

maelys_result_t maelys_datalog_manifest_load(const char *manifest_path,
                                             unsigned flags,
                                             maelys_datalog_policy_set_t *out_set) {
    return maelys_datalog_manifest_load_ex(manifest_path, flags, out_set, NULL);
}

maelys_result_t maelys_datalog_manifest_load_ex(const char *manifest_path,
                                                unsigned flags,
                                                maelys_datalog_policy_set_t *out_set,
                                                maelys_datalog_diagnostic_t *out_diag) {
    if (!manifest_path || !out_set) return MAELYS_ERR_INVALID_ARGUMENT;
    if (out_diag) maelys_datalog_diagnostic_clear(out_diag);

    char *manifest_buf = NULL;
    size_t manifest_len = 0u;
    maelys_result_t rc = read_file(manifest_path, &manifest_buf, &manifest_len);
    if (rc != MAELYS_OK) {
        manifest_diag(out_diag,
                      MAELYS_DATALOG_DIAG_MANIFEST_POLICY_NOT_FOUND,
                      manifest_path,
                      "manifest file not found",
                      "ensure the manifest path exists");
        return rc;
    }

    char manifest_dir[512];
    dirname_of(manifest_path, manifest_dir, sizeof(manifest_dir));

    maelys_datalog_policy_bundle_entry_t *bundle = NULL;
    char **src_buffers = NULL;
    size_t bundle_count = 0u;
    rc = build_bundle_from_manifest(manifest_path,
                                    manifest_dir,
                                    manifest_buf,
                                    manifest_len,
                                    &bundle,
                                    &src_buffers,
                                    &bundle_count,
                                    out_diag);
    if (rc == MAELYS_OK) {
        rc = maelys_datalog_manifest_load_from_text(manifest_buf,
                                                    manifest_len,
                                                    bundle,
                                                    bundle_count,
                                                    flags,
                                                    out_set,
                                                    out_diag);
    }

    free_bundle(bundle, src_buffers, bundle_count);
    free(manifest_buf);
    return rc;
}
