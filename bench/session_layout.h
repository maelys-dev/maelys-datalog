/* SPDX-License-Identifier: MPL-2.0 */
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#define L_VALUE(key, value) fprintf(out, key ",%" PRIuMAX "\n", (uintmax_t)(value))
#define L_TYPE(name, type) do { L_VALUE("sizeof." name, sizeof(type)); \
    L_VALUE("alignof." name, _Alignof(type)); } while (0)
#define L_ADDRESS(name, pointer) do { L_VALUE("address." name, (uintptr_t)(pointer)); \
    L_VALUE("address_mod64." name, (uintptr_t)(pointer) % 64); } while (0)
#define L_FIELD(name, type, pointer, field) do { \
    L_VALUE("offsetof." name "." #field, offsetof(type, field)); \
    L_VALUE("sizeof." name "." #field, sizeof((pointer)->field)); \
    L_ADDRESS(name "." #field, &(pointer)->field); } while (0)
