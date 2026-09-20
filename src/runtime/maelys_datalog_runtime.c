/* SPDX-License-Identifier: MPL-2.0 */
#include "src/public/maelys_datalog_public_internal.h"
#include "src/public/maelys_datalog_values_internal.h"
#include "src/compiler/maelys_datalog_program_internal.h"
#include "src/core/maelys_datalog_prepared_session_internal.h"
#include "src/core/maelys_datalog_solver_internal.h"
#include "src/core/maelys_datalog_filter.h"
#include "src/core/maelys_datalog_query_internal.h"
#include "common/maelys_sha256.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdatomic.h>

struct maelys_datalog_session_config {
    uint64_t required_capabilities;
    uint64_t work_limit;
    unsigned explanation_kinds;
    void *explanation_storage;
    size_t explanation_bytes;
};

static maelys_datalog_status_t configure_explanation_workspace(
    maelys_datalog_session_t *, const maelys_datalog_session_config_t *);
static void destroy_explanation_workspace(maelys_datalog_session_t *);

maelys_datalog_status_t maelys_datalog_session_config_create(
    maelys_datalog_session_config_t **out) {
    if (!out) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    *out = calloc(1u, sizeof(**out));
    return *out ? MAELYS_DATALOG_STATUS_OK : MAELYS_DATALOG_STATUS_INTERNAL;
}
maelys_datalog_status_t maelys_datalog_session_config_set_required_capabilities(
    maelys_datalog_session_config_t *config, uint64_t capabilities) {
    if (!config || (capabilities & ~MAELYS_DATALOG_CAP_ALL))
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    config->required_capabilities = capabilities;
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t maelys_datalog_session_config_get_required_capabilities(
    const maelys_datalog_session_config_t *config, uint64_t *out) {
    if (!config || !out) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    *out = config->required_capabilities;
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t maelys_datalog_session_config_set_work_limit(
    maelys_datalog_session_config_t *config, uint64_t work_limit) {
    if (!config) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    config->work_limit = work_limit;
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t maelys_datalog_session_config_get_work_limit(
    const maelys_datalog_session_config_t *config, uint64_t *out) {
    if (!config || !out) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    *out = config->work_limit;
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t maelys_datalog_session_config_free(
    maelys_datalog_session_config_t *config) {
    free(config);
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t maelys_datalog_session_config_set_explanation_workspace(
    maelys_datalog_session_config_t *config, unsigned kinds) {
    if (!config || (kinds & ~(unsigned)(MAELYS_DATALOG_EXPLAIN_TRUE | MAELYS_DATALOG_EXPLAIN_FALSE)))
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    config->explanation_kinds = kinds;
    config->explanation_storage = NULL;
    config->explanation_bytes = 0;
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t maelys_datalog_session_config_set_explanation_storage(
    maelys_datalog_session_config_t *config, unsigned kinds, void *storage, size_t bytes) {
    if (!config || !kinds ||
        (kinds & ~(unsigned)(MAELYS_DATALOG_EXPLAIN_TRUE | MAELYS_DATALOG_EXPLAIN_FALSE)) ||
        !storage || (uintptr_t)storage % _Alignof(max_align_t) ||
        bytes > UINTPTR_MAX - (uintptr_t)storage)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    config->explanation_kinds = kinds;
    config->explanation_storage = storage;
    config->explanation_bytes = bytes;
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t maelys_datalog_session_create_configured(
    const maelys_datalog_policy_t *policy, size_t index,
    const maelys_datalog_session_config_t *config, maelys_datalog_session_t **out) {
    if (!config) return maelys_datalog_session_create(policy, index, out);
    const maelys_datalog_session_options_t options = {
        .abi_version = MAELYS_DATALOG_BACKEND_ABI_VERSION,
        .struct_size = sizeof(options),
        .backend = NULL,
        .required_capabilities = config->required_capabilities,
        .work_limit = config->work_limit,
    };
    maelys_datalog_status_t rc = maelys_datalog_session_create_ex(policy, index, &options, out);
    if (!rc) {
        rc = configure_explanation_workspace(*out, config);
        if (rc) {
            (void)maelys_datalog_session_free(*out);
            *out = NULL;
        }
    }
    return rc;
}

struct maelys_datalog_result {
    maelys_datalog_session_t *owner;
    void *state;
    maelys_datalog_prepared_explanation_t *explanations;
    maelys_datalog_fact_set_t derived;
    size_t per_predicate[MAELYS_DATALOG_MAX_PREDICATES];
    /* Retained payload; live entries are defined solely by derived.count. */
    maelys_datalog_fact_t facts[MAELYS_DATALOG_MAX_IDB_FACTS];
};
struct maelys_datalog_session {
    maelys_datalog_prepared_session_t *inputs;
    maelys_datalog_program_t program;
    maelys_datalog_backend_t backend;
    char name[64], semantic_id[128], execution_fingerprint[65];
    void *state;
    maelys_datalog_result_t *active;
    maelys_datalog_result_t result_storage;
    uint64_t work_limit;
    int busy;
    int borrows_inputs; /* The reference solves the materialized EDB directly. */
    int reference_backend; /* Selected canonical descriptor, not a claimed name. */
    unsigned explanation_kinds;
    void *explanation_storage;
    size_t explanation_bytes;
    int owns_explanation_storage;
    maelys_datalog_session_t *workspace_next;
    uint64_t result_generation, explanation_generation;
    maelys_datalog_prepared_explanation_t *explanation_cache;
    maelys_datalog_fact_t explanation_query; /* Result-scoped canonical values. */
    /* These phases never overlap: import finishes before canonical export.
     * Reuse bounded session storage rather than allocating per solve. */
    union {
        maelys_datalog_input_fact_t inputs[MAELYS_DATALOG_MAX_EDB_FACTS];
        maelys_datalog_public_fact_t canonical[MAELYS_DATALOG_MAX_EDB_FACTS];
    } solve_scratch;
};
struct maelys_datalog_backend_output {
    maelys_datalog_result_t *result;
    maelys_datalog_status_t error;
    uint64_t work;
    size_t filter_count, filter_cost;
};
struct maelys_datalog_prepared_explanation {
    maelys_datalog_result_t *owner;
    maelys_datalog_prepared_explanation_t *next;
    size_t storage_bytes, text_size;
    maelys_datalog_explanation_kind_t kind;
};
#define EXPLANATION_ALIGNMENT _Alignof(max_align_t)
#define EXPLANATION_HEADER_BYTES \
    ((sizeof(maelys_datalog_prepared_explanation_t) + EXPLANATION_ALIGNMENT - 1u) \
     / EXPLANATION_ALIGNMENT * EXPLANATION_ALIGNMENT)
static maelys_datalog_status_t output_fail(maelys_datalog_backend_output_t *out,
                                           maelys_datalog_status_t rc) {
    if (out && out->error == MAELYS_DATALOG_STATUS_OK)
        out->error = rc;
    return out ? out->error : rc;
}
maelys_datalog_status_t maelys_datalog_backend_charge(maelys_datalog_backend_output_t *out,
                                                      uint64_t units) {
    if (!out || !out->result)
        return output_fail(out, MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    if (out->error)
        return out->error;
    if (units > out->result->owner->work_limit - out->work)
        return output_fail(out, MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
    out->work += units;
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t maelys_datalog_backend_emit(maelys_datalog_backend_output_t *out,
                                                    const maelys_datalog_public_fact_t *in) {
    if (!out || !out->result || !in || !in->predicate || in->arity > MAELYS_DATALOG_MAX_TERMS)
        return output_fail(out, MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    if (out->error)
        return out->error;
    maelys_datalog_result_t *result = out->result;
    maelys_datalog_session_t *s = result->owner;
    const maelys_datalog_ruleset_t *r = &s->inputs->working;
    maelys_datalog_fact_t fact = {0};
    fact.arity = (uint8_t)in->arity;
    if (!maelys_datalog_predicate_registry_find(&r->registry, in->predicate, in->arity,
                                                &fact.predicate_id))
        return output_fail(out, MAELYS_DATALOG_STATUS_INVALID_FIELD);
    const maelys_datalog_predicate_def_t *d =
        maelys_datalog_predicate_registry_get(&r->registry, fact.predicate_id);
    if (!(d->kind_flags & MAELYS_DATALOG_PRED_KIND_IDB) ||
        (d->kind_flags & (MAELYS_DATALOG_PRED_KIND_EDB | MAELYS_DATALOG_PRED_KIND_POLICY_FACT)))
        return output_fail(out, MAELYS_DATALOG_STATUS_INVALID_FIELD);
    int found = 0;
    maelys_datalog_status_t rc = maelys_datalog_resolve_public_terms(
        &s->inputs->working.symbols, in->terms, in->arity, fact.terms, &found, 1);
    if (rc != MAELYS_DATALOG_STATUS_OK)
        return output_fail(out, rc);
    if (!found)
        return output_fail(out, MAELYS_DATALOG_STATUS_INVALID_FIELD);
    if (maelys_datalog_fact_set_contains(&result->derived, &fact))
        return MAELYS_DATALOG_STATUS_OK;
    if (result->derived.count >= MAELYS_DATALOG_MAX_IDB_FACTS ||
        result->per_predicate[fact.predicate_id] >= MAELYS_DATALOG_MAX_FACTS_PER_PRED)
        return output_fail(out, MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
    result->facts[result->derived.count++] = fact;
    result->derived.sorted = 0;
    ++result->per_predicate[fact.predicate_id];
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t maelys_datalog_backend_filter(maelys_datalog_backend_output_t *out,
                                                      const char *name, const char *semantic_id,
                                                      const unsigned char *value,
                                                      size_t value_length,
                                                      const unsigned char *pattern,
                                                      size_t pattern_length, int *matched) {
    if (!out || !out->result || !name || !semantic_id || !matched || (!value && value_length) ||
        (!pattern && pattern_length))
        return output_fail(out, MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    if (out->error)
        return out->error;
    const maelys_datalog_ruleset_t *r = out->result->owner->program.ruleset;
    const maelys_datalog_filter_definition_t *d = maelys_datalog_filter_by_name_in(r->modules, name);
    if (!d || strcmp(semantic_id, d->semantic_id))
        return output_fail(out, MAELYS_DATALOG_STATUS_UNSUPPORTED);
    int declared = 0;
    for (size_t i = 0; i < r->filter_program_count; ++i) {
        const maelys_datalog_filter_program_t *f = &r->filter_programs[i];
        if (f->kind == d->kind && f->pattern_length == pattern_length &&
            (!pattern_length ||
             !memcmp(pattern, r->filter_pattern_pool + f->pattern_offset, pattern_length))) {
            declared = 1;
            break;
        }
    }
    if (!declared)
        return output_fail(out, MAELYS_DATALOG_STATUS_INVALID_FIELD);
    size_t cost = 0;
    maelys_result_t rc = maelys_datalog_filter_cost_in(r->modules, d->kind, value_length, pattern_length, &cost);
    if (rc != MAELYS_OK)
        return output_fail(out, (maelys_datalog_status_t)rc);
    if (out->filter_count >= MAELYS_DATALOG_MAX_FILTER_EVALUATIONS ||
        cost > MAELYS_DATALOG_MAX_FILTER_COST_UNITS - out->filter_cost)
        return output_fail(out, MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
    ++out->filter_count;
    out->filter_cost += cost;
    rc = maelys_datalog_filter_evaluate_in(r->modules, d->kind, value, value_length, pattern, pattern_length,
                                        matched);
    return rc == MAELYS_OK ? MAELYS_DATALOG_STATUS_OK
                           : output_fail(out, (maelys_datalog_status_t)rc);
}

maelys_datalog_status_t maelys_datalog_context_session_create(maelys_datalog_context_t *context,
    const maelys_datalog_policy_t *policy, size_t index, const char *backend_name,
    uint64_t required, uint64_t work_limit, maelys_datalog_session_t **out) {
    if (out) *out = NULL;
    if (!context || !policy || !out) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (!maelys_datalog_context_is_sealed(context)) return MAELYS_DATALOG_STATUS_INVALID_STATE;
    if (index >= policy->set.policy_count) return MAELYS_DATALOG_STATUS_NOT_FOUND;
    if (policy->set.policies[index].modules != context) return MAELYS_DATALOG_STATUS_INVALID_FIELD;
    const maelys_datalog_backend_t *backend = maelys_datalog_context_backend(context, backend_name);
    if (!backend) return MAELYS_DATALOG_STATUS_NOT_FOUND;
    maelys_datalog_session_options_t options = {MAELYS_DATALOG_BACKEND_ABI_VERSION,
        sizeof(options), backend, required, work_limit};
    return maelys_datalog_session_create_ex(policy, index, &options, out);
}

maelys_datalog_status_t
maelys_datalog_session_create_ex(const maelys_datalog_policy_t *policy, size_t index,
                                 const maelys_datalog_session_options_t *options,
                                 maelys_datalog_session_t **out) {
    if (out)
        *out = NULL;
    if (!policy || !out)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (index >= policy->set.policy_count)
        return MAELYS_DATALOG_STATUS_NOT_FOUND;
    if (options && (options->abi_version != MAELYS_DATALOG_BACKEND_ABI_VERSION ||
                    options->struct_size != sizeof(*options) ||
                    (options->required_capabilities & ~MAELYS_DATALOG_CAP_ALL)))
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    const maelys_datalog_backend_t *b =
        options && options->backend ? options->backend : maelys_datalog_backend_reference();
    if (!maelys_datalog_backend_descriptor_valid(b))
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    maelys_datalog_program_t view = {.ruleset = &policy->set.policies[index]};
    maelys_datalog_program_info_t info;
    maelys_datalog_program_info(&view, &info);
    uint64_t required = info.required_capabilities | (options ? options->required_capabilities : 0);
    if (options && options->work_limit)
        required |= MAELYS_DATALOG_CAP_WORK_LIMIT;
    if (required & ~b->capabilities)
        return MAELYS_DATALOG_STATUS_UNSUPPORTED;
    maelys_datalog_session_t *s = calloc(1u, sizeof(*s));
    if (!s)
        return MAELYS_DATALOG_STATUS_INTERNAL;
    maelys_result_t rc = maelys_datalog_prepared_session_create(view.ruleset, &s->inputs);
    if (rc != MAELYS_OK) {
        free(s);
        return (maelys_datalog_status_t)rc;
    }
    s->program.ruleset = &s->inputs->prepared;
    s->program.prepared_inputs = s->inputs;
    s->backend = *b;
    s->reference_backend = b == maelys_datalog_backend_reference();
    s->borrows_inputs = b->solve == maelys_datalog_backend_reference()->solve;
    memcpy(s->name, b->name, strlen(b->name) + 1u);
    memcpy(s->semantic_id, b->semantic_id, strlen(b->semantic_id) + 1u);
    s->backend.name = s->name;
    s->backend.semantic_id = s->semantic_id;
    s->work_limit = options && options->work_limit ? options->work_limit : UINT64_C(1048576);
    char program_fingerprint[65];
    maelys_datalog_status_t fingerprint_status =
        maelys_datalog_program_fingerprint(&s->program, program_fingerprint);
    if (fingerprint_status != MAELYS_DATALOG_STATUS_OK) {
        maelys_datalog_prepared_session_destroy(s->inputs);
        free(s);
        return fingerprint_status;
    }
    char identity[512];
    int n =
        snprintf(identity, sizeof(identity), "maelys-execution-v1\n%s\n%s\n%s\n%llu\n%llu\n%s\n",
                 program_fingerprint, s->name, s->semantic_id, (unsigned long long)required,
                 (unsigned long long)s->work_limit, MAELYS_DATALOG_SIZE_PROFILE_NAME);
    if (n < 0 || (size_t)n >= sizeof(identity) ||
        maelys_sha256_hex((const unsigned char *)identity, (size_t)n, s->execution_fingerprint)) {
        maelys_datalog_prepared_session_destroy(s->inputs);
        free(s);
        return MAELYS_DATALOG_STATUS_INTERNAL;
    }
    maelys_datalog_status_t status =
        maelys_datalog_callback_status(s->backend.prepare(&s->program, &s->state));
    if (status != MAELYS_DATALOG_STATUS_OK) {
        s->backend.destroy(s->state);
        maelys_datalog_prepared_session_destroy(s->inputs);
        free(s);
        return status;
    }
    *out = s;
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t maelys_datalog_session_create(const maelys_datalog_policy_t *policy,
                                                      size_t index,
                                                      maelys_datalog_session_t **out) {
    return maelys_datalog_session_create_ex(policy, index, NULL, out);
}
maelys_datalog_status_t maelys_datalog_session_program(const maelys_datalog_session_t *s,
                                                       const maelys_datalog_program_t **out) {
    if (!s || !out)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    *out = &s->program;
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t maelys_datalog_session_fingerprint(const maelys_datalog_session_t *s,
                                                           char out[65]) {
    if (!s || !out)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    memcpy(out, s->inputs->prepared.sha256, 65u);
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t
maelys_datalog_session_execution_fingerprint(const maelys_datalog_session_t *s, char out[65]) {
    if (!s || !out)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    memcpy(out, s->execution_fingerprint, 65u);
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t maelys_datalog_session_free(maelys_datalog_session_t *s) {
    if (!s)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (s->active || s->busy)
        return MAELYS_DATALOG_STATUS_INVALID_STATE;
    s->busy = 1;
    s->backend.destroy(s->state);
    maelys_datalog_prepared_session_destroy(s->inputs);
    destroy_explanation_workspace(s);
    memset(s, 0, sizeof(*s));
    free(s);
    return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t solve_input_error(
    maelys_datalog_public_diagnostic_t *diag, maelys_datalog_status_t status,
    const char *format, ...) {
    if (diag) {
        diag->source = MAELYS_DATALOG_DIAGNOSTIC_SOLVE;
        diag->code = status;
        snprintf(diag->phase, sizeof(diag->phase), "input");
        va_list args;
        va_start(args, format);
        vsnprintf(diag->message, sizeof(diag->message), format, args);
        va_end(args);
        snprintf(diag->hint, sizeof(diag->hint),
                 "Fact and term indices are zero-based. Correct the batch and retry; no result was published.");
    }
    return status;
}

maelys_datalog_status_t maelys_datalog_session_solve(maelys_datalog_session_t *s,
                                                     const maelys_datalog_public_fact_t *facts,
                                                     size_t count, maelys_datalog_result_t **out,
                                                     maelys_datalog_public_diagnostic_t *diag) {
    if (out)
        *out = NULL;
    maelys_datalog_public_diagnostic_clear(diag);
    if (!s || !out)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (s->busy || s->active)
        return MAELYS_DATALOG_STATUS_INVALID_STATE;
    if (!facts && count)
        return solve_input_error(diag, MAELYS_DATALOG_STATUS_INVALID_ARGUMENT,
                                 "Input batch is NULL but fact_count is %zu.", count);
    if (count > MAELYS_DATALOG_MAX_EDB_FACTS)
        return solve_input_error(diag, MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE,
                                 "Input batch contains %zu facts; EDB fact limit is %u (before deduplication).",
                                 count, MAELYS_DATALOG_MAX_EDB_FACTS);
    maelys_datalog_input_fact_t *inputs = count ? s->solve_scratch.inputs : NULL;
    if (count) memset(inputs, 0, count * sizeof(*inputs));
    maelys_datalog_status_t status = MAELYS_DATALOG_STATUS_OK;
    for (size_t i = 0; i < count && status == MAELYS_DATALOG_STATUS_OK; ++i) {
        if (!facts[i].predicate) {
            status = solve_input_error(diag, MAELYS_DATALOG_STATUS_INVALID_ARGUMENT,
                                       "Invalid fact at index %zu: predicate is NULL.", i);
            break;
        }
        if (facts[i].arity > MAELYS_DATALOG_MAX_TERMS) {
            status = solve_input_error(diag, MAELYS_DATALOG_STATUS_INVALID_ARGUMENT,
                                       "Invalid fact at index %zu (%.63s): arity %zu exceeds limit %u.",
                                       i, facts[i].predicate, facts[i].arity, MAELYS_DATALOG_MAX_TERMS);
            break;
        }
        inputs[i].predicate = facts[i].predicate;
        inputs[i].arity = facts[i].arity;
        for (size_t j = 0; j < facts[i].arity; ++j) {
            status = maelys_datalog_import_public_value(&facts[i].terms[j], 0, &inputs[i].terms[j]);
            if (status != MAELYS_DATALOG_STATUS_OK) {
                solve_input_error(diag, status,
                                  "Invalid fact at index %zu (%.63s), term %zu: %s (kind %d).",
                                  i, facts[i].predicate, j,
                                  facts[i].terms[j].kind == MAELYS_DATALOG_VALUE_SYMBOL
                                      ? "NULL symbol" : "unsupported value kind",
                                  (int)facts[i].terms[j].kind);
                break;
            }
        }
    }
    if (status == MAELYS_DATALOG_STATUS_OK) {
        char message[256] = {0};
        status = (maelys_datalog_status_t)maelys_datalog_prepared_session_materialize_inputs_diagnosed(
            s->inputs, inputs, count, message, sizeof(message));
        if (status != MAELYS_DATALOG_STATUS_OK)
            solve_input_error(diag, status, "%s", message[0] ? message : "Input materialization failed.");
    }
    if (status != MAELYS_DATALOG_STATUS_OK)
        return status;
    /* Canonical public facts exist for external backends only. */
    size_t canonical_count = s->borrows_inputs ? 0 : s->inputs->edb.fact_set.count;
    maelys_datalog_public_fact_t *canonical =
        canonical_count ? s->solve_scratch.canonical : NULL;
    if (canonical_count) memset(canonical, 0, canonical_count * sizeof(*canonical));
    for (size_t i = 0; i < canonical_count; ++i) {
        status = (maelys_datalog_status_t)maelys_datalog_export_fact(
            &s->inputs->working, &s->inputs->edb.fact_set.facts[i], &canonical[i]);
        if (status != MAELYS_DATALOG_STATUS_OK) {
            return status;
        }
    }
    maelys_datalog_result_t *result = &s->result_storage;
    memset(result, 0, offsetof(maelys_datalog_result_t, facts));
    result->owner = s;
    maelys_datalog_fact_set_init(&result->derived, result->facts, MAELYS_DATALOG_MAX_IDB_FACTS);
    maelys_datalog_backend_output_t output = {result, MAELYS_DATALOG_STATUS_OK, 0, 0, 0};
    s->busy = 1;
    status = maelys_datalog_callback_status(
        s->backend.solve(s->state, canonical, canonical_count, &output, &result->state, diag));
    if (output.error)
        status = output.error;
    if (status == MAELYS_DATALOG_STATUS_OK)
        status = (maelys_datalog_status_t)maelys_datalog_fact_set_sort(&result->derived);
    if (status != MAELYS_DATALOG_STATUS_OK) {
        s->backend.destroy_result(s->state, result->state);
        memset(result, 0, offsetof(maelys_datalog_result_t, facts));
        s->busy = 0;
        if (diag && diag->source == MAELYS_DATALOG_DIAGNOSTIC_NONE) {
            diag->source = MAELYS_DATALOG_DIAGNOSTIC_SOLVE;
            diag->code = status;
            snprintf(diag->phase, sizeof(diag->phase), "backend");
            snprintf(diag->message, sizeof(diag->message), "backend failed: %s",
                     maelys_datalog_status_name(status));
        }
        return status;
    }
    s->busy = 0;
    ++s->result_generation; /* Cache is cleared on release, including at wrap. */
    s->active = result;
    *out = result;
    return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t query_predicate(const maelys_datalog_result_t *result,
                                               const char *predicate, size_t arity,
                                               maelys_datalog_predicate_id_t *pid) {
    if (!result || !result->owner || !predicate)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (result->owner->busy)
        return MAELYS_DATALOG_STATUS_INVALID_STATE;
    if (arity > MAELYS_DATALOG_MAX_TERMS)
        return MAELYS_DATALOG_STATUS_INVALID_FIELD;
    return (maelys_datalog_status_t)maelys_datalog_validate_query_predicate(
        result->owner->program.ruleset, predicate, arity, pid);
}
maelys_datalog_status_t maelys_datalog_result_query(const maelys_datalog_result_t *result,
                                                    const char *predicate,
                                                    const maelys_datalog_public_value_t *terms,
                                                    size_t arity, int *present) {
    if (!present)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    maelys_datalog_fact_t fact = {0};
    maelys_datalog_status_t rc = query_predicate(result, predicate, arity, &fact.predicate_id);
    if (rc != MAELYS_DATALOG_STATUS_OK)
        return rc;
    fact.arity = (uint8_t)arity;
    int found;
    rc = maelys_datalog_resolve_public_terms(&result->owner->inputs->working.symbols, terms, arity,
                                             fact.terms, &found, 0);
    if (rc != MAELYS_DATALOG_STATUS_OK)
        return rc;
    int answer = 0;
    if (found) {
        const maelys_datalog_ruleset_t *r = result->owner->program.ruleset;
        for (size_t i = 0; i < r->fact_count; ++i)
            if (maelys_datalog_fact_equals(&r->facts[i], &fact))
                answer = 1;
        if (maelys_datalog_fact_set_contains(&result->owner->inputs->edb.fact_set, &fact) ||
            maelys_datalog_fact_set_contains(&result->derived, &fact))
            answer = 1;
    }
    *present = answer;
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t maelys_datalog_result_enumerate(const maelys_datalog_result_t *result,
                                                        const char *predicate, size_t arity,
                                                        maelys_datalog_public_fact_view_t *out,
                                                        size_t capacity, size_t *count) {
    if (!count || (!out && capacity) || capacity > SIZE_MAX / sizeof(*out))
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    maelys_datalog_predicate_id_t pid;
    maelys_datalog_status_t rc = query_predicate(result, predicate, arity, &pid);
    if (rc != MAELYS_DATALOG_STATUS_OK)
        return rc;
    size_t n = 0;
    for (size_t i = 0; i < result->derived.count; ++i) {
        const maelys_datalog_fact_t *f = &result->facts[i];
        if (f->predicate_id != pid)
            continue;
        if (n < capacity) {
            maelys_datalog_public_fact_view_t v = {0};
            v.arity = f->arity;
            for (size_t t = 0; t < arity; ++t) {
                v.terms[t].kind = (maelys_datalog_value_kind_t)f->terms[t].kind;
                if (f->terms[t].kind == MAELYS_DATALOG_TERM_SYMBOL)
                    v.terms[t].as.symbol_id = f->terms[t].as.symbol;
                else if (f->terms[t].kind == MAELYS_DATALOG_TERM_INT)
                    v.terms[t].as.integer = f->terms[t].as.integer;
                else
                    v.terms[t].as.boolean = f->terms[t].as.boolean;
            }
            out[n] = v;
        }
        ++n;
    }
    *count = n;
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t maelys_datalog_result_derived_fact_count(
    const maelys_datalog_result_t *result, size_t *out_count) {
    if (!result || !result->owner || !out_count)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (result->owner->busy)
        return MAELYS_DATALOG_STATUS_INVALID_STATE;
    *out_count = result->derived.count;
    return MAELYS_DATALOG_STATUS_OK;
}

maelys_datalog_status_t maelys_datalog_result_symbol_text(const maelys_datalog_result_t *result,
                                                          uint32_t id, const char **text,
                                                          size_t *length) {
    if (!result || !result->owner || !text || !length)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    const maelys_datalog_symbol_table_t *symbols = &result->owner->inputs->working.symbols;
    if (!maelys_datalog_symbol_id_is_valid(symbols, id))
        return MAELYS_DATALOG_STATUS_INVALID_STATE;
    *text = maelys_datalog_symbol_text(symbols, id);
    *length = symbols->entries[id - 1u].len;
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t maelys_datalog_session_explanation_storage_bound(
    const maelys_datalog_session_t *s, maelys_datalog_explanation_kind_t kind,
    size_t *bytes, size_t *alignment) {
    if (!s || !bytes || !alignment ||
        (kind != MAELYS_DATALOG_EXPLAIN_TRUE && kind != MAELYS_DATALOG_EXPLAIN_FALSE))
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (s->busy) return MAELYS_DATALOG_STATUS_INVALID_STATE;
    if (!s->reference_backend) return MAELYS_DATALOG_STATUS_UNSUPPORTED;
    size_t n = sizeof(maelys_datalog_explanation_t), a = _Alignof(maelys_datalog_explanation_t);
    if (kind == MAELYS_DATALOG_EXPLAIN_FALSE) {
        maelys_result_t rc = maelys_datalog_why_false_storage_bound(&n, &a);
        if (rc) return (maelys_datalog_status_t)rc;
    }
    if (!a || (a & (a - 1u)) || a > EXPLANATION_ALIGNMENT ||
        n > SIZE_MAX - EXPLANATION_HEADER_BYTES)
        return MAELYS_DATALOG_STATUS_INVALID_STATE;
    *bytes = EXPLANATION_HEADER_BYTES + n;
    *alignment = EXPLANATION_ALIGNMENT;
    return MAELYS_DATALOG_STATUS_OK;
}

static maelys_datalog_status_t explanation_available(
    const maelys_datalog_result_t *result, maelys_datalog_explanation_kind_t kind) {
    if (!result || !result->owner ||
        (kind != MAELYS_DATALOG_EXPLAIN_TRUE && kind != MAELYS_DATALOG_EXPLAIN_FALSE))
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (result->owner->busy || result->owner->active != result)
        return MAELYS_DATALOG_STATUS_INVALID_STATE;
    uint64_t cap = kind == MAELYS_DATALOG_EXPLAIN_TRUE
        ? MAELYS_DATALOG_CAP_EXPLAIN_TRUE : MAELYS_DATALOG_CAP_EXPLAIN_FALSE;
    return result->owner->backend.capabilities & cap
        ? MAELYS_DATALOG_STATUS_OK : MAELYS_DATALOG_STATUS_UNSUPPORTED;
}
maelys_datalog_status_t maelys_datalog_result_explanation_storage_requirements(
    const maelys_datalog_result_t *result, maelys_datalog_explanation_kind_t kind,
    size_t *bytes, size_t *alignment) {
    if (!bytes || !alignment) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    maelys_datalog_status_t rc = explanation_available(result, kind);
    if (rc) return rc;
    maelys_datalog_session_t *s = result->owner;
    size_t n = 0, a = 0;
    s->busy = 1;
    rc = maelys_datalog_callback_status(s->backend.explanation_storage_requirements(
        s->state, result->state, kind, &n, &a));
    s->busy = 0;
    if (rc) return rc;
    if (!n || !a || (a & (a - 1u)) || a > EXPLANATION_ALIGNMENT ||
        n > SIZE_MAX - EXPLANATION_HEADER_BYTES)
        return MAELYS_DATALOG_STATUS_INVALID_STATE;
    *bytes = EXPLANATION_HEADER_BYTES + n;
    *alignment = EXPLANATION_ALIGNMENT;
    return MAELYS_DATALOG_STATUS_OK;
}
static int explanation_overlap(const void *a, size_t an, const void *b, size_t bn) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x <= y ? y - x < an : x - y < bn;
}

/* Workspace ownership is checked only at creation/destruction, not on the hot
 * path. The intrusive registry allocates nothing. Separate sessions may be
 * created concurrently; no callback or allocator runs while holding the lock. */
static atomic_flag workspace_lock = ATOMIC_FLAG_INIT;
static maelys_datalog_session_t *workspaces;
static void lock_workspaces(void) {
    while (atomic_flag_test_and_set_explicit(&workspace_lock, memory_order_acquire)) {}
}
static void unlock_workspaces(void) {
    atomic_flag_clear_explicit(&workspace_lock, memory_order_release);
}
static maelys_datalog_status_t configure_explanation_workspace(
    maelys_datalog_session_t *s, const maelys_datalog_session_config_t *config) {
    if (!config->explanation_kinds) return MAELYS_DATALOG_STATUS_OK;
    if (!s->reference_backend) return MAELYS_DATALOG_STATUS_UNSUPPORTED;
    size_t bound = 0;
    for (unsigned k = MAELYS_DATALOG_EXPLAIN_TRUE; k <= MAELYS_DATALOG_EXPLAIN_FALSE; k <<= 1) {
        if (!(config->explanation_kinds & k)) continue;
        uint64_t capability = k == MAELYS_DATALOG_EXPLAIN_TRUE
            ? MAELYS_DATALOG_CAP_EXPLAIN_TRUE : MAELYS_DATALOG_CAP_EXPLAIN_FALSE;
        if (!(s->backend.capabilities & capability)) return MAELYS_DATALOG_STATUS_UNSUPPORTED;
        size_t bytes, alignment;
        maelys_datalog_status_t rc = maelys_datalog_session_explanation_storage_bound(
            s, (maelys_datalog_explanation_kind_t)k, &bytes, &alignment);
        if (rc) return rc;
        if (bytes > bound) bound = bytes;
    }
    void *storage = config->explanation_storage;
    size_t bytes = storage ? config->explanation_bytes : bound;
    if (bytes < bound) return MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL;
    if (!storage) {
        storage = malloc(bytes);
        if (!storage) return MAELYS_DATALOG_STATUS_INTERNAL;
    }
    lock_workspaces();
    for (maelys_datalog_session_t *p = workspaces; p; p = p->workspace_next) {
        if (explanation_overlap(storage, bytes, p->explanation_storage, p->explanation_bytes)) {
            unlock_workspaces();
            if (!config->explanation_storage) free(storage);
            return MAELYS_DATALOG_STATUS_INVALID_STATE;
        }
    }
    s->explanation_storage = storage;
    s->explanation_bytes = bytes;
    s->owns_explanation_storage = !config->explanation_storage;
    s->explanation_kinds = config->explanation_kinds;
    s->workspace_next = workspaces;
    workspaces = s;
    unlock_workspaces();
    return MAELYS_DATALOG_STATUS_OK;
}
static void destroy_explanation_workspace(maelys_datalog_session_t *s) {
    if (!s->explanation_storage) return;
    lock_workspaces();
    maelys_datalog_session_t **link = &workspaces;
    while (*link != s) link = &(*link)->workspace_next;
    *link = s->workspace_next;
    unlock_workspaces();
    if (s->owns_explanation_storage) free(s->explanation_storage);
}
maelys_datalog_status_t maelys_datalog_result_prepare_explanation(
    maelys_datalog_result_t *result, maelys_datalog_explanation_kind_t kind,
    const char *predicate, const maelys_datalog_public_value_t *terms, size_t arity,
    void *storage, size_t bytes, maelys_datalog_prepared_explanation_t **out) {
    if (!storage || !out || (uintptr_t)storage % EXPLANATION_ALIGNMENT ||
        bytes > UINTPTR_MAX - (uintptr_t)storage)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    maelys_datalog_status_t rc = explanation_available(result, kind);
    if (rc) return rc;
    maelys_datalog_predicate_id_t pid;
    rc = query_predicate(result, predicate, arity, &pid);
    if (rc) return rc;
    maelys_datalog_session_t *s = result->owner;
    maelys_datalog_term_t checked[MAELYS_DATALOG_MAX_TERMS];
    int found;
    rc = maelys_datalog_resolve_public_terms(&s->inputs->working.symbols, terms, arity, checked,
                                             &found, 0);
    if (rc) return rc;
    if (!found) return MAELYS_DATALOG_STATUS_NOT_FOUND;
    size_t needed_bytes, alignment;
    rc = maelys_datalog_result_explanation_storage_requirements(result, kind, &needed_bytes, &alignment);
    if (rc) return rc;
    if (bytes < needed_bytes) return MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL;
    for (maelys_datalog_prepared_explanation_t *p = result->explanations; p; p = p->next)
        if (explanation_overlap(storage, bytes, p, p->storage_bytes))
            return MAELYS_DATALOG_STATUS_INVALID_STATE;
    size_t needed = SIZE_MAX;
    s->busy = 1;
    rc = maelys_datalog_callback_status(s->backend.explanation_prepare(
        s->state, result->state, kind, predicate, terms, arity,
        (unsigned char *)storage + EXPLANATION_HEADER_BYTES,
        needed_bytes - EXPLANATION_HEADER_BYTES, &needed));
    s->busy = 0;
    if (rc) return rc;
    if (needed == SIZE_MAX) return MAELYS_DATALOG_STATUS_INVALID_STATE;
    maelys_datalog_prepared_explanation_t *p = storage;
    *p = (maelys_datalog_prepared_explanation_t){
        result, result->explanations, bytes, needed, kind};
    result->explanations = p;
    *out = p;
    return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t prepared_valid(const maelys_datalog_prepared_explanation_t *p) {
    if (!p) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (!p->owner) return MAELYS_DATALOG_STATUS_INVALID_STATE;
    maelys_datalog_status_t rc = explanation_available(p->owner, p->kind);
    if (rc) return rc;
    for (const maelys_datalog_prepared_explanation_t *q = p->owner->explanations; q; q = q->next)
        if (q == p) return MAELYS_DATALOG_STATUS_OK;
    return MAELYS_DATALOG_STATUS_INVALID_STATE;
}
maelys_datalog_status_t maelys_datalog_prepared_explanation_text_size(
    const maelys_datalog_prepared_explanation_t *p, size_t *out) {
    if (!out) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    maelys_datalog_status_t rc = prepared_valid(p);
    if (rc) return rc;
    *out = p->text_size;
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t maelys_datalog_prepared_explanation_write_text(
    const maelys_datalog_prepared_explanation_t *p, char *text, size_t capacity) {
    if (!text) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    maelys_datalog_status_t rc = prepared_valid(p);
    if (rc) return rc;
    if (capacity > UINTPTR_MAX - (uintptr_t)text ||
        explanation_overlap(text, capacity, p, p->storage_bytes))
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (capacity <= p->text_size) {
        if (capacity) text[0] = '\0';
        return MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE;
    }
    maelys_datalog_session_t *s = p->owner->owner;
    s->busy = 1;
    rc = maelys_datalog_callback_status(s->backend.explanation_write_text(
        s->state, p->owner->state, p->kind,
        (const unsigned char *)p + EXPLANATION_HEADER_BYTES, text, capacity));
    s->busy = 0;
    if (!rc && memchr(text, '\0', p->text_size + 1u) != text + p->text_size)
        return MAELYS_DATALOG_STATUS_INVALID_STATE;
    return rc;
}
maelys_datalog_status_t maelys_datalog_prepared_explanation_release(
    maelys_datalog_prepared_explanation_t *p) {
    maelys_datalog_status_t rc = prepared_valid(p);
    if (rc) return rc;
    maelys_datalog_prepared_explanation_t **link = &p->owner->explanations;
    while (*link != p) link = &(*link)->next;
    *link = p->next;
    memset(p, 0, sizeof(*p));
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t maelys_datalog_result_explain_text_in(
    maelys_datalog_result_t *result, maelys_datalog_explanation_kind_t kind,
    const char *predicate, const maelys_datalog_public_value_t *terms, size_t arity,
    void *storage, size_t storage_bytes, char *text, size_t capacity, size_t *required) {
    if (!text || !required || !storage ||
        capacity > UINTPTR_MAX - (uintptr_t)text ||
        explanation_overlap(text, capacity, storage, storage_bytes))
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    maelys_datalog_prepared_explanation_t *p = NULL;
    maelys_datalog_status_t rc = maelys_datalog_result_prepare_explanation(
        result, kind, predicate, terms, arity, storage, storage_bytes, &p);
    if (rc) return rc;
    rc = maelys_datalog_prepared_explanation_text_size(p, required);
    if (!rc) rc = maelys_datalog_prepared_explanation_write_text(p, text, capacity);
    maelys_datalog_status_t released = maelys_datalog_prepared_explanation_release(p);
    return rc ? rc : released;
}

static void clear_explanation_cache(maelys_datalog_session_t *s) {
    if (s->explanation_cache) {
        (void)maelys_datalog_prepared_explanation_release(s->explanation_cache);
        s->explanation_cache = NULL;
    }
}
static maelys_datalog_status_t cached_explain_text(maelys_datalog_result_t *result,
    maelys_datalog_explanation_kind_t kind, const char *predicate,
    const maelys_datalog_public_value_t *terms, size_t arity,
    char *text, size_t capacity, size_t *required) {
    maelys_datalog_session_t *s = result->owner;
    if (!(s->explanation_kinds & (unsigned)kind)) return MAELYS_DATALOG_STATUS_UNSUPPORTED;
    /* A borrowed workspace is also visible to its owner. Reject output aliases
     * before touching the cache: even a size query must not overwrite its handle. */
    if (explanation_overlap(required, sizeof(*required), s->explanation_storage, s->explanation_bytes) ||
        (text && (capacity > UINTPTR_MAX - (uintptr_t)text ||
                  explanation_overlap(text, capacity, s->explanation_storage, s->explanation_bytes) ||
                  explanation_overlap(required, sizeof(*required), text, capacity))))
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    maelys_datalog_fact_t query = {0};
    maelys_datalog_status_t rc = query_predicate(result, predicate, arity, &query.predicate_id);
    if (rc) return rc;
    query.arity = (uint8_t)arity;
    int found = 0;
    rc = maelys_datalog_resolve_public_terms(&s->inputs->working.symbols,
        terms, arity, query.terms, &found, 0);
    if (rc) return rc;
    if (!found) return MAELYS_DATALOG_STATUS_NOT_FOUND;
    if (!s->explanation_cache || s->explanation_generation != s->result_generation ||
        s->explanation_cache->kind != kind ||
        !maelys_datalog_fact_equals(&query, &s->explanation_query)) {
        clear_explanation_cache(s);
        rc = maelys_datalog_result_prepare_explanation(result, kind, predicate, terms, arity,
            s->explanation_storage, s->explanation_bytes, &s->explanation_cache);
        if (rc) return rc;
        s->explanation_query = query;
        s->explanation_generation = s->result_generation;
    }
    *required = s->explanation_cache->text_size;
    return text ? maelys_datalog_prepared_explanation_write_text(s->explanation_cache, text, capacity)
                : MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t result_explain_text(const maelys_datalog_result_t *result,
    const char *predicate, const maelys_datalog_public_value_t *terms, size_t arity,
    char *text, size_t capacity, size_t *required, int why_false) {
    if (!required || (!text && capacity)) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    maelys_datalog_predicate_id_t pid;
    maelys_datalog_status_t rc = query_predicate(result, predicate, arity, &pid);
    if (rc) return rc;
    maelys_datalog_explanation_kind_t kind = why_false ? MAELYS_DATALOG_EXPLAIN_FALSE : MAELYS_DATALOG_EXPLAIN_TRUE;
    rc = explanation_available(result, kind);
    if (rc) return rc;
    if (result->owner->explanation_kinds)
        return cached_explain_text((maelys_datalog_result_t *)result, kind,
                                  predicate, terms, arity, text, capacity, required);
    size_t bytes, alignment;
    rc = maelys_datalog_result_explanation_storage_requirements(result, kind, &bytes, &alignment);
    if (rc) return rc;
    void *storage = malloc(bytes);
    if (!storage) return MAELYS_DATALOG_STATUS_INTERNAL;
    maelys_datalog_prepared_explanation_t *p = NULL;
    rc = maelys_datalog_result_prepare_explanation((maelys_datalog_result_t *)result,
        kind, predicate, terms, arity, storage, bytes, &p);
    if (!rc) {
        *required = p->text_size;
        if (text) rc = maelys_datalog_prepared_explanation_write_text(p, text, capacity);
        (void)maelys_datalog_prepared_explanation_release(p);
    }
    free(storage);
    return rc;
}
maelys_datalog_status_t
maelys_datalog_result_explain_true_text(const maelys_datalog_result_t *result,
                                        const char *predicate,
                                        const maelys_datalog_public_value_t *terms, size_t arity,
                                        char *text, size_t capacity, size_t *required) {
    return result_explain_text(result, predicate, terms, arity, text, capacity, required, 0);
}
maelys_datalog_status_t
maelys_datalog_result_explain_false_text(const maelys_datalog_result_t *result,
                                         const char *predicate,
                                         const maelys_datalog_public_value_t *terms, size_t arity,
                                         char *text, size_t capacity, size_t *required) {
    return result_explain_text(result, predicate, terms, arity, text, capacity, required, 1);
}
maelys_datalog_status_t maelys_datalog_result_free(maelys_datalog_result_t *result) {
    if (!result || !result->owner)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    maelys_datalog_session_t *s = result->owner;
    if (s->busy || s->active != result)
        return MAELYS_DATALOG_STATUS_INVALID_STATE;
    for (maelys_datalog_prepared_explanation_t *p = result->explanations; p; p = p->next)
        if (p != s->explanation_cache) return MAELYS_DATALOG_STATUS_INVALID_STATE;
    clear_explanation_cache(s);
    s->busy = 1;
    s->backend.destroy_result(s->state, result->state);
    s->active = NULL;
    s->busy = 0;
    memset(result, 0, offsetof(maelys_datalog_result_t, facts));
    return MAELYS_DATALOG_STATUS_OK;
}
