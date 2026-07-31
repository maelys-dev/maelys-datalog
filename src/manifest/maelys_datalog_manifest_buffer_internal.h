#pragma once
#ifndef MAELYS_DATALOG_MANIFEST_BUFFER_INTERNAL_H
#define MAELYS_DATALOG_MANIFEST_BUFFER_INTERNAL_H

#include "src/manifest/maelys_datalog_manifest.h"

/* Internal bridge used by the file-manifest loader. It keeps the public
 * buffer API fail-closed on MAELYS-DATALOG-v2 while preserving the distinct
 * historical file-manifest profile "enforce". Not part of the umbrella API. */
maelys_result_t maelys_datalog_manifest_load_from_text_expected_profile(
    const char *manifest_json,
    size_t manifest_json_len,
    const maelys_datalog_policy_bundle_entry_t *bundle,
    size_t bundle_count,
    unsigned flags,
    const char *expected_profile,
    maelys_datalog_policy_set_t *out_set,
    maelys_datalog_diagnostic_t *out_diag);

#endif
