#pragma once
#ifndef MAELYS_DATALOG_MANIFEST_H
#define MAELYS_DATALOG_MANIFEST_H

#include "src/core/maelys_datalog_diagnostic.h"
#include "src/core/maelys_datalog_predicate_registry.h"
#include "src/core/maelys_datalog_ruleset.h"
#include "src/core/maelys_datalog_types.h"

#define MAELYS_DATALOG_MANIFEST_ALLOW_TEST_ONLY 1u
#define MAELYS_DATALOG_INLINE_MAX_DOMAIN_LEN 63u
#define MAELYS_DATALOG_INLINE_MAX_POLICY_ID_LEN 127u

/*
 * Bundle entry for buffer-based manifest loading.
 * Each entry matches one policy entry in the manifest JSON by policy_id.
 */
typedef struct {
    const char *policy_id;   /* matches the policy_id field in the manifest JSON */
    const char *src;         /* .dl source text (need not be NUL-terminated) */
    size_t src_len;          /* byte length of src */
} maelys_datalog_policy_bundle_entry_t;

typedef struct {
    int allow_test_only;
} maelys_datalog_manifest_load_options_t;

typedef struct {
    maelys_datalog_ruleset_t policies[8];
    size_t policy_count;
    maelys_datalog_query_whitelist_entry_t query_whitelist[MAELYS_DATALOG_MAX_QUERY_WHITELIST];
    size_t query_whitelist_count;
    int enforces_query_whitelist;
} maelys_datalog_policy_set_t;

/*
 * Load a policy set from in-memory buffers.
 * manifest_json need not be NUL-terminated; manifest_json_len is authoritative.
 * Domains must be registered before this call (same as manifest_load_ex).
 * The file field in each manifest policy entry is ignored; sources are taken
 * from bundle[]. If no bundle entry matches a policy_id, returns NOT_FOUND.
 */
maelys_result_t maelys_datalog_manifest_load_from_text(
    const char *manifest_json,
    size_t manifest_json_len,
    const maelys_datalog_policy_bundle_entry_t *bundle,
    size_t bundle_count,
    unsigned flags,
    maelys_datalog_policy_set_t *out_set,
    maelys_datalog_diagnostic_t *out_diag);

/*
 * Load a single policy from in-memory .dl source text.
 * The caller provides no manifest JSON, no SHA-256, and no bundle.
 *
 * domain and policy_id must be non-NULL, non-empty, and within the
 * MAELYS_DATALOG_INLINE_MAX_* byte limits excluding the NUL terminator.
 * src points to src_len bytes and does not need to be NUL-terminated.
 *
 * flags must be 0. The selected domain is the sole predicate vocabulary
 * authority; inline loading does not expose idb_predicates or queries overlays.
 */
maelys_result_t maelys_datalog_load_policy_inline(
    const char *domain,
    const char *policy_id,
    const char *src,
    size_t src_len,
    unsigned flags,
    maelys_datalog_policy_set_t *out_set,
    maelys_datalog_diagnostic_t *out_diag);

/*
 * Register a domain from a static predicate table, then load a single inline
 * policy for that domain.
 *
 * domain_name and predicates are stored by pointer in the global domain
 * registry and must remain valid for as long as the registry may reference
 * them. This API is intended for C static tables, tests, examples, embedded C,
 * and WASM builds where the table is compiled into the module.
 */
maelys_result_t maelys_datalog_load_policy_inline_with_static_domain(
    const maelys_datalog_predicate_def_t *predicates,
    size_t predicate_count,
    const char *domain_name,
    const char *policy_id,
    const char *src,
    size_t src_len,
    unsigned flags,
    maelys_datalog_policy_set_t *out_set,
    maelys_datalog_diagnostic_t *out_diag);

maelys_result_t maelys_datalog_manifest_load(const char *manifest_path,
                                             unsigned flags,
                                             maelys_datalog_policy_set_t *out_set);
maelys_result_t maelys_datalog_manifest_load_ex(const char *manifest_path,
                                                unsigned flags,
                                                maelys_datalog_policy_set_t *out_set,
                                                maelys_datalog_diagnostic_t *out_diag);
void maelys_datalog_policy_set_clear(maelys_datalog_policy_set_t *set);

#endif
