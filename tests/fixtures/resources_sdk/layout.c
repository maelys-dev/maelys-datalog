/* SPDX-License-Identifier: MPL-2.0 */
#include <maelys/datalog_backend.h>
#include <maelys/datalog_resources.h>
#include <stddef.h>
#include <stdio.h>
#ifdef __cplusplus
#include <type_traits>
#define SIZE_FIELD(T, f) static_assert(std::is_same<decltype(T::f), size_t>::value, #T "." #f)
#define ALIGN(T) alignof(T)
#else
#define SIZE_FIELD(T, f) _Static_assert(_Generic(((T *)0)->f, size_t: 1, default: 0), #T "." #f)
#define ALIGN(T) _Alignof(T)
#endif
#define QUOTAS(T) SIZE_FIELD(T, input_facts); SIZE_FIELD(T, derived_facts); \
    SIZE_FIELD(T, symbols); SIZE_FIELD(T, text_bytes)
QUOTAS(maelys_datalog_session_resource_request_t);
QUOTAS(maelys_datalog_session_resources_t);
#define PLAN(f) SIZE_FIELD(maelys_datalog_session_storage_plan_t, f)
PLAN(arena_bytes); PLAN(arena_alignment); PLAN(host_bytes); PLAN(backend_bytes);
PLAN(explanation_bytes); PLAN(padding_bytes); PLAN(external_backend_bytes);
PLAN(external_explanation_bytes); PLAN(total_execution_bytes);
#define COUNT(f) SIZE_FIELD(maelys_datalog_extension_v2_t, f)
COUNT(frontend_count); COUNT(backend_v5_count); COUNT(backend_v6_count);
COUNT(planner_count); COUNT(filter_count);
#define RECORD(T) printf(#T " %zu %zu\n", sizeof(T), (size_t)ALIGN(T))
#define FIELD(T, f) printf(#T "." #f " %zu %zu\n", offsetof(T, f), sizeof(((T *)0)->f))
#define RESOURCE(T) RECORD(T); FIELD(T, struct_size); FIELD(T, contract_version); \
    FIELD(T, memory_mode); FIELD(T, required_features); FIELD(T, input_facts); \
    FIELD(T, derived_facts); FIELD(T, symbols); FIELD(T, text_bytes)
int main(void) {
    RESOURCE(maelys_datalog_session_resource_request_t);
    FIELD(maelys_datalog_session_resource_request_t, capacity_mask);
    RESOURCE(maelys_datalog_session_resources_t);
    RECORD(maelys_datalog_session_storage_plan_t);
#define P(f) FIELD(maelys_datalog_session_storage_plan_t, f)
    P(struct_size); P(plan_version); P(reserved); P(required_features);
    P(arena_bytes); P(arena_alignment); P(host_bytes); P(backend_bytes);
    P(explanation_bytes); P(padding_bytes); P(external_backend_bytes);
    P(external_explanation_bytes); P(total_execution_bytes);
    RECORD(maelys_datalog_backend_v6_t); RECORD(maelys_datalog_extension_v2_t);
    return 0;
}
