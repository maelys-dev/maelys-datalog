/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_TEST_ALLOCATION_GUARD_H
#define MAELYS_TEST_ALLOCATION_GUARD_H
#include <stdlib.h>
#include <string.h>
void *maelys_test_malloc(size_t);
void *maelys_test_calloc(size_t, size_t);
void *maelys_test_realloc(void *, size_t);
void maelys_test_free(void *);
void *maelys_test_memset(void *, int, size_t);
#define malloc maelys_test_malloc
#define calloc maelys_test_calloc
#define realloc maelys_test_realloc
#define free maelys_test_free
#ifdef memset
#undef memset
#endif
#define memset maelys_test_memset
#endif
