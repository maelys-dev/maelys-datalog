/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_DATALOG_MODULE_H
#define MAELYS_DATALOG_MODULE_H
#include "datalog.h"
#ifdef __cplusplus
extern "C" {
#endif

#define MAELYS_DATALOG_MODULE_ABI_VERSION 1u
#define MAELYS_DATALOG_MODULE_MAX_FILTERS 16u /* additional to standard filters */
#define MAELYS_DATALOG_MODULE_NAME_BYTES 64u
#define MAELYS_DATALOG_MODULE_SEMANTIC_ID_BYTES 128u

/* Trusted native code, not sandboxed plugins. Register on the startup thread
 * BEFORE initializing/loading any policy. Descriptors/strings are copied;
 * callback code must stay loaded for process lifetime. No replacement/unload.
 * Callbacks must be deterministic, bounded, allocation-free and reentrant:
 * no retained arguments, mutation of inputs, engine calls, I/O, time, locale
 * or mutable external state. semantic_id versions behavior AND cost accounting.
 * Change it when either changes. Module identities enter policy fingerprints.
 */
typedef maelys_datalog_status_t (*maelys_datalog_filter_validate_fn)(const unsigned char *pattern,
                                                                     size_t pattern_length);
typedef maelys_datalog_status_t (*maelys_datalog_filter_cost_fn)(size_t value_length,
                                                                 size_t pattern_length,
                                                                 size_t *out_cost);
typedef maelys_datalog_status_t (*maelys_datalog_filter_evaluate_fn)(const unsigned char *value,
                                                                     size_t value_length,
                                                                     const unsigned char *pattern,
                                                                     size_t pattern_length,
                                                                     int *out_matched);
typedef struct {
    uint32_t abi_version;
    size_t struct_size;
    const char *name;        /* lower-case predicate identifier, max 63 bytes */
    const char *semantic_id; /* ASCII letters/digits/dot/hyphen/underscore */
    maelys_datalog_filter_validate_fn validate_pattern;
    /* Conservative work bound, charged BEFORE evaluate; >= 1 for external
     * filters. Overflow/unsupported inputs must return an error. */
    maelys_datalog_filter_cost_fn cost;
    maelys_datalog_filter_evaluate_fn evaluate;
} maelys_datalog_filter_module_t;

/* Only safe, unplanned literals are offered. Choose an index in this array.
 * The core retains binding checks, delta anchoring, execution and ownership.
 * default_score reproduces the reference heuristic (higher is preferred).
 * predicate_name is NULL for a comparison/filter; flags use public
 * MAELYS_DATALOG_PREDICATE_* values. All pointers are callback-scoped. */
typedef enum {
    MAELYS_DATALOG_JOIN_ATOM = 1,
    MAELYS_DATALOG_JOIN_COMPARISON = 2,
    MAELYS_DATALOG_JOIN_NEGATED_ATOM = 3,
    MAELYS_DATALOG_JOIN_FILTER = 4
} maelys_datalog_join_kind_t;
typedef struct {
    size_t body_index;
    maelys_datalog_join_kind_t kind;
    const char *predicate_name;
    unsigned predicate_flags;
    size_t arity;
    uint64_t variable_mask;
    uint64_t bound_variable_mask;
    size_t constant_terms;
    size_t bound_terms;
    int64_t default_score;
} maelys_datalog_join_candidate_t;
typedef maelys_datalog_status_t (*maelys_datalog_join_choose_fn)(
    const maelys_datalog_join_candidate_t *candidates, size_t candidate_count,
    size_t *out_candidate_index);
typedef struct {
    uint32_t abi_version;
    size_t struct_size;
    const char *name;
    const char *semantic_id;
    maelys_datalog_join_choose_fn choose;
} maelys_datalog_planner_module_t;

MAELYS_DATALOG_API maelys_datalog_status_t
maelys_datalog_register_filter_module(const maelys_datalog_filter_module_t *module);
MAELYS_DATALOG_API maelys_datalog_status_t
maelys_datalog_register_planner_module(const maelys_datalog_planner_module_t *module);
#ifdef __cplusplus
}
#endif
#endif
