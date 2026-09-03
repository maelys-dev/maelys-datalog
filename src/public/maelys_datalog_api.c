#include "include/maelys/datalog.h"

#include "common/maelys_errors.h"
#include "src/core/maelys_datalog_domain_registry.h"
#include "src/core/maelys_datalog_explanation_format.h"
#include "src/core/maelys_datalog_predicate_registry.h"
#include "src/core/maelys_datalog_prepared_session.h"
#include "src/core/maelys_datalog_prepared_session_internal.h"
#include "src/core/maelys_datalog_solver.h"
#include "src/core/maelys_datalog_symbol_table.h"
#include "src/manifest/maelys_datalog_manifest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert((int)MAELYS_DATALOG_STATUS_OK == (int)MAELYS_OK,
               "public status values must match the engine");
_Static_assert((int)MAELYS_DATALOG_STATUS_INVALID_STATE == (int)MAELYS_ERR_INVALID_STATE,
               "public status values must match the engine");
_Static_assert((int)MAELYS_DATALOG_VALUE_SYMBOL == (int)MAELYS_DATALOG_TERM_SYMBOL,
               "public term values must match the engine");
_Static_assert((int)MAELYS_DATALOG_VALUE_INTEGER == (int)MAELYS_DATALOG_TERM_INT,
               "public term values must match the engine");
_Static_assert((int)MAELYS_DATALOG_VALUE_BOOLEAN == (int)MAELYS_DATALOG_TERM_BOOL,
               "public term values must match the engine");
_Static_assert(MAELYS_DATALOG_PUBLIC_MAX_TERMS == MAELYS_DATALOG_MAX_TERMS,
               "public fact arity must match the engine");
_Static_assert(MAELYS_DATALOG_PUBLIC_MAX_POLICY_ATOMS == MAELYS_DATALOG_MAX_ATOMS,
               "public policy atom capacity must match the engine");
_Static_assert(MAELYS_DATALOG_PUBLIC_MAX_POLICY_ATOM_BYTES ==
                   sizeof(((maelys_datalog_predicate_registry_t *)0)->atoms[0]) - 1u,
               "public policy atom byte capacity must match the engine");
_Static_assert((unsigned)MAELYS_DATALOG_PREDICATE_EDB == MAELYS_DATALOG_PRED_KIND_EDB,
               "public predicate flags must match the engine");
_Static_assert((unsigned)MAELYS_DATALOG_PREDICATE_IDB == MAELYS_DATALOG_PRED_KIND_IDB,
               "public predicate flags must match the engine");
_Static_assert((unsigned)MAELYS_DATALOG_PREDICATE_QUERY == MAELYS_DATALOG_PRED_KIND_QUERY,
               "public predicate flags must match the engine");
_Static_assert((unsigned)MAELYS_DATALOG_PREDICATE_POLICY_FACT ==
                   MAELYS_DATALOG_PRED_KIND_POLICY_FACT,
               "public predicate flags must match the engine");

struct maelys_datalog_policy {
    maelys_datalog_policy_set_t set;
};

struct maelys_datalog_session {
    maelys_datalog_prepared_session_t *inner;
};

struct maelys_datalog_result {
    maelys_datalog_solve_result_t *inner;
    maelys_datalog_session_t *owner;
};

static maelys_datalog_status_t public_status(maelys_result_t status) {
    return (maelys_datalog_status_t)status;
}

const char *maelys_datalog_status_name(maelys_datalog_status_t status) {
    switch (status) {
        case MAELYS_DATALOG_STATUS_OK: return "ok";
        case MAELYS_DATALOG_STATUS_INVALID_ARGUMENT: return "invalid_argument";
        case MAELYS_DATALOG_STATUS_INVALID_FIELD: return "invalid_field";
        case MAELYS_DATALOG_STATUS_NOT_FOUND: return "not_found";
        case MAELYS_DATALOG_STATUS_NOT_IMPLEMENTED: return "not_implemented";
        case MAELYS_DATALOG_STATUS_UNSUPPORTED: return "unsupported";
        case MAELYS_DATALOG_STATUS_TIMEOUT: return "timeout";
        case MAELYS_DATALOG_STATUS_IO: return "io";
        case MAELYS_DATALOG_STATUS_INTERNAL: return "internal";
        case MAELYS_DATALOG_STATUS_UNAUTHORIZED: return "unauthorized";
        case MAELYS_DATALOG_STATUS_FORBIDDEN: return "forbidden";
        case MAELYS_DATALOG_STATUS_RATE_LIMITED: return "rate_limited";
        case MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE: return "payload_too_large";
        case MAELYS_DATALOG_STATUS_INVALID_STATE: return "invalid_state";
        default: return "unknown";
    }
}

void maelys_datalog_public_diagnostic_clear(
    maelys_datalog_public_diagnostic_t *diagnostic) {
    if (diagnostic) memset(diagnostic, 0, sizeof(*diagnostic));
}

static void copy_load_diagnostic(
    maelys_datalog_public_diagnostic_t *destination,
    const maelys_datalog_diagnostic_t *source) {
    if (!destination || !source) return;
    maelys_datalog_public_diagnostic_t value;
    memset(&value, 0, sizeof(value));
    value.source = MAELYS_DATALOG_DIAGNOSTIC_LOAD;
    value.code = (int)source->code;
    value.line = source->line;
    value.column = source->column;
    memcpy(value.phase, source->phase, sizeof(value.phase));
    memcpy(value.message, source->message, sizeof(value.message));
    memcpy(value.hint, source->hint, sizeof(value.hint));
    *destination = value;
}

static void copy_solve_diagnostic(
    maelys_datalog_public_diagnostic_t *destination,
    const maelys_datalog_solve_diagnostic_t *source) {
    if (!destination || !source) return;
    maelys_datalog_public_diagnostic_t value;
    memset(&value, 0, sizeof(value));
    value.source = MAELYS_DATALOG_DIAGNOSTIC_SOLVE;
    value.code = (int)source->category;
    (void)snprintf(value.phase, sizeof(value.phase), "%s", "solve");
    (void)snprintf(value.message,
                   sizeof(value.message),
                   "%s",
                   maelys_datalog_solve_diagnostic_category_name(source->category));
    *destination = value;
}

static int public_predicates_match(
    const maelys_datalog_domain_def_t *existing,
    const maelys_datalog_public_domain_t *candidate) {
    if (!existing || !candidate || existing->install_predicates ||
        existing->predicate_count != candidate->predicate_count ||
        existing->atom_count != candidate->atom_count) {
        return 0;
    }
    for (size_t i = 0u; i < candidate->predicate_count; i++) {
        if (!candidate->predicates[i].name ||
            strcmp(existing->predicates[i].name, candidate->predicates[i].name) != 0 ||
            existing->predicates[i].arity != candidate->predicates[i].arity ||
            existing->predicates[i].kind_flags != candidate->predicates[i].flags) {
            return 0;
        }
    }
    for (size_t i = 0u; i < candidate->atom_count; i++) {
        if (!candidate->atoms[i] || strcmp(existing->atoms[i], candidate->atoms[i]) != 0) {
            return 0;
        }
    }
    return 1;
}

maelys_datalog_status_t maelys_datalog_domain_register(
    const maelys_datalog_public_domain_t *domain) {
    if (!domain || !domain->name || !domain->predicates ||
        domain->predicate_count == 0u ||
        domain->predicate_count > MAELYS_DATALOG_MAX_PREDICATES ||
        domain->atom_count > MAELYS_DATALOG_MAX_ATOMS ||
        (domain->atom_count > 0u && !domain->atoms)) {
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    }
    const maelys_datalog_domain_def_t *existing =
        maelys_datalog_domain_registry_find(domain->name);
    if (existing) {
        return public_predicates_match(existing, domain)
                   ? MAELYS_DATALOG_STATUS_OK
                   : MAELYS_DATALOG_STATUS_INVALID_FIELD;
    }

    maelys_datalog_predicate_def_t predicates[MAELYS_DATALOG_MAX_PREDICATES];
    memset(predicates, 0, sizeof(predicates));
    for (size_t i = 0u; i < domain->predicate_count; i++) {
        const maelys_datalog_public_predicate_t *source = &domain->predicates[i];
        if (!source->name) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
        const size_t name_length = strnlen(source->name, sizeof(predicates[i].name));
        if (name_length == 0u || name_length >= sizeof(predicates[i].name) ||
            source->arity > MAELYS_DATALOG_MAX_ARITY) {
            return MAELYS_DATALOG_STATUS_INVALID_FIELD;
        }
        memcpy(predicates[i].name, source->name, name_length + 1u);
        predicates[i].arity = source->arity;
        predicates[i].kind_flags = source->flags;
    }
    maelys_datalog_domain_def_t internal = {
        .domain_name = domain->name,
        .predicates = predicates,
        .predicate_count = domain->predicate_count,
        .atoms = domain->atoms,
        .atom_count = domain->atom_count,
        .description = NULL,
        .install_predicates = NULL,
    };
    return public_status(maelys_datalog_domain_registry_register(&internal));
}

static maelys_datalog_status_t allocate_policy(
    maelys_datalog_policy_t **out_policy,
    maelys_datalog_policy_t **out_allocated) {
    if (out_policy) *out_policy = NULL;
    if (!out_policy || !out_allocated) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    maelys_datalog_policy_t *policy = calloc(1u, sizeof(*policy));
    if (!policy) return MAELYS_DATALOG_STATUS_INTERNAL;
    *out_allocated = policy;
    return MAELYS_DATALOG_STATUS_OK;
}

maelys_datalog_status_t maelys_datalog_policy_load_inline(
    const char *domain,
    const char *policy_id,
    const char *source,
    size_t source_length,
    maelys_datalog_policy_t **out_policy,
    maelys_datalog_public_diagnostic_t *out_diagnostic) {
    maelys_datalog_public_diagnostic_clear(out_diagnostic);
    maelys_datalog_policy_t *policy = NULL;
    maelys_datalog_status_t allocation = allocate_policy(out_policy, &policy);
    if (allocation != MAELYS_DATALOG_STATUS_OK) return allocation;
    maelys_datalog_diagnostic_t diagnostic;
    maelys_datalog_diagnostic_clear(&diagnostic);
    maelys_result_t status = maelys_datalog_load_policy_inline(
        domain, policy_id, source, source_length, 0u, &policy->set, &diagnostic);
    if (status != MAELYS_OK) {
        copy_load_diagnostic(out_diagnostic, &diagnostic);
        memset(policy, 0, sizeof(*policy));
        free(policy);
        return public_status(status);
    }
    *out_policy = policy;
    return MAELYS_DATALOG_STATUS_OK;
}

maelys_datalog_status_t maelys_datalog_policy_load_manifest(
    const char *manifest_path,
    unsigned flags,
    maelys_datalog_policy_t **out_policy,
    maelys_datalog_public_diagnostic_t *out_diagnostic) {
    maelys_datalog_public_diagnostic_clear(out_diagnostic);
    const unsigned supported_flags =
        MAELYS_DATALOG_PUBLIC_ALLOW_TEST_ONLY |
        MAELYS_DATALOG_PUBLIC_ALLOW_UNDECLARED_POLICY_ATOMS;
    if (flags & ~supported_flags) {
        if (out_policy) *out_policy = NULL;
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    }
    maelys_datalog_policy_t *policy = NULL;
    maelys_datalog_status_t allocation = allocate_policy(out_policy, &policy);
    if (allocation != MAELYS_DATALOG_STATUS_OK) return allocation;
    maelys_datalog_diagnostic_t diagnostic;
    maelys_datalog_diagnostic_clear(&diagnostic);
    unsigned manifest_flags = 0u;
    if (flags & MAELYS_DATALOG_PUBLIC_ALLOW_TEST_ONLY) {
        manifest_flags |= MAELYS_DATALOG_MANIFEST_ALLOW_TEST_ONLY;
    }
    if (flags & MAELYS_DATALOG_PUBLIC_ALLOW_UNDECLARED_POLICY_ATOMS) {
        manifest_flags |= MAELYS_DATALOG_MANIFEST_ALLOW_UNDECLARED_POLICY_ATOMS;
    }
    maelys_result_t status = maelys_datalog_manifest_load_ex(
        manifest_path, manifest_flags, &policy->set, &diagnostic);
    if (status != MAELYS_OK) {
        copy_load_diagnostic(out_diagnostic, &diagnostic);
        memset(policy, 0, sizeof(*policy));
        free(policy);
        return public_status(status);
    }
    *out_policy = policy;
    return MAELYS_DATALOG_STATUS_OK;
}

maelys_datalog_status_t maelys_datalog_policy_count(
    const maelys_datalog_policy_t *policy,
    size_t *out_count) {
    if (!policy || !out_count) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    const size_t count = policy->set.policy_count;
    if (count == 0u) return MAELYS_DATALOG_STATUS_INVALID_STATE;
    *out_count = count;
    return MAELYS_DATALOG_STATUS_OK;
}

maelys_datalog_status_t maelys_datalog_policy_fingerprint(
    const maelys_datalog_policy_t *policy,
    char out_fingerprint[MAELYS_DATALOG_PUBLIC_FINGERPRINT_BYTES]) {
    if (!policy || !out_fingerprint) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    char fingerprint[MAELYS_DATALOG_PUBLIC_FINGERPRINT_BYTES];
    maelys_result_t status = maelys_datalog_policy_set_fingerprint(
        &policy->set, fingerprint);
    if (status != MAELYS_OK) return public_status(status);
    memcpy(out_fingerprint, fingerprint, sizeof(fingerprint));
    return MAELYS_DATALOG_STATUS_OK;
}

maelys_datalog_status_t maelys_datalog_policy_free(maelys_datalog_policy_t *policy) {
    if (!policy) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    maelys_datalog_policy_set_clear(&policy->set);
    memset(policy, 0, sizeof(*policy));
    free(policy);
    return MAELYS_DATALOG_STATUS_OK;
}

maelys_datalog_status_t maelys_datalog_session_create(
    const maelys_datalog_policy_t *policy,
    size_t policy_index,
    maelys_datalog_session_t **out_session) {
    if (out_session) *out_session = NULL;
    if (!policy || !out_session) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (policy_index >= policy->set.policy_count) return MAELYS_DATALOG_STATUS_NOT_FOUND;
    maelys_datalog_session_t *session = calloc(1u, sizeof(*session));
    if (!session) return MAELYS_DATALOG_STATUS_INTERNAL;
    maelys_result_t status = maelys_datalog_prepared_session_create(
        &policy->set.policies[policy_index], &session->inner);
    if (status != MAELYS_OK) {
        free(session);
        return public_status(status);
    }
    *out_session = session;
    return MAELYS_DATALOG_STATUS_OK;
}

maelys_datalog_status_t maelys_datalog_session_fingerprint(
    const maelys_datalog_session_t *session,
    char out_fingerprint[MAELYS_DATALOG_PUBLIC_FINGERPRINT_BYTES]) {
    if (!session || !session->inner || !out_fingerprint) {
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    }
    const char *fingerprint = maelys_datalog_prepared_session_fingerprint(session->inner);
    if (!fingerprint) return MAELYS_DATALOG_STATUS_INVALID_STATE;
    char value[MAELYS_DATALOG_PUBLIC_FINGERPRINT_BYTES];
    memcpy(value, fingerprint, sizeof(value));
    memcpy(out_fingerprint, value, sizeof(value));
    return MAELYS_DATALOG_STATUS_OK;
}

static maelys_datalog_status_t convert_public_value(
    const maelys_datalog_public_value_t *source,
    maelys_datalog_input_term_t *destination) {
    if (!source || !destination) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    memset(destination, 0, sizeof(*destination));
    switch (source->kind) {
        case MAELYS_DATALOG_VALUE_SYMBOL:
            if (!source->as.symbol) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
            destination->kind = MAELYS_DATALOG_TERM_SYMBOL;
            destination->as.symbol = source->as.symbol;
            return MAELYS_DATALOG_STATUS_OK;
        case MAELYS_DATALOG_VALUE_INTEGER:
            destination->kind = MAELYS_DATALOG_TERM_INT;
            destination->as.integer = source->as.integer;
            return MAELYS_DATALOG_STATUS_OK;
        case MAELYS_DATALOG_VALUE_BOOLEAN:
            destination->kind = MAELYS_DATALOG_TERM_BOOL;
            destination->as.boolean = source->as.boolean ? 1 : 0;
            return MAELYS_DATALOG_STATUS_OK;
        default:
            return MAELYS_DATALOG_STATUS_INVALID_FIELD;
    }
}

maelys_datalog_status_t maelys_datalog_session_solve(
    maelys_datalog_session_t *session,
    const maelys_datalog_public_fact_t *facts,
    size_t fact_count,
    maelys_datalog_result_t **out_result,
    maelys_datalog_public_diagnostic_t *out_diagnostic) {
    if (out_result) *out_result = NULL;
    maelys_datalog_public_diagnostic_clear(out_diagnostic);
    if (!session || !session->inner || (!facts && fact_count > 0u) || !out_result) {
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    }
    if (fact_count > MAELYS_DATALOG_MAX_EDB_FACTS ||
        fact_count > SIZE_MAX / sizeof(maelys_datalog_input_fact_t)) {
        return MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE;
    }
    maelys_datalog_input_fact_t *inputs = NULL;
    if (fact_count > 0u) {
        inputs = calloc(fact_count, sizeof(*inputs));
        if (!inputs) return MAELYS_DATALOG_STATUS_INTERNAL;
    }
    maelys_datalog_status_t conversion = MAELYS_DATALOG_STATUS_OK;
    for (size_t i = 0u; i < fact_count && conversion == MAELYS_DATALOG_STATUS_OK; i++) {
        if (!facts[i].predicate || facts[i].arity > MAELYS_DATALOG_PUBLIC_MAX_TERMS) {
            conversion = MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
            break;
        }
        inputs[i].predicate = facts[i].predicate;
        inputs[i].arity = facts[i].arity;
        for (size_t j = 0u; j < facts[i].arity; j++) {
            conversion = convert_public_value(&facts[i].terms[j], &inputs[i].terms[j]);
            if (conversion != MAELYS_DATALOG_STATUS_OK) break;
        }
    }
    if (conversion != MAELYS_DATALOG_STATUS_OK) {
        free(inputs);
        return conversion;
    }

    maelys_datalog_solve_diagnostic_t diagnostic;
    memset(&diagnostic, 0, sizeof(diagnostic));
    maelys_datalog_solve_result_t *inner = NULL;
    maelys_result_t status = maelys_datalog_prepared_session_solve_ex(
        session->inner, inputs, fact_count, &inner, &diagnostic);
    free(inputs);
    if (status != MAELYS_OK) {
        copy_solve_diagnostic(out_diagnostic, &diagnostic);
        return public_status(status);
    }
    maelys_datalog_result_t *result = calloc(1u, sizeof(*result));
    if (!result) {
        maelys_datalog_solve_result_free(inner);
        return MAELYS_DATALOG_STATUS_INTERNAL;
    }
    result->inner = inner;
    result->owner = session;
    *out_result = result;
    return MAELYS_DATALOG_STATUS_OK;
}

maelys_datalog_status_t maelys_datalog_session_free(maelys_datalog_session_t *session) {
    if (!session || !session->inner) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    maelys_result_t status = maelys_datalog_prepared_session_destroy(session->inner);
    if (status != MAELYS_OK) return public_status(status);
    session->inner = NULL;
    memset(session, 0, sizeof(*session));
    free(session);
    return MAELYS_DATALOG_STATUS_OK;
}

static maelys_datalog_status_t convert_query_terms(
    const maelys_datalog_result_t *result,
    const maelys_datalog_public_value_t *terms,
    size_t arity,
    maelys_datalog_term_t converted[MAELYS_DATALOG_PUBLIC_MAX_TERMS],
    int *out_all_symbols_found) {
    if (!result || !result->inner || !result->owner || !result->owner->inner ||
        (!terms && arity > 0u) || arity > MAELYS_DATALOG_PUBLIC_MAX_TERMS ||
        !converted || !out_all_symbols_found) {
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    }
    memset(converted, 0, sizeof(*converted) * MAELYS_DATALOG_PUBLIC_MAX_TERMS);
    int all_found = 1;
    for (size_t i = 0u; i < arity; i++) {
        switch (terms[i].kind) {
            case MAELYS_DATALOG_VALUE_SYMBOL: {
                if (!terms[i].as.symbol) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
                converted[i].kind = MAELYS_DATALOG_TERM_SYMBOL;
                int found = 0;
                maelys_datalog_symbol_id_t id = MAELYS_DATALOG_SYMBOL_ID_INVALID;
                maelys_result_t status = maelys_datalog_prepared_session_lookup_symbol(
                    result->owner->inner, terms[i].as.symbol, &id, &found);
                if (status != MAELYS_OK) return public_status(status);
                if (!found) all_found = 0;
                converted[i].as.symbol = id;
                break;
            }
            case MAELYS_DATALOG_VALUE_INTEGER:
                converted[i].kind = MAELYS_DATALOG_TERM_INT;
                converted[i].as.integer = terms[i].as.integer;
                break;
            case MAELYS_DATALOG_VALUE_BOOLEAN:
                converted[i].kind = MAELYS_DATALOG_TERM_BOOL;
                converted[i].as.boolean = terms[i].as.boolean ? 1 : 0;
                break;
            default:
                return MAELYS_DATALOG_STATUS_INVALID_FIELD;
        }
    }
    *out_all_symbols_found = all_found;
    return MAELYS_DATALOG_STATUS_OK;
}

maelys_datalog_status_t maelys_datalog_result_query(
    const maelys_datalog_result_t *result,
    const char *predicate,
    const maelys_datalog_public_value_t *terms,
    size_t arity,
    int *out_present) {
    if (!result || !result->inner || !predicate || !out_present) {
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    }
    maelys_result_t validation = maelys_datalog_validate_solved_ground_query(
        result->inner, predicate, arity);
    if (validation != MAELYS_OK) return public_status(validation);
    maelys_datalog_term_t converted[MAELYS_DATALOG_PUBLIC_MAX_TERMS];
    int all_found = 0;
    maelys_datalog_status_t status = convert_query_terms(
        result, terms, arity, converted, &all_found);
    if (status != MAELYS_DATALOG_STATUS_OK) return status;
    if (!all_found) {
        *out_present = 0;
        return MAELYS_DATALOG_STATUS_OK;
    }
    bool present = false;
    maelys_result_t query = maelys_datalog_query_solved_ground_fact(
        result->inner, predicate, converted, arity, &present);
    if (query != MAELYS_OK) return public_status(query);
    *out_present = present ? 1 : 0;
    return MAELYS_DATALOG_STATUS_OK;
}

maelys_datalog_status_t maelys_datalog_result_enumerate(
    const maelys_datalog_result_t *result,
    const char *predicate,
    size_t arity,
    maelys_datalog_public_fact_view_t *out_facts,
    size_t out_capacity,
    size_t *out_count) {
    if (!result || !result->inner || !predicate || !out_count ||
        (out_capacity > 0u && !out_facts) ||
        out_capacity > SIZE_MAX / sizeof(maelys_datalog_fact_t) ||
        out_capacity > SIZE_MAX / sizeof(maelys_datalog_public_fact_view_t)) {
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    }
    maelys_datalog_fact_t *facts = NULL;
    maelys_datalog_public_fact_view_t *views = NULL;
    if (out_capacity > 0u) {
        facts = calloc(out_capacity, sizeof(*facts));
        views = calloc(out_capacity, sizeof(*views));
        if (!facts || !views) {
            free(facts);
            free(views);
            return MAELYS_DATALOG_STATUS_INTERNAL;
        }
    }
    size_t count = 0u;
    maelys_result_t status = maelys_datalog_solve_result_enumerate_predicate_facts(
        result->inner, predicate, arity, facts, out_capacity, &count);
    if (status == MAELYS_OK) {
        const size_t copied = count < out_capacity ? count : out_capacity;
        for (size_t i = 0u; i < copied; i++) {
            views[i].arity = facts[i].arity;
            for (size_t j = 0u; j < facts[i].arity; j++) {
                views[i].terms[j].kind = (maelys_datalog_value_kind_t)facts[i].terms[j].kind;
                switch (facts[i].terms[j].kind) {
                    case MAELYS_DATALOG_TERM_SYMBOL:
                        views[i].terms[j].as.symbol_id = facts[i].terms[j].as.symbol;
                        break;
                    case MAELYS_DATALOG_TERM_INT:
                        views[i].terms[j].as.integer = facts[i].terms[j].as.integer;
                        break;
                    case MAELYS_DATALOG_TERM_BOOL:
                        views[i].terms[j].as.boolean = facts[i].terms[j].as.boolean;
                        break;
                    default:
                        status = MAELYS_ERR_INVALID_STATE;
                        break;
                }
                if (status != MAELYS_OK) break;
            }
            if (status != MAELYS_OK) break;
        }
        if (status == MAELYS_OK) {
            if (copied > 0u) memcpy(out_facts, views, copied * sizeof(*views));
            *out_count = count;
        }
    }
    free(facts);
    free(views);
    return public_status(status);
}

maelys_datalog_status_t maelys_datalog_result_symbol_text(
    const maelys_datalog_result_t *result,
    uint32_t symbol_id,
    const char **out_text,
    size_t *out_length) {
    if (!result || !result->inner) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    return public_status(maelys_datalog_solve_result_symbol_text(
        result->inner, (maelys_datalog_symbol_id_t)symbol_id, out_text, out_length));
}

maelys_datalog_status_t maelys_datalog_result_explain_true_text(
    const maelys_datalog_result_t *result,
    const char *predicate,
    const maelys_datalog_public_value_t *terms,
    size_t arity,
    char *out_text,
    size_t out_capacity,
    size_t *out_required) {
    if (!result || !result->inner || !result->owner || !result->owner->inner ||
        !predicate || !out_required || (out_capacity > 0u && !out_text)) {
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    }
    maelys_result_t validation = maelys_datalog_validate_solved_ground_query(
        result->inner, predicate, arity);
    if (validation != MAELYS_OK) return public_status(validation);
    maelys_datalog_term_t converted[MAELYS_DATALOG_PUBLIC_MAX_TERMS];
    int all_found = 0;
    maelys_datalog_status_t conversion = convert_query_terms(
        result, terms, arity, converted, &all_found);
    if (conversion != MAELYS_DATALOG_STATUS_OK) return conversion;
    if (!all_found) return MAELYS_DATALOG_STATUS_NOT_FOUND;

    maelys_datalog_predicate_id_t predicate_id = 0u;
    if (!maelys_datalog_predicate_registry_find(
            &result->owner->inner->working.registry, predicate, arity, &predicate_id)) {
        return MAELYS_DATALOG_STATUS_INVALID_STATE;
    }
    maelys_datalog_fact_t fact;
    memset(&fact, 0, sizeof(fact));
    fact.predicate_id = predicate_id;
    fact.arity = (uint8_t)arity;
    memcpy(fact.terms, converted, arity * sizeof(converted[0]));

    maelys_datalog_explanation_t *explanation = calloc(1u, sizeof(*explanation));
    if (!explanation) return MAELYS_DATALOG_STATUS_INTERNAL;
    maelys_result_t status = maelys_datalog_explain_solved_fact(
        result->inner, &fact, explanation);
    if (status == MAELYS_OK) {
        status = maelys_datalog_format_explanation_text(
            &result->owner->inner->working,
            explanation,
            out_text,
            out_capacity,
            out_required);
    }
    memset(explanation, 0, sizeof(*explanation));
    free(explanation);
    return public_status(status);
}

maelys_datalog_status_t maelys_datalog_result_free(maelys_datalog_result_t *result) {
    if (!result || !result->inner) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    maelys_datalog_solve_result_free(result->inner);
    result->inner = NULL;
    result->owner = NULL;
    memset(result, 0, sizeof(*result));
    free(result);
    return MAELYS_DATALOG_STATUS_OK;
}
