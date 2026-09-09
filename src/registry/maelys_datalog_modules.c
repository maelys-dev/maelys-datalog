/* SPDX-License-Identifier: MPL-2.0 */
#include "modules/standard/string_filters.h"
#include "src/core/maelys_datalog_filter.h"
#include "src/registry/maelys_datalog_modules_internal.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define STANDARD_FILTER_COUNT 3u
#define FILTER_CAPACITY (STANDARD_FILTER_COUNT + MAELYS_DATALOG_MODULE_MAX_FILTERS)
_Static_assert(FILTER_CAPACITY <= UINT8_MAX, "filter ID must fit the ruleset POD");
typedef struct {
    maelys_datalog_filter_definition_t definition;
    maelys_datalog_filter_module_t module;
    char name[MAELYS_DATALOG_MODULE_NAME_BYTES];
    char semantic_id[MAELYS_DATALOG_MODULE_SEMANTIC_ID_BYTES];
} filter_slot_t;
/* Startup-thread initialization; immutable once sealed. */
static filter_slot_t filters[FILTER_CAPACITY];
static size_t filter_count;
static atomic_int initialized;
static atomic_int sealed;
static maelys_datalog_planner_module_t planner;
static char planner_name[MAELYS_DATALOG_MODULE_NAME_BYTES];
static char planner_semantic_id[MAELYS_DATALOG_MODULE_SEMANTIC_ID_BYTES];
static int filter_descriptor_valid(const maelys_datalog_filter_module_t *);
static int planner_descriptor_valid(const maelys_datalog_planner_module_t *);

int maelys_datalog_identity_valid(const char *s, size_t capacity, int predicate) {
    if (!s || !s[0] || (predicate && (s[0] < 'a' || s[0] > 'z')))
        return 0;
    for (size_t i = 0u; i < capacity; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (!c)
            return 1;
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')
            continue;
        if (!predicate && ((c >= 'A' && c <= 'Z') || c == '.' || c == '-'))
            continue;
        return 0;
    }
    return 0;
}
static void append_filter_to(filter_slot_t *slots, size_t *count,
                             const maelys_datalog_filter_module_t *module) {
    filter_slot_t *slot = &slots[*count];
    slot->module = *module;
    memcpy(slot->name, module->name, strlen(module->name) + 1u);
    memcpy(slot->semantic_id, module->semantic_id, strlen(module->semantic_id) + 1u);
    slot->module.name = slot->name;
    slot->module.semantic_id = slot->semantic_id;
    slot->definition = (maelys_datalog_filter_definition_t){
        (maelys_datalog_filter_kind_t)(*count + 1u), slot->name, slot->semantic_id, &slot->module};
    ++*count;
}
static void initialize(void) {
    if (atomic_load_explicit(&initialized, memory_order_acquire) == 2)
        return;
    int expected = 0;
    if (atomic_compare_exchange_strong_explicit(&initialized, &expected, 1, memory_order_acquire,
                                                memory_order_acquire)) {
        size_t count;
        const maelys_datalog_filter_module_t *standard =
            maelys_datalog_standard_string_filters(&count);
        for (size_t i = 0u; i < count && i < STANDARD_FILTER_COUNT; ++i)
            append_filter_to(filters, &filter_count, &standard[i]);
        atomic_store_explicit(&initialized, 2, memory_order_release);
    } else {
        /* Standard-only clients may perform their first policy loads in
         * parallel. Only this bounded, callback-free initialization waits. */
        while (atomic_load_explicit(&initialized, memory_order_acquire) != 2) {
        }
    }
}
void maelys_datalog_modules_seal(void) {
    if (atomic_load_explicit(&sealed, memory_order_acquire))
        return;
    initialize();
    atomic_store_explicit(&sealed, 1, memory_order_release);
}
const maelys_datalog_filter_definition_t *maelys_datalog_filter_by_name(const char *name) {
    initialize();
    if (!name)
        return NULL;
    for (size_t i = 0u; i < filter_count; ++i) {
        if (strcmp(filters[i].name, name) == 0)
            return &filters[i].definition;
    }
    return NULL;
}
const maelys_datalog_filter_definition_t *
maelys_datalog_filter_by_kind(maelys_datalog_filter_kind_t kind) {
    initialize();
    if ((unsigned)kind == 0u || (unsigned)kind > filter_count)
        return NULL;
    return &filters[(unsigned)kind - 1u].definition;
}
maelys_datalog_status_t
maelys_datalog_register_filter_module(const maelys_datalog_filter_module_t *module) {
    initialize();
    if (sealed)
        return MAELYS_DATALOG_STATUS_INVALID_STATE;
    if (!filter_descriptor_valid(module)) {
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    }
    for (size_t i = 0u; i < filter_count; ++i) {
        if (strcmp(filters[i].name, module->name) == 0 ||
            strcmp(filters[i].semantic_id, module->semantic_id) == 0) {
            return MAELYS_DATALOG_STATUS_INVALID_FIELD;
        }
    }
    if (filter_count == FILTER_CAPACITY)
        return MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE;
    append_filter_to(filters, &filter_count, module);
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t
maelys_datalog_register_planner_module(const maelys_datalog_planner_module_t *module) {
    if (sealed)
        return MAELYS_DATALOG_STATUS_INVALID_STATE;
    if (!planner_descriptor_valid(module)) {
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    }
    if (planner.choose)
        return MAELYS_DATALOG_STATUS_INVALID_STATE;
    memcpy(planner_name, module->name, strlen(module->name) + 1u);
    memcpy(planner_semantic_id, module->semantic_id, strlen(module->semantic_id) + 1u);
    planner = *module;
    planner.name = planner_name;
    planner.semantic_id = planner_semantic_id;
    return MAELYS_DATALOG_STATUS_OK;
}
const maelys_datalog_planner_module_t *maelys_datalog_active_planner(void) {
    return planner.choose ? &planner : NULL;
}

/* Explicit contexts never consult or mutate the compatibility registry above.
 * Only immutable catalog pointers travel with rulesets; they are retained by
 * public policies and prepared sessions, not by transient ruleset POD copies. */
typedef struct {
    char name[64], semantic_id[128];
} identity_t;
typedef struct {
    identity_t id;
    maelys_datalog_frontend_t value;
} frontend_slot_t;
typedef struct {
    identity_t id;
    maelys_datalog_backend_t value;
} backend_slot_t;
typedef struct {
    identity_t id;
    maelys_datalog_planner_module_t value;
} planner_slot_t;
typedef struct {
    filter_slot_t filters[FILTER_CAPACITY];
    frontend_slot_t frontends[MAELYS_DATALOG_CONTEXT_MAX_COMPONENTS];
    backend_slot_t backends[MAELYS_DATALOG_CONTEXT_MAX_COMPONENTS];
    planner_slot_t planners[MAELYS_DATALOG_CONTEXT_MAX_COMPONENTS];
    identity_t packages[MAELYS_DATALOG_CONTEXT_MAX_EXTENSIONS];
    size_t filter_count, frontend_count, backend_count, planner_count, package_count;
    size_t selected_planner;
} catalog_t;
struct maelys_datalog_context {
    atomic_size_t references;
    int sealed;
    catalog_t catalog;
};

static int valid_identity(const char *name, const char *semantic_id) {
    return maelys_datalog_identity_valid(name, 64u, 1) &&
           maelys_datalog_identity_valid(semantic_id, 128u, 0);
}
int maelys_datalog_frontend_descriptor_valid(const maelys_datalog_frontend_t *d) {
    return d && d->abi_version == MAELYS_DATALOG_PROGRAM_ABI_VERSION &&
           d->struct_size == sizeof(*d) && d->lower && valid_identity(d->name, d->semantic_id);
}
int maelys_datalog_backend_descriptor_valid(const maelys_datalog_backend_t *d) {
    return d && d->abi_version == MAELYS_DATALOG_BACKEND_ABI_VERSION &&
           d->struct_size == sizeof(*d) && valid_identity(d->name, d->semantic_id) && d->prepare &&
           d->solve && d->destroy && d->destroy_result &&
           !(d->capabilities & ~MAELYS_DATALOG_CAP_ALL) &&
           (!(d->capabilities & MAELYS_DATALOG_CAP_EXPLAIN_TRUE) || d->explain_true) &&
           (!(d->capabilities & MAELYS_DATALOG_CAP_EXPLAIN_FALSE) || d->explain_false);
}
static int filter_descriptor_valid(const maelys_datalog_filter_module_t *d) {
    return d && d->abi_version == MAELYS_DATALOG_MODULE_ABI_VERSION &&
           d->struct_size == sizeof(*d) && valid_identity(d->name, d->semantic_id) &&
           d->validate_pattern && d->cost && d->evaluate && strcmp(d->name, "not") &&
           strcmp(d->name, "true") && strcmp(d->name, "false") && strcmp(d->name, "or");
}
static int planner_descriptor_valid(const maelys_datalog_planner_module_t *d) {
    return d && d->abi_version == MAELYS_DATALOG_MODULE_ABI_VERSION &&
           d->struct_size == sizeof(*d) && valid_identity(d->name, d->semantic_id) && d->choose;
}
static void identity_copy(identity_t *id, const char *name, const char *semantic_id) {
    memcpy(id->name, name, strlen(name) + 1u);
    memcpy(id->semantic_id, semantic_id, strlen(semantic_id) + 1u);
}
static int identity_conflicts(const identity_t *id, const char *name, const char *semantic_id) {
    return !strcmp(id->name, name) || !strcmp(id->semantic_id, semantic_id);
}
static void catalog_rebind(catalog_t *c) {
    for (size_t i = 0; i < c->filter_count; ++i) {
        filter_slot_t *s = &c->filters[i];
        s->module.name = s->name;
        s->module.semantic_id = s->semantic_id;
        s->definition.name = s->name;
        s->definition.semantic_id = s->semantic_id;
        s->definition.module = &s->module;
    }
    for (size_t i = 0; i < c->frontend_count; ++i) {
        c->frontends[i].value.name = c->frontends[i].id.name;
        c->frontends[i].value.semantic_id = c->frontends[i].id.semantic_id;
    }
    for (size_t i = 0; i < c->backend_count; ++i) {
        c->backends[i].value.name = c->backends[i].id.name;
        c->backends[i].value.semantic_id = c->backends[i].id.semantic_id;
    }
    for (size_t i = 0; i < c->planner_count; ++i) {
        c->planners[i].value.name = c->planners[i].id.name;
        c->planners[i].value.semantic_id = c->planners[i].id.semantic_id;
    }
}
maelys_datalog_status_t maelys_datalog_context_create(maelys_datalog_context_t **out) {
    if (!out)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    *out = calloc(1, sizeof(**out));
    if (!*out)
        return MAELYS_DATALOG_STATUS_INTERNAL;
    atomic_init(&(*out)->references, 1u);
    catalog_t *c = &(*out)->catalog;
    c->selected_planner = SIZE_MAX;
    size_t n;
    const maelys_datalog_filter_module_t *standard = maelys_datalog_standard_string_filters(&n);
    for (size_t i = 0; i < n; ++i)
        append_filter_to(c->filters, &c->filter_count, &standard[i]);
    const maelys_datalog_frontend_t *f = maelys_datalog_frontend_datalog();
    c->frontends[0].value = *f;
    identity_copy(&c->frontends[0].id, f->name, f->semantic_id);
    c->frontend_count = 1;
    const maelys_datalog_backend_t *b = maelys_datalog_backend_reference();
    c->backends[0].value = *b;
    identity_copy(&c->backends[0].id, b->name, b->semantic_id);
    c->backend_count = 1;
    catalog_rebind(c);
    return MAELYS_DATALOG_STATUS_OK;
}
void maelys_datalog_context_retain(maelys_datalog_context_t *c) {
    if (c)
        atomic_fetch_add_explicit(&c->references, 1u, memory_order_relaxed);
}
void maelys_datalog_context_release(maelys_datalog_context_t *c) {
    if (c && atomic_fetch_sub_explicit(&c->references, 1u, memory_order_acq_rel) == 1u)
        free(c);
}
maelys_datalog_status_t maelys_datalog_context_free(maelys_datalog_context_t *c) {
    if (!c)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    maelys_datalog_context_release(c);
    return MAELYS_DATALOG_STATUS_OK;
}
int maelys_datalog_context_is_sealed(const maelys_datalog_context_t *c) { return c && c->sealed; }

static maelys_datalog_status_t catalog_add(catalog_t *c, const maelys_datalog_extension_t *e) {
    for (size_t i = 0; i < c->package_count; ++i)
        if (identity_conflicts(&c->packages[i], e->name, e->semantic_id))
            return MAELYS_DATALOG_STATUS_INVALID_FIELD;
    for (size_t k = 0; k < e->filter_count; ++k) {
        const maelys_datalog_filter_module_t *d = &e->filters[k];
        if (!filter_descriptor_valid(d))
            return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
        for (size_t i = 0; i < c->filter_count; ++i)
            if (!strcmp(c->filters[i].name, d->name) ||
                !strcmp(c->filters[i].semantic_id, d->semantic_id))
                return MAELYS_DATALOG_STATUS_INVALID_FIELD;
        append_filter_to(c->filters, &c->filter_count, d);
    }
    for (size_t k = 0; k < e->frontend_count; ++k) {
        const maelys_datalog_frontend_t *d = &e->frontends[k];
        if (!maelys_datalog_frontend_descriptor_valid(d))
            return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
        for (size_t i = 0; i < c->frontend_count; ++i)
            if (identity_conflicts(&c->frontends[i].id, d->name, d->semantic_id))
                return MAELYS_DATALOG_STATUS_INVALID_FIELD;
        frontend_slot_t *s = &c->frontends[c->frontend_count++];
        s->value = *d;
        identity_copy(&s->id, d->name, d->semantic_id);
    }
    for (size_t k = 0; k < e->backend_count; ++k) {
        const maelys_datalog_backend_t *d = &e->backends[k];
        if (!maelys_datalog_backend_descriptor_valid(d))
            return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
        for (size_t i = 0; i < c->backend_count; ++i)
            if (identity_conflicts(&c->backends[i].id, d->name, d->semantic_id))
                return MAELYS_DATALOG_STATUS_INVALID_FIELD;
        backend_slot_t *s = &c->backends[c->backend_count++];
        s->value = *d;
        identity_copy(&s->id, d->name, d->semantic_id);
    }
    for (size_t k = 0; k < e->planner_count; ++k) {
        const maelys_datalog_planner_module_t *d = &e->planners[k];
        if (!planner_descriptor_valid(d))
            return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
        for (size_t i = 0; i < c->planner_count; ++i)
            if (identity_conflicts(&c->planners[i].id, d->name, d->semantic_id))
                return MAELYS_DATALOG_STATUS_INVALID_FIELD;
        planner_slot_t *s = &c->planners[c->planner_count++];
        s->value = *d;
        identity_copy(&s->id, d->name, d->semantic_id);
    }
    identity_copy(&c->packages[c->package_count++], e->name, e->semantic_id);
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t maelys_datalog_context_register(maelys_datalog_context_t *ctx,
                                                        const maelys_datalog_extension_t *e) {
    if (!ctx || !e)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (ctx->sealed)
        return MAELYS_DATALOG_STATUS_INVALID_STATE;
    if (e->abi_version != MAELYS_DATALOG_EXTENSION_ABI_VERSION || e->struct_size != sizeof(*e) ||
        !valid_identity(e->name, e->semantic_id) || (!e->frontends && e->frontend_count) ||
        (!e->backends && e->backend_count) || (!e->planners && e->planner_count) ||
        (!e->filters && e->filter_count) ||
        !(e->frontend_count || e->backend_count || e->planner_count || e->filter_count))
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    const catalog_t *c = &ctx->catalog;
    if (c->package_count == MAELYS_DATALOG_CONTEXT_MAX_EXTENSIONS ||
        e->filter_count > FILTER_CAPACITY - c->filter_count ||
        e->frontend_count > MAELYS_DATALOG_CONTEXT_MAX_COMPONENTS - c->frontend_count ||
        e->backend_count > MAELYS_DATALOG_CONTEXT_MAX_COMPONENTS - c->backend_count ||
        e->planner_count > MAELYS_DATALOG_CONTEXT_MAX_COMPONENTS - c->planner_count)
        return MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE;
    catalog_t *candidate = malloc(sizeof(*candidate));
    if (!candidate)
        return MAELYS_DATALOG_STATUS_INTERNAL;
    *candidate = *c;
    maelys_datalog_status_t rc = catalog_add(candidate, e);
    if (rc == MAELYS_DATALOG_STATUS_OK) {
        ctx->catalog = *candidate;
        catalog_rebind(&ctx->catalog);
    }
    free(candidate);
    return rc;
}
maelys_datalog_status_t maelys_datalog_context_seal(maelys_datalog_context_t *c, const char *name) {
    if (!c)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (c->sealed)
        return MAELYS_DATALOG_STATUS_INVALID_STATE;
    size_t selected = SIZE_MAX;
    if (name) {
        for (size_t i = 0; i < c->catalog.planner_count; ++i)
            if (!strcmp(c->catalog.planners[i].id.name, name)) {
                selected = i;
                break;
            }
        if (selected == SIZE_MAX)
            return MAELYS_DATALOG_STATUS_NOT_FOUND;
    }
    c->catalog.selected_planner = selected;
    c->sealed = 1;
    return MAELYS_DATALOG_STATUS_OK;
}
const maelys_datalog_planner_module_t *
maelys_datalog_context_planner(const maelys_datalog_context_t *c) {
    if (!c)
        return maelys_datalog_active_planner();
    return c->catalog.selected_planner == SIZE_MAX
               ? NULL
               : &c->catalog.planners[c->catalog.selected_planner].value;
}
const maelys_datalog_frontend_t *maelys_datalog_context_frontend(const maelys_datalog_context_t *c,
                                                                 const char *name) {
    if (!c || !c->sealed)
        return NULL;
    if (!name || !strcmp(name, c->catalog.frontends[0].id.name))
        return maelys_datalog_frontend_datalog(); /* preserve built-in identity authority */
    for (size_t i = 1; i < c->catalog.frontend_count; ++i)
        if (!strcmp(name, c->catalog.frontends[i].id.name))
            return &c->catalog.frontends[i].value;
    return NULL;
}
const maelys_datalog_backend_t *maelys_datalog_context_backend(const maelys_datalog_context_t *c,
                                                               const char *name) {
    if (!c || !c->sealed)
        return NULL;
    if (!name)
        return maelys_datalog_backend_reference();
    for (size_t i = 0; i < c->catalog.backend_count; ++i)
        if (!strcmp(name, c->catalog.backends[i].id.name))
            return &c->catalog.backends[i].value;
    return NULL;
}
const maelys_datalog_filter_definition_t *
maelys_datalog_filter_by_name_in(const maelys_datalog_context_t *c, const char *name) {
    if (!c)
        return maelys_datalog_filter_by_name(name);
    if (name)
        for (size_t i = 0; i < c->catalog.filter_count; ++i)
            if (!strcmp(name, c->catalog.filters[i].name))
                return &c->catalog.filters[i].definition;
    return NULL;
}
const maelys_datalog_filter_definition_t *
maelys_datalog_filter_by_kind_in(const maelys_datalog_context_t *c,
                                 maelys_datalog_filter_kind_t kind) {
    if (!c)
        return maelys_datalog_filter_by_kind(kind);
    return (unsigned)kind && (unsigned)kind <= c->catalog.filter_count
               ? &c->catalog.filters[(unsigned)kind - 1u].definition
               : NULL;
}
maelys_datalog_status_t maelys_datalog_context_component_count(const maelys_datalog_context_t *c,
                                                               maelys_datalog_extension_kind_t kind,
                                                               size_t *out) {
    if (!c || !out)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    switch (kind) {
    case MAELYS_DATALOG_EXTENSION_FRONTEND:
        *out = c->catalog.frontend_count;
        break;
    case MAELYS_DATALOG_EXTENSION_BACKEND:
        *out = c->catalog.backend_count;
        break;
    case MAELYS_DATALOG_EXTENSION_PLANNER:
        *out = c->catalog.planner_count;
        break;
    case MAELYS_DATALOG_EXTENSION_FILTER:
        *out = c->catalog.filter_count;
        break;
    default:
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    }
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t
maelys_datalog_context_component_info(const maelys_datalog_context_t *c,
                                      maelys_datalog_extension_kind_t kind, size_t index,
                                      maelys_datalog_component_info_t *out) {
    size_t count;
    if (!out)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    maelys_datalog_status_t rc = maelys_datalog_context_component_count(c, kind, &count);
    if (rc)
        return rc;
    if (index >= count)
        return MAELYS_DATALOG_STATUS_NOT_FOUND;
    const identity_t *id = NULL;
    switch (kind) {
    case MAELYS_DATALOG_EXTENSION_FRONTEND:
        id = &c->catalog.frontends[index].id;
        break;
    case MAELYS_DATALOG_EXTENSION_BACKEND:
        id = &c->catalog.backends[index].id;
        break;
    case MAELYS_DATALOG_EXTENSION_PLANNER:
        id = &c->catalog.planners[index].id;
        break;
    case MAELYS_DATALOG_EXTENSION_FILTER:
        *out = (maelys_datalog_component_info_t){c->catalog.filters[index].name,
                                                 c->catalog.filters[index].semantic_id};
        return MAELYS_DATALOG_STATUS_OK;
    default:
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    }
    *out = (maelys_datalog_component_info_t){id->name, id->semantic_id};
    return MAELYS_DATALOG_STATUS_OK;
}
