/* SPDX-License-Identifier: MPL-2.0 */
#include "reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(datalog_cli_error_t *error, const char *message, size_t index,
                int indexed) {
    (void)snprintf(error->message, sizeof(error->message), "%s", message);
    error->index = index;
    error->has_index = indexed;
    return -1;
}

static int keys(const maelys_json_document_t *doc, maelys_json_value_t object,
                const char *const *allowed, datalog_cli_error_t *error) {
    size_t count = 0;
    if (maelys_json_object_size(doc, object, &count) != MAELYS_JSON_OK)
        return fail(error, "expected a JSON object", 0, 0);
    for (size_t i = 0; i < count; ++i) {
        maelys_json_view_t key;
        maelys_json_value_t value;
        if (maelys_json_object_member_at(doc, object, i, &key, &value) != MAELYS_JSON_OK)
            return fail(error, "invalid JSON object", 0, 0);
        int found = 0;
        for (size_t j = 0; allowed[j]; ++j)
            if (strcmp(key.data, allowed[j]) == 0) found = 1;
        if (!found) {
            (void)snprintf(error->message, sizeof(error->message),
                           "unknown domain key: %s", key.data);
            return -1;
        }
    }
    return 0;
}

static int string_member(const maelys_json_document_t *doc,
                         maelys_json_value_t object, const char *key,
                         size_t maximum, const char **out,
                         datalog_cli_error_t *error) {
    maelys_json_view_t text;
    if (maelys_json_object_get_string(doc, object, key, &text) != MAELYS_JSON_OK ||
        text.size == 0 || text.size > maximum) {
        (void)snprintf(error->message, sizeof(error->message),
                       "domain %s must be a nonempty string of at most %zu bytes", key,
                       maximum);
        return -1;
    }
    *out = text.data;
    return 0;
}

int datalog_cli_domain_read(const char *path, datalog_cli_domain_t *out,
                            datalog_cli_error_t *error) {
    static const char *const root_keys[] =
        {"format", "name", "predicates", "atoms", NULL};
    static const char *const predicate_keys[] =
        {"name", "arity", "role", "query", NULL};
    maelys_json_limits_t limits = {1024u * 1024u, 32u, 65536u};
    maelys_json_error_t json_error;
    memset(out, 0, sizeof(*out));
    memset(error, 0, sizeof(*error));
    out->path = path;
    maelys_json_result_t rc = maelys_json_document_parse_file(
        path, MAELYS_JSON_PROFILE_RFC8259, &limits, &out->document, &json_error);
    if (rc != MAELYS_JSON_OK) {
        error->io_error = rc == MAELYS_JSON_ERR_IO;
        error->internal_error = rc == MAELYS_JSON_ERR_MEMORY;
        error->parse_error = !error->io_error && !error->internal_error;
        error->line = json_error.line;
        error->column = json_error.column;
        (void)snprintf(error->message, sizeof(error->message),
                       "domain JSON rejected (%d) at %zu:%zu", (int)rc,
                       json_error.line, json_error.column);
        return -1;
    }
    const maelys_json_document_t *doc = out->document;
    maelys_json_value_t root = maelys_json_document_root(doc), node;
    if (keys(doc, root, root_keys, error) != 0) return -1;
    const char *format;
    if (string_member(doc, root, "format", 64u, &format, error) != 0) return -1;
    if (strcmp(format, "maelys-datalog-domain-v1") != 0)
        return fail(error, "unsupported domain format", 0, 0);
    if (string_member(doc, root, "name", 63u, &out->declaration.name, error) != 0)
        return -1;
    size_t max_predicates = 0, max_atoms = 0, max_atom_bytes = 0, max_arity = 0;
    if (maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_PREDICATES,
                                &max_predicates) != MAELYS_DATALOG_STATUS_OK ||
        maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_POLICY_ATOMS,
                                &max_atoms) != MAELYS_DATALOG_STATUS_OK ||
        maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_POLICY_ATOM_BYTES,
                                &max_atom_bytes) != MAELYS_DATALOG_STATUS_OK ||
        maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_ARITY,
                                &max_arity) != MAELYS_DATALOG_STATUS_OK) {
        error->internal_error = 1;
        return fail(error, "cannot read engine limits", 0, 0);
    }
    if (maelys_json_object_get(doc, root, "predicates", &node) != MAELYS_JSON_OK ||
        maelys_json_array_size(doc, node, &out->declaration.predicate_count) != MAELYS_JSON_OK ||
        out->declaration.predicate_count > max_predicates)
        return fail(error, "invalid predicate array or capacity", 0, 0);
    out->predicates = calloc(out->declaration.predicate_count + 1u,
                             sizeof(*out->predicates));
    if (!out->predicates) {
        error->internal_error = 1;
        return fail(error, "out of memory", 0, 0);
    }
    out->declaration.predicates = out->predicates;
    for (size_t i = 0; i < out->declaration.predicate_count; ++i) {
        maelys_json_value_t item;
        const char *role;
        uint64_t arity;
        if (maelys_json_array_get(doc, node, i, &item) != MAELYS_JSON_OK ||
            keys(doc, item, predicate_keys, error) != 0) {
            error->index = i; error->has_index = 1; return -1;
        }
        if (string_member(doc, item, "name", 63u, &out->predicates[i].name,
                          error) != 0 ||
            string_member(doc, item, "role", 32u, &role, error) != 0 ||
            maelys_json_object_get_u64(doc, item, "arity", &arity) != MAELYS_JSON_OK ||
            arity > max_arity || arity > MAELYS_DATALOG_PUBLIC_MAX_TERMS)
            return fail(error, "invalid predicate name, role or arity", i, 1);
        out->predicates[i].arity = (size_t)arity;
        if (strcmp(role, "edb") == 0)
            out->predicates[i].flags = MAELYS_DATALOG_PREDICATE_EDB;
        else if (strcmp(role, "idb") == 0)
            out->predicates[i].flags = MAELYS_DATALOG_PREDICATE_IDB;
        else if (strcmp(role, "policy_fact") == 0)
            out->predicates[i].flags = MAELYS_DATALOG_PREDICATE_POLICY_FACT;
        else return fail(error, "unknown predicate role", i, 1);
        maelys_json_value_t query;
        if (maelys_json_object_get(doc, item, "query", &query) == MAELYS_JSON_OK) {
            int enabled;
            if (maelys_json_value_boolean(doc, query, &enabled) != MAELYS_JSON_OK)
                return fail(error, "query must be boolean", i, 1);
            if (enabled) out->predicates[i].flags |= MAELYS_DATALOG_PREDICATE_QUERY;
        }
        for (size_t j = 0; j < i; ++j)
            if (strcmp(out->predicates[j].name, out->predicates[i].name) == 0)
                return fail(error, "duplicate predicate", i, 1);
    }
    if (maelys_json_object_get(doc, root, "atoms", &node) != MAELYS_JSON_OK ||
        maelys_json_array_size(doc, node, &out->declaration.atom_count) != MAELYS_JSON_OK ||
        out->declaration.atom_count > max_atoms)
        return fail(error, "invalid atom array or capacity", 0, 0);
    out->atoms = calloc(out->declaration.atom_count + 1u, sizeof(*out->atoms));
    if (!out->atoms) {
        error->internal_error = 1;
        return fail(error, "out of memory", 0, 0);
    }
    out->declaration.atoms = out->atoms;
    for (size_t i = 0; i < out->declaration.atom_count; ++i) {
        maelys_json_value_t item;
        maelys_json_view_t atom;
        if (maelys_json_array_get(doc, node, i, &item) != MAELYS_JSON_OK ||
            maelys_json_value_string(doc, item, &atom) != MAELYS_JSON_OK ||
            atom.size == 0 || atom.size > max_atom_bytes)
            return fail(error, "invalid atom", i, 1);
        out->atoms[i] = atom.data;
        for (size_t j = 0; j < i; ++j)
            if (strcmp(out->atoms[j], atom.data) == 0)
                return fail(error, "duplicate atom", i, 1);
    }
    return 0;
}

const maelys_datalog_predicate_t *datalog_cli_predicate(
    const datalog_cli_domain_t *domain, const char *name) {
    for (size_t i = 0; i < domain->declaration.predicate_count; ++i)
        if (strcmp(name, domain->predicates[i].name) == 0)
            return &domain->predicates[i];
    return NULL;
}

void datalog_cli_domain_free(datalog_cli_domain_t *domain) {
    free(domain->predicates);
    free(domain->atoms);
    maelys_json_document_release(domain->document);
    memset(domain, 0, sizeof(*domain));
}
