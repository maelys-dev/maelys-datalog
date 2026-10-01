/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_DATALOG_EXTENSION_H
#define MAELYS_DATALOG_EXTENSION_H
#include "datalog_resources.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Component descriptors are incomplete here. Include the corresponding
 * component-author header only when defining or inspecting a descriptor. */
typedef struct maelys_datalog_backend_t maelys_datalog_backend_t;
typedef struct maelys_datalog_backend_v6_t maelys_datalog_backend_v6_t;
typedef struct maelys_datalog_backend_v7_t maelys_datalog_backend_v7_t;
typedef struct maelys_datalog_frontend_t maelys_datalog_frontend_t;
typedef struct maelys_datalog_filter_module_t maelys_datalog_filter_module_t;
typedef struct maelys_datalog_planner_module_t maelys_datalog_planner_module_t;

#define MAELYS_DATALOG_EXTENSION_ABI_VERSION 1u
#define MAELYS_DATALOG_CONTEXT_MAX_EXTENSIONS 16u
#define MAELYS_DATALOG_CONTEXT_MAX_COMPONENTS 16u
typedef struct maelys_datalog_context maelys_datalog_context_t;

/* A declaration, not executable initialization. One package may provide any
 * combination of typed components. Registration copies descriptors/identities
 * atomically; no callback is run. Code must stay loaded until all contexts,
 * policies and sessions using it have been released. No dynamic loader. */
typedef struct {
    uint32_t abi_version;
    size_t struct_size;
    const char *name;
    const char *semantic_id;
    const maelys_datalog_frontend_t *frontends;
    size_t frontend_count;
    const maelys_datalog_backend_t *backends;
    size_t backend_count;
    const maelys_datalog_planner_module_t *planners;
    size_t planner_count;
    const maelys_datalog_filter_module_t *filters;
    size_t filter_count;
} maelys_datalog_extension_t;

/* Build on one thread, then seal once. A new context contains standard filters,
 * Datalog and the reference backend, never legacy process-global extensions.
 * NULL planner selects the reference heuristic; a name selects a registered
 * planner for policies compiled in this context. Sealed catalogs are immutable.
 * Domain registration is still process-wide; contexts isolate extensions only. */
MAELYS_DATALOG_API maelys_datalog_status_t
maelys_datalog_context_create(maelys_datalog_context_t **);
MAELYS_DATALOG_API maelys_datalog_status_t
maelys_datalog_context_register(maelys_datalog_context_t *, const maelys_datalog_extension_t *);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_context_seal(maelys_datalog_context_t *,
                                                                       const char *planner_name);
/* Release the caller's handle; policies/sessions retain their own references. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_context_free(maelys_datalog_context_t *);

/* Names are explicit selections, never fallback hints. NULL frontend/backend
 * selects the built-in reference. Loading requires a sealed context. Existing
 * policy/session APIs also work on the resulting handles. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_context_load_inline(
    maelys_datalog_context_t *, const char *frontend_name, const char *domain,
    const char *policy_id, const char *source, size_t source_length, maelys_datalog_policy_t **,
    maelys_datalog_diagnostic_t *);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_context_session_create(
    maelys_datalog_context_t *, const maelys_datalog_policy_t *, size_t policy_index,
    const char *backend_name, uint64_t required_capabilities, uint64_t work_limit,
    maelys_datalog_session_t **);

typedef enum {
    MAELYS_DATALOG_EXTENSION_FRONTEND = 1,
    MAELYS_DATALOG_EXTENSION_BACKEND,
    MAELYS_DATALOG_EXTENSION_PLANNER,
    MAELYS_DATALOG_EXTENSION_FILTER
} maelys_datalog_extension_kind_t;
typedef struct {
    const char *name;
    const char *semantic_id;
} maelys_datalog_component_info_t;
/* Metadata is borrowed from the context, not executable callback pointers. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_context_component_count(
    const maelys_datalog_context_t *, maelys_datalog_extension_kind_t, size_t *);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_context_component_info(
    const maelys_datalog_context_t *, maelys_datalog_extension_kind_t, size_t,
    maelys_datalog_component_info_t *);

#define MAELYS_DATALOG_EXTENSION_V2_ABI_VERSION 2u
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

typedef struct {
    uint32_t abi_version;
    size_t struct_size;
    const maelys_datalog_backend_t *backend; /* NULL selects the reference. */
    uint64_t required_capabilities;
    /* Zero selects 1,048,576 host work units. Nonzero requires WORK_LIMIT. */
    uint64_t work_limit;
} maelys_datalog_session_options_t;

MAELYS_DATALOG_API const maelys_datalog_backend_t *maelys_datalog_backend_reference(void);
MAELYS_DATALOG_API const maelys_datalog_backend_v6_t *
maelys_datalog_backend_reference_v6(void);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_session_create_ex(
    const maelys_datalog_policy_t *, size_t policy_index, const maelys_datalog_session_options_t *,
    maelys_datalog_session_t **);
/* Compose backend/work requirements with the existing explanation storage
 * configuration. The descriptor is copied; callback code must outlive sessions.
 * Unsupported combinations fail before any session is returned. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_session_config_set_backend(
    maelys_datalog_session_config_t *, const maelys_datalog_backend_t *);
/* Query without preparing a session; NULL backend selects the reference.
 * Outputs are unchanged on failure. No allocation. Requirements are checked
 * again once at session creation; a backend must return deterministic values. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_backend_storage_requirements(
    const maelys_datalog_policy_t *, size_t policy_index, const maelys_datalog_backend_t *,
    size_t *out_bytes, size_t *out_alignment);
/* Copies the descriptor, borrows its buffer; NULL clears the selection.
 * Backend/context selection does not reset this storage. Each live session
 * needs disjoint storage, including the two sessions borrowed by a window.
 * Size and alignment must satisfy the selected backend at creation, otherwise
 * INVALID_ARGUMENT before prepare. No implicit allocation/fallback. Plain
 * session_create_ex/context_session_create supply no backend storage. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_session_config_set_backend_storage(
    maelys_datalog_session_config_t *, const maelys_datalog_backend_storage_t *);
/* Select a registered backend, or NULL for the reference, in a sealed context.
 * The config retains
 * the context; the policy must have been compiled in that same context. Setting
 * a direct backend resets this selection, and vice versa. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_session_config_set_context(
    maelys_datalog_session_config_t *, maelys_datalog_context_t *, const char *backend_name);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_session_config_set_requirements(
    maelys_datalog_session_config_t *, uint64_t capabilities, uint64_t work_limit);

MAELYS_DATALOG_API maelys_datalog_status_t
maelys_datalog_session_config_set_backend_v6(
    maelys_datalog_session_config_t *, const maelys_datalog_backend_v6_t *);
/* Copies identities/descriptor; code must outlive the session. NULL restores
 * ordinary reference configuration. Requires a retained-input attachment
 * before solving; ordinary session_solve is refused. Unknown version, short
 * descriptor or missing callbacks reject without modifying the configuration.
 * There is no ABI 7 extension registry or language capability bit in V1. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_session_config_set_backend_v7(
    maelys_datalog_session_config_t *, const maelys_datalog_backend_v7_t *);

/* Explicit per-load selection, not a mutable global grammar. Frontends are
 * trusted native code. They must not retain source/builder arguments or reenter
 * loading. Successful lowering is ALWAYS followed by core validation. */
MAELYS_DATALOG_API const maelys_datalog_frontend_t *maelys_datalog_frontend_datalog(void);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_policy_load_frontend(
    const char *domain, const char *policy_id, const char *source, size_t source_length,
    const maelys_datalog_frontend_t *, maelys_datalog_policy_t **,
    maelys_datalog_diagnostic_t *);

/* Caller-owned policy storage follows datalog_resources.h. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_policy_load_frontend_in(
    void *, size_t, const char *domain, const char *policy_id, const char *source, size_t,
    const maelys_datalog_frontend_t *, maelys_datalog_policy_t **, maelys_datalog_diagnostic_t *);
#ifdef __cplusplus
}
#endif
#endif
