#ifndef MAELYS_DATALOG_BUILDERS_H
#define MAELYS_DATALOG_BUILDERS_H

#include <maelys/datalog.h>

/* Aligned byte storage, C11/C++17. bytes must be a positive integer constant:
 * never a VLA, no allocation, no prepare/release hidden inside this declaration.
 * For a static array, choose an application constant and check it against
 * session_explanation_storage_bound at startup; the SDK supplies no compile-time
 * bound. Alternatively reserve aligned space at startup inside memory already
 * owned by the application, using that runtime bound (not this array macro).
 * Storage is reusable after an explanation is released or explain_text_in ends.
 * For static duration, declare the array at file scope.
 */
#if defined(__cplusplus)
#define MAELYS_DATALOG_EXPLANATION_STORAGE(name, bytes) \
    alignas(max_align_t) unsigned char name[(bytes)]; \
    static_assert((bytes) > 0, "explanation storage needs a positive integer constant")
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define MAELYS_DATALOG_EXPLANATION_STORAGE(name, bytes) \
    _Alignas(max_align_t) unsigned char name[(bytes)]; \
    _Static_assert((bytes) > 0, "explanation storage needs a positive integer constant")
#endif

/* Predicate declaration initializers (C and C++), not registration calls.
 * Use inside an array or to initialize one public_predicate_t. With constant
 * arguments these are valid static initializers; no allocation or copying is
 * performed. The name retains the ordinary declaration's borrowed lifetime.
 * Each argument appears once. Name/arity validation still occurs at domain
 * registration; flag combinations are checked when loading a policy's registry.
 * Exactly one origin (EDB, IDB, POLICY_FACT), optionally QUERY.
 * QUERY permits querying; it is not an origin, so there is no query-only
 * declaration initializer. The C11 QUERY(...) below executes a query instead. */
#define MAELYS_DATALOG_EDB(name, arity) \
    { (name), (arity), MAELYS_DATALOG_PREDICATE_EDB }
#define MAELYS_DATALOG_EDB_QUERY(name, arity) \
    { (name), (arity), MAELYS_DATALOG_PREDICATE_EDB | MAELYS_DATALOG_PREDICATE_QUERY }
#define MAELYS_DATALOG_IDB(name, arity) \
    { (name), (arity), MAELYS_DATALOG_PREDICATE_IDB }
#define MAELYS_DATALOG_IDB_QUERY(name, arity) \
    { (name), (arity), MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY }
#define MAELYS_DATALOG_POLICY_FACT(name, arity) \
    { (name), (arity), MAELYS_DATALOG_PREDICATE_POLICY_FACT }
#define MAELYS_DATALOG_POLICY_FACT_QUERY(name, arity) \
    { (name), (arity), MAELYS_DATALOG_PREDICATE_POLICY_FACT | MAELYS_DATALOG_PREDICATE_QUERY }

/* Public domain initializers (C and C++), not expressions or registration calls.
 * predicates and atoms must be actual, nonempty fixed-size arrays in scope,
 * never pointers (including array parameters) or VLAs: sizeof derives counts.
 * For dynamic tables, explicit counts or no predicates, initialize the ordinary
 * public_domain_t directly. WITH_ATOMS requires an atom array; use NO_ATOMS for
 * an empty vocabulary. These macros do not check the array-only precondition.
 * NO_ATOMS declares no policy-source atoms; it does not forbid request EDB
 * symbols and does not change loading permissions such as PUBLIC_ALLOW_NONE.
 * No allocation, copying or registration occurs. Names/tables/strings are
 * borrowed until domain_register returns; successful registration owns copies.
 * Each argument is evaluated once (sizeof operands are not evaluated for these
 * fixed-size arrays). Constant arguments permit file-scope static initializers.
 * Validation remains the responsibility of domain_register and policy loading.
 */
#define MAELYS_DATALOG_DOMAIN_NO_ATOMS(name, predicates) \
    { (name), (predicates), sizeof(predicates) / sizeof((predicates)[0]), NULL, 0u }
#define MAELYS_DATALOG_DOMAIN_WITH_ATOMS(name, predicates, atoms) \
    { (name), (predicates), sizeof(predicates) / sizeof((predicates)[0]), \
      (atoms), sizeof(atoms) / sizeof((atoms)[0]) }

/* Symbol value initializer (C and C++), not an expression or a copy.
 * The argument is evaluated once. The borrowed string must remain valid until
 * the consuming call returns; that call still validates it (including NULL).
 * Use ordinary typed values for other kinds or dynamically built arrays. */
#define MAELYS_DATALOG_SYMBOL(value) \
    { MAELYS_DATALOG_VALUE_SYMBOL, { (value) } }

/* C11 source convenience; no exported symbols or ABI/layout changes.
 *
 *   MAELYS_DATALOG_ADD_FACT(edb, &diagnostic, "owns", "alice", "roadmap.pdf")
 *   MAELYS_DATALOG_ADD_FACT(edb, &diagnostic, "enabled", MAELYS_DATALOG_BOOL(1))
 *   MAELYS_DATALOG_ADD_FACT(edb, &diagnostic, "ready")  // arity zero
 *   MAELYS_DATALOG_ADD_FACTS(edb, &diagnostic,
 *       MAELYS_DATALOG_FACT("user", "alice"),
 *       MAELYS_DATALOG_FACT("owns", "alice", "roadmap.pdf"))
 *   MAELYS_DATALOG_QUERY(result, &present, "allow", "alice", "roadmap.pdf")
 *
 * Zero to four terms. char * and const char * become symbols; standard
 * signed/unsigned integer types
 * become INTEGER after an INT64_MIN..INT64_MAX check; _Bool becomes BOOLEAN.
 * Plain char is numeric (its signedness is implementation-defined). Enumerated
 * types follow their compatible integer type. Other types, including floating
 * point, arbitrary pointers and structs, are rejected at compile time.
 * In C11 true/false and comparison results have type int, not _Bool: use
 * MAELYS_DATALOG_BOOL(expression) to request boolean semantics explicitly.
 *
 * Each argument is evaluated once; evaluation order is NOT specified. Do not
 * use mutually dependent side effects in arguments. Conversions and temporary
 * arrays have automatic storage, never heap storage. Range errors return
 * INVALID_ARGUMENT with an input diagnostic before any append. All other
 * errors, input copies and capacity bounds are those of the underlying API.
 * FACT constructs a borrowed, checked descriptor; it inserts nothing and does
 * not copy strings. Use it as an ADD_FACTS argument, not as a public_fact_t
 * initializer. Its strings must remain valid until ADD_FACTS returns.
 * ADD_FACTS requires at least one FACT, checks all conversions, then calls
 * input_edb_add_facts exactly once. Failure appends NONE of the batch and
 * preserves existing facts. Separate ADD_FACT calls are separate transactions.
 * Diagnostic fact/term indices are zero-based within the submitted batch.
 * Automatic storage scales with the number of written FACT arguments; use
 * the typed array/count function for large, dynamic or empty batches.
 * QUERY uses the same conversions but returns the result_query status without
 * a diagnostic argument, just like that function. Check status before reading
 * present: success writes 0 or 1, failure leaves it unchanged. An out-of-range
 * integer returns INVALID_ARGUMENT without calling result_query.
 * No hidden return/goto: callers must check the returned status normally.
 *
 * This convenience layer is C11-only. C++ and older C consumers retain the
 * ordinary typed-value functions above. detail_* names are implementation
 * details, not a supported interface. The underlying functions remain usable
 * with dynamic arrays, zero-allocation caller-owned storage and FFI bindings. */
#ifndef __cplusplus
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
typedef struct {
    maelys_datalog_public_value_t value;
    int out_of_range;
} maelys_datalog_detail_argument_t;

typedef struct {
    maelys_datalog_public_fact_t fact;
    unsigned out_of_range_mask;
} maelys_datalog_detail_fact_t;

/* The diagnostic buffer holds at least 256 bytes. Decimal size_t needs no
 * more than 3 * sizeof(size_t) digits; avoid stdio and heap-backed formatting. */
static inline size_t maelys_datalog_detail_decimal(char *out, size_t number) {
    char reversed[3u * sizeof(size_t)];
    size_t count = 0u;
    do {
        reversed[count++] = (char)('0' + number % 10u);
        number /= 10u;
    } while (number);
    for (size_t i = 0u; i < count; ++i) out[i] = reversed[count - i - 1u];
    return count;
}

static inline maelys_datalog_status_t maelys_datalog_detail_range_error(
    maelys_datalog_public_diagnostic_t *diagnostic, size_t fact, size_t term) {
    maelys_datalog_public_diagnostic_clear(diagnostic);
    if (diagnostic) {
        const char prefix[] = "Fact ", middle[] = ", term ";
        const char suffix[] = ": integer outside INT64_MIN..INT64_MAX.";
        const char phase[] = "input";
        const char hint[] = "Use a representable signed 64-bit value; no facts were appended.";
        size_t pos = 0u;
        for (size_t i = 0u; i < sizeof(prefix) - 1u; ++i) diagnostic->message[pos++] = prefix[i];
        pos += maelys_datalog_detail_decimal(diagnostic->message + pos, fact);
        for (size_t i = 0u; i < sizeof(middle) - 1u; ++i) diagnostic->message[pos++] = middle[i];
        pos += maelys_datalog_detail_decimal(diagnostic->message + pos, term);
        for (size_t i = 0u; i < sizeof(suffix); ++i) diagnostic->message[pos++] = suffix[i];
        diagnostic->source = MAELYS_DATALOG_DIAGNOSTIC_SOLVE;
        diagnostic->code = MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
        for (size_t i = 0u; i < sizeof(phase); ++i) diagnostic->phase[i] = phase[i];
        for (size_t i = 0u; i < sizeof(hint); ++i) diagnostic->hint[i] = hint[i];
    }
    return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
}

static inline maelys_datalog_detail_argument_t
maelys_datalog_detail_symbol(const char *value) {
    maelys_datalog_detail_argument_t arg = {0};
    arg.value.kind = MAELYS_DATALOG_VALUE_SYMBOL;
    arg.value.as.symbol = value;
    return arg;
}

static inline maelys_datalog_detail_argument_t
maelys_datalog_detail_signed(intmax_t value) {
    maelys_datalog_detail_argument_t arg = {0};
    arg.value.kind = MAELYS_DATALOG_VALUE_INTEGER;
    arg.out_of_range = value < INT64_MIN || value > INT64_MAX;
    if (!arg.out_of_range) arg.value.as.integer = (int64_t)value;
    return arg;
}

static inline maelys_datalog_detail_argument_t
maelys_datalog_detail_unsigned(uintmax_t value) {
    maelys_datalog_detail_argument_t arg = {0};
    arg.value.kind = MAELYS_DATALOG_VALUE_INTEGER;
    arg.out_of_range = value > (uintmax_t)INT64_MAX;
    if (!arg.out_of_range) arg.value.as.integer = (int64_t)value;
    return arg;
}

static inline maelys_datalog_detail_argument_t
maelys_datalog_detail_boolean(_Bool value) {
    maelys_datalog_detail_argument_t arg = {0};
    arg.value.kind = MAELYS_DATALOG_VALUE_BOOLEAN;
    arg.value.as.boolean = value ? 1 : 0;
    return arg;
}

static inline maelys_datalog_status_t maelys_datalog_detail_add_fact(
    maelys_datalog_input_edb_t *edb, maelys_datalog_public_diagnostic_t *diagnostic,
    const char *predicate, const maelys_datalog_detail_argument_t *arguments, size_t count) {
    maelys_datalog_public_value_t terms[MAELYS_DATALOG_PUBLIC_MAX_TERMS];
    for (size_t i = 0u; i < count; ++i) {
        if (arguments[i].out_of_range)
            return maelys_datalog_detail_range_error(diagnostic, 0u, i);
        terms[i] = arguments[i].value;
    }
    return maelys_datalog_input_edb_add_fact(edb, predicate, terms, count, diagnostic);
}

static inline maelys_datalog_detail_fact_t maelys_datalog_detail_fact(
    const char *predicate, const maelys_datalog_detail_argument_t *arguments, size_t count) {
    maelys_datalog_detail_fact_t built = {0};
    built.fact.predicate = predicate;
    built.fact.arity = count;
    for (size_t i = 0u; i < count; ++i) {
        built.fact.terms[i] = arguments[i].value;
        if (arguments[i].out_of_range) built.out_of_range_mask |= 1u << i;
    }
    return built;
}

static inline maelys_datalog_status_t maelys_datalog_detail_add_facts(
    maelys_datalog_input_edb_t *edb, maelys_datalog_public_diagnostic_t *diagnostic,
    const maelys_datalog_detail_fact_t *built, maelys_datalog_public_fact_t *facts,
    size_t count) {
    for (size_t i = 0u; i < count; ++i) {
        for (size_t j = 0u; j < built[i].fact.arity; ++j)
            if (built[i].out_of_range_mask & (1u << j))
                return maelys_datalog_detail_range_error(diagnostic, i, j);
        facts[i] = built[i].fact;
    }
    return maelys_datalog_input_edb_add_facts(edb, facts, count, diagnostic);
}

static inline maelys_datalog_status_t maelys_datalog_detail_query(
    const maelys_datalog_result_t *result, int *present, const char *predicate,
    const maelys_datalog_detail_argument_t *arguments, size_t count) {
    maelys_datalog_public_value_t terms[MAELYS_DATALOG_PUBLIC_MAX_TERMS];
    for (size_t i = 0u; i < count; ++i) {
        if (arguments[i].out_of_range)
            return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
        terms[i] = arguments[i].value;
    }
    return maelys_datalog_result_query(result, predicate, terms, count, present);
}

#define MAELYS_DATALOG_BOOL(value) ((_Bool)(value))
#define MAELYS_DATALOG_DETAIL_ARGUMENT(value) _Generic((value), \
    char *: maelys_datalog_detail_symbol, \
    const char *: maelys_datalog_detail_symbol, \
    _Bool: maelys_datalog_detail_boolean, \
    char: maelys_datalog_detail_signed, \
    signed char: maelys_datalog_detail_signed, \
    short: maelys_datalog_detail_signed, \
    int: maelys_datalog_detail_signed, \
    long: maelys_datalog_detail_signed, \
    long long: maelys_datalog_detail_signed, \
    unsigned char: maelys_datalog_detail_unsigned, \
    unsigned short: maelys_datalog_detail_unsigned, \
    unsigned int: maelys_datalog_detail_unsigned, \
    unsigned long: maelys_datalog_detail_unsigned, \
    unsigned long long: maelys_datalog_detail_unsigned \
)(value)
#define MAELYS_DATALOG_DETAIL_ADD_0(edb, diagnostic, predicate) \
    maelys_datalog_input_edb_add_fact((edb), (predicate), NULL, 0u, (diagnostic))
#define MAELYS_DATALOG_DETAIL_ADD_1(edb, diagnostic, predicate, a) \
    maelys_datalog_detail_add_fact((edb), (diagnostic), (predicate), \
        (const maelys_datalog_detail_argument_t[]){MAELYS_DATALOG_DETAIL_ARGUMENT(a)}, 1u)
#define MAELYS_DATALOG_DETAIL_ADD_2(edb, diagnostic, predicate, a, b) \
    maelys_datalog_detail_add_fact((edb), (diagnostic), (predicate), \
        (const maelys_datalog_detail_argument_t[]){MAELYS_DATALOG_DETAIL_ARGUMENT(a), \
            MAELYS_DATALOG_DETAIL_ARGUMENT(b)}, 2u)
#define MAELYS_DATALOG_DETAIL_ADD_3(edb, diagnostic, predicate, a, b, c) \
    maelys_datalog_detail_add_fact((edb), (diagnostic), (predicate), \
        (const maelys_datalog_detail_argument_t[]){MAELYS_DATALOG_DETAIL_ARGUMENT(a), \
            MAELYS_DATALOG_DETAIL_ARGUMENT(b), MAELYS_DATALOG_DETAIL_ARGUMENT(c)}, 3u)
#define MAELYS_DATALOG_DETAIL_ADD_4(edb, diagnostic, predicate, a, b, c, d) \
    maelys_datalog_detail_add_fact((edb), (diagnostic), (predicate), \
        (const maelys_datalog_detail_argument_t[]){MAELYS_DATALOG_DETAIL_ARGUMENT(a), \
            MAELYS_DATALOG_DETAIL_ARGUMENT(b), MAELYS_DATALOG_DETAIL_ARGUMENT(c), \
            MAELYS_DATALOG_DETAIL_ARGUMENT(d)}, 4u)
#define MAELYS_DATALOG_DETAIL_SELECT(_p, _a, _b, _c, _d, selected, ...) selected
#define MAELYS_DATALOG_ADD_FACT(edb, diagnostic, ...) \
    MAELYS_DATALOG_DETAIL_SELECT(__VA_ARGS__, MAELYS_DATALOG_DETAIL_ADD_4, \
        MAELYS_DATALOG_DETAIL_ADD_3, MAELYS_DATALOG_DETAIL_ADD_2, \
        MAELYS_DATALOG_DETAIL_ADD_1, MAELYS_DATALOG_DETAIL_ADD_0, unused) \
    (edb, diagnostic, __VA_ARGS__)
#define MAELYS_DATALOG_DETAIL_FACT_0(predicate) \
    maelys_datalog_detail_fact((predicate), NULL, 0u)
#define MAELYS_DATALOG_DETAIL_FACT_1(predicate, a) \
    maelys_datalog_detail_fact((predicate), \
        (const maelys_datalog_detail_argument_t[]){MAELYS_DATALOG_DETAIL_ARGUMENT(a)}, 1u)
#define MAELYS_DATALOG_DETAIL_FACT_2(predicate, a, b) \
    maelys_datalog_detail_fact((predicate), \
        (const maelys_datalog_detail_argument_t[]){MAELYS_DATALOG_DETAIL_ARGUMENT(a), \
            MAELYS_DATALOG_DETAIL_ARGUMENT(b)}, 2u)
#define MAELYS_DATALOG_DETAIL_FACT_3(predicate, a, b, c) \
    maelys_datalog_detail_fact((predicate), \
        (const maelys_datalog_detail_argument_t[]){MAELYS_DATALOG_DETAIL_ARGUMENT(a), \
            MAELYS_DATALOG_DETAIL_ARGUMENT(b), MAELYS_DATALOG_DETAIL_ARGUMENT(c)}, 3u)
#define MAELYS_DATALOG_DETAIL_FACT_4(predicate, a, b, c, d) \
    maelys_datalog_detail_fact((predicate), \
        (const maelys_datalog_detail_argument_t[]){MAELYS_DATALOG_DETAIL_ARGUMENT(a), \
            MAELYS_DATALOG_DETAIL_ARGUMENT(b), MAELYS_DATALOG_DETAIL_ARGUMENT(c), \
            MAELYS_DATALOG_DETAIL_ARGUMENT(d)}, 4u)
#define MAELYS_DATALOG_FACT(...) \
    MAELYS_DATALOG_DETAIL_SELECT(__VA_ARGS__, MAELYS_DATALOG_DETAIL_FACT_4, \
        MAELYS_DATALOG_DETAIL_FACT_3, MAELYS_DATALOG_DETAIL_FACT_2, \
        MAELYS_DATALOG_DETAIL_FACT_1, MAELYS_DATALOG_DETAIL_FACT_0, unused)(__VA_ARGS__)
/* sizeof's non-VLA operand is unevaluated: duplicated tokens do not duplicate
 * calls or side effects. Scratch has a compile-time bound, even for FACTs
 * whose predicate/terms are supplied by function calls. */
#define MAELYS_DATALOG_DETAIL_FACT_COUNT(...) \
    (sizeof((const maelys_datalog_detail_fact_t[]){__VA_ARGS__}) / \
        sizeof(maelys_datalog_detail_fact_t))
#define MAELYS_DATALOG_ADD_FACTS(edb, diagnostic, ...) \
    maelys_datalog_detail_add_facts((edb), (diagnostic), \
        (const maelys_datalog_detail_fact_t[]){__VA_ARGS__}, \
        (maelys_datalog_public_fact_t[MAELYS_DATALOG_DETAIL_FACT_COUNT(__VA_ARGS__)]){{0}}, \
        MAELYS_DATALOG_DETAIL_FACT_COUNT(__VA_ARGS__))
#define MAELYS_DATALOG_DETAIL_QUERY_0(result, present, predicate) \
    maelys_datalog_result_query((result), (predicate), NULL, 0u, (present))
#define MAELYS_DATALOG_DETAIL_QUERY_1(result, present, predicate, a) \
    maelys_datalog_detail_query((result), (present), (predicate), \
        (const maelys_datalog_detail_argument_t[]){MAELYS_DATALOG_DETAIL_ARGUMENT(a)}, 1u)
#define MAELYS_DATALOG_DETAIL_QUERY_2(result, present, predicate, a, b) \
    maelys_datalog_detail_query((result), (present), (predicate), \
        (const maelys_datalog_detail_argument_t[]){MAELYS_DATALOG_DETAIL_ARGUMENT(a), \
            MAELYS_DATALOG_DETAIL_ARGUMENT(b)}, 2u)
#define MAELYS_DATALOG_DETAIL_QUERY_3(result, present, predicate, a, b, c) \
    maelys_datalog_detail_query((result), (present), (predicate), \
        (const maelys_datalog_detail_argument_t[]){MAELYS_DATALOG_DETAIL_ARGUMENT(a), \
            MAELYS_DATALOG_DETAIL_ARGUMENT(b), MAELYS_DATALOG_DETAIL_ARGUMENT(c)}, 3u)
#define MAELYS_DATALOG_DETAIL_QUERY_4(result, present, predicate, a, b, c, d) \
    maelys_datalog_detail_query((result), (present), (predicate), \
        (const maelys_datalog_detail_argument_t[]){MAELYS_DATALOG_DETAIL_ARGUMENT(a), \
            MAELYS_DATALOG_DETAIL_ARGUMENT(b), MAELYS_DATALOG_DETAIL_ARGUMENT(c), \
            MAELYS_DATALOG_DETAIL_ARGUMENT(d)}, 4u)
#define MAELYS_DATALOG_QUERY(result, present, ...) \
    MAELYS_DATALOG_DETAIL_SELECT(__VA_ARGS__, MAELYS_DATALOG_DETAIL_QUERY_4, \
        MAELYS_DATALOG_DETAIL_QUERY_3, MAELYS_DATALOG_DETAIL_QUERY_2, \
        MAELYS_DATALOG_DETAIL_QUERY_1, MAELYS_DATALOG_DETAIL_QUERY_0, unused) \
    (result, present, __VA_ARGS__)
#endif

#endif /* !__cplusplus */

#endif /* MAELYS_DATALOG_BUILDERS_H */
