#include "src/public/maelys_datalog_public_internal.h"
#include "src/compiler/maelys_datalog_program_internal.h"

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
    return maelys_datalog_policy_load_frontend(domain, policy_id, source, source_length,
        NULL, out_policy, out_diagnostic);
}

maelys_datalog_status_t maelys_datalog_policy_load_frontend(
    const char *domain, const char *policy_id, const char *source, size_t source_length,
    const maelys_datalog_frontend_t *frontend, maelys_datalog_policy_t **out_policy,
    maelys_datalog_public_diagnostic_t *out_diagnostic) {
    maelys_datalog_public_diagnostic_clear(out_diagnostic);
    maelys_datalog_policy_t *policy = NULL;
    maelys_datalog_status_t rc = allocate_policy(out_policy, &policy);
    if (rc != MAELYS_DATALOG_STATUS_OK) return rc;
    maelys_result_t status = maelys_datalog_compile_frontend(domain, policy_id, source,
        source_length, frontend, &policy->set.policies[0], out_diagnostic);
    if (status != MAELYS_OK) {
        if (out_diagnostic && out_diagnostic->source == MAELYS_DATALOG_DIAGNOSTIC_NONE) {
            out_diagnostic->source = MAELYS_DATALOG_DIAGNOSTIC_LOAD;
            out_diagnostic->code = status;
            snprintf(out_diagnostic->phase, sizeof(out_diagnostic->phase), "frontend");
            snprintf(out_diagnostic->message, sizeof(out_diagnostic->message), "frontend compilation failed");
        }
        memset(policy, 0, sizeof(*policy)); free(policy);
        return public_status(status);
    }
    policy->set.policy_count = 1u;
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
        maelys_datalog_copy_load_diagnostic(out_diagnostic, &diagnostic);
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
