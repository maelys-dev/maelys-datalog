/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_DATALOG_RESOURCES_H
#define MAELYS_DATALOG_RESOURCES_H
#include "datalog.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Caller-owned, aligned, immovable storage, disjoint from other live storage,
 * inputs and outputs. The buffer lives through session destruction. The host
 * copies this descriptor, never the buffer, and never accesses or frees its
 * contents. NULL bytes is allowed only with size == 0 and zero requirements.
 * alignment is a nonzero power of two <= alignof(max_align_t). */
typedef struct {
    size_t struct_size;
    void *bytes;
    size_t size;
    size_t alignment;
} maelys_datalog_backend_storage_t;


#define MAELYS_DATALOG_RESOURCE_CONTRACT_VERSION 1u
#define MAELYS_DATALOG_SESSION_PLAN_VERSION 1u

/* Separate namespace from MAELYS_DATALOG_CAP_* (language/execution features). */
#define MAELYS_DATALOG_RESOURCE_SESSION_CAPACITIES (UINT64_C(1) << 0)
#define MAELYS_DATALOG_RESOURCE_CALLER_ALLOCATOR   (UINT64_C(1) << 1)
/* Historical mask retained unchanged for separately compiled older consumers. */
#define MAELYS_DATALOG_RESOURCE_SUPPORTED_014 \
    MAELYS_DATALOG_RESOURCE_SESSION_CAPACITIES

#define MAELYS_DATALOG_RESOURCE_SUPPORTED \
    (MAELYS_DATALOG_RESOURCE_SUPPORTED_014 | MAELYS_DATALOG_RESOURCE_CALLER_ALLOCATOR)

/* uint32_t fields, not a profile-sized or compiler-sized enum in ABI records. */
#define MAELYS_DATALOG_MEMORY_FIXED           UINT32_C(0)
#define MAELYS_DATALOG_MEMORY_BACKEND_ELASTIC UINT32_C(1)
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

#define MAELYS_DATALOG_CALLER_ALLOCATOR_VERSION 1u

/* The host copies the known descriptor members. Context/code are borrowed
 * through destruction and the last release, including failed preparation. */
typedef struct {
    size_t struct_size;
    uint32_t contract_version;
    uint32_t reserved; /* zero */
    uint64_t required_features; /* zero in V1 */
    void *context;
    void *(*acquire)(void *context, size_t bytes, size_t alignment);
    void (*release)(void *context, void *pointer, size_t bytes, size_t alignment);
} maelys_datalog_caller_allocator_t;
#define MAELYS_DATALOG_CALLER_ALLOCATOR_PREFIX_SIZE \
    (offsetof(maelys_datalog_caller_allocator_t, required_features) + sizeof(uint64_t))
#define MAELYS_DATALOG_CALLER_ALLOCATOR_V1_SIZE \
    sizeof(maelys_datalog_caller_allocator_t)
#define MAELYS_DATALOG_CALLER_ALLOCATOR_INIT \
    { MAELYS_DATALOG_CALLER_ALLOCATOR_V1_SIZE, \
      MAELYS_DATALOG_CALLER_ALLOCATOR_VERSION, UINT32_C(0), UINT64_C(0), \
      NULL, NULL, NULL }

/* Immutable V1 request prefix, then allocator extension. Set base.struct_size
 * to this complete size, base.memory_mode to BACKEND_ELASTIC and require
 * CALLER_ALLOCATOR. Older hosts reject this required feature before reading the tail. */
typedef struct {
    maelys_datalog_session_resource_request_t base;
    size_t execution_byte_cap;
    const maelys_datalog_caller_allocator_t *allocator;
} maelys_datalog_session_allocation_request_t;
#define MAELYS_DATALOG_ALLOCATION_REQUEST_V1_SIZE \
    sizeof(maelys_datalog_session_allocation_request_t)
#define MAELYS_DATALOG_ALLOCATION_REQUEST_PREFIX_SIZE MAELYS_DATALOG_RESOURCE_REQUEST_PREFIX_SIZE
#define MAELYS_DATALOG_ALLOCATION_REQUEST_INIT \
    { { MAELYS_DATALOG_ALLOCATION_REQUEST_V1_SIZE, MAELYS_DATALOG_RESOURCE_CONTRACT_VERSION, \
        MAELYS_DATALOG_MEMORY_BACKEND_ELASTIC, MAELYS_DATALOG_RESOURCE_CALLER_ALLOCATOR, \
        UINT64_C(0), 0, 0, 0, 0 }, 0, NULL }

/* Read-only elastic accounting, no allocator callbacks or allocation. FIXED
 * returns UNSUPPORTED with unchanged output, not a zero memory claim. Readable
 * with a result/explanation lease, but not reentrantly during an operation.
 * A solve starts the peak at current charge; abort/cleanup preserve that peak.
 * Larger tails remain untouched; failed output validation writes nothing. */
#define MAELYS_DATALOG_ALLOCATION_STATS_VERSION 1u
typedef struct {
    size_t struct_size;
    uint32_t contract_version;
    uint32_t reserved;
    uint64_t required_features;
    size_t cap_bytes;
    size_t current_bytes;
    size_t remaining_bytes;
    size_t operation_peak_bytes;
} maelys_datalog_session_allocation_stats_t;
#define MAELYS_DATALOG_ALLOCATION_STATS_PREFIX_SIZE \
    (offsetof(maelys_datalog_session_allocation_stats_t, required_features) + sizeof(uint64_t))
#define MAELYS_DATALOG_ALLOCATION_STATS_V1_SIZE \
    sizeof(maelys_datalog_session_allocation_stats_t)
#define MAELYS_DATALOG_ALLOCATION_STATS_INIT \
    { MAELYS_DATALOG_ALLOCATION_STATS_V1_SIZE, MAELYS_DATALOG_ALLOCATION_STATS_VERSION, \
      UINT32_C(0), UINT64_C(0), 0, 0, 0, 0 }
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_session_get_allocation_stats(
    const maelys_datalog_session_t *, maelys_datalog_session_allocation_stats_t *);

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


/* Caller-owned policy storage: fixed profile capacity (up to eight policies),
 * no allocation for the policy object and no heap fallback. Loading itself may
 * allocate in JSON parsing, contexts or extension callbacks: NOT a whole-loader
 * zero-malloc guarantee. Storage must be unused, aligned, disjoint from inputs,
 * outputs and diagnostics, and outlive the handle. Failure may overwrite it;
 * *out remains NULL. Close before reuse. free closes but never frees this arena.
 * The convenience loaders allocate exactly one policy object in addition to
 * loader/callback allocations. Sessions make their own prepared program copy. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_policy_storage_requirements(size_t *, size_t *);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_policy_load_manifest_text_in(
    void *, size_t, const char *, size_t, const maelys_datalog_policy_bundle_entry_t *, size_t,
    unsigned, maelys_datalog_policy_t **, maelys_datalog_diagnostic_t *);
#ifdef __cplusplus
}
#endif
#endif
