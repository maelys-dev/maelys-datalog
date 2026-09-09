/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_DATALOG_EXTENSION_H
#define MAELYS_DATALOG_EXTENSION_H
#include "datalog_backend.h"
#include "datalog_module.h"
#ifdef __cplusplus
extern "C" {
#endif

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
    maelys_datalog_public_diagnostic_t *);
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
#ifdef __cplusplus
}
#endif
#endif
