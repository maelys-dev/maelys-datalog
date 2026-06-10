#pragma once
#ifndef MAELYS_DATALOG_DIAGNOSTIC_H
#define MAELYS_DATALOG_DIAGNOSTIC_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    MAELYS_DATALOG_DIAG_NONE = 0,
    MAELYS_DATALOG_DIAG_MANIFEST_INVALID_JSON,
    MAELYS_DATALOG_DIAG_MANIFEST_INVALID_FIELD,
    MAELYS_DATALOG_DIAG_MANIFEST_UNKNOWN_FIELD,
    MAELYS_DATALOG_DIAG_MANIFEST_UNKNOWN_DOMAIN,
    MAELYS_DATALOG_DIAG_MANIFEST_SHA_MISMATCH,
    MAELYS_DATALOG_DIAG_MANIFEST_POLICY_NOT_FOUND,
    MAELYS_DATALOG_DIAG_MANIFEST_TEST_ONLY_REJECTED,
    MAELYS_DATALOG_DIAG_LEXER_INVALID_TOKEN,
    MAELYS_DATALOG_DIAG_LEXER_UNSUPPORTED_CONSTRUCT,
    MAELYS_DATALOG_DIAG_LEXER_INVALID_UTF8,
    MAELYS_DATALOG_DIAG_LEXER_STRING_TOO_LONG,
    MAELYS_DATALOG_DIAG_PARSER_EXPECTED_PREDICATE,
    MAELYS_DATALOG_DIAG_PARSER_UNKNOWN_PREDICATE,
    MAELYS_DATALOG_DIAG_PARSER_ARITY_MISMATCH,
    MAELYS_DATALOG_DIAG_PARSER_UNKNOWN_ATOM,
    MAELYS_DATALOG_DIAG_PARSER_RULE_HEAD_EDB_FORBIDDEN,
    MAELYS_DATALOG_DIAG_PARSER_RULE_BODY_LITERAL_OVERFLOW,
    MAELYS_DATALOG_DIAG_PARSER_UNSAFE_VARIABLE,
    MAELYS_DATALOG_DIAG_PARSER_INVALID_COMPARISON,
    MAELYS_DATALOG_DIAG_PARSER_EXPECTED_DOT,
    MAELYS_DATALOG_DIAG_PARSER_EXPECTED_NECK,
    MAELYS_DATALOG_DIAG_PARSER_FACT_USES_NON_BASE_PREDICATE,
    MAELYS_DATALOG_DIAG_PARSER_ANONYMOUS_VARIABLE_IN_HEAD,
    MAELYS_DATALOG_DIAG_PARSER_ANONYMOUS_VARIABLE_IN_COMPARISON,
    MAELYS_DATALOG_DIAG_PARSER_ANONYMOUS_VARIABLE_IN_FACT,
    MAELYS_DATALOG_DIAG_PARSER_TOO_MANY_VARIABLES,
    MAELYS_DATALOG_DIAG_POLICY_NOT_STRATIFIABLE,
    MAELYS_DATALOG_DIAG_RUNTIME_INVALID_COMPARISON,
    MAELYS_DATALOG_DIAG_REGISTRY_CONFLICT,
    MAELYS_DATALOG_DIAG_REGISTRY_MUTATION_AFTER_FREEZE
} maelys_datalog_diag_code_t;

typedef struct {
    maelys_datalog_diag_code_t code;
    uint8_t compare_result;
    uint8_t expected_kind;
    uint8_t observed_lhs_kind;
    uint8_t observed_rhs_kind;
    uint8_t failed_op;
    uint8_t term_index;
    uint8_t expected_arity;
    uint8_t observed_arity;
    uint16_t predicate_id;
    char phase[32];
    char file[256];
    size_t line;
    size_t column;
    char predicate[96];
    size_t arity;
    char token[96];
    char field[96];
    char domain[96];
    size_t count;
    size_t limit;
    char message[256];
    char hint[256];
} maelys_datalog_diagnostic_t;

void maelys_datalog_diagnostic_clear(maelys_datalog_diagnostic_t *diag);
void maelys_datalog_diagnostic_set(maelys_datalog_diagnostic_t *diag,
                                   maelys_datalog_diag_code_t code,
                                   const char *phase,
                                   const char *file,
                                   size_t line,
                                   size_t column,
                                   const char *message,
                                   const char *hint);
void maelys_datalog_diagnostic_set_predicate(maelys_datalog_diagnostic_t *diag,
                                             const char *predicate,
                                             size_t arity);
void maelys_datalog_diagnostic_set_limit(maelys_datalog_diagnostic_t *diag,
                                         size_t count,
                                         size_t limit);
void maelys_datalog_diagnostic_set_comparison_error(maelys_datalog_diagnostic_t *diag,
                                                    uint8_t compare_result,
                                                    uint8_t expected_kind,
                                                    uint8_t observed_lhs_kind,
                                                    uint8_t observed_rhs_kind,
                                                    uint8_t failed_op,
                                                    uint8_t term_index);
const char *maelys_datalog_diag_code_name(maelys_datalog_diag_code_t code);

#endif
