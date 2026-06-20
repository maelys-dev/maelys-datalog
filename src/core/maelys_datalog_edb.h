#pragma once
#ifndef MAELYS_DATALOG_EDB_H
#define MAELYS_DATALOG_EDB_H

#include "common/maelys_errors.h"
#include "src/core/maelys_datalog_predicate_registry.h"
#include "src/core/maelys_datalog_symbol_table.h"
#include "src/core/maelys_datalog_types.h"

typedef struct {
    maelys_datalog_fact_t *facts;
    size_t fact_capacity;
    size_t fact_count;
    maelys_datalog_fact_set_t fact_set;
    size_t facts_per_pred[MAELYS_DATALOG_MAX_PREDICATES];
    int immutable;
    maelys_datalog_symbol_table_t *symbols;
    const maelys_datalog_predicate_registry_t *registry;
} maelys_datalog_edb_t;

maelys_result_t maelys_datalog_edb_init(maelys_datalog_edb_t *edb,
                                        maelys_datalog_fact_t *fact_pool,
                                        size_t fact_capacity,
                                        maelys_datalog_symbol_table_t *symbols,
                                        const maelys_datalog_predicate_registry_t *registry);
void maelys_datalog_edb_clear(maelys_datalog_edb_t *edb);
maelys_result_t maelys_datalog_edb_add_fact(maelys_datalog_edb_t *edb,
                                            const char *predicate,
                                            const maelys_datalog_term_t *terms,
                                            size_t arity);
/**
 * Add a 1-arity EDB fact with an open runtime symbol value.
 *
 * Interns `value` into the policy symbol table and inserts it as a
 * SYMBOL-typed fact. Does not require `value` to be pre-registered in the
 * atom vocabulary. This is the canonical path for runtime observations such
 * as user names, branch names, file paths, and request IDs.
 */
maelys_result_t maelys_datalog_edb_add_runtime_symbol_fact(maelys_datalog_edb_t *edb,
                                                           const char *predicate,
                                                           const char *value);
/**
 * Intern an open runtime symbol into the EDB's symbol table.
 *
 * The returned id is scoped to this EDB/ruleset symbol table. It is a
 * 1-based handle; MAELYS_DATALOG_SYMBOL_ID_INVALID (0) is never returned
 * for a successful intern.
 */
maelys_result_t maelys_datalog_edb_intern_runtime_symbol(maelys_datalog_edb_t *edb,
                                                         const char *text,
                                                         maelys_datalog_symbol_id_t *out_id);
/**
 * Add a 1-arity EDB fact using a pre-interned runtime symbol id.
 */
maelys_result_t maelys_datalog_edb_add_symbol_id_fact(maelys_datalog_edb_t *edb,
                                                      const char *predicate,
                                                      maelys_datalog_symbol_id_t value);
/**
 * Add a 2-arity EDB fact using two pre-interned runtime symbol ids.
 */
maelys_result_t maelys_datalog_edb_add_symbol_ids_fact(maelys_datalog_edb_t *edb,
                                                       const char *predicate,
                                                       maelys_datalog_symbol_id_t left,
                                                       maelys_datalog_symbol_id_t right);
/**
 * Add multiple 1-arity EDB facts using pre-interned runtime symbol ids.
 *
 * Empty batches are accepted as no-ops after validating edb and predicate.
 * Non-empty batches validate the whole batch before inserting any fact.
 */
maelys_result_t maelys_datalog_edb_add_symbol_id_facts(
    maelys_datalog_edb_t *edb,
    const char *predicate,
    const maelys_datalog_symbol_id_t *values,
    size_t value_count);
/**
 * Add multiple 2-arity EDB facts using pre-interned runtime symbol ids.
 *
 * `pairs` is a flat array: left0, right0, left1, right1, ...
 * `pair_count` is the number of binary facts, not the array length.
 */
maelys_result_t maelys_datalog_edb_add_symbol_ids_facts(
    maelys_datalog_edb_t *edb,
    const char *predicate,
    const maelys_datalog_symbol_id_t *pairs,
    size_t pair_count);
/**
 * Add multiple 1-arity EDB facts from open runtime symbol strings.
 *
 * Strings are interned monotonically into the EDB's symbol table, then the
 * resulting ids are committed with the atomic symbol-id batch path. Empty
 * batches are accepted as no-ops after validating edb and predicate.
 */
maelys_result_t maelys_datalog_edb_add_runtime_symbol_facts(
    maelys_datalog_edb_t *edb,
    const char *predicate,
    const char *const *values,
    size_t value_count);
/**
 * Add multiple 2-arity EDB facts from open runtime symbol string pairs.
 *
 * `flat_pairs` is a flat array: left0, right0, left1, right1, ...
 * `pair_count` is the number of binary facts, not the array length. Symbol
 * interning is monotonic and is not rolled back if a later string fails.
 */
maelys_result_t maelys_datalog_edb_add_runtime_symbol_pair_facts(
    maelys_datalog_edb_t *edb,
    const char *predicate,
    const char *const *flat_pairs,
    size_t pair_count);
/**
 * Add a 1-arity EDB fact with a closed atom value.
 *
 * Checks that `atom` is pre-declared in the atom vocabulary before inserting
 * the fact. Returns MAELYS_ERR_FORBIDDEN when the atom is absent, including
 * the atom_count == 0 case used by inline/WASM dynamic domains.
 */
maelys_result_t maelys_datalog_edb_add_atom_fact(maelys_datalog_edb_t *edb,
                                                 const char *predicate,
                                                 const char *atom);
maelys_result_t maelys_datalog_edb_finalize(maelys_datalog_edb_t *edb);
int maelys_datalog_term_equal(const maelys_datalog_term_t *a,
                              const maelys_datalog_term_t *b);
int maelys_datalog_fact_equals(const maelys_datalog_fact_t *a,
                               const maelys_datalog_fact_t *b);
int maelys_datalog_fact_cmp(const maelys_datalog_fact_t *a,
                            const maelys_datalog_fact_t *b);
void maelys_datalog_fact_set_init(maelys_datalog_fact_set_t *set,
                                  maelys_datalog_fact_t *facts,
                                  size_t capacity);
maelys_result_t maelys_datalog_fact_set_sort(maelys_datalog_fact_set_t *set);
maelys_result_t maelys_datalog_fact_set_dedup(maelys_datalog_fact_set_t *set);
int maelys_datalog_fact_set_contains(const maelys_datalog_fact_set_t *set,
                                     const maelys_datalog_fact_t *fact);
maelys_result_t maelys_datalog_fact_set_predicate_range(const maelys_datalog_fact_set_t *set,
                                                        maelys_datalog_predicate_id_t predicate_id,
                                                        size_t *begin,
                                                        size_t *end);
maelys_result_t maelys_datalog_fact_set_merge_delta(maelys_datalog_fact_set_t *current,
                                                    const maelys_datalog_fact_set_t *delta,
                                                    maelys_datalog_fact_t *merge_buffer,
                                                    size_t merge_capacity,
                                                    size_t *inserted_count);
int maelys_datalog_edb_contains(const maelys_datalog_edb_t *edb,
                                const maelys_datalog_fact_t *fact);

#endif
