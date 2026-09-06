/* SPDX-License-Identifier: MPL-2.0 */
/* Independent, deliberately simple full-scan positive-Datalog fixed point.
 * A conformance example, not a performance product. Only the public SDK is
 * used; comparisons, negation, filters and explanations are rejected. */
#include <maelys/datalog_backend.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const maelys_datalog_program_t *program;
    maelys_datalog_program_info_t info;
    maelys_datalog_ir_rule_t *rules;
} naive_t;
typedef struct {
    maelys_datalog_public_value_t values[MAELYS_DATALOG_IR_MAX_VARIABLES];
    uint32_t bound;
} bindings_t;
typedef struct {
    naive_t *prepared;
    maelys_datalog_backend_output_t *output;
    maelys_datalog_public_fact_t *facts;
    size_t count, capacity, available;
} work_t;
static int equal_value(const maelys_datalog_public_value_t *a,
                       const maelys_datalog_public_value_t *b) {
    if (a->kind != b->kind)
        return 0;
    if (a->kind == MAELYS_DATALOG_VALUE_SYMBOL)
        return !strcmp(a->as.symbol, b->as.symbol);
    if (a->kind == MAELYS_DATALOG_VALUE_INTEGER)
        return a->as.integer == b->as.integer;
    return a->as.boolean == b->as.boolean;
}
static maelys_datalog_public_value_t constant(const maelys_datalog_ir_term_t *term) {
    maelys_datalog_public_value_t value = {0};
    value.kind = (maelys_datalog_value_kind_t)term->kind;
    if (term->kind == MAELYS_DATALOG_IR_SYMBOL)
        value.as.symbol = term->as.symbol;
    else if (term->kind == MAELYS_DATALOG_IR_INTEGER)
        value.as.integer = term->as.integer;
    else
        value.as.boolean = term->as.boolean;
    return value;
}
static maelys_datalog_status_t add_derived(work_t *w, const maelys_datalog_ir_atom_t *head,
                                           const bindings_t *bindings) {
    maelys_datalog_public_fact_t f = {0};
    f.predicate = head->predicate;
    f.arity = head->arity;
    for (size_t i = 0; i < head->arity; ++i) {
        const maelys_datalog_ir_term_t *t = &head->terms[i];
        if (t->kind == MAELYS_DATALOG_IR_VARIABLE) {
            if (!(bindings->bound & (UINT32_C(1) << t->as.variable)))
                return MAELYS_DATALOG_STATUS_INVALID_STATE;
            f.terms[i] = bindings->values[t->as.variable];
        } else
            f.terms[i] = constant(t);
    }
    for (size_t i = 0; i < w->count; ++i) {
        maelys_datalog_status_t rc = maelys_datalog_backend_charge(w->output, 1u);
        if (rc != MAELYS_DATALOG_STATUS_OK)
            return rc;
        const maelys_datalog_public_fact_t *a = &w->facts[i];
        if (strcmp(a->predicate, f.predicate) || a->arity != f.arity)
            continue;
        size_t j = 0;
        for (; j < f.arity; ++j)
            if (!equal_value(&a->terms[j], &f.terms[j]))
                break;
        if (j == f.arity)
            return MAELYS_DATALOG_STATUS_OK;
    }
    if (w->count == w->capacity)
        return MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE;
    maelys_datalog_status_t rc = maelys_datalog_backend_emit(w->output, &f);
    if (rc == MAELYS_DATALOG_STATUS_OK)
        w->facts[w->count++] = f;
    return rc;
}
static maelys_datalog_status_t join(work_t *w, const maelys_datalog_ir_rule_t *rule, size_t index,
                                    const bindings_t *bindings) {
    if (index == rule->body_count)
        return add_derived(w, &rule->head, bindings);
    const maelys_datalog_ir_atom_t *atom = &rule->body[index].atom;
    for (size_t i = 0; i < w->available; ++i) {
        maelys_datalog_status_t rc = maelys_datalog_backend_charge(w->output, 1u);
        if (rc != MAELYS_DATALOG_STATUS_OK)
            return rc;
        const maelys_datalog_public_fact_t *fact = &w->facts[i];
        if (fact->arity != atom->arity || strcmp(fact->predicate, atom->predicate))
            continue;
        bindings_t next = *bindings;
        size_t t = 0;
        for (; t < atom->arity; ++t) {
            const maelys_datalog_ir_term_t *term = &atom->terms[t];
            if (term->kind == MAELYS_DATALOG_IR_VARIABLE) {
                uint32_t bit = UINT32_C(1) << term->as.variable;
                if (next.bound & bit) {
                    if (!equal_value(&next.values[term->as.variable], &fact->terms[t]))
                        break;
                } else {
                    next.bound |= bit;
                    next.values[term->as.variable] = fact->terms[t];
                }
            } else {
                maelys_datalog_public_value_t value = constant(term);
                if (!equal_value(&value, &fact->terms[t]))
                    break;
            }
        }
        if (t == atom->arity) {
            rc = join(w, rule, index + 1u, &next);
            if (rc != MAELYS_DATALOG_STATUS_OK)
                return rc;
        }
    }
    return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t prepare(const maelys_datalog_program_t *program, void **out) {
    naive_t *s = calloc(1u, sizeof(*s));
    if (!s)
        return MAELYS_DATALOG_STATUS_INTERNAL;
    *out = s;
    s->program = program;
    maelys_datalog_status_t rc = maelys_datalog_program_info(program, &s->info);
    if (rc != MAELYS_DATALOG_STATUS_OK)
        return rc;
    if (s->info.required_capabilities & ~MAELYS_DATALOG_CAP_POSITIVE)
        return MAELYS_DATALOG_STATUS_UNSUPPORTED;
    s->rules = s->info.rule_count ? calloc(s->info.rule_count, sizeof(*s->rules)) : NULL;
    if (s->info.rule_count && !s->rules)
        return MAELYS_DATALOG_STATUS_INTERNAL;
    for (size_t i = 0; i < s->info.rule_count; ++i) {
        rc = maelys_datalog_program_rule(program, i, &s->rules[i]);
        if (rc != MAELYS_DATALOG_STATUS_OK)
            return rc;
    }
    return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t solve(void *state, const maelys_datalog_public_fact_t *inputs,
                                     size_t input_count, maelys_datalog_backend_output_t *output,
                                     void **out_result, maelys_datalog_public_diagnostic_t *diag) {
    (void)diag;
    *out_result = NULL;
    naive_t *s = state;
    work_t w = {0};
    w.prepared = s;
    w.output = output;
    w.capacity = input_count + s->info.fact_count + s->info.max_derived_facts;
    w.facts = calloc(w.capacity ? w.capacity : 1u, sizeof(*w.facts));
    if (!w.facts)
        return MAELYS_DATALOG_STATUS_INTERNAL;
    if (input_count)
        memcpy(w.facts, inputs, input_count * sizeof(*inputs));
    w.count = input_count;
    maelys_datalog_status_t rc = MAELYS_DATALOG_STATUS_OK;
    for (size_t i = 0; i < s->info.fact_count; ++i) {
        maelys_datalog_ir_atom_t atom;
        rc = maelys_datalog_program_fact(s->program, i, &atom);
        if (rc != MAELYS_DATALOG_STATUS_OK)
            goto done;
        maelys_datalog_public_fact_t *f = &w.facts[w.count++];
        f->predicate = atom.predicate;
        f->arity = atom.arity;
        for (size_t j = 0; j < atom.arity; ++j)
            f->terms[j] = constant(&atom.terms[j]);
    }
    /* Each non-converged round adds a fact; capacities bound the round count. */
    for (size_t round = 0; round <= s->info.max_derived_facts; ++round) {
        w.available = w.count;
        for (size_t i = 0; i < s->info.rule_count; ++i) {
            rc = maelys_datalog_backend_charge(output, 1u);
            if (rc != MAELYS_DATALOG_STATUS_OK)
                goto done;
            bindings_t bindings = {0};
            rc = join(&w, &s->rules[i], 0u, &bindings);
            if (rc != MAELYS_DATALOG_STATUS_OK)
                goto done;
        }
        if (w.count == w.available)
            goto done;
    }
    rc = MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE;
done:
    free(w.facts); /* No borrowed input pointers survive the callback. */
    return rc;
}
static void destroy_result(void *state, void *result) {
    (void)state;
    (void)result;
}
static void destroy(void *state) {
    naive_t *s = state;
    if (s) {
        free(s->rules);
        free(s);
    }
}
const maelys_datalog_backend_t *example_naive_backend(void) {
    static const maelys_datalog_backend_t backend = {MAELYS_DATALOG_BACKEND_ABI_VERSION,
                                                     sizeof(maelys_datalog_backend_t),
                                                     "naive",
                                                     "example.naive.v1",
                                                     MAELYS_DATALOG_CAP_POSITIVE |
                                                         MAELYS_DATALOG_CAP_WORK_LIMIT,
                                                     prepare,
                                                     solve,
                                                     NULL,
                                                     NULL,
                                                     destroy_result,
                                                     destroy};
    return &backend;
}
