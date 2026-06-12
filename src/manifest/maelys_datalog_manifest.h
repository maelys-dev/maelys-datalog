#pragma once
#ifndef MAELYS_DATALOG_MANIFEST_H
#define MAELYS_DATALOG_MANIFEST_H

#include "src/core/maelys_datalog_policy.h"
#include "src/core/maelys_datalog_diagnostic.h"

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

maelys_result_t maelys_datalog_manifest_load(const char *manifest_path,
                                             unsigned flags,
                                             maelys_datalog_policy_set_t *out_set);
maelys_result_t maelys_datalog_manifest_load_ex(const char *manifest_path,
                                                unsigned flags,
                                                maelys_datalog_policy_set_t *out_set,
                                                maelys_datalog_diagnostic_t *out_diag);
void maelys_datalog_policy_set_clear(maelys_datalog_policy_set_t *set);

#endif
