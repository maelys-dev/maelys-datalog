#include "policy/maelys_datalog_audit.h"

#include <stdio.h>
#include <string.h>

void maelys_datalog_proof_init(maelys_datalog_proof_tree_t *proof,
                               const char *policy_id,
                               const char *sha256,
                               int verbose) {
    if (!proof) return;
    memset(proof, 0, sizeof(*proof));
    if (policy_id) snprintf(proof->policy_id, sizeof(proof->policy_id), "%s", policy_id);
    if (sha256) snprintf(proof->sha256, sizeof(proof->sha256), "%s", sha256);
    proof->verbose = verbose ? 1 : 0;
}

void maelys_datalog_proof_add(maelys_datalog_proof_tree_t *proof,
                              size_t rule_id,
                              maelys_datalog_predicate_id_t predicate_id,
                              const maelys_datalog_fact_t *derived_fact,
                              maelys_datalog_deny_reason_t reason,
                              size_t depth,
                              uint16_t parent_index) {
    if (!proof) return;
    if (proof->node_count >= MAELYS_DATALOG_MAX_PROOF_NODES ||
        depth >= MAELYS_DATALOG_MAX_PROOF_DEPTH) {
        proof->truncated = 1;
        return;
    }
    maelys_datalog_proof_node_t *n = &proof->nodes[proof->node_count++];
    n->rule_id = rule_id;
    n->predicate_id = predicate_id;
    n->deny_reason = reason;
    n->depth = depth;
    n->parent_index = parent_index;
    if (derived_fact) {
        n->derived_fact = *derived_fact;
    } else {
        memset(&n->derived_fact, 0, sizeof(n->derived_fact));
    }
}

const char *maelys_datalog_deny_reason_name(maelys_datalog_deny_reason_t reason) {
    switch (reason) {
        case MAELYS_DATALOG_DENY_EXPLICIT: return "DENY_EXPLICIT";
        case MAELYS_DATALOG_DENY_DEFAULT: return "DENY_DEFAULT";
        case MAELYS_DATALOG_DENY_MAX_DEPTH: return "DENY_MAX_DEPTH";
        case MAELYS_DATALOG_DENY_EDB_OVERFLOW: return "DENY_EDB_OVERFLOW";
        case MAELYS_DATALOG_DENY_IDB_OVERFLOW: return "DENY_IDB_OVERFLOW";
        case MAELYS_DATALOG_DENY_CONFLICT: return "DENY_CONFLICT";
        case MAELYS_DATALOG_DENY_POLICY_LOAD_ERROR: return "DENY_POLICY_LOAD_ERROR";
        case MAELYS_DATALOG_DENY_NONE:
        default: return "DENY_NONE";
    }
}
