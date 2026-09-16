/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_DATALOG_SORT_INTERNAL_H
#define MAELYS_DATALOG_SORT_INTERNAL_H
#include <stddef.h>

/* In-place heapsort: libc qsort is permitted to allocate scratch storage.
 * Callers supply bounded valid arrays; no recursion or auxiliary array. */
static inline void maelys_sort_swap(unsigned char *a, unsigned char *b, size_t width) {
    for (size_t i = 0; i < width; ++i) {
        unsigned char byte = a[i]; a[i] = b[i]; b[i] = byte;
    }
}
static inline void maelys_sort_sift(unsigned char *base, size_t root, size_t count,
                                    size_t width, int (*cmp)(const void *, const void *)) {
    while (root < count / 2u) {
        size_t child = root * 2u + 1u;
        if (child + 1u < count && cmp(base + child * width, base + (child + 1u) * width) < 0)
            ++child;
        if (cmp(base + root * width, base + child * width) >= 0) return;
        maelys_sort_swap(base + root * width, base + child * width, width);
        root = child;
    }
}
static inline void maelys_datalog_sort(void *base, size_t count, size_t width,
                                      int (*cmp)(const void *, const void *)) {
    if (count < 2u) return;
    unsigned char *bytes = base;
    for (size_t i = count / 2u; i > 0u; --i)
        maelys_sort_sift(bytes, i - 1u, count, width, cmp);
    for (size_t end = count - 1u; end > 0u; --end) {
        maelys_sort_swap(bytes, bytes + end * width, width);
        maelys_sort_sift(bytes, 0u, end, width, cmp);
    }
}
#endif
