/* SPDX-License-Identifier: MPL-2.0 */
/* Diagnostic translation unit; never linked into the installed SDK. */
#include "src/core/maelys_datalog_solver.c"
#include "session_storage_inventory.h"

size_t maelys_inventory_solver(void) {
    typedef maelys_datalog_internal_solve_result_t result_t;
    STORAGE_TYPE("result", result_t);
    STORAGE_VALUE("result.metadata_prefix", offsetof(result_t, idb_facts));
    STORAGE_FIELD("result", result_t, facts_per_pred);
    STORAGE_FIELD("result", result_t, stratum_idb_end);
    STORAGE_FIELD("result", result_t, idb_facts);
    STORAGE_FIELD("result", result_t, idb_proof_index);
    STORAGE_FIELD("result", result_t, edb_ranges);
    STORAGE_FIELD("result", result_t, proof);
    STORAGE_FIELD("result", result_t, premise_pool);
    STORAGE_FIELD("result", result_t, node_premise_begin);
    STORAGE_FIELD("result", result_t, node_premise_count);
    STORAGE_FIELD("result", result_t, node_has_premises);
    STORAGE_FIELD("result", result_t, witness_slots);
    const size_t provenance = sizeof(((result_t *)0)->proof)
        + sizeof(((result_t *)0)->premise_pool) + sizeof(((result_t *)0)->node_premise_begin)
        + sizeof(((result_t *)0)->node_premise_count) + sizeof(((result_t *)0)->node_has_premises)
        + sizeof(((result_t *)0)->witness_slots);
    STORAGE_VALUE("result.provenance_and_witness", provenance);
    STORAGE_VALUE("result.other_metadata_and_padding", sizeof(result_t) - provenance
        - sizeof(((result_t *)0)->idb_facts) - sizeof(((result_t *)0)->idb_proof_index)
        - sizeof(((result_t *)0)->edb_ranges));
    STORAGE_VALUE("reservation.legacy_edb_snapshot_addition",
        MAELYS_DATALOG_MAX_EDB_FACTS * sizeof(maelys_datalog_internal_fact_t));
    return sizeof(result_t);
}
