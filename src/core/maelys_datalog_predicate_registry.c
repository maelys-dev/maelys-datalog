#include "src/core/maelys_datalog_predicate_registry.h"

#include <string.h>

void maelys_datalog_predicate_registry_init(maelys_datalog_predicate_registry_t *registry) {
    if (!registry) return;
    memset(registry, 0, sizeof(*registry));
}

void maelys_datalog_predicate_registry_init_core(maelys_datalog_predicate_registry_t *registry) {
    maelys_datalog_predicate_registry_init(registry);
}

static int same_name(const char *a, const char *b) {
    return a && b && strcmp(a, b) == 0;
}

static int valid_kind_flags(unsigned kind_flags) {
    if (kind_flags == MAELYS_DATALOG_PRED_KIND_EDB) return 1;
    if (kind_flags == MAELYS_DATALOG_PRED_KIND_POLICY_FACT) return 1;
    if (kind_flags == MAELYS_DATALOG_PRED_KIND_IDB) return 1;
    if (kind_flags == (MAELYS_DATALOG_PRED_KIND_QUERY | MAELYS_DATALOG_PRED_KIND_IDB)) return 1;
    return 0;
}

maelys_result_t maelys_datalog_predicate_registry_add(maelys_datalog_predicate_registry_t *registry,
                                                      const char *name,
                                                      size_t arity,
                                                      unsigned kind_flags) {
    if (!registry || !name || !*name || arity > MAELYS_DATALOG_MAX_ARITY ||
        !valid_kind_flags(kind_flags)) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    if (registry->frozen) return MAELYS_ERR_INVALID_STATE;

    for (size_t i = 0; i < registry->count; i++) {
        if (!same_name(registry->defs[i].name, name)) continue;
        if (registry->defs[i].arity != arity) return MAELYS_ERR_INVALID_FIELD;
        if (registry->defs[i].kind_flags == kind_flags ||
            (registry->defs[i].kind_flags | kind_flags) == registry->defs[i].kind_flags) {
            return MAELYS_OK;
        }
        return MAELYS_ERR_INVALID_FIELD;
    }

    if (registry->count >= MAELYS_DATALOG_MAX_PREDICATES) return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    size_t len = strlen(name);
    if (len >= sizeof(registry->defs[0].name)) return MAELYS_ERR_INVALID_FIELD;
    memcpy(registry->defs[registry->count].name, name, len + 1u);
    registry->defs[registry->count].arity = arity;
    registry->defs[registry->count].kind_flags = kind_flags;
    registry->count++;
    return MAELYS_OK;
}

maelys_result_t maelys_datalog_predicate_registry_add_builtin(maelys_datalog_predicate_registry_t *registry,
                                                              const char *name,
                                                              size_t arity,
                                                              unsigned kind_flags) {
    return maelys_datalog_predicate_registry_add(registry, name, arity, kind_flags);
}

maelys_result_t maelys_datalog_predicate_registry_add_domain(maelys_datalog_predicate_registry_t *registry,
                                                             const char *name,
                                                             size_t arity,
                                                             unsigned kind_flags) {
    return maelys_datalog_predicate_registry_add(registry, name, arity, kind_flags);
}

maelys_result_t maelys_datalog_predicate_registry_add_manifest_predicate(
    maelys_datalog_predicate_registry_t *registry,
    const char *name,
    size_t arity,
    unsigned kind_flags) {
    if (kind_flags & (MAELYS_DATALOG_PRED_KIND_EDB | MAELYS_DATALOG_PRED_KIND_POLICY_FACT)) {
        return MAELYS_ERR_INVALID_FIELD;
    }
    return maelys_datalog_predicate_registry_add(registry, name, arity, kind_flags);
}

maelys_result_t maelys_datalog_predicate_registry_add_atom(maelys_datalog_predicate_registry_t *registry,
                                                           const char *atom) {
    if (!registry || !atom || !*atom) return MAELYS_ERR_INVALID_ARGUMENT;
    if (registry->frozen) return MAELYS_ERR_INVALID_STATE;
    for (size_t i = 0; i < registry->atom_count; i++) {
        if (strcmp(registry->atoms[i], atom) == 0) return MAELYS_OK;
    }
    if (registry->atom_count >= MAELYS_DATALOG_MAX_ATOMS) return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    size_t len = strlen(atom);
    if (len >= sizeof(registry->atoms[0])) return MAELYS_ERR_INVALID_FIELD;
    memcpy(registry->atoms[registry->atom_count], atom, len + 1u);
    registry->atom_count++;
    return MAELYS_OK;
}

int maelys_datalog_predicate_registry_atom_allowed(const maelys_datalog_predicate_registry_t *registry,
                                                   const char *atom) {
    if (!registry || !atom || !*atom) return 0;
    for (size_t i = 0; i < registry->atom_count; i++) {
        if (strcmp(registry->atoms[i], atom) == 0) return 1;
    }
    return 0;
}

maelys_result_t maelys_datalog_predicate_registry_freeze(maelys_datalog_predicate_registry_t *registry) {
    if (!registry) return MAELYS_ERR_INVALID_ARGUMENT;
    registry->frozen = 1;
    return MAELYS_OK;
}

int maelys_datalog_predicate_registry_is_frozen(const maelys_datalog_predicate_registry_t *registry) {
    return registry && registry->frozen;
}

int maelys_datalog_predicate_registry_find(const maelys_datalog_predicate_registry_t *registry,
                                           const char *name,
                                           size_t arity,
                                           maelys_datalog_predicate_id_t *out_id) {
    if (!registry || !name) return 0;
    for (size_t i = 0; i < registry->count; i++) {
        if (registry->defs[i].arity == arity && strcmp(registry->defs[i].name, name) == 0) {
            if (out_id) *out_id = (maelys_datalog_predicate_id_t)i;
            return 1;
        }
    }
    return 0;
}

const maelys_datalog_predicate_def_t *maelys_datalog_predicate_registry_get(
    const maelys_datalog_predicate_registry_t *registry,
    maelys_datalog_predicate_id_t id) {
    if (!registry || id >= registry->count) return NULL;
    return &registry->defs[id];
}
