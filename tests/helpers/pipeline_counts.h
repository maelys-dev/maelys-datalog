/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_DATALOG_PIPELINE_TESTING_H
#define MAELYS_DATALOG_PIPELINE_TESTING_H

/* Used only by test-generated translation units, never by shipped sources. */
#ifdef MAELYS_TESTING
#include <stddef.h>
typedef struct {
    size_t parses, validations, fingerprints, preparations, materializations;
} maelys_datalog_pipeline_counts_t;
extern _Thread_local maelys_datalog_pipeline_counts_t maelys_datalog_pipeline_counts;
#endif
#endif
