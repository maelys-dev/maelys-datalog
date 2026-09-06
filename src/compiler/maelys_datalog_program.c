/* SPDX-License-Identifier: MPL-2.0 */
#include "src/compiler/maelys_datalog_program_internal.h"
#include "src/core/maelys_datalog_filter.h"
#include "src/core/maelys_datalog_domain_registry.h"
#include "common/maelys_sha256.h"
#include "common/maelys_utf8.h"
#include <stdio.h>
#include <string.h>

_Static_assert(MAELYS_DATALOG_IR_MAX_BODY == MAELYS_DATALOG_MAX_BODY_LITERALS, "IR body limit");
_Static_assert(MAELYS_DATALOG_IR_MAX_EXPRESSIONS == MAELYS_DATALOG_MAX_ARITH_EXPR_NODES,
               "IR expressions");
_Static_assert(MAELYS_DATALOG_IR_MAX_VARIABLES == MAELYS_DATALOG_MAX_RULE_VARIABLES,
               "IR variables");
_Static_assert((int)MAELYS_DATALOG_IR_SYMBOL == (int)MAELYS_DATALOG_TERM_SYMBOL,
               "IR enum IR_SYMBOL");
_Static_assert((int)MAELYS_DATALOG_IR_INTEGER == (int)MAELYS_DATALOG_TERM_INT,
               "IR enum IR_INTEGER");
_Static_assert((int)MAELYS_DATALOG_IR_BOOLEAN == (int)MAELYS_DATALOG_TERM_BOOL,
               "IR enum IR_BOOLEAN");
_Static_assert((int)MAELYS_DATALOG_IR_VARIABLE == (int)MAELYS_DATALOG_TERM_VAR,
               "IR enum IR_VARIABLE");
_Static_assert((int)MAELYS_DATALOG_IR_ATOM == (int)MAELYS_DATALOG_LITERAL_ATOM, "IR enum IR_ATOM");
_Static_assert((int)MAELYS_DATALOG_IR_COMPARISON == (int)MAELYS_DATALOG_LITERAL_COMPARISON,
               "IR enum IR_COMPARISON");
_Static_assert((int)MAELYS_DATALOG_IR_NEGATION == (int)MAELYS_DATALOG_LITERAL_NEGATED_ATOM,
               "IR enum IR_NEGATION");
_Static_assert((int)MAELYS_DATALOG_IR_FILTER == (int)MAELYS_DATALOG_LITERAL_FILTER,
               "IR enum IR_FILTER");
_Static_assert((int)MAELYS_DATALOG_IR_EQ == (int)MAELYS_DATALOG_CMP_EQ, "IR enum IR_EQ");
_Static_assert((int)MAELYS_DATALOG_IR_NE == (int)MAELYS_DATALOG_CMP_NEQ, "IR enum IR_NE");
_Static_assert((int)MAELYS_DATALOG_IR_LT == (int)MAELYS_DATALOG_CMP_LT, "IR enum IR_LT");
_Static_assert((int)MAELYS_DATALOG_IR_LE == (int)MAELYS_DATALOG_CMP_LTE, "IR enum IR_LE");
_Static_assert((int)MAELYS_DATALOG_IR_GT == (int)MAELYS_DATALOG_CMP_GT, "IR enum IR_GT");
_Static_assert((int)MAELYS_DATALOG_IR_GE == (int)MAELYS_DATALOG_CMP_GTE, "IR enum IR_GE");
_Static_assert((int)MAELYS_DATALOG_IR_EXPR_INTEGER == (int)MAELYS_DATALOG_ARITH_EXPR_INT_LITERAL,
               "IR enum IR_EXPR_INTEGER");
_Static_assert((int)MAELYS_DATALOG_IR_EXPR_VARIABLE == (int)MAELYS_DATALOG_ARITH_EXPR_VAR,
               "IR enum IR_EXPR_VARIABLE");
_Static_assert((int)MAELYS_DATALOG_IR_EXPR_ADD == (int)MAELYS_DATALOG_ARITH_EXPR_ADD,
               "IR enum IR_EXPR_ADD");
_Static_assert((int)MAELYS_DATALOG_IR_EXPR_SUB == (int)MAELYS_DATALOG_ARITH_EXPR_SUB,
               "IR enum IR_EXPR_SUB");
_Static_assert((int)MAELYS_DATALOG_IR_EXPR_MUL == (int)MAELYS_DATALOG_ARITH_EXPR_MUL,
               "IR enum IR_EXPR_MUL");

maelys_datalog_status_t maelys_datalog_callback_status(maelys_datalog_status_t s) {
    return s <= MAELYS_DATALOG_STATUS_OK && s >= MAELYS_DATALOG_STATUS_INVALID_STATE
               ? s
               : MAELYS_DATALOG_STATUS_INTERNAL;
}
void maelys_datalog_copy_load_diagnostic(maelys_datalog_public_diagnostic_t *out,
                                         const maelys_datalog_diagnostic_t *in) {
    if (!out || !in)
        return;
    memset(out, 0, sizeof(*out));
    out->source = MAELYS_DATALOG_DIAGNOSTIC_LOAD;
    out->code = in->code;
    out->line = in->line;
    out->column = in->column;
    snprintf(out->phase, sizeof(out->phase), "%s", in->phase);
    snprintf(out->message, sizeof(out->message), "%s", in->message);
    snprintf(out->hint, sizeof(out->hint), "%s", in->hint);
}
void maelys_datalog_copy_solve_diagnostic(maelys_datalog_public_diagnostic_t *out,
                                          const maelys_datalog_solve_diagnostic_t *in) {
    if (!out || !in)
        return;
    memset(out, 0, sizeof(*out));
    out->source = MAELYS_DATALOG_DIAGNOSTIC_SOLVE;
    out->code = in->category;
    snprintf(out->phase, sizeof(out->phase), "solve");
    snprintf(out->message, sizeof(out->message), "%s",
             maelys_datalog_solve_diagnostic_category_name(in->category));
}

static maelys_result_t export_term(const maelys_datalog_ruleset_t *r,
                                   const maelys_datalog_term_t *in, maelys_datalog_ir_term_t *out) {
    memset(out, 0, sizeof(*out));
    out->kind = (maelys_datalog_ir_term_kind_t)in->kind;
    switch (in->kind) {
    case MAELYS_DATALOG_TERM_SYMBOL:
        out->as.symbol = maelys_datalog_symbol_text(&r->symbols, in->as.symbol);
        return out->as.symbol ? MAELYS_OK : MAELYS_ERR_INVALID_STATE;
    case MAELYS_DATALOG_TERM_INT:
        out->as.integer = in->as.integer;
        break;
    case MAELYS_DATALOG_TERM_BOOL:
        out->as.boolean = in->as.boolean;
        break;
    case MAELYS_DATALOG_TERM_VAR:
        out->as.variable = in->as.variable;
        break;
    default:
        return MAELYS_ERR_INVALID_STATE;
    }
    return MAELYS_OK;
}
static maelys_result_t export_atom(const maelys_datalog_ruleset_t *r,
                                   const maelys_datalog_fact_t *in, maelys_datalog_ir_atom_t *out) {
    memset(out, 0, sizeof(*out));
    const maelys_datalog_predicate_def_t *d =
        maelys_datalog_predicate_registry_get(&r->registry, in->predicate_id);
    if (!d || in->arity != d->arity || in->arity > MAELYS_DATALOG_MAX_TERMS)
        return MAELYS_ERR_INVALID_STATE;
    out->predicate = d->name;
    out->arity = in->arity;
    for (size_t i = 0; i < in->arity; ++i) {
        maelys_result_t rc = export_term(r, &in->terms[i], &out->terms[i]);
        if (rc != MAELYS_OK)
            return rc;
    }
    return MAELYS_OK;
}
maelys_result_t maelys_datalog_export_fact(const maelys_datalog_ruleset_t *r,
                                           const maelys_datalog_fact_t *in,
                                           maelys_datalog_public_fact_t *out) {
    maelys_datalog_ir_atom_t a;
    maelys_result_t rc = export_atom(r, in, &a);
    if (rc != MAELYS_OK)
        return rc;
    memset(out, 0, sizeof(*out));
    out->predicate = a.predicate;
    out->arity = a.arity;
    for (size_t i = 0; i < a.arity; ++i) {
        out->terms[i].kind = (maelys_datalog_value_kind_t)a.terms[i].kind;
        switch (a.terms[i].kind) {
        case MAELYS_DATALOG_IR_SYMBOL:
            out->terms[i].as.symbol = a.terms[i].as.symbol;
            break;
        case MAELYS_DATALOG_IR_INTEGER:
            out->terms[i].as.integer = a.terms[i].as.integer;
            break;
        case MAELYS_DATALOG_IR_BOOLEAN:
            out->terms[i].as.boolean = a.terms[i].as.boolean;
            break;
        default:
            return MAELYS_ERR_INVALID_STATE;
        }
    }
    return MAELYS_OK;
}
maelys_datalog_status_t maelys_datalog_program_info(const maelys_datalog_program_t *p,
                                                    maelys_datalog_program_info_t *out) {
    if (!p || !p->ruleset || !out)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    const maelys_datalog_ruleset_t *r = p->ruleset;
    uint64_t caps = MAELYS_DATALOG_CAP_POSITIVE;
    for (size_t i = 0; i < r->rule_count; ++i)
        for (size_t j = 0; j < r->rules[i].body_count; ++j) {
            const maelys_datalog_literal_t *l = &r->rules[i].body[j];
            if (l->kind == MAELYS_DATALOG_LITERAL_NEGATED_ATOM)
                caps |= MAELYS_DATALOG_CAP_NEGATION;
            if (l->kind == MAELYS_DATALOG_LITERAL_COMPARISON)
                caps |= MAELYS_DATALOG_CAP_COMPARISONS;
            if (l->has_arith_expr)
                caps |= MAELYS_DATALOG_CAP_ARITHMETIC;
            if (l->kind == MAELYS_DATALOG_LITERAL_FILTER)
                caps |= MAELYS_DATALOG_CAP_FILTERS;
        }
    *out = (maelys_datalog_program_info_t){MAELYS_DATALOG_PROGRAM_ABI_VERSION,
                                           r->policy_id,
                                           r->domain,
                                           r->sha256,
                                           r->registry.count,
                                           r->fact_count,
                                           r->rule_count,
                                           MAELYS_DATALOG_MAX_EDB_FACTS,
                                           MAELYS_DATALOG_MAX_IDB_FACTS,
                                           MAELYS_DATALOG_MAX_FACTS_PER_PRED,
                                           caps};
    return MAELYS_DATALOG_STATUS_OK;
}

static void hash_number(maelys_sha256_ctx_t *h, uint64_t value) {
    unsigned char bytes[8];
    for (size_t i = 0; i < 8u; ++i)
        bytes[7u - i] = (unsigned char)(value >> (8u * i));
    maelys_sha256_update(h, bytes, sizeof(bytes));
}
static void hash_bytes(maelys_sha256_ctx_t *h, const void *data, size_t n) {
    hash_number(h, n);
    if (n)
        maelys_sha256_update(h, data, n);
}
static void hash_text(maelys_sha256_ctx_t *h, const char *text) {
    hash_bytes(h, text, strlen(text));
}
static void hash_term(maelys_sha256_ctx_t *h, const maelys_datalog_ruleset_t *r,
                      const maelys_datalog_term_t *term) {
    hash_number(h, term->kind);
    if (term->kind == MAELYS_DATALOG_TERM_SYMBOL)
        hash_text(h, maelys_datalog_symbol_text(&r->symbols, term->as.symbol));
    else if (term->kind == MAELYS_DATALOG_TERM_INT)
        hash_number(h, (uint64_t)term->as.integer);
    else if (term->kind == MAELYS_DATALOG_TERM_BOOL)
        hash_number(h, term->as.boolean);
    else
        hash_number(h, term->as.variable);
}
static void hash_atom(maelys_sha256_ctx_t *h, const maelys_datalog_ruleset_t *r,
                      const maelys_datalog_fact_t *atom) {
    hash_number(h, atom->predicate_id);
    hash_number(h, atom->arity);
    for (size_t i = 0; i < atom->arity; ++i)
        hash_term(h, r, &atom->terms[i]);
}
maelys_datalog_status_t maelys_datalog_program_fingerprint(const maelys_datalog_program_t *p,
                                                           char out[65]) {
    if (!p || !p->ruleset || !out)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (!p->ruleset->compiled_fingerprint[0])
        return MAELYS_DATALOG_STATUS_INVALID_STATE;
    memcpy(out, p->ruleset->compiled_fingerprint, 65);
    return MAELYS_DATALOG_STATUS_OK;
}

maelys_result_t maelys_datalog_compute_program_fingerprint(const maelys_datalog_ruleset_t *r,
                                                           char out[65]) {
    maelys_sha256_ctx_t h;
    maelys_sha256_init(&h);
    hash_text(&h, "maelys-program-v1");
    hash_text(&h, r->policy_id);
    hash_text(&h, r->domain);
    hash_text(&h, r->sha256);
    hash_text(&h, r->frontend_name);
    hash_text(&h, r->frontend_semantic_id);
    hash_text(&h, r->source_sha256);
    hash_number(&h, r->positive_recursion_supported);
    hash_number(&h, r->negation_supported);
    hash_number(&h, r->negation_recursion_supported);
    hash_number(&h, r->test_only);
    hash_number(&h, r->registry.count);
    for (size_t i = 0; i < r->registry.count; ++i) {
        hash_text(&h, r->registry.defs[i].name);
        hash_number(&h, r->registry.defs[i].arity);
        hash_number(&h, r->registry.defs[i].kind_flags);
    }
    hash_number(&h, r->registry.atom_count);
    for (size_t i = 0; i < r->registry.atom_count; ++i)
        hash_text(&h, r->registry.atoms[i]);
    hash_number(&h, r->enforces_query_whitelist);
    hash_number(&h, r->query_whitelist_count);
    for (size_t i = 0; i < r->query_whitelist_count; ++i) {
        hash_text(&h, r->query_whitelist[i].name);
        hash_number(&h, r->query_whitelist[i].arity);
    }
    hash_number(&h, r->fact_count);
    for (size_t i = 0; i < r->fact_count; ++i)
        hash_atom(&h, r, &r->facts[i]);
    hash_number(&h, r->rule_count);
    for (size_t i = 0; i < r->rule_count; ++i) {
        const maelys_datalog_rule_t *rule = &r->rules[i];
        hash_number(&h, rule->rule_id);
        hash_number(&h, r->rule_sources[i].line);
        hash_number(&h, r->rule_sources[i].column);
        hash_atom(&h, r, &rule->head);
        hash_number(&h, rule->expr_node_count);
        for (size_t j = 0; j < rule->expr_node_count; ++j) {
            const maelys_datalog_arith_expr_node_t *n = &rule->expr_nodes[j];
            hash_number(&h, n->kind);
            if (n->kind <= MAELYS_DATALOG_ARITH_EXPR_VAR)
                hash_term(&h, r, &n->term);
            else {
                hash_number(&h, n->left);
                hash_number(&h, n->right);
            }
        }
        hash_number(&h, rule->body_count);
        for (size_t j = 0; j < rule->body_count; ++j) {
            const maelys_datalog_literal_t *l = &rule->body[j];
            hash_number(&h, l->kind);
            if (l->kind == MAELYS_DATALOG_LITERAL_ATOM ||
                l->kind == MAELYS_DATALOG_LITERAL_NEGATED_ATOM)
                hash_atom(&h, r, &l->atom);
            else if (l->kind == MAELYS_DATALOG_LITERAL_COMPARISON) {
                hash_number(&h, l->op);
                hash_number(&h, l->has_arith_expr);
                if (l->has_arith_expr) {
                    hash_number(&h, l->lhs_expr_root);
                    hash_number(&h, l->rhs_expr_root);
                } else {
                    hash_term(&h, r, &l->lhs);
                    hash_term(&h, r, &l->rhs);
                }
            } else {
                const maelys_datalog_filter_definition_t *d =
                    maelys_datalog_filter_by_kind((maelys_datalog_filter_kind_t)l->filter_kind);
                if (!d || l->filter_program_index >= r->filter_program_count)
                    return MAELYS_ERR_INVALID_STATE;
                const maelys_datalog_filter_program_t *f =
                    &r->filter_programs[l->filter_program_index];
                hash_text(&h, d->name);
                hash_text(&h, d->semantic_id);
                hash_term(&h, r, &l->filter_value);
                hash_bytes(&h, r->filter_pattern_pool + f->pattern_offset, f->pattern_length);
            }
        }
    }
    unsigned char digest[32];
    maelys_sha256_final(&h, digest);
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < 32u; ++i) {
        out[2 * i] = hex[digest[i] >> 4u];
        out[2 * i + 1] = hex[digest[i] & 15u];
    }
    out[64] = 0;
    return MAELYS_OK;
}
maelys_datalog_status_t maelys_datalog_program_predicate(const maelys_datalog_program_t *p,
                                                         size_t index,
                                                         maelys_datalog_public_predicate_t *out) {
    if (!p || !p->ruleset || !out)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (index >= p->ruleset->registry.count)
        return MAELYS_DATALOG_STATUS_NOT_FOUND;
    const maelys_datalog_predicate_def_t *d = &p->ruleset->registry.defs[index];
    *out = (maelys_datalog_public_predicate_t){d->name, d->arity, d->kind_flags};
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t maelys_datalog_program_fact(const maelys_datalog_program_t *p, size_t index,
                                                    maelys_datalog_ir_atom_t *out) {
    if (!p || !p->ruleset || !out)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (index >= p->ruleset->fact_count)
        return MAELYS_DATALOG_STATUS_NOT_FOUND;
    maelys_datalog_ir_atom_t a;
    maelys_result_t rc = export_atom(p->ruleset, &p->ruleset->facts[index], &a);
    if (rc == MAELYS_OK)
        *out = a;
    return (maelys_datalog_status_t)rc;
}
maelys_datalog_status_t maelys_datalog_program_rule(const maelys_datalog_program_t *p, size_t index,
                                                    maelys_datalog_ir_rule_t *out) {
    if (!p || !p->ruleset || !out)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (index >= p->ruleset->rule_count)
        return MAELYS_DATALOG_STATUS_NOT_FOUND;
    const maelys_datalog_ruleset_t *r = p->ruleset;
    const maelys_datalog_rule_t *in = &r->rules[index];
    maelys_datalog_ir_rule_t rule = {0};
    maelys_result_t rc = export_atom(r, &in->head, &rule.head);
    if (rc != MAELYS_OK)
        return (maelys_datalog_status_t)rc;
    rule.body_count = in->body_count;
    rule.expression_count = in->expr_node_count;
    rule.source = r->rule_sources[index];
    for (size_t i = 0; i < in->expr_node_count; ++i) {
        const maelys_datalog_arith_expr_node_t *a = &in->expr_nodes[i];
        maelys_datalog_ir_expression_t *b = &rule.expressions[i];
        b->kind = (maelys_datalog_ir_expression_kind_t)a->kind;
        b->left = a->left == UINT8_MAX ? UINT32_MAX : a->left;
        b->right = a->right == UINT8_MAX ? UINT32_MAX : a->right;
        if (a->kind <= MAELYS_DATALOG_ARITH_EXPR_VAR) {
            rc = export_term(r, &a->term, &b->term);
            if (rc != MAELYS_OK)
                return (maelys_datalog_status_t)rc;
        }
    }
    for (size_t i = 0; i < in->body_count; ++i) {
        const maelys_datalog_literal_t *a = &in->body[i];
        maelys_datalog_ir_literal_t *b = &rule.body[i];
        b->kind = (maelys_datalog_ir_literal_kind_t)a->kind;
        b->lhs_expression = b->rhs_expression = UINT32_MAX;
        if (a->kind == MAELYS_DATALOG_LITERAL_ATOM ||
            a->kind == MAELYS_DATALOG_LITERAL_NEGATED_ATOM)
            rc = export_atom(r, &a->atom, &b->atom);
        else if (a->kind == MAELYS_DATALOG_LITERAL_COMPARISON) {
            b->comparison = (maelys_datalog_ir_comparison_t)a->op;
            b->has_arithmetic = a->has_arith_expr;
            if (a->has_arith_expr) {
                b->lhs_expression = a->lhs_expr_root;
                b->rhs_expression = a->rhs_expr_root;
            } else {
                rc = export_term(r, &a->lhs, &b->lhs);
                if (rc == MAELYS_OK)
                    rc = export_term(r, &a->rhs, &b->rhs);
            }
        } else if (a->kind == MAELYS_DATALOG_LITERAL_FILTER) {
            const maelys_datalog_filter_definition_t *d =
                maelys_datalog_filter_by_kind((maelys_datalog_filter_kind_t)a->filter_kind);
            if (!d || a->filter_program_index >= r->filter_program_count)
                return MAELYS_DATALOG_STATUS_INVALID_STATE;
            const maelys_datalog_filter_program_t *f = &r->filter_programs[a->filter_program_index];
            b->filter_name = d->name;
            b->filter_semantic_id = d->semantic_id;
            b->pattern = r->filter_pattern_pool + f->pattern_offset;
            b->pattern_length = f->pattern_length;
            rc = export_term(r, &a->filter_value, &b->filter_value);
        } else
            rc = MAELYS_ERR_INVALID_STATE;
        if (rc != MAELYS_OK)
            return (maelys_datalog_status_t)rc;
    }
    *out = rule;
    return MAELYS_DATALOG_STATUS_OK;
}

static maelys_datalog_status_t fail(maelys_datalog_program_builder_t *b, maelys_result_t rc) {
    if (b && b->error == MAELYS_DATALOG_STATUS_OK)
        b->error = (maelys_datalog_status_t)rc;
    return b ? b->error : (maelys_datalog_status_t)rc;
}
static maelys_result_t import_term(maelys_datalog_ruleset_t *r, const maelys_datalog_ir_term_t *in,
                                   maelys_datalog_term_t *out, int variables) {
    memset(out, 0, sizeof(*out));
    out->kind = (maelys_datalog_term_kind_t)in->kind;
    switch (in->kind) {
    case MAELYS_DATALOG_IR_SYMBOL: {
        if (!in->as.symbol)
            return MAELYS_ERR_INVALID_ARGUMENT;
        size_t n = strnlen(in->as.symbol, MAELYS_DATALOG_MAX_STRING_BYTES);
        if (n == MAELYS_DATALOG_MAX_STRING_BYTES)
            return MAELYS_ERR_PAYLOAD_TOO_LARGE;
        if (!maelys_datalog_predicate_registry_atom_allowed(&r->registry, in->as.symbol))
            return MAELYS_ERR_INVALID_FIELD;
        return maelys_datalog_symbol_intern(&r->symbols, in->as.symbol, n, &out->as.symbol);
    }
    case MAELYS_DATALOG_IR_INTEGER:
        if (in->as.integer < 0 || in->as.integer > MAELYS_DATALOG_MAX_INT)
            return MAELYS_ERR_INVALID_FIELD;
        out->as.integer = in->as.integer;
        break;
    case MAELYS_DATALOG_IR_BOOLEAN:
        if (in->as.boolean != 0 && in->as.boolean != 1)
            return MAELYS_ERR_INVALID_FIELD;
        out->as.boolean = in->as.boolean;
        break;
    case MAELYS_DATALOG_IR_VARIABLE:
        if (!variables || in->as.variable >= MAELYS_DATALOG_MAX_RULE_VARIABLES)
            return MAELYS_ERR_INVALID_FIELD;
        out->as.variable = in->as.variable;
        break;
    default:
        return MAELYS_ERR_INVALID_FIELD;
    }
    return MAELYS_OK;
}
static maelys_result_t import_atom(maelys_datalog_ruleset_t *r, const maelys_datalog_ir_atom_t *in,
                                   maelys_datalog_fact_t *out, int variables) {
    if (!in->predicate || in->arity > MAELYS_DATALOG_MAX_TERMS)
        return MAELYS_ERR_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (!maelys_datalog_predicate_registry_find(&r->registry, in->predicate, in->arity,
                                                &out->predicate_id))
        return MAELYS_ERR_INVALID_FIELD;
    out->arity = (uint8_t)in->arity;
    for (size_t i = 0; i < in->arity; ++i) {
        maelys_result_t rc = import_term(r, &in->terms[i], &out->terms[i], variables);
        if (rc != MAELYS_OK)
            return rc;
    }
    return MAELYS_OK;
}
maelys_datalog_status_t maelys_datalog_program_add_fact(maelys_datalog_program_builder_t *b,
                                                        const maelys_datalog_ir_atom_t *in) {
    if (!b || !b->ruleset || !in)
        return fail(b, MAELYS_ERR_INVALID_ARGUMENT);
    if (b->error)
        return b->error;
    maelys_datalog_ruleset_t *r = b->ruleset;
    if (r->fact_count >= MAELYS_DATALOG_MAX_RULE_FACTS)
        return fail(b, MAELYS_ERR_PAYLOAD_TOO_LARGE);
    maelys_datalog_fact_t fact;
    maelys_result_t rc = import_atom(r, in, &fact, 0);
    if (rc != MAELYS_OK)
        return fail(b, rc);
    r->facts[r->fact_count++] = fact;
    r->program_validated = 0;
    r->compiled_fingerprint[0] = 0;
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t maelys_datalog_program_add_rule(maelys_datalog_program_builder_t *b,
                                                        const maelys_datalog_ir_rule_t *in) {
    if (!b || !b->ruleset || !in)
        return fail(b, MAELYS_ERR_INVALID_ARGUMENT);
    if (b->error)
        return b->error;
    maelys_datalog_ruleset_t *r = b->ruleset;
    if (r->rule_count >= MAELYS_DATALOG_MAX_RULES || in->body_count > MAELYS_DATALOG_IR_MAX_BODY ||
        in->expression_count > MAELYS_DATALOG_IR_MAX_EXPRESSIONS)
        return fail(b, MAELYS_ERR_PAYLOAD_TOO_LARGE);
    if (!in->body_count || (!in->source.line != !in->source.column))
        return fail(b, MAELYS_ERR_INVALID_FIELD);
    maelys_datalog_rule_t rule = {0};
    rule.rule_id = r->rule_count + 1u;
    rule.body_count = in->body_count;
    rule.expr_node_count = (uint8_t)in->expression_count;
    maelys_result_t rc = import_atom(r, &in->head, &rule.head, 1);
    if (rc != MAELYS_OK)
        return fail(b, rc);
    for (size_t i = 0; i < in->expression_count; ++i) {
        const maelys_datalog_ir_expression_t *a = &in->expressions[i];
        maelys_datalog_arith_expr_node_t *n = &rule.expr_nodes[i];
        n->kind = (maelys_datalog_arith_expr_kind_t)a->kind;
        n->left = n->right = UINT8_MAX;
        if (a->kind == MAELYS_DATALOG_IR_EXPR_INTEGER ||
            a->kind == MAELYS_DATALOG_IR_EXPR_VARIABLE) {
            if (a->term.kind != (a->kind == MAELYS_DATALOG_IR_EXPR_INTEGER
                                     ? MAELYS_DATALOG_IR_INTEGER
                                     : MAELYS_DATALOG_IR_VARIABLE))
                return fail(b, MAELYS_ERR_INVALID_FIELD);
            rc = import_term(r, &a->term, &n->term, 1);
            if (rc != MAELYS_OK)
                return fail(b, rc);
        } else if (a->kind >= MAELYS_DATALOG_IR_EXPR_ADD && a->kind <= MAELYS_DATALOG_IR_EXPR_MUL) {
            if (a->left >= i || a->right >= i)
                return fail(b, MAELYS_ERR_INVALID_FIELD);
            n->left = (uint8_t)a->left;
            n->right = (uint8_t)a->right;
        } else
            return fail(b, MAELYS_ERR_INVALID_FIELD);
    }
    for (size_t i = 0; i < in->body_count; ++i) {
        const maelys_datalog_ir_literal_t *a = &in->body[i];
        maelys_datalog_literal_t *l = &rule.body[i];
        l->kind = (maelys_datalog_literal_kind_t)a->kind;
        l->lhs_expr_root = l->rhs_expr_root = UINT8_MAX;
        if (a->kind == MAELYS_DATALOG_IR_ATOM || a->kind == MAELYS_DATALOG_IR_NEGATION)
            rc = import_atom(r, &a->atom, &l->atom, 1);
        else if (a->kind == MAELYS_DATALOG_IR_COMPARISON) {
            if (a->has_arithmetic != 0 && a->has_arithmetic != 1)
                return fail(b, MAELYS_ERR_INVALID_FIELD);
            l->op = (maelys_datalog_cmp_op_t)a->comparison;
            l->has_arith_expr = (uint8_t)a->has_arithmetic;
            if (a->has_arithmetic) {
                if (a->lhs_expression >= in->expression_count ||
                    a->rhs_expression >= in->expression_count)
                    return fail(b, MAELYS_ERR_INVALID_FIELD);
                l->lhs_expr_root = (uint8_t)a->lhs_expression;
                l->rhs_expr_root = (uint8_t)a->rhs_expression;
            } else {
                rc = import_term(r, &a->lhs, &l->lhs, 1);
                if (rc == MAELYS_OK)
                    rc = import_term(r, &a->rhs, &l->rhs, 1);
            }
        } else if (a->kind == MAELYS_DATALOG_IR_FILTER) {
            const maelys_datalog_filter_definition_t *d =
                maelys_datalog_filter_by_name(a->filter_name);
            if (!d || (a->filter_semantic_id && strcmp(a->filter_semantic_id, d->semantic_id)))
                return fail(b, MAELYS_ERR_UNSUPPORTED);
            if (a->pattern_length > MAELYS_DATALOG_MAX_FILTER_PATTERN_BYTES ||
                r->filter_program_count >= MAELYS_DATALOG_MAX_FILTER_PROGRAMS ||
                a->pattern_length >
                    MAELYS_DATALOG_FILTER_PATTERN_POOL_BYTES - r->filter_pattern_pool_used)
                return fail(b, MAELYS_ERR_PAYLOAD_TOO_LARGE);
            if ((!a->pattern && a->pattern_length) ||
                !maelys_utf8_validate(a->pattern, a->pattern_length))
                return fail(b, MAELYS_ERR_INVALID_FIELD);
            l->filter_kind = (uint8_t)d->kind;
            l->filter_program_index = (uint16_t)r->filter_program_count;
            r->filter_programs[r->filter_program_count++] =
                (maelys_datalog_filter_program_t){(uint32_t)r->filter_pattern_pool_used,
                                                  (uint16_t)a->pattern_length, (uint8_t)d->kind, 0};
            if (a->pattern_length)
                memcpy(r->filter_pattern_pool + r->filter_pattern_pool_used, a->pattern,
                       a->pattern_length);
            r->filter_pattern_pool_used += a->pattern_length;
            rc = import_term(r, &a->filter_value, &l->filter_value, 1);
        } else
            return fail(b, MAELYS_ERR_INVALID_FIELD);
        if (rc != MAELYS_OK)
            return fail(b, rc);
    }
    r->rule_sources[r->rule_count] = in->source;
    r->rules[r->rule_count++] = rule;
    r->program_validated = 0;
    r->compiled_fingerprint[0] = 0;
    return MAELYS_DATALOG_STATUS_OK;
}

static maelys_datalog_status_t datalog_lower(const char *source, size_t length,
                                             maelys_datalog_program_builder_t *builder,
                                             maelys_datalog_public_diagnostic_t *out) {
    maelys_datalog_diagnostic_t diag = {0};
    maelys_datalog_ruleset_t *r = builder->ruleset;
    /* The standard language retains its historic source authority. A frontend
     * with a distinct semantic identity retains the generic identity fields. */
    if (!strcmp(r->frontend_name, "datalog") &&
        !strcmp(r->frontend_semantic_id, "maelys.datalog.v2")) {
        r->frontend_name[0] = r->frontend_semantic_id[0] = r->source_sha256[0] = 0;
    }
    maelys_result_t rc = maelys_datalog_parse_only(r, source, length, "inline", 0,
                                                   builder->parse_origin, &diag);
    if (rc != MAELYS_OK)
        maelys_datalog_copy_load_diagnostic(out, &diag);
    return (maelys_datalog_status_t)rc;
}
const maelys_datalog_frontend_t *maelys_datalog_frontend_datalog(void) {
    static const maelys_datalog_frontend_t frontend = {MAELYS_DATALOG_PROGRAM_ABI_VERSION,
                                                       sizeof(maelys_datalog_frontend_t), "datalog",
                                                       "maelys.datalog.v2", datalog_lower};
    return &frontend;
}
maelys_result_t maelys_datalog_compile_frontend(const char *domain, const char *policy_id,
                                                const char *source, size_t length,
                                                const maelys_datalog_frontend_t *frontend,
                                                maelys_datalog_ruleset_t *r,
                                                maelys_datalog_public_diagnostic_t *out) {
    if (!frontend)
        frontend = maelys_datalog_frontend_datalog();
    if (!r || !domain || !policy_id || !source || !length || !domain[0] || !policy_id[0] ||
        frontend->abi_version != MAELYS_DATALOG_PROGRAM_ABI_VERSION ||
        frontend->struct_size != sizeof(*frontend) || !frontend->lower ||
        !maelys_datalog_identity_valid(frontend->name, 64u, 1) ||
        !maelys_datalog_identity_valid(frontend->semantic_id, 128u, 0))
        return MAELYS_ERR_INVALID_ARGUMENT;
    if (strlen(domain) >= sizeof(r->domain) || strlen(policy_id) >= sizeof(r->policy_id))
        return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    maelys_datalog_diagnostic_t diag = {0};
    if (!maelys_datalog_domain_registry_find(domain)) {
        maelys_datalog_diagnostic_set(&diag, MAELYS_DATALOG_DIAG_MANIFEST_UNKNOWN_DOMAIN,
            "manifest", "inline", 0, 0, "unknown policy domain",
            "install a domain registry or disable the policy");
        maelys_datalog_copy_load_diagnostic(out, &diag);
        return MAELYS_ERR_UNSUPPORTED;
    }
    if (!maelys_utf8_validate((const unsigned char *)source, length)) {
        maelys_datalog_diagnostic_set(&diag, MAELYS_DATALOG_DIAG_LEXER_INVALID_UTF8,
            "manifest", "inline", 0, 0, "invalid UTF-8 in policy text",
            "ensure the policy text is valid UTF-8");
        maelys_datalog_copy_load_diagnostic(out, &diag);
        return MAELYS_ERR_INVALID_FIELD;
    }
    maelys_result_t rc =
        maelys_datalog_ruleset_init(r, policy_id, domain, MAELYS_DATALOG_SHA256_UNSET, 0);
    if (rc != MAELYS_OK)
        return rc;
    rc = maelys_datalog_domain_registry_install(domain, &r->registry);
    if (rc == MAELYS_OK)
        rc = maelys_datalog_predicate_registry_freeze(&r->registry);
    if (rc != MAELYS_OK)
        return rc;
    if (maelys_sha256_hex((const unsigned char *)source, length, r->source_sha256))
        return MAELYS_ERR_INTERNAL;
    memcpy(r->sha256, r->source_sha256, sizeof(r->sha256));
    memcpy(r->frontend_name, frontend->name, strlen(frontend->name) + 1u);
    memcpy(r->frontend_semantic_id, frontend->semantic_id, strlen(frontend->semantic_id) + 1u);
    maelys_datalog_parse_origin_t origin = {0};
    maelys_datalog_program_builder_t builder = {r, MAELYS_DATALOG_STATUS_OK, &origin};
    maelys_datalog_status_t status =
        maelys_datalog_callback_status(frontend->lower(source, length, &builder, out));
    if (builder.error)
        status = builder.error;
    if (status != MAELYS_DATALOG_STATUS_OK)
        return (maelys_result_t)status;
    rc = maelys_datalog_validate_program(r, "inline", &origin, &diag);
    if (rc != MAELYS_OK) {
        maelys_datalog_copy_load_diagnostic(out, &diag);
        return rc;
    }
    return maelys_datalog_ruleset_finalize_sha256(r);
}
