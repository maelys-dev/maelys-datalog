#include "src/core/maelys_datalog_diagnostic.h"

#include <stdio.h>
#include <string.h>

_Static_assert(offsetof(maelys_datalog_internal_diagnostic_t, limit_kind) + 1u <=
               offsetof(maelys_datalog_internal_diagnostic_t, line),
               "bound identity occupies the pre-existing alignment gap");
_Static_assert(MAELYS_DATALOG_LIMIT_MAX_FACTS_PER_PRED <= UINT8_MAX,
               "compact bound identity must fit");

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

void maelys_datalog_internal_diagnostic_clear(maelys_datalog_internal_diagnostic_t *diag) {
    if (!diag) return;
    memset(diag, 0, sizeof(*diag));
}

void maelys_datalog_internal_diagnostic_set(maelys_datalog_internal_diagnostic_t *diag,
                                   maelys_datalog_diag_code_t code,
                                   const char *phase,
                                   const char *file,
                                   size_t line,
                                   size_t column,
                                   const char *message,
                                   const char *hint) {
    if (!diag) return;
    maelys_datalog_internal_diagnostic_clear(diag);
    diag->code = code;
    diag->line = line;
    diag->column = column;
    (void)copy_bounded(diag->phase, sizeof(diag->phase), phase);
    (void)copy_bounded(diag->file, sizeof(diag->file), file);
    (void)copy_bounded(diag->message, sizeof(diag->message), message);
    (void)copy_bounded(diag->hint, sizeof(diag->hint), hint);
}

void maelys_datalog_internal_diagnostic_set_predicate(maelys_datalog_internal_diagnostic_t *diag,
                                             const char *predicate,
                                             size_t arity) {
    if (!diag) return;
    (void)copy_bounded(diag->predicate, sizeof(diag->predicate), predicate);
    diag->arity = arity;
}

void maelys_datalog_internal_diagnostic_set_limit(maelys_datalog_internal_diagnostic_t *diag,
                                         size_t count,
                                         size_t limit) {
    if (!diag) return;
    diag->count = count;
    diag->limit = limit;
}

void maelys_datalog_internal_diagnostic_set_comparison_error(maelys_datalog_internal_diagnostic_t *diag,
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
        case MAELYS_DATALOG_DIAG_PARSER_INVALID_FILTER: return "parser_invalid_filter";
        case MAELYS_DATALOG_DIAG_PARSER_EXPECTED_DOT: return "parser_expected_dot";
        case MAELYS_DATALOG_DIAG_PARSER_EXPECTED_NECK: return "parser_expected_neck";
        case MAELYS_DATALOG_DIAG_PARSER_FACT_USES_NON_BASE_PREDICATE: return "parser_fact_uses_non_base_predicate";
        case MAELYS_DATALOG_DIAG_PARSER_ANONYMOUS_VARIABLE_IN_HEAD: return "parser_anonymous_variable_in_head";
        case MAELYS_DATALOG_DIAG_PARSER_ANONYMOUS_VARIABLE_IN_COMPARISON: return "parser_anonymous_variable_in_comparison";
        case MAELYS_DATALOG_DIAG_PARSER_ANONYMOUS_VARIABLE_IN_FACT: return "parser_anonymous_variable_in_fact";
        case MAELYS_DATALOG_DIAG_PARSER_TOO_MANY_VARIABLES: return "parser_too_many_variables";
        case MAELYS_DATALOG_DIAG_POLICY_NOT_STRATIFIABLE: return "policy_not_stratifiable";
        case MAELYS_DATALOG_DIAG_RUNTIME_INVALID_COMPARISON: return "runtime_invalid_comparison";
        case MAELYS_DATALOG_DIAG_RUNTIME_INVALID_FILTER: return "runtime_invalid_filter";
        case MAELYS_DATALOG_DIAG_REGISTRY_CONFLICT: return "registry_conflict";
        case MAELYS_DATALOG_DIAG_REGISTRY_MUTATION_AFTER_FREEZE: return "registry_mutation_after_freeze";
        case MAELYS_DATALOG_DIAG_MALFORMED_PROGRAM: return "malformed_program";
        case MAELYS_DATALOG_DIAG_OPERATION_REJECTED: return "operation_rejected";
        case MAELYS_DATALOG_DIAG_SOLVE_MAX_DEPTH: return "solve_max_depth";
        case MAELYS_DATALOG_DIAG_SOLVE_IDB_OVERFLOW: return "solve_idb_overflow";
        case MAELYS_DATALOG_DIAG_SOLVE_COMPARISON_TYPE_ERROR: return "solve_comparison_type_error";
        case MAELYS_DATALOG_DIAG_SOLVE_FILTER_ERROR: return "solve_filter_error";
        case MAELYS_DATALOG_DIAG_SOLVE_MALFORMED_FACT: return "solve_malformed_fact";
        case MAELYS_DATALOG_DIAG_SOLVE_MALFORMED_EDB: return "solve_malformed_edb";
        case MAELYS_DATALOG_DIAG_SOLVE_INVALID_STATE: return "solve_invalid_state";
        case MAELYS_DATALOG_DIAG_SOLVE_INVALID_ARGUMENT: return "solve_invalid_argument";
        case MAELYS_DATALOG_DIAG_SOLVE_INTERNAL_ERROR: return "solve_internal_error";
        default: return "unknown";
    }
}

maelys_datalog_status_t maelys_datalog_diagnostic_init(void *storage, size_t bytes) {
    if (!storage || (uintptr_t)storage % _Alignof(maelys_datalog_diagnostic_t))
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (bytes < sizeof(maelys_datalog_diagnostic_t))
        return MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL;
    maelys_datalog_diagnostic_t *d = storage;
    memset(d, 0, sizeof(*d));
    d->struct_size = bytes;
    d->abi_version = MAELYS_DATALOG_DIAGNOSTIC_ABI_VERSION;
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t maelys_datalog_diagnostic_clear(maelys_datalog_diagnostic_t *d) {
    if (!d) return MAELYS_DATALOG_STATUS_OK;
    if ((uintptr_t)d % _Alignof(maelys_datalog_diagnostic_t))
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (d->struct_size < sizeof(*d)) return MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL;
    if (d->abi_version != MAELYS_DATALOG_DIAGNOSTIC_ABI_VERSION)
        return MAELYS_DATALOG_STATUS_UNSUPPORTED;
    /* Reset semantic fields, not the unused tails of owned C strings. Keep
     * absent numeric sections zero, including for callbacks and CFFI readers.
     * Initialization still zeroes the whole known object once. This is not
     * secure erasure; bytes after each first NUL remain unspecified. */
    memset((unsigned char *)d + offsetof(maelys_datalog_diagnostic_t, source), 0,
           offsetof(maelys_datalog_diagnostic_t, phase) - offsetof(maelys_datalog_diagnostic_t, source));
    memset((unsigned char *)d + offsetof(maelys_datalog_diagnostic_t, arity), 0,
           offsetof(maelys_datalog_diagnostic_t, token) - offsetof(maelys_datalog_diagnostic_t, arity));
    d->phase[0] = d->message[0] = d->hint[0] = d->file[0] = d->predicate[0] = '\0';
    d->token[0] = d->field[0] = d->domain[0] = '\0';
    return MAELYS_DATALOG_STATUS_OK;
}
