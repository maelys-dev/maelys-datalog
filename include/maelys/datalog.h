#pragma once
#ifndef MAELYS_DATALOG_PUBLIC_H
#define MAELYS_DATALOG_PUBLIC_H

#include <stddef.h>
#include <stdint.h>

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

/* Execution requirements shared by consumers and extension authors. Language
 * requirements are computed by the engine; these bits may add requirements,
 * never remove them. Values are stable and append-only. */
#define MAELYS_DATALOG_CAP_POSITIVE (UINT64_C(1) << 0)
#define MAELYS_DATALOG_CAP_NEGATION (UINT64_C(1) << 1)
#define MAELYS_DATALOG_CAP_COMPARISONS (UINT64_C(1) << 2)
#define MAELYS_DATALOG_CAP_ARITHMETIC (UINT64_C(1) << 3)
#define MAELYS_DATALOG_CAP_FILTERS (UINT64_C(1) << 4)
#define MAELYS_DATALOG_CAP_EXPLAIN_TRUE (UINT64_C(1) << 5)
#define MAELYS_DATALOG_CAP_WORK_LIMIT (UINT64_C(1) << 6)
#define MAELYS_DATALOG_CAP_EXPLAIN_FALSE (UINT64_C(1) << 7)
#define MAELYS_DATALOG_CAP_LANGUAGE (UINT64_C(31))
#define MAELYS_DATALOG_CAP_ALL (UINT64_C(255))

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
typedef struct maelys_datalog_session_config maelys_datalog_session_config_t;
typedef struct maelys_datalog_input_edb maelys_datalog_input_edb_t;

/* Append-only identifiers for capacities of the loaded library, not current
 * occupancy. Scalar queries keep this surface extensible without struct growth. */
typedef enum {
    MAELYS_DATALOG_LIMIT_MAX_SYMBOLS = 1,
    MAELYS_DATALOG_LIMIT_STRING_POOL_BYTES,
    MAELYS_DATALOG_LIMIT_MAX_PREDICATES,
    MAELYS_DATALOG_LIMIT_MAX_RULES,
    MAELYS_DATALOG_LIMIT_MAX_ARITY,
    MAELYS_DATALOG_LIMIT_MAX_BODY_LITERALS,
    MAELYS_DATALOG_LIMIT_MAX_DEPTH,
    MAELYS_DATALOG_LIMIT_MAX_EDB_FACTS,
    MAELYS_DATALOG_LIMIT_MAX_IDB_FACTS,
    MAELYS_DATALOG_LIMIT_MAX_FACTS_PER_PRED,
    MAELYS_DATALOG_LIMIT_MAX_STRING_BYTES,
    MAELYS_DATALOG_LIMIT_INPUT_EDB_TEXT_BYTES
} maelys_datalog_limit_t;

/* Unknown identifiers return UNSUPPORTED; NULL out_value returns
 * INVALID_ARGUMENT. On failure the output is unchanged. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_limit_get(
    maelys_datalog_limit_t limit, size_t *out_value);

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
/* Consumer configuration, independent of backend descriptors/ABI. Creation
 * defaults to no additional capabilities and work_limit=0. On failure create
 * clears a non-NULL output. Setters replace the previous value; unknown bits
 * return INVALID_ARGUMENT without mutation. Getters require both pointers and
 * leave output unchanged on failure. Single-threaded mutation; callers must
 * synchronize concurrent access. Free accepts NULL, but not an already freed
 * non-NULL handle. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_session_config_create(
    maelys_datalog_session_config_t **out_config);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_session_config_set_required_capabilities(
    maelys_datalog_session_config_t *config, uint64_t capabilities);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_session_config_get_required_capabilities(
    const maelys_datalog_session_config_t *config, uint64_t *out_capabilities);
/* Zero selects the native default setting. A nonzero limit requires WORK_LIMIT
 * from the backend. The reference backend currently refuses it as UNSUPPORTED.
 * This is cooperative work accounting, never a wall-clock deadline. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_session_config_set_work_limit(
    maelys_datalog_session_config_t *config, uint64_t work_limit);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_session_config_get_work_limit(
    const maelys_datalog_session_config_t *config, uint64_t *out_work_limit);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_session_config_free(
    maelys_datalog_session_config_t *config);
/* Selects the reference backend explicitly, never a fallback. NULL config is
 * equivalent to session_create. Values are copied during this call: config may
 * be changed, reused or freed immediately afterward, without affecting existing
 * sessions. Unsatisfied capabilities return UNSUPPORTED, never weaker execution.
 * On any failure a non-NULL out_session is cleared. Custom backend descriptors
 * remain in datalog_backend.h (session_create_ex). */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_session_create_configured(
    const maelys_datalog_policy_t *policy, size_t policy_index,
    const maelys_datalog_session_config_t *config,
    maelys_datalog_session_t **out_session);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_session_fingerprint(
    const maelys_datalog_session_t *session,
    char out_fingerprint[MAELYS_DATALOG_PUBLIC_FINGERPRINT_BYTES]);
/* Unlike session_fingerprint (policy authority), this identity also binds the
 * backend identity, execution options and size profile; not runtime inputs.
 * Both output buffers remain unchanged on failure. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_session_execution_fingerprint(
    const maelys_datalog_session_t *session,
    char out_fingerprint[MAELYS_DATALOG_PUBLIC_FINGERPRINT_BYTES]);
/* All facts are supplied in one batch; no result is published on failure.
 * Input errors use phase="input" and a descriptive message containing the
 * zero-based fact/term index when attributable. Aggregate capacity errors name
 * the bound. Message prose is diagnostic, not a machine-readable grammar;
 * line/column remain source coordinates and are not repurposed as fact indices.
 * Inputs are borrowed only for this call. A failed input can be retried with a
 * corrected batch on the same session. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_session_solve(
    maelys_datalog_session_t *session,
    const maelys_datalog_public_fact_t *facts,
    size_t fact_count,
    maelys_datalog_result_t **out_result,
    maelys_datalog_public_diagnostic_t *out_diagnostic);
/* Thread-confined input buffer; independent of policies and sessions.
 * Storage requirements are queried from the loaded library, not hardcoded.
 * fact_capacity must be 1..MAX_EDB_FACTS; text_capacity is 0..INPUT_EDB_TEXT_BYTES.
 * Distinct predicate/symbol strings share one text arena, including one NUL per
 * distinct byte string. Repeated strings share storage across facts and roles.
 * Requirements outputs are unchanged on failure; init/create outputs become
 * NULL on failure. init requires the returned alignment and at least the
 * returned size. Caller storage must remain alive, unmoved and exclusively
 * owned by the EDB until free. It must not overlap inputs, outputs or diagnostics.
 * init never allocates. add_fact/add_facts/count/clear never allocate in either
 * storage mode. free releases memory only for a create-owned buffer; after free
 * the handle is invalid, but caller-owned storage can be reused/reinitialized.
 * clear resets usage without wiping bytes; it is not a secure erase.
 * create_with_capacity makes ONE allocation at construction, never grows it.
 * create reserves MAX_EDB_FACTS entries and INPUT_EDB_TEXT_BYTES of text. Query
 * both limits from the library. Text defaults to the native symbol-pool budget
 * plus registry-name storage (currently 32 KiB + 8 KiB, in both profiles).
 * Smaller explicit capacities may be used; exhaustion never grows the arena.
 *
 * add_fact/add_facts copy predicate names and symbol bytes before returning;
 * integer/boolean values are copied by value (nonzero booleans become 1).
 * Each append validates the entire batch before writing; failures consume no
 * entries or text bytes. Inputs must remain unchanged until return and must
 * not alias EDB storage. Empty batches are OK. There is no heap fallback.
 * MAX_EDB_FACTS bounds entries BEFORE deduplication; MAX_STRING_BYTES bounds
 * each predicate name and symbol, excluding NUL; MAX_ARITY bounds each fact.
 * Shape and these storage bounds are checked on insertion. Domain membership,
 * declared arity, symbol-pool and per-predicate capacities are checked at solve.
 * Storage interning does not assign result symbol IDs: domain validation and
 * canonical symbol-ID assignment remain solve-time operations. count returns
 * stored entries, not distinct facts. clear retains the handle; free accepts
 * NULL. Neither operation affects an already returned solve result.
 * As with sessions, callers must serialize all access to a given handle. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_input_edb_storage_requirements(
    size_t fact_capacity, size_t text_capacity, size_t *out_bytes, size_t *out_alignment);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_input_edb_init(
    void *storage, size_t storage_bytes, size_t fact_capacity, size_t text_capacity,
    maelys_datalog_input_edb_t **out_edb);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_input_edb_create_with_capacity(
    size_t fact_capacity, size_t text_capacity, maelys_datalog_input_edb_t **out_edb);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_input_edb_create(
    maelys_datalog_input_edb_t **out_edb);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_input_edb_add_fact(
    maelys_datalog_input_edb_t *edb, const char *predicate,
    const maelys_datalog_public_value_t *terms, size_t arity,
    maelys_datalog_public_diagnostic_t *out_diagnostic);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_input_edb_add_facts(
    maelys_datalog_input_edb_t *edb, const maelys_datalog_public_fact_t *facts,
    size_t fact_count, maelys_datalog_public_diagnostic_t *out_diagnostic);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_input_edb_count(
    const maelys_datalog_input_edb_t *edb, size_t *out_count);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_input_edb_clear(
    maelys_datalog_input_edb_t *edb);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_input_edb_free(
    maelys_datalog_input_edb_t *edb);
/* Same semantics and result lease as session_solve. Borrows the buffer only
 * during this call, never consumes it and never retains its pointers. A new
 * solve recomputes from the whole buffer, not from an incremental delta. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_session_solve_edb(
    maelys_datalog_session_t *session, const maelys_datalog_input_edb_t *edb,
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
/* Number of distinct derived IDB facts across ALL predicates, including those
 * not queryable. Excludes runtime EDB and policy facts. No re-solve; unchanged
 * output on error. This is an aggregate, not a bypass of query permissions. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_result_derived_fact_count(
    const maelys_datalog_result_t *result, size_t *out_count);
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
/* Why-false uses the same buffer contract. The MAELYS-DATALOG-WHY-FALSE-v1
 * text is part of this contract: a status line (complete, truncated, or
 * not-applicable when the fact is present), named limit hits (`none` or a
 * comma-separated subset of candidate-rules, substitutions, depth, diagnostics,
 * filter-cost), counters, then per-diagnostic bindings, supports and one
 * obstacle. Variables print as ?N and binding=N where N is the rule-local IR
 * variable id reported by maelys_datalog_program_rule; the standard grammar
 * maps A-Z to 0-25 and anonymous variables to 26 and above. The reference
 * explores at most 128 candidate rules, 4,096 substitutions per rule, depth 10
 * and 16 diagnostics; backend ABI v2 fixes these bounds, they are not
 * caller-tunable. Truncated text is not a proof of non-derivability. */
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
