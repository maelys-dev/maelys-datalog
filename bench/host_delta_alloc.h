/* SPDX-License-Identifier: MPL-2.0 */
#include <stdlib.h>
void *delta_malloc(size_t);
void *delta_calloc(size_t, size_t);
void *delta_realloc(void *, size_t);
void delta_free(void *);
extern int delta_alloc_disabled;
extern size_t delta_alloc_calls, delta_alloc_bytes;
#define malloc delta_malloc
#define calloc delta_calloc
#define realloc delta_realloc
#define free delta_free
