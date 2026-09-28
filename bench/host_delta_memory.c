/* SPDX-License-Identifier: MPL-2.0 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
static int active;
static uint64_t copied, moved, set;
void *delta_memory_copy(void *d, const void *s, size_t n) { if(active) copied+=n; return memcpy(d,s,n); }
void *delta_memory_move(void *d, const void *s, size_t n) { if(active) moved+=n; return memmove(d,s,n); }
void *delta_memory_set(void *d, int v, size_t n) { if(active) set+=n; return memset(d,v,n); }
void delta_memory_collect(void) { active=!active; }
void delta_memory_dump(const char *name) {
    fprintf(stderr,"%s,%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n",name,copied,moved,set);
    copied=moved=set=0;
}
