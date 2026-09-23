#pragma once
#ifndef MAELYS_DATALOG_PUBLIC_H
#define MAELYS_DATALOG_PUBLIC_H

/* Shared application data declarations; not the backend callback ABI. */
#define MAELYS_DATALOG_APPLICATION_TYPES_VERSION 1u

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
/* Independent optional manifest-loading permissions, combined with |.
 * NONE is not a deny bit: NONE | X equals X. Use NONE alone for no permissions. */
/* No optional manifest-loading permissions. This value is permanently zero. */
#define MAELYS_DATALOG_PUBLIC_ALLOW_NONE 0u
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
/* Stratified distinct count. Future aggregate operators require separate negotiation. */
#define MAELYS_DATALOG_CAP_AGGREGATES (UINT64_C(1) << 8)
/* Independently negotiated integer aggregates; AGGREGATES still means count. */
#define MAELYS_DATALOG_CAP_MIN (UINT64_C(1) << 9)
#define MAELYS_DATALOG_CAP_MAX (UINT64_C(1) << 10)
#define MAELYS_DATALOG_CAP_SUM (UINT64_C(1) << 11)
/* Historical base-language mask; opt into aggregates explicitly. */
#define MAELYS_DATALOG_CAP_LANGUAGE (UINT64_C(31))
#define MAELYS_DATALOG_CAP_ALL (UINT64_C(4095))

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
    MAELYS_DATALOG_STATUS_INVALID_STATE = -13,
    MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL = -14
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

/* Common predicate declaration for stable and low-level domain registration.
 * Registration copies name synchronously into bounded storage (63 bytes plus
 * NUL); the pointer does not require allocation or a lasting caller buffer.
 * Program accessors also use this shape for read-only borrowed views: their
 * returned names live with the program, as specified in datalog_program.h. */
typedef struct {
    const char *name;
    size_t arity;
    unsigned flags;
} maelys_datalog_predicate_t;

/* Stable declarative vocabulary, copied by domain_register. Predicates and
 * policy-source atoms are explicit; internal registry callbacks and metadata
 * are not part of this consumer contract. This is not a low-level struct alias. */
typedef struct {
    const char *name;
    const maelys_datalog_predicate_t *predicates;
    size_t predicate_count;
    const char *const *atoms;
    size_t atom_count;
} maelys_datalog_domain_t;

typedef struct {
    maelys_datalog_value_kind_t kind;
    union {
        const char *symbol;
        int64_t integer;
        int boolean;
    } as;
} maelys_datalog_value_t;

typedef struct {
    const char *predicate;
    size_t arity;
    maelys_datalog_value_t terms[MAELYS_DATALOG_PUBLIC_MAX_TERMS];
} maelys_datalog_fact_t;

typedef struct {
    maelys_datalog_value_kind_t kind;
    union {
        uint32_t symbol_id;
        int64_t integer;
        int boolean;
    } as;
} maelys_datalog_term_view_t;

typedef struct {
    size_t arity;
    maelys_datalog_term_view_t terms[MAELYS_DATALOG_PUBLIC_MAX_TERMS];
} maelys_datalog_fact_view_t;

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

/* atoms belongs to the registered domain, not to a manifest. It authorizes
 * symbolic constants in Datalog predicate arguments; it creates no facts.
 * Runtime EDB strings need no atom declaration. For example, an EDB blocked/1
 * may receive "mallory" and a rule may use not(blocked(User)) without that atom.
 * Writing blocked("mallory") in a rule body requires the atom even for an EDB
 * predicate; writing blocked("mallory"). also requires POLICY_FACT origin.
 * Standard filter pattern parameters have their own validation. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_domain_register(
    const maelys_datalog_domain_t *domain);

/* The Datalog inline path checks domain constants with no permission override.
 * It takes no flags and reads no manifest test_only metadata. This signature
 * differs from legacy/advanced inline loaders with a reserved zero argument. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_policy_load_inline(
    const char *domain,
    const char *policy_id,
    const char *source,
    size_t source_length,
    maelys_datalog_policy_t **out_policy,
    maelys_datalog_public_diagnostic_t *out_diagnostic);

/* flags is a set of independent permissions, not ordered strict/permissive modes:
 * - ALLOW_NONE: no optional permissions. An enabled test_only entry fails the
 *   entire load with FORBIDDEN, rather than being silently skipped.
 * - ALLOW_TEST_ONLY: admit such entries for normal evaluation; this is not a
 *   simulation or production-environment detector. SHA-256 and atom checks remain.
 * - ALLOW_UNDECLARED_POLICY_ATOMS: admit policy-local constants without changing
 *   the global domain. Predicate, capability and capacity checks still apply.
 * Disabled entries follow the manifest's existing disabled-entry contract.
 * Every unknown bit, including one combined with known permissions, is rejected
 * with INVALID_ARGUMENT; older libraries must not silently accept future bits.
 * On failure, a non-NULL out_policy is set to NULL; no partial policy is returned. */
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
/* Opt-in reference explanation workspace. kinds is a mask of EXPLAIN_TRUE and
 * EXPLAIN_FALSE (not capability bits). workspace(0) disables it; defaults reserve
 * nothing. Each setter replaces both the previous mask and storage mode, without
 * mutation on invalid arguments. workspace reserves one additional allocation
 * at session creation, of max(per-kind storage bound), never per explanation.
 * storage borrows a max_align_t-aligned, exclusive, immovable range until session
 * destruction; it must not overlap engine objects, inputs or output text. Its
 * size is checked against the same bound at creation (STORAGE_TOO_SMALL); live
 * session workspace ranges may not overlap (INVALID_STATE). Reusing a config
 * with borrowed storage cannot create a second live session using that range.
 * Config destruction does not release borrowed storage. No fallback allocation.
 * ABI 3 has no bound for custom backends: this mode supports only the canonical
 * reference backend, not copied/wrapped descriptors. Other backends are not
 * configurable through session_create_configured. Custom filter callbacks keep
 * their own allocation contract. The mask/storage choice changes no fingerprint.
 * Neither mode reserves output text or makes the session thread-safe. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_session_config_set_explanation_workspace(
    maelys_datalog_session_config_t *config, unsigned kinds);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_session_config_set_explanation_storage(
    maelys_datalog_session_config_t *config, unsigned kinds, void *storage, size_t bytes);
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
 * corrected batch on the same session.
 * After reference-session creation, engine-owned input conversion, sorting,
 * solving, querying and result release make no allocator calls. The session
 * reserves its public/native result and provenance at initialization. One live
 * result leases that storage; release it before solving again or freeing the
 * session. A released handle must never be used again (its address may recur).
 * Policy/session setup and on-demand explanations may allocate. Custom
 * backends/filter callbacks and host libc internals are outside this guarantee. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_session_solve(
    maelys_datalog_session_t *session,
    const maelys_datalog_fact_t *facts,
    size_t fact_count,
    maelys_datalog_result_t **out_result,
    maelys_datalog_public_diagnostic_t *out_diagnostic);
/* Thread-confined input buffer; independent of policies and sessions.
 * Storage requirements are queried from the loaded library, not hardcoded.
 * fact_capacity must be 1..MAX_EDB_FACTS; text_capacity is 0..INPUT_EDB_TEXT_BYTES.
 * Distinct predicate/symbol strings share one text arena, including one NUL per
 * distinct byte string. Repeated strings share storage across facts and roles.
 * The returned storage size also includes a bounded string index and a batch
 * rollback journal, sized from D = min(fact_capacity * (MAX_ARITY + 1),
 * ceil(text_capacity / 2)); neither consumes text_capacity. The empty string
 * costs one byte, all other distinct strings cost at least two bytes.
 * The internal INPUT_INDEX_THRESHOLD is S=16 possible distinct strings:
 * D<S uses linear arena/prefix scans with no index or journal; D>=S uses
 * indexed lookup. This regime is fixed by init's capacities, not batch data.
 * Requirements outputs are unchanged on failure; init/create outputs become
 * NULL on failure. init requires the returned alignment and at least the
 * returned size. Caller storage must remain alive, unmoved and exclusively
 * owned by the EDB until free. It must not overlap inputs, outputs or diagnostics.
 * init never allocates. add_fact/add_facts/count/clear never allocate in either
 * storage mode. free releases memory only for a create-owned buffer; after free
 * the handle is invalid, but caller-owned storage can be reused/reinitialized.
 * clear resets usage without wiping bytes; it is not a secure erase. Indexed
 * buffers advance a generation in O(1), except on an 8-bit generation wrap,
 * which clears the per-slot generation array. No worst-case O(1) guarantee.
 * create_with_capacity makes ONE allocation at construction, never grows it.
 * create reserves MAX_EDB_FACTS entries and INPUT_EDB_TEXT_BYTES of text. Query
 * both limits from the library. Text defaults to the native symbol-pool budget
 * plus registry-name storage (currently 32 KiB + 8 KiB, in both profiles).
 * Smaller explicit capacities may be used; exhaustion never grows the arena.
 *
 * add_fact/add_facts copy predicate names and symbol bytes before returning;
 * integer/boolean values are copied by value (nonzero booleans become 1).
 * Each append validates the entire batch before publishing facts or text;
 * failures restore the internal index and leave the entire arena unchanged.
 * Inputs must remain unchanged until return and must
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
    const maelys_datalog_value_t *terms, size_t arity,
    maelys_datalog_public_diagnostic_t *out_diagnostic);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_input_edb_add_facts(
    maelys_datalog_input_edb_t *edb, const maelys_datalog_fact_t *facts,
    size_t fact_count, maelys_datalog_public_diagnostic_t *out_diagnostic);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_input_edb_count(
    const maelys_datalog_input_edb_t *edb, size_t *out_count);
/* O(1) occupancy of the buffer's interned predicate/symbol text, including NULs,
 * and its configured text capacity. Excludes indexes/metadata, native vocabulary
 * and policy storage. Clear resets usage to zero; a rejected append preserves it.
 * No allocation. Both outputs are required and unchanged on error. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_input_edb_text_usage(
    const maelys_datalog_input_edb_t *edb, size_t *out_used, size_t *out_capacity);
/* Borrow the ordered raw entries (before native validation/deduplication).
 * Neither the array nor its strings may be modified. The view is valid until
 * the next successful mutation or free of edb. It must not be fed back into a
 * mutating operation on the same edb; copying to a DIFFERENT buffer is allowed.
 * No allocation. Errors leave both outputs unchanged. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_input_edb_view(
    const maelys_datalog_input_edb_t *edb,
    const maelys_datalog_fact_t **out_facts, size_t *out_count);
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
    const maelys_datalog_value_t *terms,
    size_t arity,
    int *out_present);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_result_enumerate(
    const maelys_datalog_result_t *result,
    const char *predicate,
    size_t arity,
    maelys_datalog_fact_view_t *out_facts,
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
 * interning it. Text may include sensitive policy/input values.
 * Without a configured workspace, each call allocates and prepares as before.
 * With a workspace, direct-text calls make no reference-engine allocator calls:
 * a one-entry cache retains preparation across measure/write/retry for the same
 * result generation, kind, predicate, arity and typed term values (not pointers).
 * Another valid direct-text explanation request replaces it; invalid requests
 * leave it alone, failed preparation leaves it empty. Ordinary queries and
 * explicitly caller-owned prepared explanations do not evict it. Short output
 * retains the cache. Kinds outside the configured mask return UNSUPPORTED,
 * never an allocating fallback. result_free automatically releases the internal
 * cache; externally prepared explanations still have to be released explicitly.
 * Output text remains caller-owned; text and out_required must not overlap each
 * other or the session workspace. Formatting still costs work; cache hits avoid
 * exploration, not text rendering. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_result_explain_true_text(
    const maelys_datalog_result_t *result,
    const char *predicate,
    const maelys_datalog_value_t *terms,
    size_t arity,
    char *out_text,
    size_t out_capacity,
    size_t *out_required);
/* Why-false uses the same buffer contract. Text starts with MAELYS-DATALOG-v2
 * then document=why-false on its own line. This serialization contract replaces
 * the historical MAELYS-DATALOG-WHY-FALSE-v1 header without a C ABI change.
 * It continues with a status line (complete, truncated, or
 * not-applicable when the fact is present), named limit hits (`none` or a
 * comma-separated subset of candidate-rules, substitutions, depth, diagnostics,
 * filter-cost), counters, then per-diagnostic bindings, supports and one
 * obstacle. Variables print as ?N and binding=N where N is the rule-local IR
 * variable id reported by maelys_datalog_program_rule; the standard grammar
 * maps A-Z to 0-25 and anonymous variables to 26 and above. The reference
 * explores at most 128 candidate rules, 4,096 substitutions per rule, depth 10
 * and 16 diagnostics; backend ABI v3 preserves these bounds, they are not
 * caller-tunable. Truncated text is not a proof of non-derivability. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_result_explain_false_text(
    const maelys_datalog_result_t *result,
    const char *predicate,
    const maelys_datalog_value_t *terms,
    size_t arity,
    char *out_text,
    size_t out_capacity,
    size_t *out_required);
/* Releases the result lease. The reference backend resets bookkeeping, not
 * the retained fact/provenance storage: this is NOT secure memory erasure.
 * Result views become invalid immediately; subsequent solves cannot query
 * data outside their newly initialized counts and validity markers. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_result_free(
    maelys_datalog_result_t *result);

typedef struct maelys_datalog_prepared_explanation maelys_datalog_prepared_explanation_t;
typedef enum {
    MAELYS_DATALOG_EXPLAIN_TRUE = 1,
    MAELYS_DATALOG_EXPLAIN_FALSE = 2
} maelys_datalog_explanation_kind_t;

/* Reference backend only: a per-kind upper bound for every result of this
 * library's build profile, including the opaque handle and alignment padding.
 * Available before solving; independent of request facts. Other backends
 * (including copied/wrapped reference descriptors) return UNSUPPORTED: ABI 3
 * provides only per-result requirements. This is caller-storage size, not a
 * bound on text length or total stack usage. Alignment <= alignof(max_align_t).
 * Errors leave outputs unchanged; NULL outputs/invalid kind -> INVALID_ARGUMENT,
 * callback reentry -> INVALID_STATE. No allocation or result lease is acquired.
 */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_session_explanation_storage_bound(
    const maelys_datalog_session_t *session, maelys_datalog_explanation_kind_t kind,
    size_t *out_bytes, size_t *out_alignment);

/* One-shot caller-owned explanation: prepare, measure, write, release. The
 * reference makes no engine allocator calls; custom filters/backends retain
 * their own contract. No explanation handle survives, even on write failure.
 * Storage has the same exclusive/aligned lifetime and non-overlap requirements
 * as prepare_explanation below. out_text and out_required must be non-NULL;
 * there is no NULL-text size-query mode. Scalar outputs must not alias storage
 * or text. Preparation may modify storage even on failure.
 *
 * Once preparation succeeds, out_required receives text length excluding NUL.
 * A short TEXT buffer returns PAYLOAD_TOO_LARGE, with out_required populated
 * and out_text[0] = NUL if capacity > 0 (no partial text). Insufficient STORAGE
 * returns STORAGE_TOO_SMALL, but leaves out_required unchanged: nothing
 * has been prepared. Other pre-prepare errors leave output parameters unchanged.
 * A retry prepares again. Retain a prepared handle instead when growing a text
 * buffer without rebuilding, or writing repeatedly. Document status=truncated
 * describes bounded exploration, NOT PAYLOAD_TOO_LARGE; a larger text buffer
 * does not remove exploration limits. Backend write failures follow write_text's
 * output contract below; their text must not be consumed.
 */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_result_explain_text_in(
    maelys_datalog_result_t *result, maelys_datalog_explanation_kind_t kind,
    const char *predicate, const maelys_datalog_value_t *terms, size_t arity,
    void *storage, size_t storage_bytes, char *out_text, size_t capacity,
    size_t *out_required);

/* Prepare once, then measure/render the retained explanation without searching
 * again. These functions never re-solve. Unlike the allocating *_explain_*_text
 * convenience calls above, the reference implementation makes NO allocator
 * calls on this path (including Why-false scratch). Bounded automatic stack
 * storage remains in use. Custom filters/backends must honor their own contract.
 *
 * Requirements depend on the live result, kind, backend and build profile;
 * query them for each result. Alignment is a power of two <= alignof(max_align_t).
 * Use an appropriately aligned caller arena; no hidden fallback/growth occurs.
 * Storage is exclusive, immovable and must not overlap arguments, output text,
 * another live handle or engine objects. Do not reuse it before release.
 *
 * Preparation borrows the result (not query strings): result_free returns
 * INVALID_STATE until every prepared explanation is released. Release does NOT
 * free caller memory or erase sensitive data. No function is thread-safe on a
 * shared session; the existing session-confinement rule applies.
 *
 * Errors leave scalar/handle output parameters unchanged; failed preparation may modify the
 * supplied storage but acquires no lease. Missing capabilities -> UNSUPPORTED;
 * invalid alignment -> INVALID_ARGUMENT; insufficient storage -> STORAGE_TOO_SMALL.
 * The text/status/limits match the convenience calls (a bounded/truncated
 * explanation is distinct from a too-small text buffer).
 */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_result_explanation_storage_requirements(
    const maelys_datalog_result_t *result, maelys_datalog_explanation_kind_t kind,
    size_t *out_bytes, size_t *out_alignment);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_result_prepare_explanation(
    maelys_datalog_result_t *result, maelys_datalog_explanation_kind_t kind,
    const char *predicate, const maelys_datalog_value_t *terms, size_t arity,
    void *storage, size_t storage_bytes, maelys_datalog_prepared_explanation_t **out);
/* Cached size, excluding the terminating NUL; no formatting or proof traversal. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_prepared_explanation_text_size(
    const maelys_datalog_prepared_explanation_t *, size_t *out_required);
/* Write only: NULL is invalid, including at capacity zero. No size-query mode.
 * Needs text_size + 1 bytes. On PAYLOAD_TOO_LARGE only out_text[0] is set to NUL
 * when capacity > 0; no partial text. Repeated successful writes are byte-identical.
 * If a third-party backend violates its callback contract, output text after
 * the resulting error is unspecified and must not be consumed. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_prepared_explanation_write_text(
    const maelys_datalog_prepared_explanation_t *, char *out_text, size_t capacity);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_prepared_explanation_release(
    maelys_datalog_prepared_explanation_t *);

#ifdef __cplusplus
}
#endif

/* Source conveniences are separate from the exported API. Their _Generic
 * implementation is guarded against C++ in this installed header. */
#include <maelys/datalog_builders.h>

#endif
