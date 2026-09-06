/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_DATALOG_PIPELINE_TESTING_H
#define MAELYS_DATALOG_PIPELINE_TESTING_H

/* Test/benchmark instrumentation only: no production state or public ABI. */
#ifdef MAELYS_TESTING
#include <stddef.h>
typedef struct {
    size_t parses, validations, fingerprints, preparations, materializations;
} maelys_datalog_pipeline_counts_t;
extern _Thread_local maelys_datalog_pipeline_counts_t maelys_datalog_pipeline_counts;
#define MAELYS_DATALOG_COUNT_PIPELINE(field) (++maelys_datalog_pipeline_counts.field)
#else
#define MAELYS_DATALOG_COUNT_PIPELINE(field) ((void)0)
#endif
#endif
