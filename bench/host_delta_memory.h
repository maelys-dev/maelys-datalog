/* SPDX-License-Identifier: MPL-2.0 */
/* Separate observer build only: explicit primitive byte requests, not hardware
 * traffic or all compiler-generated stores. Never used for instruction counts. */
#include <string.h>
void *delta_memory_copy(void *, const void *, size_t);
void *delta_memory_move(void *, const void *, size_t);
void *delta_memory_set(void *, int, size_t);
void delta_memory_collect(void);
void delta_memory_dump(const char *);
#define memcpy delta_memory_copy
#define memmove delta_memory_move
#define memset delta_memory_set
