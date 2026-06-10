#include "policy/maelys_datalog_diagnostic.h"

#include <stdio.h>
#include <string.h>

/* Returns 1 when the null-terminated source fits, 0 on truncation or error.
 * Current callers pass string literals or file paths; this guards future long
 * diagnostic strings without changing fail-closed behavior.
 */
static int copy_bounded(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0) return 0;
    if (!src) src = "";
    int n = snprintf(dst, cap, "%s", src);
    return n >= 0 && (size_t)n < cap;
}

void maelys_datalog_diagnostic_clear(maelys_datalog_diagnostic_t *diag) {
    if (!diag) return;
    memset(diag, 0, sizeof(*diag));
}

void maelys_datalog_diagnostic_set(maelys_datalog_diagnostic_t *diag,
                                   maelys_datalog_diag_code_t code,
                                   const char *phase,
                                   const char *file,
                                   size_t line,
                                   size_t column,
                                   const char *message,
                                   const char *hint) {
    if (!diag) return;
    maelys_datalog_diagnostic_clear(diag);
    diag->code = code;
    diag->line = line;
    diag->column = column;
    (void)copy_bounded(diag->phase, sizeof(diag->phase), phase);
    (void)copy_bounded(diag->file, sizeof(diag->file), file);
    (void)copy_bounded(diag->message, sizeof(diag->message), message);
    (void)copy_bounded(diag->hint, sizeof(diag->hint), hint);
}

void maelys_datalog_diagnostic_set_predicate(maelys_datalog_diagnostic_t *diag,
                                             const char *predicate,
                                             size_t arity) {
    if (!diag) return;
    (void)copy_bounded(diag->predicate, sizeof(diag->predicate), predicate);
    diag->arity = arity;
}

void maelys_datalog_diagnostic_set_limit(maelys_datalog_diagnostic_t *diag,
                                         size_t count,
                                         size_t limit) {
    if (!diag) return;
    diag->count = count;
    diag->limit = limit;
}

void maelys_datalog_diagnostic_set_comparison_error(maelys_datalog_diagnostic_t *diag,
                                                    uint8_t compare_result,
                                                    uint8_t expected_kind,
                                                    uint8_t observed_lhs_kind,
                                                    uint8_t observed_rhs_kind,
                                                    uint8_t failed_op,
                                                    uint8_t term_index) {
    if (!diag) return;
    diag->code = MAELYS_DATALOG_DIAG_RUNTIME_INVALID_COMPARISON;
    diag->compare_result = compare_result;
    diag->expected_kind = expected_kind;
    diag->observed_lhs_kind = observed_lhs_kind;
    diag->observed_rhs_kind = observed_rhs_kind;
    diag->failed_op = failed_op;
    diag->term_index = term_index;
}

const char *maelys_datalog_diag_code_name(maelys_datalog_diag_code_t code) {
    switch (code) {
        case MAELYS_DATALOG_DIAG_NONE: return "none";
        case MAELYS_DATALOG_DIAG_MANIFEST_INVALID_JSON: return "manifest_invalid_json";
        case MAELYS_DATALOG_DIAG_MANIFEST_INVALID_FIELD: return "manifest_invalid_field";
        case MAELYS_DATALOG_DIAG_MANIFEST_UNKNOWN_FIELD: return "manifest_unknown_field";
        case MAELYS_DATALOG_DIAG_MANIFEST_UNKNOWN_DOMAIN: return "manifest_unknown_domain";
        case MAELYS_DATALOG_DIAG_MANIFEST_SHA_MISMATCH: return "manifest_sha_mismatch";
        case MAELYS_DATALOG_DIAG_MANIFEST_POLICY_NOT_FOUND: return "manifest_policy_not_found";
        case MAELYS_DATALOG_DIAG_MANIFEST_TEST_ONLY_REJECTED: return "manifest_test_only_rejected";
        case MAELYS_DATALOG_DIAG_LEXER_INVALID_TOKEN: return "lexer_invalid_token";
        case MAELYS_DATALOG_DIAG_LEXER_UNSUPPORTED_CONSTRUCT: return "lexer_unsupported_construct";
        case MAELYS_DATALOG_DIAG_LEXER_INVALID_UTF8: return "lexer_invalid_utf8";
        case MAELYS_DATALOG_DIAG_LEXER_STRING_TOO_LONG: return "lexer_string_too_long";
        case MAELYS_DATALOG_DIAG_PARSER_EXPECTED_PREDICATE: return "parser_expected_predicate";
        case MAELYS_DATALOG_DIAG_PARSER_UNKNOWN_PREDICATE: return "parser_unknown_predicate";
        case MAELYS_DATALOG_DIAG_PARSER_ARITY_MISMATCH: return "parser_arity_mismatch";
        case MAELYS_DATALOG_DIAG_PARSER_UNKNOWN_ATOM: return "parser_unknown_atom";
        case MAELYS_DATALOG_DIAG_PARSER_RULE_HEAD_EDB_FORBIDDEN: return "parser_rule_head_edb_forbidden";
        case MAELYS_DATALOG_DIAG_PARSER_RULE_BODY_LITERAL_OVERFLOW: return "parser_rule_body_literal_overflow";
        case MAELYS_DATALOG_DIAG_PARSER_UNSAFE_VARIABLE: return "parser_unsafe_variable";
        case MAELYS_DATALOG_DIAG_PARSER_INVALID_COMPARISON: return "parser_invalid_comparison";
        case MAELYS_DATALOG_DIAG_PARSER_EXPECTED_DOT: return "parser_expected_dot";
        case MAELYS_DATALOG_DIAG_PARSER_EXPECTED_NECK: return "parser_expected_neck";
        case MAELYS_DATALOG_DIAG_PARSER_FACT_USES_NON_BASE_PREDICATE: return "parser_fact_uses_non_base_predicate";
        case MAELYS_DATALOG_DIAG_PARSER_ANONYMOUS_VARIABLE_IN_HEAD: return "parser_anonymous_variable_in_head";
        case MAELYS_DATALOG_DIAG_PARSER_ANONYMOUS_VARIABLE_IN_COMPARISON: return "parser_anonymous_variable_in_comparison";
        case MAELYS_DATALOG_DIAG_PARSER_ANONYMOUS_VARIABLE_IN_FACT: return "parser_anonymous_variable_in_fact";
        case MAELYS_DATALOG_DIAG_PARSER_TOO_MANY_VARIABLES: return "parser_too_many_variables";
        case MAELYS_DATALOG_DIAG_POLICY_NOT_STRATIFIABLE: return "policy_not_stratifiable";
        case MAELYS_DATALOG_DIAG_RUNTIME_INVALID_COMPARISON: return "runtime_invalid_comparison";
        case MAELYS_DATALOG_DIAG_REGISTRY_CONFLICT: return "registry_conflict";
        case MAELYS_DATALOG_DIAG_REGISTRY_MUTATION_AFTER_FREEZE: return "registry_mutation_after_freeze";
        default: return "unknown";
    }
}
