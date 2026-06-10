#pragma once
#ifndef MAELYS_DATALOG_EDB_H
#define MAELYS_DATALOG_EDB_H

#include "policy/maelys_datalog_policy.h"

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
maelys_result_t maelys_datalog_edb_add_symbol_fact(maelys_datalog_edb_t *edb,
                                                   const char *predicate,
                                                   const char *arg0);
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
