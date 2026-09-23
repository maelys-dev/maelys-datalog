#include <maelys/datalog.h>

#if defined(MAELYS_DATALOG_BACKEND_H) || defined(MAELYS_DATALOG_PROGRAM_H) || defined(MAELYS_DATALOG_MODULE_H)
#error "The consumer facade must not pull in extension or IR headers"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Application-owned static budget, checked against the library at startup. */
MAELYS_DATALOG_EXPLANATION_STORAGE(explanation_storage, 1024u * 1024u);

static int require_status(maelys_datalog_status_t status) {
    if (status == MAELYS_DATALOG_STATUS_OK) return 1;
    fprintf(stderr, "public API error: %s\n", maelys_datalog_status_name(status));
    return 0;
}

int main(void) {
    size_t max_arity = 0u;
    if (!require_status(maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_ARITY, &max_arity)) ||
        max_arity != MAELYS_DATALOG_PUBLIC_MAX_TERMS) return 16;
    static const maelys_datalog_predicate_t predicates[] = {
        MAELYS_DATALOG_EDB("observed", 1),
        MAELYS_DATALOG_IDB_QUERY("allow", 1),
    };
    /* No domain atoms and no policy constants: "alice" below exists only in
     * the solved EDB, so the symbol lookup at exit 9 passes only when the
     * result reads its working symbol table, not the prepared policy's. Keep
     * it that way; this is what makes the result-scoped authority observable. */
    const maelys_datalog_domain_t domain = {
        "installed_consumer", predicates, 2u, NULL, 0u,
    };
    if (!require_status(maelys_datalog_domain_register(&domain))) return 1;

    const char source[] = "allow(X) :- observed(X).\n";
    maelys_datalog_policy_t *policy = NULL;
    if (!require_status(maelys_datalog_policy_load_inline(
            domain.name, "installed", source, strlen(source), &policy, NULL))) return 2;
    char fingerprint[MAELYS_DATALOG_PUBLIC_FINGERPRINT_BYTES];
    if (!require_status(maelys_datalog_policy_fingerprint(policy, fingerprint))) return 3;

    maelys_datalog_session_t *session = NULL;
    maelys_datalog_session_config_t *config = NULL;
    if (!require_status(maelys_datalog_session_config_create(&config))) return 18;
    if (!require_status(maelys_datalog_session_config_set_required_capabilities(
            config, MAELYS_DATALOG_CAP_EXPLAIN_TRUE | MAELYS_DATALOG_CAP_EXPLAIN_FALSE))) return 19;
    if (!require_status(maelys_datalog_session_config_set_explanation_workspace(
            config, MAELYS_DATALOG_EXPLAIN_TRUE | MAELYS_DATALOG_EXPLAIN_FALSE))) return 38;
    if (!require_status(maelys_datalog_session_create_configured(policy, 0u, config, &session))) return 4;
    for (int k = 1; k <= 2; ++k) {
        size_t bound = 0, alignment = 0;
        if (!require_status(maelys_datalog_session_explanation_storage_bound(session,
                (maelys_datalog_explanation_kind_t)k, &bound, &alignment))) return 34;
        if (bound > sizeof(explanation_storage) || (uintptr_t)explanation_storage % alignment) return 35;
    }
    /* Exercise the borrowed mode and new status from the installed SDK too.
     * Release this temporary session before reusing its storage below. */
    maelys_datalog_session_t *borrowed = NULL;
    if (!require_status(maelys_datalog_session_config_set_explanation_storage(
            config, MAELYS_DATALOG_EXPLAIN_TRUE | MAELYS_DATALOG_EXPLAIN_FALSE,
            explanation_storage, 0u))) return 39;
    if (maelys_datalog_session_create_configured(policy, 0u, config, &borrowed) !=
            MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL || borrowed != NULL) return 40;
    if (!require_status(maelys_datalog_session_config_set_explanation_storage(
            config, MAELYS_DATALOG_EXPLAIN_TRUE | MAELYS_DATALOG_EXPLAIN_FALSE,
            explanation_storage, sizeof(explanation_storage)))) return 41;
    if (!require_status(maelys_datalog_session_create_configured(policy, 0u, config, &borrowed))) return 42;
    if (!require_status(maelys_datalog_session_free(borrowed))) return 43;
    if (!require_status(maelys_datalog_session_config_free(config))) return 20;
    if (!require_status(maelys_datalog_session_execution_fingerprint(session, fingerprint)) ||
        strlen(fingerprint) != 64u) return 21;
    if (!require_status(maelys_datalog_policy_free(policy))) return 5;

    maelys_datalog_fact_t fact;
    memset(&fact, 0, sizeof(fact));
    fact.predicate = "observed";
    fact.arity = 1u;
    fact.terms[0].kind = MAELYS_DATALOG_VALUE_SYMBOL;
    fact.terms[0].as.symbol = "alice";
    maelys_datalog_result_t *result = NULL;
    maelys_datalog_input_edb_t *edb = NULL;
    /* Caller-owned storage: no private layout in C11 or C++17 consumers. */
    static union { max_align_t alignment; unsigned char bytes[4096]; } input_storage;
    size_t input_bytes = 0, input_alignment = 0;
    if (!require_status(maelys_datalog_input_edb_storage_requirements(
            4u, 128u, &input_bytes, &input_alignment))) return 22;
    if (input_bytes > sizeof(input_storage.bytes) ||
        (uintptr_t)input_storage.bytes % input_alignment) return 22;
    if (!require_status(maelys_datalog_input_edb_init(
            input_storage.bytes, sizeof(input_storage.bytes), 4u, 128u, &edb))) return 22;
#ifdef __cplusplus
    if (!require_status(maelys_datalog_input_edb_add_fact(
            edb, fact.predicate, fact.terms, fact.arity, NULL))) return 23;
#else
    /* The installed C11 header must provide the convenience layer without any
     * additional source-tree header or new library entry point. */
    if (!require_status(MAELYS_DATALOG_ADD_FACT(edb, NULL, "observed", "alice"))) return 23;
#endif
    if (!require_status(maelys_datalog_session_solve_edb(
            session, edb, &result, NULL))) return 6;
    if (!require_status(maelys_datalog_input_edb_free(edb))) return 24;

    int present = 0;
    size_t derived = 0u;
    if (!require_status(maelys_datalog_result_derived_fact_count(result, &derived)) || derived != 1u) return 17;
    if (!require_status(maelys_datalog_result_query(
            result, "allow", fact.terms, 1u, &present)) || !present) return 7;
    const maelys_datalog_value_t query[] = {MAELYS_DATALOG_SYMBOL("alice")};
    if (!require_status(maelys_datalog_result_query(
            result, "allow", query, 1u, &present)) || !present) return 7;
#ifndef __cplusplus
    if (!require_status(MAELYS_DATALOG_QUERY(result, &present, "allow", "alice")) || !present) return 7;
#endif
    maelys_datalog_fact_view_t view;
    size_t count = 0u;
    if (!require_status(maelys_datalog_result_enumerate(
            result, "allow", 1u, &view, 1u, &count)) || count != 1u) return 8;
    const char *text = NULL;
    size_t text_length = 0u;
    if (!require_status(maelys_datalog_result_symbol_text(
            result, view.terms[0].as.symbol_id, &text, &text_length)) ||
        text_length != 5u || memcmp(text, "alice", 5u) != 0) return 9;
    size_t required = 0u;
    if (!require_status(maelys_datalog_result_explain_true_text(
            result, "allow", fact.terms, 1u, NULL, 0u, &required)) || required == 0u) return 10;
    char *explanation = calloc(required + 1u, 1u);
    if (!explanation) return 11;
    if (!require_status(maelys_datalog_result_explain_true_text(
            result, "allow", fact.terms, 1u, explanation, required + 1u, &required))) return 12;
    if (!strstr(explanation, "document=why-true")) return 13;
    free(explanation);
    /* Fixed application budget, not a guess about a private native layout.
     * Query requirements and refuse if this caller-owned arena is too small. */
    static union { max_align_t alignment; unsigned char bytes[256u * 1024u]; } arena;
    size_t storage_bytes = 0u, storage_alignment = 0u;
    if (!require_status(maelys_datalog_result_explanation_storage_requirements(
            result, MAELYS_DATALOG_EXPLAIN_TRUE, &storage_bytes, &storage_alignment))) return 25;
    if (storage_bytes > sizeof(arena.bytes) || (uintptr_t)arena.bytes % storage_alignment) return 26;
    maelys_datalog_prepared_explanation_t *prepared = NULL;
    if (!require_status(maelys_datalog_result_prepare_explanation(
            result, MAELYS_DATALOG_EXPLAIN_TRUE, "allow", fact.terms, 1u,
            arena.bytes, sizeof(arena.bytes), &prepared))) return 27;
    if (!require_status(maelys_datalog_prepared_explanation_text_size(prepared, &required))) return 28;
    char rendered[8192];
    if (required >= sizeof(rendered)) return 29;
    if (!require_status(maelys_datalog_prepared_explanation_write_text(
            prepared, rendered, sizeof(rendered)))) return 30;
    if (!strstr(rendered, "document=why-true")) return 31;
    if (maelys_datalog_result_free(result) != MAELYS_DATALOG_STATUS_INVALID_STATE) return 32;
    if (!require_status(maelys_datalog_prepared_explanation_release(prepared))) return 33;
    for (int k = 1; k <= 2; ++k) {
        if (!require_status(maelys_datalog_result_explain_text_in(result,
                (maelys_datalog_explanation_kind_t)k, "allow", fact.terms, 1u,
                explanation_storage, sizeof(explanation_storage), rendered, sizeof(rendered), &required))) return 36;
        if (strlen(rendered) != required) return 37;
    }
    if (!require_status(maelys_datalog_result_free(result))) return 14;
    if (!require_status(maelys_datalog_session_free(session))) return 15;
    puts("public-api-consumer: PASS");
    return 0;
}
