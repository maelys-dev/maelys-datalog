/* SPDX-License-Identifier: MPL-2.0 */
/* REVIEW MATERIAL ONLY. Not installed, included or implemented by the SDK.
 * See backend-allocation-015.md. Names and boundaries are proposed contracts. */
#ifndef MAELYS_DATALOG_ALLOCATION_PROPOSAL_V1_H
#define MAELYS_DATALOG_ALLOCATION_PROPOSAL_V1_H
#include <maelys/datalog_resources.h>
#ifdef __cplusplus
extern "C" {
#endif

#define MAELYS_DATALOG_CALLER_ALLOCATOR_VERSION 1u
#define MAELYS_DATALOG_ALLOCATION_SERVICE_VERSION 1u
#define MAELYS_DATALOG_ALLOCATION_BUDGET_VERSION 1u

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
 * CALLER_ALLOCATOR. Neither this tail nor unknown padding is read by V1 hosts. */
typedef struct {
    maelys_datalog_session_resource_request_t base;
    size_t execution_byte_cap;
    const maelys_datalog_caller_allocator_t *allocator;
} maelys_datalog_session_allocation_request_t;
#define MAELYS_DATALOG_ALLOCATION_REQUEST_V1_SIZE \
    sizeof(maelys_datalog_session_allocation_request_t)

/* On successful inspect, fits is 0 or 1; required_charge includes all service
 * overhead. On failure every output member is unchanged. Inspect has no side
 * effects, including on failure, attempts, peaks and sticky transaction state. */
typedef struct {
    size_t struct_size;
    uint32_t contract_version;
    uint32_t reserved; /* zero */
    uint64_t required_features; /* zero in V1 */
    size_t cap_bytes;
    size_t current_bytes;
    size_t remaining_bytes;
    size_t operation_peak_bytes;
    size_t required_charge;
    uint32_t fits;
    uint32_t reserved_tail; /* zero */
} maelys_datalog_allocation_budget_t;
#define MAELYS_DATALOG_ALLOCATION_BUDGET_PREFIX_SIZE \
    (offsetof(maelys_datalog_allocation_budget_t, required_features) + sizeof(uint64_t))
#define MAELYS_DATALOG_ALLOCATION_BUDGET_V1_SIZE \
    sizeof(maelys_datalog_allocation_budget_t)
#define MAELYS_DATALOG_ALLOCATION_BUDGET_INIT \
    { MAELYS_DATALOG_ALLOCATION_BUDGET_V1_SIZE, \
      MAELYS_DATALOG_ALLOCATION_BUDGET_VERSION, UINT32_C(0), UINT64_C(0), \
      0, 0, 0, 0, 0, UINT32_C(0), UINT32_C(0) }

/* Session-bound host service; never the caller's raw allocator. A provider may
 * copy these members into its state. The service context survives through its
 * final destroy callback, and cannot be used for another session or reentrantly.
 * tracking_bytes = K; exact charge(n,a) = K + (a - 1) + n, all sums checked. */
typedef struct {
    size_t struct_size;
    uint32_t contract_version;
    uint32_t reserved; /* zero */
    uint64_t required_features; /* zero in V1 */
    size_t tracking_bytes;
    size_t maximum_alignment;
    void *context;
    maelys_datalog_status_t (*inspect)(
        void *context, size_t bytes, size_t alignment,
        maelys_datalog_allocation_budget_t *out);
    maelys_datalog_status_t (*acquire)(
        void *context, size_t bytes, size_t alignment,
        void **out_block, maelys_datalog_diagnostic_t *diagnostic);
    void (*release)(void *context, void *block);
} maelys_datalog_allocation_service_t;
#define MAELYS_DATALOG_ALLOCATION_SERVICE_PREFIX_SIZE \
    (offsetof(maelys_datalog_allocation_service_t, required_features) + sizeof(uint64_t))
#define MAELYS_DATALOG_ALLOCATION_SERVICE_V1_SIZE \
    sizeof(maelys_datalog_allocation_service_t)

/* Existing normalized V1 resources stay at offset zero. Only elastic callers
 * receive this required tail. Requirements and prepare receive the same scalar
 * values and service descriptor; service operations are forbidden in sizing. */
typedef struct {
    maelys_datalog_session_resources_t base;
    size_t execution_byte_cap;
    const maelys_datalog_allocation_service_t *allocation;
} maelys_datalog_session_allocation_resources_t;
#define MAELYS_DATALOG_ALLOCATION_RESOURCES_V1_SIZE \
    sizeof(maelys_datalog_session_allocation_resources_t)

#ifdef __cplusplus
}
#endif
#endif
