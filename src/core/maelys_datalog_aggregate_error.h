/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include <stdint.h>
/* Error-only payload. Split value storage preserves 4-byte alignment on wasm32
 * and native builds; memcpy transfers signed int64 without narrowing casts. */
typedef struct {
    uint32_t value_words[2];
    uint16_t predicate_id;
    uint8_t kind;
    uint8_t literal_kind;
    uint8_t term_index;
    uint8_t overflow;
    uint8_t reserved[2];
} maelys_datalog_aggregate_error_t;
