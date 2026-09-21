/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_DATALOG_TERM_INTERNAL_H
#define MAELYS_DATALOG_TERM_INTERNAL_H
#include "src/core/maelys_datalog_types.h"

static inline int maelys_datalog_term_cmp(const maelys_datalog_term_t *a, const maelys_datalog_term_t *b) {
    if (a->kind != b->kind) return (int)a->kind - (int)b->kind;
    switch (a->kind) {
        case MAELYS_DATALOG_TERM_SYMBOL:
            if (a->as.symbol < b->as.symbol) return -1;
            if (a->as.symbol > b->as.symbol) return 1;
            return 0;
        case MAELYS_DATALOG_TERM_INT:
            if (a->as.integer < b->as.integer) return -1;
            if (a->as.integer > b->as.integer) return 1;
            return 0;
        case MAELYS_DATALOG_TERM_BOOL: return a->as.boolean - b->as.boolean;
        case MAELYS_DATALOG_TERM_VAR:
            if (a->as.variable < b->as.variable) return -1;
            if (a->as.variable > b->as.variable) return 1;
            return 0;
        default: return 0;
    }
}

#endif
