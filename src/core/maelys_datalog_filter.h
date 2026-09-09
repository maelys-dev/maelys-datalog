#pragma once
#ifndef MAELYS_DATALOG_FILTER_H
#define MAELYS_DATALOG_FILTER_H

#include <stddef.h>
#include <stdint.h>

#include "common/maelys_errors.h"
#include "src/core/maelys_datalog_types.h"
#include "maelys/datalog_module.h"
#include "maelys/datalog_extension.h"

typedef struct {
    maelys_datalog_filter_kind_t kind;
    const char *name;
    const char *semantic_id;
    const maelys_datalog_filter_module_t *module;
} maelys_datalog_filter_definition_t;

const maelys_datalog_filter_definition_t *maelys_datalog_filter_by_name_in(
    const maelys_datalog_context_t *, const char *);
const maelys_datalog_filter_definition_t *maelys_datalog_filter_by_kind_in(
    const maelys_datalog_context_t *, maelys_datalog_filter_kind_t);
maelys_result_t maelys_datalog_filter_validate_in(const maelys_datalog_context_t *,
    maelys_datalog_filter_kind_t, const unsigned char *, size_t);
maelys_result_t maelys_datalog_filter_cost_in(const maelys_datalog_context_t *,
    maelys_datalog_filter_kind_t, size_t, size_t, size_t *);
maelys_result_t maelys_datalog_filter_evaluate_in(const maelys_datalog_context_t *,
    maelys_datalog_filter_kind_t, const unsigned char *, size_t, const unsigned char *, size_t, int *);

const maelys_datalog_filter_definition_t *maelys_datalog_filter_by_name(
    const char *name);
const maelys_datalog_filter_definition_t *maelys_datalog_filter_by_kind(
    maelys_datalog_filter_kind_t kind);
maelys_result_t maelys_datalog_filter_validate(
    maelys_datalog_filter_kind_t kind,
    const unsigned char *pattern, size_t pattern_length);
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
