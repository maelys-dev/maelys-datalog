/* SPDX-License-Identifier: MPL-2.0 */
/* Also compiled as C++17, using only the installed consumer header. */
#include <maelys/datalog_builders.h>
#include <stdint.h>

enum { STORAGE_BUDGET = 256 };
MAELYS_DATALOG_EXPLANATION_STORAGE(global_storage, STORAGE_BUDGET);

int main(void) {
#if defined(REJECT_RUNTIME)
    volatile size_t bytes = STORAGE_BUDGET;
    MAELYS_DATALOG_EXPLANATION_STORAGE(local_storage, bytes);
#elif defined(REJECT_ZERO)
    MAELYS_DATALOG_EXPLANATION_STORAGE(local_storage, 0);
#elif defined(REJECT_NEGATIVE)
    MAELYS_DATALOG_EXPLANATION_STORAGE(local_storage, -1);
#elif defined(REJECT_FLOAT)
    MAELYS_DATALOG_EXPLANATION_STORAGE(local_storage, 128.0);
#else
    MAELYS_DATALOG_EXPLANATION_STORAGE(local_storage, STORAGE_BUDGET);
#endif
#ifdef __cplusplus
    const size_t alignment = alignof(max_align_t);
#else
    const size_t alignment = _Alignof(max_align_t);
#endif
    return sizeof(global_storage) != STORAGE_BUDGET ||
        sizeof(local_storage) != STORAGE_BUDGET ||
        (uintptr_t)global_storage % alignment != 0 ||
        (uintptr_t)local_storage % alignment != 0;
}
