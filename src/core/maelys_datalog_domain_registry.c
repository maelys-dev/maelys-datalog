#include "src/core/maelys_datalog_domain_registry.h"

#include <string.h>

static maelys_datalog_domain_def_t s_domains[MAELYS_DATALOG_MAX_REGISTERED_DOMAINS];
static size_t s_domain_count = 0u;

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
    for (size_t i = 0; i < s_domain_count; i++) {
        if (strcmp(s_domains[i].domain_name, def->domain_name) == 0) return MAELYS_OK;
    }
    if (s_domain_count >= MAELYS_DATALOG_MAX_REGISTERED_DOMAINS) return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    s_domains[s_domain_count++] = *def;
    return MAELYS_OK;
}

const maelys_datalog_domain_def_t *maelys_datalog_domain_registry_find(const char *domain_name) {
    if (!domain_name || !*domain_name) return NULL;
    for (size_t i = 0; i < s_domain_count; i++) {
        if (strcmp(s_domains[i].domain_name, domain_name) == 0) return &s_domains[i];
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
    return MAELYS_OK;
}
