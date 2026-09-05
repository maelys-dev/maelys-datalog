/* SPDX-License-Identifier: MPL-2.0 */
#include "src/modules/maelys_datalog_modules_internal.h"
#include "src/core/maelys_datalog_filter.h"
#include "modules/standard/string_filters.h"
#include <stdatomic.h>
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

static int identifier_valid(const char *s, size_t capacity, int predicate) {
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
static void append_filter(const maelys_datalog_filter_module_t *module) {
    filter_slot_t *slot = &filters[filter_count];
    slot->module = *module;
    memcpy(slot->name, module->name, strlen(module->name) + 1u);
    memcpy(slot->semantic_id, module->semantic_id, strlen(module->semantic_id) + 1u);
    slot->module.name = slot->name;
    slot->module.semantic_id = slot->semantic_id;
    slot->definition =
        (maelys_datalog_filter_definition_t){(maelys_datalog_filter_kind_t)(filter_count + 1u),
                                             slot->name, slot->semantic_id, &slot->module};
    ++filter_count;
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
            append_filter(&standard[i]);
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
    if (!module || module->abi_version != MAELYS_DATALOG_MODULE_ABI_VERSION ||
        module->struct_size != sizeof(*module) || !module->validate_pattern || !module->cost ||
        !module->evaluate || !identifier_valid(module->name, MAELYS_DATALOG_MODULE_NAME_BYTES, 1) ||
        !identifier_valid(module->semantic_id, MAELYS_DATALOG_MODULE_SEMANTIC_ID_BYTES, 0) ||
        strcmp(module->name, "not") == 0 || strcmp(module->name, "true") == 0 ||
        strcmp(module->name, "false") == 0 || strcmp(module->name, "or") == 0) {
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
    append_filter(module);
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t
maelys_datalog_register_planner_module(const maelys_datalog_planner_module_t *module) {
    if (sealed)
        return MAELYS_DATALOG_STATUS_INVALID_STATE;
    if (!module || module->abi_version != MAELYS_DATALOG_MODULE_ABI_VERSION ||
        module->struct_size != sizeof(*module) || !module->choose ||
        !identifier_valid(module->name, MAELYS_DATALOG_MODULE_NAME_BYTES, 1) ||
        !identifier_valid(module->semantic_id, MAELYS_DATALOG_MODULE_SEMANTIC_ID_BYTES, 0)) {
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
