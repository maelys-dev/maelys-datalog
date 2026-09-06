/* SPDX-License-Identifier: MPL-2.0 */
#include "src/public/maelys_datalog_public_internal.h"
#include "src/public/maelys_datalog_values_internal.h"
#include "src/compiler/maelys_datalog_program_internal.h"
#include "src/core/maelys_datalog_prepared_session_internal.h"
#include "src/core/maelys_datalog_filter.h"
#include "src/core/maelys_datalog_query_internal.h"
#include "common/maelys_sha256.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct maelys_datalog_session {
    maelys_datalog_prepared_session_t *inputs;
    maelys_datalog_program_t program;
    maelys_datalog_backend_t backend;
    char name[64], semantic_id[128], execution_fingerprint[65];
    void *state;
    maelys_datalog_result_t *active;
    uint64_t work_limit;
    int busy;
};
struct maelys_datalog_result {
    maelys_datalog_session_t *owner;
    void *state;
    maelys_datalog_fact_set_t derived;
    maelys_datalog_fact_t facts[MAELYS_DATALOG_MAX_IDB_FACTS];
    size_t per_predicate[MAELYS_DATALOG_MAX_PREDICATES];
};
struct maelys_datalog_backend_output {
    maelys_datalog_result_t *result;
    maelys_datalog_status_t error;
    uint64_t work;
    size_t filter_count, filter_cost;
};
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
    const maelys_datalog_filter_definition_t *d = maelys_datalog_filter_by_name(name);
    if (!d || strcmp(semantic_id, d->semantic_id))
        return output_fail(out, MAELYS_DATALOG_STATUS_UNSUPPORTED);
    const maelys_datalog_ruleset_t *r = out->result->owner->program.ruleset;
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
    maelys_result_t rc = maelys_datalog_filter_cost(d->kind, value_length, pattern_length, &cost);
    if (rc != MAELYS_OK)
        return output_fail(out, (maelys_datalog_status_t)rc);
    if (out->filter_count >= MAELYS_DATALOG_MAX_FILTER_EVALUATIONS ||
        cost > MAELYS_DATALOG_MAX_FILTER_COST_UNITS - out->filter_cost)
        return output_fail(out, MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
    ++out->filter_count;
    out->filter_cost += cost;
    rc = maelys_datalog_filter_evaluate(d->kind, value, value_length, pattern, pattern_length,
                                        matched);
    return rc == MAELYS_OK ? MAELYS_DATALOG_STATUS_OK
                           : output_fail(out, (maelys_datalog_status_t)rc);
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
    if (b->abi_version != MAELYS_DATALOG_BACKEND_ABI_VERSION || b->struct_size != sizeof(*b) ||
        !b->prepare || !b->solve || !b->destroy || !b->destroy_result ||
        !maelys_datalog_identity_valid(b->name, 64u, 1) ||
        !maelys_datalog_identity_valid(b->semantic_id, 128u, 0) ||
        (b->capabilities & ~MAELYS_DATALOG_CAP_ALL) ||
        ((b->capabilities & MAELYS_DATALOG_CAP_EXPLAIN_TRUE) && !b->explain_true) ||
        ((b->capabilities & MAELYS_DATALOG_CAP_EXPLAIN_FALSE) && !b->explain_false))
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    maelys_datalog_program_t view = {&policy->set.policies[index]};
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
    s->backend = *b;
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
    memset(s, 0, sizeof(*s));
    free(s);
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t maelys_datalog_session_solve(maelys_datalog_session_t *s,
                                                     const maelys_datalog_public_fact_t *facts,
                                                     size_t count, maelys_datalog_result_t **out,
                                                     maelys_datalog_public_diagnostic_t *diag) {
    if (out)
        *out = NULL;
    maelys_datalog_public_diagnostic_clear(diag);
    if (!s || !out || (!facts && count))
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (s->busy || s->active)
        return MAELYS_DATALOG_STATUS_INVALID_STATE;
    if (count > MAELYS_DATALOG_MAX_EDB_FACTS)
        return MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE;
    maelys_datalog_input_fact_t *inputs = count ? calloc(count, sizeof(*inputs)) : NULL;
    if (count && !inputs)
        return MAELYS_DATALOG_STATUS_INTERNAL;
    maelys_datalog_status_t status = MAELYS_DATALOG_STATUS_OK;
    for (size_t i = 0; i < count && status == MAELYS_DATALOG_STATUS_OK; ++i) {
        if (!facts[i].predicate || facts[i].arity > MAELYS_DATALOG_MAX_TERMS) {
            status = MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
            break;
        }
        inputs[i].predicate = facts[i].predicate;
        inputs[i].arity = facts[i].arity;
        for (size_t j = 0; j < facts[i].arity; ++j) {
            status = maelys_datalog_import_public_value(&facts[i].terms[j], 0, &inputs[i].terms[j]);
            if (status != MAELYS_DATALOG_STATUS_OK)
                break;
        }
    }
    if (status == MAELYS_DATALOG_STATUS_OK)
        status = (maelys_datalog_status_t)maelys_datalog_prepared_session_materialize_inputs(
            s->inputs, inputs, count);
    free(inputs);
    if (status != MAELYS_DATALOG_STATUS_OK) {
        if (diag) {
            diag->source = MAELYS_DATALOG_DIAGNOSTIC_SOLVE;
            diag->code = status;
            snprintf(diag->phase, sizeof(diag->phase), "input");
            snprintf(diag->message, sizeof(diag->message), "invalid solve input");
        }
        return status;
    }
    size_t canonical_count = s->inputs->edb.fact_set.count;
    maelys_datalog_public_fact_t *canonical =
        canonical_count ? calloc(canonical_count, sizeof(*canonical)) : NULL;
    if (canonical_count && !canonical)
        return MAELYS_DATALOG_STATUS_INTERNAL;
    for (size_t i = 0; i < canonical_count; ++i) {
        status = (maelys_datalog_status_t)maelys_datalog_export_fact(
            &s->inputs->working, &s->inputs->edb.fact_set.facts[i], &canonical[i]);
        if (status != MAELYS_DATALOG_STATUS_OK) {
            free(canonical);
            return status;
        }
    }
    maelys_datalog_result_t *result = calloc(1u, sizeof(*result));
    if (!result) {
        free(canonical);
        return MAELYS_DATALOG_STATUS_INTERNAL;
    }
    result->owner = s;
    maelys_datalog_fact_set_init(&result->derived, result->facts, MAELYS_DATALOG_MAX_IDB_FACTS);
    maelys_datalog_backend_output_t output = {result, MAELYS_DATALOG_STATUS_OK, 0, 0, 0};
    s->busy = 1;
    status = maelys_datalog_callback_status(
        s->backend.solve(s->state, canonical, canonical_count, &output, &result->state, diag));
    free(canonical);
    if (output.error)
        status = output.error;
    if (status == MAELYS_DATALOG_STATUS_OK)
        status = (maelys_datalog_status_t)maelys_datalog_fact_set_sort(&result->derived);
    if (status != MAELYS_DATALOG_STATUS_OK) {
        s->backend.destroy_result(s->state, result->state);
        memset(result, 0, sizeof(*result));
        free(result);
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
static maelys_datalog_status_t result_explain_text(const maelys_datalog_result_t *result,
                                                   const char *predicate,
                                                   const maelys_datalog_public_value_t *terms,
                                                   size_t arity, char *text, size_t capacity,
                                                   size_t *required, int why_false) {
    if (!result || !result->owner || !required || (!text && capacity))
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    maelys_datalog_predicate_id_t pid;
    maelys_datalog_status_t rc = query_predicate(result, predicate, arity, &pid);
    if (rc != MAELYS_DATALOG_STATUS_OK)
        return rc;
    maelys_datalog_session_t *s = result->owner;
    if (!(s->backend.capabilities &
          (why_false ? MAELYS_DATALOG_CAP_EXPLAIN_FALSE : MAELYS_DATALOG_CAP_EXPLAIN_TRUE)))
        return MAELYS_DATALOG_STATUS_UNSUPPORTED;
    maelys_datalog_term_t checked[MAELYS_DATALOG_MAX_TERMS];
    int found;
    rc = maelys_datalog_resolve_public_terms(&s->inputs->working.symbols, terms, arity, checked,
                                             &found, 0);
    if (rc != MAELYS_DATALOG_STATUS_OK)
        return rc;
    if (!found)
        return MAELYS_DATALOG_STATUS_NOT_FOUND;
    size_t needed = SIZE_MAX;
    s->busy = 1;
    rc = maelys_datalog_callback_status(
        (why_false ? s->backend.explain_false : s->backend.explain_true)(
            s->state, result->state, predicate, terms, arity, text, capacity, &needed));
    s->busy = 0;
    if (needed != SIZE_MAX)
        *required = needed;
    else if (rc == MAELYS_DATALOG_STATUS_OK)
        return MAELYS_DATALOG_STATUS_INVALID_STATE;
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
    s->busy = 1;
    s->backend.destroy_result(s->state, result->state);
    s->active = NULL;
    s->busy = 0;
    memset(result, 0, sizeof(*result));
    free(result);
    return MAELYS_DATALOG_STATUS_OK;
}
