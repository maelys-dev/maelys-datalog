#include <maelys/datalog.h>

#if defined(MAELYS_DATALOG_BACKEND_H) || defined(MAELYS_DATALOG_PROGRAM_H) || defined(MAELYS_DATALOG_MODULE_H)
#error "The consumer facade must not pull in extension or IR headers"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int require_status(maelys_datalog_status_t status) {
    if (status == MAELYS_DATALOG_STATUS_OK) return 1;
    fprintf(stderr, "public API error: %s\n", maelys_datalog_status_name(status));
    return 0;
}

int main(void) {
    size_t max_arity = 0u;
    if (!require_status(maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_ARITY, &max_arity)) ||
        max_arity != MAELYS_DATALOG_PUBLIC_MAX_TERMS) return 16;
    static const maelys_datalog_public_predicate_t predicates[] = {
        {"observed", 1u, MAELYS_DATALOG_PREDICATE_EDB},
        {"allow", 1u, MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY},
    };
    /* No domain atoms and no policy constants: "alice" below exists only in
     * the solved EDB, so the symbol lookup at exit 9 passes only when the
     * result reads its working symbol table, not the prepared policy's. Keep
     * it that way; this is what makes the result-scoped authority observable. */
    const maelys_datalog_public_domain_t domain = {
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
    if (!require_status(maelys_datalog_session_create_configured(policy, 0u, config, &session))) return 4;
    if (!require_status(maelys_datalog_session_config_free(config))) return 20;
    if (!require_status(maelys_datalog_session_execution_fingerprint(session, fingerprint)) ||
        strlen(fingerprint) != 64u) return 21;
    if (!require_status(maelys_datalog_policy_free(policy))) return 5;

    maelys_datalog_public_fact_t fact;
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
    if (!require_status(maelys_datalog_input_edb_add_fact(
            edb, fact.predicate, fact.terms, fact.arity, NULL))) return 23;
    if (!require_status(maelys_datalog_session_solve_edb(
            session, edb, &result, NULL))) return 6;
    if (!require_status(maelys_datalog_input_edb_free(edb))) return 24;

    int present = 0;
    size_t derived = 0u;
    if (!require_status(maelys_datalog_result_derived_fact_count(result, &derived)) || derived != 1u) return 17;
    if (!require_status(maelys_datalog_result_query(
            result, "allow", fact.terms, 1u, &present)) || !present) return 7;
    maelys_datalog_public_fact_view_t view;
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
    if (!require_status(maelys_datalog_result_free(result))) return 14;
    if (!require_status(maelys_datalog_session_free(session))) return 15;
    puts("public-api-consumer: PASS");
    return 0;
}
