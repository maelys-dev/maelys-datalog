/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_DATALOG_SOLVER_TESTING_H
#define MAELYS_DATALOG_SOLVER_TESTING_H
#ifdef MAELYS_TESTING
#include <stddef.h>
/* Thread-local test instrumentation. Every candidate is also checked by the
 * historical linear lookup; production/benchmark builds have neither. */
typedef struct {
    size_t rule_checks, lookups, hits, skips;
    size_t audits, audit_hits, validated_audits, validated_hits;
} maelys_datalog_base_lookup_counts_t;
extern _Thread_local maelys_datalog_base_lookup_counts_t maelys_datalog_base_lookup_counts;
#endif
#endif
