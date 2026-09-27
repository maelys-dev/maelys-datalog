/* SPDX-License-Identifier: MPL-2.0 */
/* Diagnostic translation unit; never linked into the installed SDK. */
#include "src/runtime/maelys_datalog_runtime.c"
#include "session_storage_inventory.h"

int main(void) {
    typedef maelys_datalog_internal_prepared_session_t inputs_t;
    typedef maelys_datalog_symbol_table_t symbols_t;
    struct owned_inputs {
        inputs_t state;
        maelys_datalog_internal_ruleset_t snapshot;
    };
    printf("key,value\nprofile,%s\n", MAELYS_DATALOG_SIZE_PROFILE_NAME);
    STORAGE_VALUE("pointer_bytes", sizeof(void *));
    STORAGE_VALUE("capacity.input_facts", MAELYS_DATALOG_MAX_EDB_FACTS);
    STORAGE_VALUE("capacity.derived_facts", MAELYS_DATALOG_MAX_IDB_FACTS);
    STORAGE_VALUE("capacity.facts_per_predicate", MAELYS_DATALOG_MAX_FACTS_PER_PRED);
    STORAGE_VALUE("capacity.symbols", MAELYS_DATALOG_MAX_SYMBOLS);
    STORAGE_VALUE("capacity.symbol_text_bytes", MAELYS_DATALOG_STRING_POOL_BYTES);
    STORAGE_TYPE("session", maelys_datalog_session_t);
    STORAGE_FIELD("session", maelys_datalog_session_t, result_storage);
    STORAGE_TYPE("session_config", maelys_datalog_session_config_t);
    STORAGE_TYPE("inputs", inputs_t);
    STORAGE_FIELD("inputs", inputs_t, symbols);
    STORAGE_FIELD("inputs", inputs_t, fact_pool);
    STORAGE_FIELD("inputs", inputs_t, edb);
    STORAGE_FIELD("inputs", inputs_t, symbol_inputs);
    STORAGE_FIELD("inputs", inputs_t, fact_index);
    STORAGE_VALUE("inputs.metadata_and_padding", sizeof(inputs_t)
        - sizeof(((inputs_t *)0)->symbols) - sizeof(((inputs_t *)0)->fact_pool)
        - sizeof(((inputs_t *)0)->edb) - sizeof(((inputs_t *)0)->symbol_inputs));
    STORAGE_TYPE("edb", maelys_datalog_internal_edb_t);
    STORAGE_FIELD("edb", maelys_datalog_internal_edb_t, facts_per_pred);
    STORAGE_FIELD("edb", maelys_datalog_internal_edb_t, runtime_pair_ids_scratch);
    STORAGE_TYPE("symbols", symbols_t);
    STORAGE_FIELD("symbols", symbols_t, storage);
    STORAGE_FIELD("symbols", symbols_t, entries);
    STORAGE_FIELD("symbols", symbols_t, index);
    STORAGE_VALUE("symbols.metadata_and_padding", sizeof(symbols_t)
        - sizeof(((symbols_t *)0)->storage) - sizeof(((symbols_t *)0)->entries)
        - sizeof(((symbols_t *)0)->index));
    STORAGE_VALUE("stride.symbol_entry", sizeof(((symbols_t *)0)->entries[0]));
    STORAGE_TYPE("native_fact", maelys_datalog_internal_fact_t);
    STORAGE_TYPE("public_fact", maelys_datalog_fact_t);
    STORAGE_TYPE("program", maelys_datalog_internal_ruleset_t);
    STORAGE_TYPE("backend_payload", struct backend_payload);
    STORAGE_FIELD("backend_payload", struct backend_payload, canonical);
    STORAGE_FIELD("backend_payload", struct backend_payload, derived);
    const size_t result = maelys_inventory_solver();
    const size_t reference = sizeof(maelys_datalog_session_t) + sizeof(inputs_t) + result;
    const size_t snapshot = sizeof(struct owned_inputs) - sizeof(inputs_t);
    STORAGE_VALUE("reservation.reference_shared_policy", reference);
    STORAGE_VALUE("reservation.exporting_shared_policy", reference + sizeof(struct backend_payload));
    STORAGE_VALUE("reservation.caller_policy_snapshot_addition", snapshot);
    STORAGE_VALUE("reservation.reference_copied_policy", reference + snapshot);
    STORAGE_VALUE("reservation.exporting_copied_policy", reference + snapshot + sizeof(struct backend_payload));
    return 0;
}
