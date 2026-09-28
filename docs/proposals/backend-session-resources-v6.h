/* SPDX-License-Identifier: MPL-2.0 */
/* PROPOSAL ONLY: not installed, linked, or included by the engine build.
 * Review contract: backend-session-resources-c-api.md. No function bodies. */
#ifndef MAELYS_DATALOG_PROPOSED_SESSION_RESOURCES_V6_H
#define MAELYS_DATALOG_PROPOSED_SESSION_RESOURCES_V6_H
#include <stddef.h>
#include <stdint.h>
#include <maelys/datalog_advanced.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Existing MAELYS_DATALOG_BACKEND_ABI_VERSION remains 5. */
#define MAELYS_DATALOG_BACKEND_V6_ABI_VERSION 6u
#define MAELYS_DATALOG_RESOURCE_CONTRACT_VERSION 1u
#define MAELYS_DATALOG_SESSION_PLAN_VERSION 1u
#define MAELYS_DATALOG_EXTENSION_V2_ABI_VERSION 2u

/* Separate namespace from MAELYS_DATALOG_CAP_* (language/execution features). */
#define MAELYS_DATALOG_RESOURCE_SESSION_CAPACITIES (UINT64_C(1) << 0)
#define MAELYS_DATALOG_RESOURCE_CALLER_ALLOCATOR   (UINT64_C(1) << 1)
/* CALLER_ALLOCATOR is RESERVED AND UNSUPPORTED in 0.14.0. */
#define MAELYS_DATALOG_RESOURCE_SUPPORTED_014 \
    MAELYS_DATALOG_RESOURCE_SESSION_CAPACITIES

/* uint32_t fields, not a profile-sized or compiler-sized enum in ABI records. */
#define MAELYS_DATALOG_MEMORY_FIXED           UINT32_C(0)
#define MAELYS_DATALOG_MEMORY_BACKEND_ELASTIC UINT32_C(1) /* reserved; reject */
#define MAELYS_DATALOG_CAPACITY_INPUT_FACTS   (UINT64_C(1) << 0)
#define MAELYS_DATALOG_CAPACITY_DERIVED_FACTS (UINT64_C(1) << 1)
#define MAELYS_DATALOG_CAPACITY_SYMBOLS       (UINT64_C(1) << 2)
#define MAELYS_DATALOG_CAPACITY_TEXT_BYTES    (UINT64_C(1) << 3)
#define MAELYS_DATALOG_CAPACITY_ALL           UINT64_C(15)

/* Prefix-only input means defaults. A present capacity requires its complete
 * field; absent capacities use the loaded profile default, never literal LARGE.
 * No allocator pointer, cap, callback, or reserved pointer slot in this record. */
typedef struct {
    size_t struct_size;
    uint32_t contract_version;
    uint32_t memory_mode;
    uint64_t required_features;
    uint64_t capacity_mask;
    size_t input_facts;   /* E: raw snapshot count before deduplication */
    size_t derived_facts; /* D: complete IDB, helpers included */
    size_t symbols;       /* S: dictionary entries, program roots included */
    size_t text_bytes;    /* T: interned dictionary text including NULs */
} maelys_datalog_session_resource_request_t;

#define MAELYS_DATALOG_RESOURCE_REQUEST_PREFIX_SIZE \
    (offsetof(maelys_datalog_session_resource_request_t, required_features) + sizeof(uint64_t))
#define MAELYS_DATALOG_RESOURCE_REQUEST_V1_SIZE \
    sizeof(maelys_datalog_session_resource_request_t)
#define MAELYS_DATALOG_RESOURCE_REQUEST_INIT \
    { MAELYS_DATALOG_RESOURCE_REQUEST_V1_SIZE, \
      MAELYS_DATALOG_RESOURCE_CONTRACT_VERSION, MAELYS_DATALOG_MEMORY_FIXED, \
      UINT64_C(0), UINT64_C(0), 0, 0, 0, 0 }

/* Fully normalized, no presence/default sentinel. Same values for requirements
 * and prepare. Read-only descriptors are callback-scoped; retained scalar values
 * are copied into provider storage. All four fields are mandatory in V1 output.
 * Future required extensions append after the complete V1 record. */
typedef struct {
    size_t struct_size;
    uint32_t contract_version;
    uint32_t memory_mode;
    uint64_t required_features;
    size_t input_facts;
    size_t derived_facts;
    size_t symbols;
    size_t text_bytes;
} maelys_datalog_session_resources_t;
#define MAELYS_DATALOG_RESOURCES_PREFIX_SIZE \
    (offsetof(maelys_datalog_session_resources_t, required_features) + sizeof(uint64_t))
#define MAELYS_DATALOG_RESOURCES_V1_SIZE sizeof(maelys_datalog_session_resources_t)
#define MAELYS_DATALOG_RESOURCES_INIT \
    { MAELYS_DATALOG_RESOURCES_V1_SIZE, MAELYS_DATALOG_RESOURCE_CONTRACT_VERSION, \
      MAELYS_DATALOG_MEMORY_FIXED, UINT64_C(0), 0, 0, 0, 0 }

/* Independent descriptor: never cast to/from maelys_datalog_backend_t (ABI 5).
 * resource_features must include SESSION_CAPACITIES; other bits are unsupported
 * in 0.14.0, including the known reserved allocator bit. The full V6 descriptor
 * is required. Compatible optional tails may follow; no callback insertion. */
typedef struct {
    uint32_t abi_version;
    size_t struct_size;
    const char *name;
    const char *semantic_id;
    uint64_t capabilities;
    uint64_t resource_features;
    maelys_datalog_status_t (*storage_requirements)(
        const maelys_datalog_program_t *,
        const maelys_datalog_session_resources_t *,
        size_t *out_bytes, size_t *out_alignment);
    maelys_datalog_status_t (*prepare)(
        const maelys_datalog_program_t *,
        const maelys_datalog_session_resources_t *,
        const maelys_datalog_backend_storage_t *, void **out_state);
    /* Snapshot solve, complete emission, explanation and commit/abort contracts
     * retain their ABI 5 semantics and callback signatures. No delta callback. */
    maelys_datalog_status_t (*solve)(
        void *state, const maelys_datalog_fact_t *canonical_inputs,
        size_t input_count, maelys_datalog_backend_output_t *,
        void **out_result_state, maelys_datalog_diagnostic_t *);
    maelys_datalog_status_t (*explanation_storage_requirements)(
        void *state, void *result_state, maelys_datalog_explanation_kind_t,
        size_t *out_bytes, size_t *out_alignment);
    maelys_datalog_status_t (*explanation_prepare)(
        void *state, void *result_state, maelys_datalog_explanation_kind_t,
        const char *, const maelys_datalog_value_t *, size_t,
        void *storage, size_t storage_bytes, size_t *out_text_size);
    maelys_datalog_status_t (*explanation_write_text)(
        void *state, void *result_state, maelys_datalog_explanation_kind_t,
        const void *storage, char *text, size_t capacity);
    void (*commit)(void *state, void *result_state);
    void (*destroy_result)(void *state, void *result_state);
    void (*destroy)(void *state);
} maelys_datalog_backend_v6_t;
#define MAELYS_DATALOG_BACKEND_V6_SIZE sizeof(maelys_datalog_backend_v6_t)

/* No prepared state is allocated by requirements. The output is descriptive;
 * init recomputes the plan from policy/config, rather than trusting stale sizes.
 * The first four internal components sum to arena_bytes. External buffers are
 * assigned required slices, not their unused extra capacity; count each once. */
typedef struct {
    size_t struct_size;
    uint32_t plan_version;
    uint32_t reserved; /* zero */
    uint64_t required_features;
    size_t arena_bytes;
    size_t arena_alignment;
    size_t host_bytes;
    size_t backend_bytes;
    size_t explanation_bytes;
    size_t padding_bytes;
    size_t external_backend_bytes;
    size_t external_explanation_bytes;
    size_t total_execution_bytes;
} maelys_datalog_session_storage_plan_t;
#define MAELYS_DATALOG_SESSION_PLAN_PREFIX_SIZE \
    (offsetof(maelys_datalog_session_storage_plan_t, required_features) + sizeof(uint64_t))
#define MAELYS_DATALOG_SESSION_PLAN_V1_SIZE sizeof(maelys_datalog_session_storage_plan_t)
#define MAELYS_DATALOG_SESSION_PLAN_INIT \
    { MAELYS_DATALOG_SESSION_PLAN_V1_SIZE, MAELYS_DATALOG_SESSION_PLAN_VERSION, \
      UINT32_C(0), UINT64_C(0), 0, 0, 0, 0, 0, 0, 0, 0, 0 }

/* Extend the existing opaque config/session handles. NULL request restores
 * defaults. Configuration copies known values and leaves itself unchanged on
 * failure. Policy-dependent normalization/admission occurs at planning/create. */
MAELYS_DATALOG_API maelys_datalog_status_t
maelys_datalog_session_config_set_resources(
    maelys_datalog_session_config_t *,
    const maelys_datalog_session_resource_request_t *);
MAELYS_DATALOG_API maelys_datalog_status_t
maelys_datalog_session_get_resources(
    const maelys_datalog_session_t *, maelys_datalog_session_resources_t *out);
MAELYS_DATALOG_API const maelys_datalog_backend_v6_t *
maelys_datalog_backend_reference_v6(void);
MAELYS_DATALOG_API maelys_datalog_status_t
maelys_datalog_session_config_set_backend_v6(
    maelys_datalog_session_config_t *, const maelys_datalog_backend_v6_t *);

/* Same backend-arena and explanation-storage setters as the existing config.
 * Explicit ABI 5 selections stay ABI 5; no automatic migration of a provider.
 * Whole-session planning/init rejects ABI 5. create_configured keeps the legacy
 * ABI 5 default construction path, using the same effective admission rules. */
MAELYS_DATALOG_API maelys_datalog_status_t
maelys_datalog_session_storage_requirements_configured(
    const maelys_datalog_policy_t *, size_t policy_index,
    const maelys_datalog_session_config_t *,
    maelys_datalog_session_storage_plan_t *out_plan,
    maelys_datalog_diagnostic_t *);
MAELYS_DATALOG_API maelys_datalog_status_t
maelys_datalog_session_init_configured(
    void *arena, size_t arena_bytes,
    const maelys_datalog_policy_t *, size_t policy_index,
    const maelys_datalog_session_config_t *,
    maelys_datalog_session_t **out, maelys_datalog_diagnostic_t *);

/* Additive extension registration. Existing extension_t/register remain ABI 1.
 * Other components retain their own current ABI. Arrays use exact base-type
 * strides/sizes; do not pass arrays of extended descriptors. No dynamic loader. */
typedef struct {
    uint32_t abi_version;
    size_t struct_size;
    const char *name;
    const char *semantic_id;
    const maelys_datalog_frontend_t *frontends;
    size_t frontend_count;
    const maelys_datalog_backend_t *backends_v5;
    size_t backend_v5_count;
    const maelys_datalog_backend_v6_t *backends_v6;
    size_t backend_v6_count;
    const maelys_datalog_planner_module_t *planners;
    size_t planner_count;
    const maelys_datalog_filter_module_t *filters;
    size_t filter_count;
} maelys_datalog_extension_v2_t;
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_context_register_v2(
    maelys_datalog_context_t *, const maelys_datalog_extension_v2_t *);
MAELYS_DATALOG_API maelys_datalog_status_t
maelys_datalog_session_config_set_context_backend_v6(
    maelys_datalog_session_config_t *, maelys_datalog_context_t *,
    const char *backend_name); /* NULL selects the V6 reference */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_context_backend_v6_count(
    const maelys_datalog_context_t *, size_t *out);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_context_backend_v6_info(
    const maelys_datalog_context_t *, size_t, maelys_datalog_component_info_t *out);

#ifdef __cplusplus
}
#endif
#endif
