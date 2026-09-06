#pragma once
#ifndef MAELYS_DATALOG_PUBLIC_H
#define MAELYS_DATALOG_PUBLIC_H

/* Stable load diagnostic codes, shared with the legacy API. Append only. */
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
    MAELYS_DATALOG_DIAG_PARSER_INVALID_FILTER,
    MAELYS_DATALOG_DIAG_PARSER_EXPECTED_DOT,
    MAELYS_DATALOG_DIAG_PARSER_EXPECTED_NECK,
    MAELYS_DATALOG_DIAG_PARSER_FACT_USES_NON_BASE_PREDICATE,
    MAELYS_DATALOG_DIAG_PARSER_ANONYMOUS_VARIABLE_IN_HEAD,
    MAELYS_DATALOG_DIAG_PARSER_ANONYMOUS_VARIABLE_IN_COMPARISON,
    MAELYS_DATALOG_DIAG_PARSER_ANONYMOUS_VARIABLE_IN_FACT,
    MAELYS_DATALOG_DIAG_PARSER_TOO_MANY_VARIABLES,
    MAELYS_DATALOG_DIAG_POLICY_NOT_STRATIFIABLE,
    MAELYS_DATALOG_DIAG_RUNTIME_INVALID_COMPARISON,
    MAELYS_DATALOG_DIAG_RUNTIME_INVALID_FILTER,
    MAELYS_DATALOG_DIAG_REGISTRY_CONFLICT,
    MAELYS_DATALOG_DIAG_REGISTRY_MUTATION_AFTER_FREEZE,
    MAELYS_DATALOG_DIAG_MALFORMED_PROGRAM
} maelys_datalog_diag_code_t;

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && defined(MAELYS_DATALOG_SHARED)
#  if defined(MAELYS_DATALOG_BUILDING_LIBRARY)
#    define MAELYS_DATALOG_API __declspec(dllexport)
#  else
#    define MAELYS_DATALOG_API __declspec(dllimport)
#  endif
#else
#  define MAELYS_DATALOG_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define MAELYS_DATALOG_PUBLIC_API_VERSION 1u
#define MAELYS_DATALOG_PUBLIC_MAX_TERMS 4u
#define MAELYS_DATALOG_PUBLIC_FINGERPRINT_BYTES 65u
#define MAELYS_DATALOG_PUBLIC_ALLOW_TEST_ONLY 1u
#define MAELYS_DATALOG_PUBLIC_ALLOW_UNDECLARED_POLICY_ATOMS (1u << 1)
#define MAELYS_DATALOG_PUBLIC_MAX_POLICY_ATOMS 256u
#define MAELYS_DATALOG_PUBLIC_MAX_POLICY_ATOM_BYTES 63u

typedef enum {
    MAELYS_DATALOG_STATUS_OK = 0,
    MAELYS_DATALOG_STATUS_INVALID_ARGUMENT = -1,
    MAELYS_DATALOG_STATUS_INVALID_FIELD = -2,
    MAELYS_DATALOG_STATUS_NOT_FOUND = -3,
    MAELYS_DATALOG_STATUS_NOT_IMPLEMENTED = -4,
    MAELYS_DATALOG_STATUS_UNSUPPORTED = -5,
    MAELYS_DATALOG_STATUS_TIMEOUT = -6,
    MAELYS_DATALOG_STATUS_IO = -7,
    MAELYS_DATALOG_STATUS_INTERNAL = -8,
    MAELYS_DATALOG_STATUS_UNAUTHORIZED = -9,
    MAELYS_DATALOG_STATUS_FORBIDDEN = -10,
    MAELYS_DATALOG_STATUS_RATE_LIMITED = -11,
    MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE = -12,
    MAELYS_DATALOG_STATUS_INVALID_STATE = -13
} maelys_datalog_status_t;

typedef enum {
    MAELYS_DATALOG_VALUE_SYMBOL = 1,
    MAELYS_DATALOG_VALUE_INTEGER = 2,
    MAELYS_DATALOG_VALUE_BOOLEAN = 3
} maelys_datalog_value_kind_t;

typedef enum {
    MAELYS_DATALOG_PREDICATE_EDB = 1u << 0,
    MAELYS_DATALOG_PREDICATE_IDB = 1u << 1,
    MAELYS_DATALOG_PREDICATE_QUERY = 1u << 2,
    MAELYS_DATALOG_PREDICATE_POLICY_FACT = 1u << 3
} maelys_datalog_predicate_flags_t;

typedef enum {
    MAELYS_DATALOG_DIAGNOSTIC_NONE = 0,
    MAELYS_DATALOG_DIAGNOSTIC_LOAD = 1,
    MAELYS_DATALOG_DIAGNOSTIC_SOLVE = 2
} maelys_datalog_diagnostic_source_t;

typedef struct {
    maelys_datalog_diagnostic_source_t source;
    int code;
    size_t line;
    size_t column;
    char phase[32];
    char message[256];
    char hint[256];
} maelys_datalog_public_diagnostic_t;

typedef struct {
    const char *name;
    size_t arity;
    unsigned flags;
} maelys_datalog_public_predicate_t;

typedef struct {
    const char *name;
    const maelys_datalog_public_predicate_t *predicates;
    size_t predicate_count;
    const char *const *atoms;
    size_t atom_count;
} maelys_datalog_public_domain_t;

typedef struct {
    maelys_datalog_value_kind_t kind;
    union {
        const char *symbol;
        int64_t integer;
        int boolean;
    } as;
} maelys_datalog_public_value_t;

typedef struct {
    const char *predicate;
    size_t arity;
    maelys_datalog_public_value_t terms[MAELYS_DATALOG_PUBLIC_MAX_TERMS];
} maelys_datalog_public_fact_t;

typedef struct {
    maelys_datalog_value_kind_t kind;
    union {
        uint32_t symbol_id;
        int64_t integer;
        int boolean;
    } as;
} maelys_datalog_public_term_view_t;

typedef struct {
    size_t arity;
    maelys_datalog_public_term_view_t terms[MAELYS_DATALOG_PUBLIC_MAX_TERMS];
} maelys_datalog_public_fact_view_t;

typedef struct maelys_datalog_policy maelys_datalog_policy_t;
typedef struct maelys_datalog_session maelys_datalog_session_t;
typedef struct maelys_datalog_result maelys_datalog_result_t;

MAELYS_DATALOG_API const char *maelys_datalog_status_name(
    maelys_datalog_status_t status);
MAELYS_DATALOG_API void maelys_datalog_public_diagnostic_clear(
    maelys_datalog_public_diagnostic_t *diagnostic);

MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_domain_register(
    const maelys_datalog_public_domain_t *domain);

MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_policy_load_inline(
    const char *domain,
    const char *policy_id,
    const char *source,
    size_t source_length,
    maelys_datalog_policy_t **out_policy,
    maelys_datalog_public_diagnostic_t *out_diagnostic);

MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_policy_load_manifest(
    const char *manifest_path,
    unsigned flags,
    maelys_datalog_policy_t **out_policy,
    maelys_datalog_public_diagnostic_t *out_diagnostic);

MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_policy_count(
    const maelys_datalog_policy_t *policy,
    size_t *out_count);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_policy_fingerprint(
    const maelys_datalog_policy_t *policy,
    char out_fingerprint[MAELYS_DATALOG_PUBLIC_FINGERPRINT_BYTES]);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_policy_free(
    maelys_datalog_policy_t *policy);

MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_session_create(
    const maelys_datalog_policy_t *policy,
    size_t policy_index,
    maelys_datalog_session_t **out_session);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_session_fingerprint(
    const maelys_datalog_session_t *session,
    char out_fingerprint[MAELYS_DATALOG_PUBLIC_FINGERPRINT_BYTES]);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_session_solve(
    maelys_datalog_session_t *session,
    const maelys_datalog_public_fact_t *facts,
    size_t fact_count,
    maelys_datalog_result_t **out_result,
    maelys_datalog_public_diagnostic_t *out_diagnostic);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_session_free(
    maelys_datalog_session_t *session);

MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_result_query(
    const maelys_datalog_result_t *result,
    const char *predicate,
    const maelys_datalog_public_value_t *terms,
    size_t arity,
    int *out_present);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_result_enumerate(
    const maelys_datalog_result_t *result,
    const char *predicate,
    size_t arity,
    maelys_datalog_public_fact_view_t *out_facts,
    size_t out_capacity,
    size_t *out_count);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_result_symbol_text(
    const maelys_datalog_result_t *result,
    uint32_t symbol_id,
    const char **out_text,
    size_t *out_length);
/* Read-only explanation of retained state, never a re-solve. Count-only:
 * NULL text, zero capacity. Required size excludes NUL. A short buffer returns
 * PAYLOAD_TOO_LARGE and exact required size; when capacity > 0 only text[0] is
 * set to NUL. Missing backend capability returns UNSUPPORTED, outputs untouched.
 * A query symbol absent from the session vocabulary returns NOT_FOUND without
 * interning it. Text may include sensitive policy/input values. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_result_explain_true_text(
    const maelys_datalog_result_t *result,
    const char *predicate,
    const maelys_datalog_public_value_t *terms,
    size_t arity,
    char *out_text,
    size_t out_capacity,
    size_t *out_required);
/* Why-false uses the same buffer contract. Reference diagnostics are bounded:
 * their text distinguishes complete, truncated and not-applicable (fact present).
 * Truncated diagnostic text is not a proof of exhaustive non-derivability. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_result_explain_false_text(
    const maelys_datalog_result_t *result,
    const char *predicate,
    const maelys_datalog_public_value_t *terms,
    size_t arity,
    char *out_text,
    size_t out_capacity,
    size_t *out_required);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_result_free(
    maelys_datalog_result_t *result);

#ifdef __cplusplus
}
#endif

#endif
