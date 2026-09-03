#include "src/core/maelys_datalog_domain_registry.h"

#include <string.h>

typedef struct {
    maelys_datalog_domain_def_t def;
    char domain_name[64];
    char description[256];
    maelys_datalog_predicate_def_t predicates[MAELYS_DATALOG_MAX_PREDICATES];
    char atom_storage[MAELYS_DATALOG_MAX_ATOMS][64];
    const char *atoms[MAELYS_DATALOG_MAX_ATOMS];
} maelys_datalog_registered_domain_t;

static maelys_datalog_registered_domain_t
    s_domains[MAELYS_DATALOG_MAX_REGISTERED_DOMAINS];
static size_t s_domain_count = 0u;

static int copy_bounded(char *destination, size_t capacity, const char *source) {
    if (!destination || capacity == 0u || !source) return 0;
    const size_t length = strnlen(source, capacity);
    if (length == 0u || length >= capacity) return 0;
    memcpy(destination, source, length + 1u);
    return 1;
}

maelys_result_t maelys_datalog_domain_registry_register(const maelys_datalog_domain_def_t *def) {
    if (!def || !def->domain_name) return MAELYS_ERR_INVALID_ARGUMENT;
    int has_callback = (def->install_predicates != NULL);
    int has_table = (def->predicates != NULL && def->predicate_count > 0u);
    if (has_callback == has_table) return MAELYS_ERR_INVALID_ARGUMENT;
    if (has_callback && (def->predicates != NULL || def->predicate_count != 0u)) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    if (!has_callback && (def->predicates == NULL || def->predicate_count == 0u)) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    if (def->predicate_count > MAELYS_DATALOG_MAX_PREDICATES ||
        def->atom_count > MAELYS_DATALOG_MAX_ATOMS ||
        (def->atom_count > 0u && !def->atoms)) {
        return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    }
    for (size_t i = 0; i < s_domain_count; i++) {
        if (strcmp(s_domains[i].def.domain_name, def->domain_name) == 0) return MAELYS_OK;
    }
    if (s_domain_count >= MAELYS_DATALOG_MAX_REGISTERED_DOMAINS) return MAELYS_ERR_PAYLOAD_TOO_LARGE;

    maelys_datalog_registered_domain_t candidate;
    memset(&candidate, 0, sizeof(candidate));
    if (!copy_bounded(candidate.domain_name, sizeof(candidate.domain_name), def->domain_name)) {
        return MAELYS_ERR_INVALID_FIELD;
    }
    candidate.def.domain_name = candidate.domain_name;
    candidate.def.install_predicates = def->install_predicates;
    if (def->description) {
        if (!copy_bounded(candidate.description, sizeof(candidate.description), def->description)) {
            return MAELYS_ERR_INVALID_FIELD;
        }
        candidate.def.description = candidate.description;
    }
    if (has_table) {
        for (size_t i = 0u; i < def->predicate_count; i++) {
            if (!copy_bounded(candidate.predicates[i].name,
                              sizeof(candidate.predicates[i].name),
                              def->predicates[i].name)) {
                return MAELYS_ERR_INVALID_FIELD;
            }
            candidate.predicates[i].arity = def->predicates[i].arity;
            candidate.predicates[i].kind_flags = def->predicates[i].kind_flags;
        }
        candidate.def.predicates = candidate.predicates;
        candidate.def.predicate_count = def->predicate_count;
    }
    for (size_t i = 0u; i < def->atom_count; i++) {
        if (!copy_bounded(candidate.atom_storage[i],
                          sizeof(candidate.atom_storage[i]),
                          def->atoms[i])) {
            return MAELYS_ERR_INVALID_FIELD;
        }
        candidate.atoms[i] = candidate.atom_storage[i];
    }
    candidate.def.atoms = candidate.atoms;
    candidate.def.atom_count = def->atom_count;

    s_domains[s_domain_count] = candidate;
    maelys_datalog_registered_domain_t *stored = &s_domains[s_domain_count];
    stored->def.domain_name = stored->domain_name;
    stored->def.description = def->description ? stored->description : NULL;
    stored->def.predicates = has_table ? stored->predicates : NULL;
    for (size_t i = 0u; i < stored->def.atom_count; i++) stored->atoms[i] = stored->atom_storage[i];
    stored->def.atoms = stored->atoms;
    s_domain_count++;
    return MAELYS_OK;
}

const maelys_datalog_domain_def_t *maelys_datalog_domain_registry_find(const char *domain_name) {
    if (!domain_name || !*domain_name) return NULL;
    for (size_t i = 0; i < s_domain_count; i++) {
        if (strcmp(s_domains[i].def.domain_name, domain_name) == 0) return &s_domains[i].def;
    }
    return NULL;
}

maelys_result_t maelys_datalog_domain_registry_install(const char *domain_name,
                                                       maelys_datalog_predicate_registry_t *registry) {
    const maelys_datalog_domain_def_t *domain = maelys_datalog_domain_registry_find(domain_name);
    if (!domain || (!domain->install_predicates && !domain->predicates)) return MAELYS_ERR_UNSUPPORTED;
    if (domain->install_predicates) return domain->install_predicates(registry);
    for (size_t i = 0; i < domain->predicate_count; i++) {
        maelys_result_t rc = maelys_datalog_predicate_registry_add_domain(registry,
                                                                          domain->predicates[i].name,
                                                                          domain->predicates[i].arity,
                                                                          domain->predicates[i].kind_flags);
        if (rc != MAELYS_OK) return rc;
    }
    for (size_t i = 0; i < domain->atom_count; i++) {
        maelys_result_t rc = maelys_datalog_predicate_registry_add_atom(
            registry, domain->atoms[i]);
        if (rc != MAELYS_OK) return rc;
    }
    return MAELYS_OK;
}
