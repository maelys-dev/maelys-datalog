#pragma once
#ifndef MAELYS_DATALOG_PREDICATE_REGISTRY_H
#define MAELYS_DATALOG_PREDICATE_REGISTRY_H

#include "policy/maelys_datalog_policy.h"

void maelys_datalog_predicate_registry_init(maelys_datalog_predicate_registry_t *registry);
void maelys_datalog_predicate_registry_init_core(maelys_datalog_predicate_registry_t *registry);
maelys_result_t maelys_datalog_predicate_registry_add(maelys_datalog_predicate_registry_t *registry,
                                                      const char *name,
                                                      size_t arity,
                                                      unsigned kind_flags);
maelys_result_t maelys_datalog_predicate_registry_add_builtin(maelys_datalog_predicate_registry_t *registry,
                                                              const char *name,
                                                              size_t arity,
                                                              unsigned kind_flags);
maelys_result_t maelys_datalog_predicate_registry_add_domain(maelys_datalog_predicate_registry_t *registry,
                                                             const char *name,
                                                             size_t arity,
                                                             unsigned kind_flags);
maelys_result_t maelys_datalog_predicate_registry_add_manifest_predicate(
    maelys_datalog_predicate_registry_t *registry,
    const char *name,
    size_t arity,
    unsigned kind_flags);
maelys_result_t maelys_datalog_predicate_registry_add_atom(maelys_datalog_predicate_registry_t *registry,
                                                           const char *atom);
int maelys_datalog_predicate_registry_atom_allowed(const maelys_datalog_predicate_registry_t *registry,
                                                   const char *atom);
maelys_result_t maelys_datalog_predicate_registry_freeze(maelys_datalog_predicate_registry_t *registry);
int maelys_datalog_predicate_registry_is_frozen(const maelys_datalog_predicate_registry_t *registry);
int maelys_datalog_predicate_registry_find(const maelys_datalog_predicate_registry_t *registry,
                                           const char *name,
                                           size_t arity,
                                           maelys_datalog_predicate_id_t *out_id);
const maelys_datalog_predicate_def_t *maelys_datalog_predicate_registry_get(
    const maelys_datalog_predicate_registry_t *registry,
    maelys_datalog_predicate_id_t id);

#endif
