#pragma once
#ifndef MAELYS_DATALOG_AUDIT_H
#define MAELYS_DATALOG_AUDIT_H

#include "policy/maelys_datalog_policy.h"

void maelys_datalog_proof_init(maelys_datalog_proof_tree_t *proof,
                               const char *policy_id,
                               const char *sha256,
                               int verbose);
void maelys_datalog_proof_add(maelys_datalog_proof_tree_t *proof,
                              size_t rule_id,
                              maelys_datalog_predicate_id_t predicate_id,
                              const maelys_datalog_fact_t *derived_fact,
                              maelys_datalog_deny_reason_t reason,
                              size_t depth,
                              uint16_t parent_index);
const char *maelys_datalog_deny_reason_name(maelys_datalog_deny_reason_t reason);

#endif
