/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_SESSION_STORAGE_INVENTORY_H
#define MAELYS_SESSION_STORAGE_INVENTORY_H
#include <stddef.h>
#include <stdio.h>
#define STORAGE_VALUE(key, value) printf(key ",%zu\n", (size_t)(value))
#define STORAGE_TYPE(key, type) do { \
    STORAGE_VALUE("sizeof." key, sizeof(type)); \
    STORAGE_VALUE("alignof." key, _Alignof(type)); \
} while (0)
#define STORAGE_FIELD(key, type, field) do { \
    STORAGE_VALUE("sizeof." key "." #field, sizeof(((type *)0)->field)); \
    STORAGE_VALUE("offsetof." key "." #field, offsetof(type, field)); \
} while (0)
size_t maelys_inventory_solver(void);
#endif
