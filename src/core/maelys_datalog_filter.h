#pragma once
#ifndef MAELYS_DATALOG_FILTER_H
#define MAELYS_DATALOG_FILTER_H

#include <stddef.h>
#include <stdint.h>

#include "common/maelys_errors.h"
#include "src/core/maelys_datalog_types.h"

typedef struct {
    maelys_datalog_filter_kind_t kind;
    const char *name;
    const char *semantic_id;
} maelys_datalog_filter_definition_t;

const maelys_datalog_filter_definition_t *maelys_datalog_filter_by_name(
    const char *name);
const maelys_datalog_filter_definition_t *maelys_datalog_filter_by_kind(
    maelys_datalog_filter_kind_t kind);
maelys_result_t maelys_datalog_filter_cost(
    maelys_datalog_filter_kind_t kind,
    size_t value_length,
    size_t pattern_length,
    size_t *out_cost);
maelys_result_t maelys_datalog_filter_evaluate(
    maelys_datalog_filter_kind_t kind,
    const unsigned char *value,
    size_t value_length,
    const unsigned char *pattern,
    size_t pattern_length,
    int *out_matched);

#endif
