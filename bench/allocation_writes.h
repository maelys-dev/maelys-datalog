/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ALLOCATION_WRITES_H
#define ALLOCATION_WRITES_H
#include <stddef.h>
extern size_t allocation_copy_requests,allocation_move_requests,allocation_set_requests;
void *allocation_count_memcpy(void *,const void *,size_t);
void *allocation_count_memmove(void *,const void *,size_t);
void *allocation_count_memset(void *,int,size_t);
char *allocation_count_strcpy(char *,const char *);
#ifdef ALLOCATION_WRITE_TELEMETRY
#define memcpy allocation_count_memcpy
#define memmove allocation_count_memmove
#define memset allocation_count_memset
#define strcpy allocation_count_strcpy
#endif
#endif
