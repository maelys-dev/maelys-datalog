#pragma once
#ifndef MAELYS_DATALOG_DOMAIN_REGISTRY_H
#define MAELYS_DATALOG_DOMAIN_REGISTRY_H

#include <stddef.h>

#include "policy/maelys_datalog_predicate_registry.h"

#define MAELYS_DATALOG_MAX_REGISTERED_DOMAINS 16u

typedef struct {
    const char *domain_name;
    const maelys_datalog_predicate_def_t *predicates;
    size_t predicate_count;
    const char *description;
    maelys_result_t (*install_predicates)(maelys_datalog_predicate_registry_t *registry);
} maelys_datalog_domain_def_t;

maelys_result_t maelys_datalog_domain_registry_register(const maelys_datalog_domain_def_t *def);
const maelys_datalog_domain_def_t *maelys_datalog_domain_registry_find(const char *domain_name);
maelys_result_t maelys_datalog_domain_registry_install(const char *domain_name,
                                                       maelys_datalog_predicate_registry_t *registry);

#endif
