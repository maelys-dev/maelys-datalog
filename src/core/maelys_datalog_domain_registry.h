#pragma once
#ifndef MAELYS_DATALOG_DOMAIN_REGISTRY_H
#define MAELYS_DATALOG_DOMAIN_REGISTRY_H

#include <stddef.h>
#include "maelys/datalog.h"

#include "src/core/maelys_datalog_predicate_registry.h"

#define MAELYS_DATALOG_MAX_REGISTERED_DOMAINS 16u

/* Low-level declaration: exactly one of a predicate table or an installer.
 * Registration copies the name, table, atoms and optional description into
 * bounded storage. Callback code must remain available for later policy loads.
 * description is metadata only; it does not affect loading or evaluation. */
typedef struct {
    const char *domain_name;
    const maelys_datalog_public_predicate_t *predicates;
    size_t predicate_count;
    /* Installed after the table or callback succeeds; creates no facts. */
    const char *const *atoms;
    size_t atom_count;
    const char *description;
    maelys_result_t (*install_predicates)(maelys_datalog_predicate_registry_t *registry);
} maelys_datalog_domain_def_t;

/* Private stored view returned by find. All referenced bytes belong to the
 * registry; it is not an input declaration or a public backend contract. */
typedef struct {
    const char *domain_name;
    const maelys_datalog_predicate_entry_t *predicates;
    size_t predicate_count;
    const char *const *atoms;
    size_t atom_count;
    const char *description;
    maelys_result_t (*install_predicates)(maelys_datalog_predicate_registry_t *registry);
} maelys_datalog_domain_entry_t;

maelys_result_t maelys_datalog_domain_registry_register(const maelys_datalog_domain_def_t *def);
const maelys_datalog_domain_entry_t *maelys_datalog_domain_registry_find(const char *domain_name);
/* Installs the domain's predicates, then its atoms, into registry. On failure
 * the registry keeps whatever was installed before the error: this is not an
 * atomic operation and it does not roll back. A caller that must not observe a
 * partial vocabulary discards the registry, as the public policy loader does
 * with its candidate. */
maelys_result_t maelys_datalog_domain_registry_install(const char *domain_name,
                                                       maelys_datalog_predicate_registry_t *registry);

#endif
