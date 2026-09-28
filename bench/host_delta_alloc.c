/* SPDX-License-Identifier: MPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
int delta_alloc_disabled;
size_t delta_alloc_calls, delta_alloc_bytes;
static void guard(void) { if (delta_alloc_disabled) { fputs("execution allocator call\n", stderr); abort(); } }
void *delta_malloc(size_t n) { guard(); ++delta_alloc_calls; delta_alloc_bytes += n; return malloc(n); }
void *delta_calloc(size_t n, size_t s) { guard(); ++delta_alloc_calls; delta_alloc_bytes += n*s; return calloc(n,s); }
void *delta_realloc(void *p, size_t n) { guard(); ++delta_alloc_calls; delta_alloc_bytes += n; return realloc(p,n); }
void delta_free(void *p) { guard(); free(p); }
