#pragma once
#ifndef MAELYS_DATALOG_EDB_H
#define MAELYS_DATALOG_EDB_H

#include "src/core/maelys_datalog_policy.h"

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
