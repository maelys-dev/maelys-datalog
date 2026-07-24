#pragma once
#ifndef MAELYS_PY_BIND_H
#define MAELYS_PY_BIND_H

#include <stddef.h>
#include <stdint.h>

#include "src/core/maelys_datalog_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* This shim is built for Python/cffi. Its C entry points are not a standalone
 * thread-safe public API for arbitrary C callers. The Python wrapper owns the
 * process-wide lock around domain registry operations. Direct concurrent C
 * calls into register/find/load are unsafe unless a future pass adds a C-level
 * mutex here. */

typedef struct maelys_py_engine maelys_py_engine_t;
typedef struct maelys_py_ruleset maelys_py_ruleset_t;
typedef struct maelys_py_edb maelys_py_edb_t;
typedef struct maelys_py_result maelys_py_result_t;

typedef struct {
    int32_t kind;
    int64_t value;
} maelys_py_term_t;

typedef struct {
    const char *name;
    size_t arity;
    unsigned kind_flags;
} maelys_py_predicate_def_t;

typedef struct {
    size_t term_size;
    size_t term_kind_offset;
    size_t term_value_offset;
    size_t predicate_def_size;
    size_t predicate_def_name_offset;
    size_t predicate_def_arity_offset;
    size_t predicate_def_kind_flags_offset;
} maelys_py_abi_layout_t;

typedef struct {
    int32_t term_symbol;
    int32_t term_int;
    int32_t term_bool;
    int32_t term_var;
    unsigned pred_edb;
    unsigned pred_idb;
    unsigned pred_query;
    unsigned pred_policy_fact;
    int ok;
    int err_invalid_argument;
    int err_invalid_field;
    int err_invalid_state;
    int err_payload_too_large;
    int err_forbidden;
    int err_unsupported;
} maelys_py_abi_constants_t;

maelys_py_engine_t *maelys_py_engine_new(void);
void maelys_py_engine_free(maelys_py_engine_t *engine);

int maelys_py_get_build_limits(maelys_datalog_build_limits_t *out);
void maelys_py_get_abi_layout(maelys_py_abi_layout_t *out);
void maelys_py_get_abi_constants(maelys_py_abi_constants_t *out);

int maelys_py_register_domain(const char *domain_name,
                              const maelys_py_predicate_def_t *predicates,
                              size_t predicate_count);
int maelys_py_find_domain(const char *domain_name,
                          maelys_py_predicate_def_t *out_predicates,
                          size_t out_capacity,
                          size_t *out_count,
                          int *out_found,
                          int *out_inspectable);

int maelys_py_load_inline_ruleset(maelys_py_engine_t *engine,
                                  const char *domain_name,
                                  const char *ruleset_id,
                                  const char *source,
                                  size_t source_len,
                                  maelys_py_ruleset_t **out_ruleset);
const char *maelys_py_last_diag_message(maelys_py_engine_t *engine);
const char *maelys_py_last_diag_hint(maelys_py_engine_t *engine);

void maelys_py_ruleset_free(maelys_py_ruleset_t *ruleset);

maelys_py_edb_t *maelys_py_edb_new(maelys_py_ruleset_t *ruleset);
void maelys_py_edb_free(maelys_py_edb_t *edb);
int maelys_py_edb_add_fact(maelys_py_edb_t *edb,
                           const char *predicate,
                           const maelys_py_term_t *terms,
                           size_t arity);

int maelys_py_intern_symbol(maelys_py_ruleset_t *ruleset,
                            const char *text,
                            uint32_t *out_symbol_id);
int maelys_py_symbol_lookup_readonly(maelys_py_ruleset_t *ruleset,
                                     const char *text,
                                     size_t len,
                                     uint32_t *out_symbol_id,
                                     int *out_found);
int maelys_py_symbol_id_is_valid(maelys_py_ruleset_t *ruleset,
                                 uint32_t symbol_id,
                                 int *out_valid);
const char *maelys_py_symbol_text(maelys_py_ruleset_t *ruleset,
                                  uint32_t symbol_id);

int maelys_py_solve(maelys_py_ruleset_t *ruleset,
                    maelys_py_edb_t *edb,
                    maelys_py_result_t **out_result);
void maelys_py_result_free(maelys_py_result_t *result);

int maelys_py_result_derived_fact_count(maelys_py_result_t *result,
                                        size_t *out_count);
int maelys_py_result_validate_query_predicate(maelys_py_result_t *result,
                                              const char *predicate,
                                              size_t arity);
int maelys_py_result_contains_fact(maelys_py_result_t *result,
                                   const char *predicate,
                                   const maelys_py_term_t *terms,
                                   size_t arity,
                                   int *out_present);
int maelys_py_result_enumerate_predicate_facts(maelys_py_result_t *result,
                                               const char *predicate,
                                               size_t arity,
                                               maelys_py_term_t *out_terms,
                                               size_t out_capacity,
                                               size_t *out_count);

#ifdef __cplusplus
}
#endif

#endif
