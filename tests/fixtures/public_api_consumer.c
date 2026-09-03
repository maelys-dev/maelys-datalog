#include <maelys/datalog.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int require_status(maelys_datalog_status_t status) {
    if (status == MAELYS_DATALOG_STATUS_OK) return 1;
    fprintf(stderr, "public API error: %s\n", maelys_datalog_status_name(status));
    return 0;
}

int main(void) {
    static const maelys_datalog_public_predicate_t predicates[] = {
        {"observed", 1u, MAELYS_DATALOG_PREDICATE_EDB},
        {"allow", 1u, MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY},
    };
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
    if (!require_status(maelys_datalog_session_create(policy, 0u, &session))) return 4;
    if (!require_status(maelys_datalog_policy_free(policy))) return 5;

    maelys_datalog_public_fact_t fact;
    memset(&fact, 0, sizeof(fact));
    fact.predicate = "observed";
    fact.arity = 1u;
    fact.terms[0].kind = MAELYS_DATALOG_VALUE_SYMBOL;
    fact.terms[0].as.symbol = "alice";
    maelys_datalog_result_t *result = NULL;
    if (!require_status(maelys_datalog_session_solve(
            session, &fact, 1u, &result, NULL))) return 6;

    int present = 0;
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
