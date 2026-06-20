#pragma once
#ifndef MAELYS_DATALOG_DECISION_H
#define MAELYS_DATALOG_DECISION_H

#include "common/maelys_errors.h"
#include "src/core/maelys_datalog_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAELYS_DATALOG_DECISION_NAME_ALLOW "allow"
#define MAELYS_DATALOG_DECISION_NAME_REDUCED "reduced"
#define MAELYS_DATALOG_DECISION_NAME_DENY "deny"
#define MAELYS_DATALOG_DECISION_NAME_DENY_DEFAULT "deny_default"
#define MAELYS_DATALOG_DECISION_NAME_DENY_CONFLICT "deny_conflict"

typedef enum {
    MAELYS_DATALOG_DECISION_DENY = 0,
    MAELYS_DATALOG_DECISION_ALLOW = 1,
    MAELYS_DATALOG_DECISION_REDUCED = 2,
    MAELYS_DATALOG_DECISION_DENY_DEFAULT = 3,
    MAELYS_DATALOG_DECISION_DENY_CONFLICT = 4
} maelys_datalog_decision_t;

const char *maelys_datalog_decision_name(maelys_datalog_decision_t decision);
maelys_result_t maelys_datalog_decision_from_queries(
    const maelys_datalog_query_result_t *deny_result,
    const maelys_datalog_query_result_t *reduce_result,
    const maelys_datalog_query_result_t *allow_result,
    maelys_datalog_decision_t *out_decision);

#ifdef __cplusplus
}
#endif

#endif
