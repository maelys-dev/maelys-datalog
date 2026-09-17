/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_DATALOG_SORT_INTERNAL_H
#define MAELYS_DATALOG_SORT_INTERNAL_H
#include <stddef.h>

/* Typed introsort. No allocator, VLA or element-sized byte-swap loop. Already
 * ordered input needs one linear scan. Median-of-three, two-way partitioning
 * limits movement on nearly ordered data; a depth budget falls back to heapsort.
 * Only the smaller partition recurses, bounding stack depth by log2(count).
 * CMP compares pointers to TYPE and preserves canonical value order. */
#define MAELYS_DEFINE_SORT(NAME, TYPE, CMP)                                      \
static void NAME##_swap(TYPE *a, TYPE *b) {                                     \
    TYPE t = *a; *a = *b; *b = t;                                              \
}                                                                             \
static void NAME##_sift(TYPE *a, size_t root, size_t n) {                        \
    TYPE value = a[root];                                                      \
    while (root < n / 2u) {                                                    \
        size_t child = root * 2u + 1u;                                         \
        if (child + 1u < n && CMP(&a[child], &a[child + 1u]) < 0) ++child;       \
        if (CMP(&value, &a[child]) >= 0) break;                                 \
        a[root] = a[child]; root = child;                                      \
    }                                                                         \
    a[root] = value;                                                           \
}                                                                             \
static void NAME##_heap(TYPE *a, size_t n) {                                    \
    for (size_t i = n / 2u; i > 0u; --i) NAME##_sift(a, i - 1u, n);             \
    for (size_t end = n; end > 1u; ) {                                         \
        --end; NAME##_swap(a, a + end); NAME##_sift(a, 0u, end);                 \
    }                                                                         \
}                                                                             \
static void NAME##_partition(TYPE *a, size_t n, size_t depth) {                 \
    while (n > 16u) {                                                         \
        if (!depth) { NAME##_heap(a, n); return; }                              \
        --depth;                                                              \
        size_t mid = n / 2u;                                                   \
        if (CMP(a, a + mid) > 0) NAME##_swap(a, a + mid);                       \
        if (CMP(a + mid, a + n - 1u) > 0) NAME##_swap(a + mid, a + n - 1u);     \
        if (CMP(a, a + mid) > 0) NAME##_swap(a, a + mid);                       \
        TYPE pivot = a[mid];                                                  \
        size_t lo = 0u, hi = n - 1u;                                          \
        for (;;) {                                                           \
            while (CMP(a + lo, &pivot) < 0) ++lo;                              \
            while (CMP(a + hi, &pivot) > 0) --hi;                              \
            if (lo >= hi) break;                                              \
            NAME##_swap(a + lo, a + hi); ++lo; --hi;                           \
        }                                                                     \
        if (lo < n - lo) {                                                     \
            NAME##_partition(a, lo, depth); a += lo; n -= lo;                  \
        } else {                                                              \
            NAME##_partition(a + lo, n - lo, depth); n = lo;                   \
        }                                                                     \
    }                                                                         \
    for (size_t i = 1u; i < n; ++i) {                                          \
        TYPE value = a[i]; size_t j = i;                                       \
        while (j && CMP(&value, a + j - 1u) < 0) { a[j] = a[j - 1u]; --j; }    \
        a[j] = value;                                                         \
    }                                                                         \
}                                                                             \
static void NAME(TYPE *a, size_t n) {                                          \
    size_t end = 0u;                                                          \
    for (size_t i = 1u; i < n; ++i)                                           \
        if (CMP(a + i - 1u, a + i) > 0) end = i + 1u;                         \
    if (!end) return;                                                        \
    /* Past the last inversion the suffix is ordered. Keep its portion       \
     * above the prefix maximum untouched, even if the prefix is unordered. */\
    if (end < n) {                                                           \
        size_t maximum = 0u;                                                 \
        for (size_t i = 1u; i < end; ++i)                                     \
            if (CMP(a + maximum, a + i) < 0) maximum = i;                     \
        size_t low = end, high = n;                                           \
        while (low < high) {                                                 \
            size_t mid = low + (high - low) / 2u;                             \
            if (CMP(a + mid, a + maximum) < 0) low = mid + 1u;                 \
            else high = mid;                                                 \
        }                                                                    \
        n = low;                                                             \
    }                                                                        \
    size_t depth = 0u;                                                        \
    for (size_t count = n; count > 1u; count >>= 1u) depth += 2u;                \
    NAME##_partition(a, n, depth);                                             \
}
#endif
