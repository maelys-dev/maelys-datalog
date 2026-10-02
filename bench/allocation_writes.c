/* SPDX-License-Identifier: MPL-2.0 */
/* Separate telemetry build only: counts explicit C call byte requests. It may
 * retain otherwise dead writes. Never use this binary's software/time counts
 * as the ordinary engine measurement, or infer physical bytes from these sums. */
#include <string.h>
#include "allocation_writes.h"
#undef memcpy
#undef memmove
#undef memset
#undef strcpy
size_t allocation_copy_requests,allocation_move_requests,allocation_set_requests;
void *allocation_count_memcpy(void *d,const void *s,size_t n){allocation_copy_requests+=n;return memcpy(d,s,n);}
void *allocation_count_memmove(void *d,const void *s,size_t n){allocation_move_requests+=n;return memmove(d,s,n);}
void *allocation_count_memset(void *d,int c,size_t n){allocation_set_requests+=n;return memset(d,c,n);}
char *allocation_count_strcpy(char *d,const char *s){allocation_copy_requests+=strlen(s)+1;return strcpy(d,s);}
