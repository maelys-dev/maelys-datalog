/* SPDX-License-Identifier: MPL-2.0 */
#include "src/compiler/maelys_datalog_program_internal.h"
#include "src/core/maelys_datalog_filter.h"
#include <string.h>
typedef struct {
    maelys_datalog_ruleset_t *ruleset;
    const char *file_path;
    size_t line, column;
    maelys_datalog_diagnostic_t *diag;
} validation_context_t;
static void validation_diag(validation_context_t *p, maelys_datalog_diag_code_t code,
                            const char *message, const char *hint) {
    maelys_datalog_diagnostic_set(p->diag, code, "parser", p->file_path, p->line, p->column,
                                  message, hint);
}
static void vars_in_atom(const maelys_datalog_fact_t *a, uint32_t *mask) {
    for (size_t i = 0; i < a->arity; i++) {
        if (a->terms[i].kind == MAELYS_DATALOG_TERM_VAR &&
            a->terms[i].as.variable < MAELYS_DATALOG_MAX_RULE_VARIABLES) {
            *mask |= (1u << a->terms[i].as.variable);
        }
    }
}

static void vars_in_arith_expr(const maelys_datalog_rule_t *rule, uint8_t root, uint32_t *mask) {
    if (!rule || !mask || root >= rule->expr_node_count ||
        root == MAELYS_DATALOG_ARITH_EXPR_NO_NODE) {
        return;
    }
    const maelys_datalog_arith_expr_node_t *node = &rule->expr_nodes[root];
    switch (node->kind) {
    case MAELYS_DATALOG_ARITH_EXPR_VAR:
        if (node->term.as.variable < MAELYS_DATALOG_MAX_RULE_VARIABLES) {
            *mask |= (1u << node->term.as.variable);
        }
        return;
    case MAELYS_DATALOG_ARITH_EXPR_ADD:
    case MAELYS_DATALOG_ARITH_EXPR_SUB:
    case MAELYS_DATALOG_ARITH_EXPR_MUL:
        vars_in_arith_expr(rule, node->left, mask);
        vars_in_arith_expr(rule, node->right, mask);
        return;
    case MAELYS_DATALOG_ARITH_EXPR_INT_LITERAL:
    default:
        return;
    }
}

static maelys_result_t validate_rule_impl(validation_context_t *p,
                                          const maelys_datalog_rule_t *rule) {
    maelys_datalog_ruleset_t *r = p->ruleset;
    const maelys_datalog_predicate_def_t *head_def =
        maelys_datalog_predicate_registry_get(&r->registry, rule->head.predicate_id);
    if (!head_def || (head_def->kind_flags &
                      (MAELYS_DATALOG_PRED_KIND_EDB | MAELYS_DATALOG_PRED_KIND_POLICY_FACT))) {
        validation_diag(p, MAELYS_DATALOG_DIAG_PARSER_RULE_HEAD_EDB_FORBIDDEN,
                        "base predicate used in rule head",
                        "EDB/runtime and policy fact predicates cannot appear in rule heads");
        if (head_def)
            maelys_datalog_diagnostic_set_predicate(p->diag, head_def->name, head_def->arity);
        return MAELYS_ERR_INVALID_FIELD;
    }
    uint32_t head_vars = 0;
    uint32_t body_vars = 0;
    vars_in_atom(&rule->head, &head_vars);
    for (size_t i = 0; i < rule->body_count; i++) {
        if (rule->body[i].kind == MAELYS_DATALOG_LITERAL_ATOM) {
            vars_in_atom(&rule->body[i].atom, &body_vars);
            if (rule->body[i].atom.predicate_id == rule->head.predicate_id)
                r->has_positive_recursion = 1;
        }
    }
    for (size_t i = 0; i < rule->body_count; i++) {
        if (rule->body[i].kind == MAELYS_DATALOG_LITERAL_FILTER) {
            const maelys_datalog_term_t *value = &rule->body[i].filter_value;
            if (value->kind == MAELYS_DATALOG_TERM_VAR &&
                value->as.variable < MAELYS_DATALOG_MAX_RULE_VARIABLES &&
                ((1u << value->as.variable) & ~body_vars)) {
                validation_diag(p, MAELYS_DATALOG_DIAG_PARSER_UNSAFE_VARIABLE,
                                "filter variable not bound by positive body atom",
                                "bind the filter value in a positive body atom first");
                maelys_datalog_diagnostic_set_predicate(p->diag, head_def->name, head_def->arity);
                return MAELYS_ERR_INVALID_FIELD;
            }
        }
    }
    for (size_t i = 0; i < rule->body_count; i++) {
        if (rule->body[i].kind == MAELYS_DATALOG_LITERAL_COMPARISON) {
            uint32_t cmp_vars = 0;
            if (rule->body[i].has_arith_expr) {
                vars_in_arith_expr(rule, rule->body[i].lhs_expr_root, &cmp_vars);
                vars_in_arith_expr(rule, rule->body[i].rhs_expr_root, &cmp_vars);
            } else {
                if (rule->body[i].lhs.kind == MAELYS_DATALOG_TERM_VAR &&
                    rule->body[i].lhs.as.variable < MAELYS_DATALOG_MAX_RULE_VARIABLES) {
                    cmp_vars |= (1u << rule->body[i].lhs.as.variable);
                }
                if (rule->body[i].rhs.kind == MAELYS_DATALOG_TERM_VAR &&
                    rule->body[i].rhs.as.variable < MAELYS_DATALOG_MAX_RULE_VARIABLES) {
                    cmp_vars |= (1u << rule->body[i].rhs.as.variable);
                }
            }
            if (cmp_vars & ~body_vars) {
                validation_diag(p, MAELYS_DATALOG_DIAG_PARSER_UNSAFE_VARIABLE,
                                "comparison variable not bound by positive body atom",
                                "bind all comparison variables in positive body atoms first");
                maelys_datalog_diagnostic_set_predicate(p->diag, head_def->name, head_def->arity);
                return MAELYS_ERR_INVALID_FIELD;
            }
        }
    }
    for (size_t i = 0; i < rule->body_count; i++) {
        if (rule->body[i].kind == MAELYS_DATALOG_LITERAL_NEGATED_ATOM) {
            uint32_t neg_vars = 0;
            vars_in_atom(&rule->body[i].atom, &neg_vars);
            if (neg_vars & ~body_vars) {
                validation_diag(p, MAELYS_DATALOG_DIAG_PARSER_UNSAFE_VARIABLE,
                                "negated atom variable not bound by positive body atom",
                                "bind all not() variables in positive body atoms first");
                maelys_datalog_diagnostic_set_predicate(p->diag, head_def->name, head_def->arity);
                return MAELYS_ERR_INVALID_FIELD;
            }
        }
    }
    if (head_vars & ~body_vars) {
        validation_diag(p, MAELYS_DATALOG_DIAG_PARSER_UNSAFE_VARIABLE,
                        "head variable not bound by positive body atom",
                        "bind every head variable in a positive body atom");
        maelys_datalog_diagnostic_set_predicate(p->diag, head_def->name, head_def->arity);
        return MAELYS_ERR_INVALID_FIELD;
    }
    return MAELYS_OK;
}

static maelys_result_t assign_strata_impl(validation_context_t *p) {
    if (!p || !p->ruleset)
        return MAELYS_ERR_INVALID_ARGUMENT;
    maelys_datalog_ruleset_t *ruleset = p->ruleset;
    if (!ruleset->negation_supported)
        return MAELYS_OK;

    memset(ruleset->strata, 0, sizeof(ruleset->strata));
    ruleset->max_stratum = 0;
    ruleset->strata_assigned = 0;

    int changed = 1;
    size_t iters = 0;
    while (changed && iters <= MAELYS_DATALOG_MAX_PREDICATES) {
        changed = 0;
        for (size_t r = 0; r < ruleset->rule_count; r++) {
            const maelys_datalog_rule_t *rule = &ruleset->rules[r];
            maelys_datalog_predicate_id_t hpid = rule->head.predicate_id;
            if (hpid >= MAELYS_DATALOG_MAX_PREDICATES)
                return MAELYS_ERR_INVALID_FIELD;
            uint32_t s = ruleset->strata[hpid];
            for (size_t i = 0; i < rule->body_count; i++) {
                const maelys_datalog_literal_t *literal = &rule->body[i];
                if (literal->kind != MAELYS_DATALOG_LITERAL_ATOM &&
                    literal->kind != MAELYS_DATALOG_LITERAL_NEGATED_ATOM) {
                    continue;
                }
                maelys_datalog_predicate_id_t bpid = literal->atom.predicate_id;
                if (bpid >= MAELYS_DATALOG_MAX_PREDICATES)
                    return MAELYS_ERR_INVALID_FIELD;
                uint32_t ns = ruleset->strata[bpid];
                if (literal->kind == MAELYS_DATALOG_LITERAL_NEGATED_ATOM) {
                    if (ns >= MAELYS_DATALOG_MAX_STRATA) {
                        validation_diag(
                            p, MAELYS_DATALOG_DIAG_POLICY_NOT_STRATIFIABLE,
                            "negation stratum limit exceeded",
                            "remove recursion through negation or reduce negation depth");
                        return MAELYS_ERR_INVALID_FIELD;
                    }
                    ns += 1u;
                }
                if (ns > s)
                    s = ns;
            }
            if (s >= MAELYS_DATALOG_MAX_STRATA) {
                validation_diag(p, MAELYS_DATALOG_DIAG_POLICY_NOT_STRATIFIABLE,
                                "negation stratum limit exceeded",
                                "reduce stratification depth below MAELYS_DATALOG_MAX_STRATA");
                return MAELYS_ERR_INVALID_FIELD;
            }
            if (s > ruleset->strata[hpid]) {
                ruleset->strata[hpid] = s;
                changed = 1;
            }
        }
        iters++;
    }

    if (changed) {
        validation_diag(p, MAELYS_DATALOG_DIAG_POLICY_NOT_STRATIFIABLE,
                        "policy is not stratifiable", "remove recursion through negation");
        return MAELYS_ERR_INVALID_FIELD;
    }

    uint32_t max = 0;
    for (size_t i = 0; i < MAELYS_DATALOG_MAX_PREDICATES; i++) {
        if (ruleset->strata[i] > max)
            max = ruleset->strata[i];
    }
    ruleset->max_stratum = max;
    ruleset->strata_assigned = 1;
    return MAELYS_OK;
}

maelys_result_t maelys_datalog_validate_rule(maelys_datalog_ruleset_t *ruleset,
                                             const maelys_datalog_rule_t *rule, const char *file,
                                             size_t line, size_t column,
                                             maelys_datalog_diagnostic_t *diag) {
    validation_context_t p = {ruleset, file, line, column, diag};
    return validate_rule_impl(&p, rule);
}
maelys_result_t maelys_datalog_assign_strata(maelys_datalog_ruleset_t *ruleset, const char *file,
                                             size_t line, size_t column,
                                             maelys_datalog_diagnostic_t *diag) {
    validation_context_t p = {ruleset, file, line, column, diag};
    return assign_strata_impl(&p);
}

static int valid_term(const maelys_datalog_ruleset_t *r, const maelys_datalog_term_t *t,
                      int variables) {
    switch (t->kind) {
    case MAELYS_DATALOG_TERM_SYMBOL:
        return maelys_datalog_symbol_id_is_valid(&r->symbols, t->as.symbol);
    case MAELYS_DATALOG_TERM_INT:
        return t->as.integer >= 0 && t->as.integer <= MAELYS_DATALOG_MAX_INT;
    case MAELYS_DATALOG_TERM_BOOL:
        return t->as.boolean == 0 || t->as.boolean == 1;
    case MAELYS_DATALOG_TERM_VAR:
        return variables && t->as.variable < MAELYS_DATALOG_MAX_RULE_VARIABLES;
    default:
        return 0;
    }
}
static int valid_atom(const maelys_datalog_ruleset_t *r, const maelys_datalog_fact_t *a,
                      int variables) {
    const maelys_datalog_predicate_def_t *d =
        maelys_datalog_predicate_registry_get(&r->registry, a->predicate_id);
    if (!d || a->arity > MAELYS_DATALOG_MAX_TERMS || a->arity != d->arity)
        return 0;
    for (size_t i = 0; i < a->arity; ++i)
        if (!valid_term(r, &a->terms[i], variables))
            return 0;
    return 1;
}
static int valid_expressions(const maelys_datalog_ruleset_t *r, const maelys_datalog_rule_t *rule) {
    if (rule->expr_node_count > MAELYS_DATALOG_MAX_ARITH_EXPR_NODES)
        return 0;
    size_t expanded[MAELYS_DATALOG_MAX_ARITH_EXPR_NODES] = {0};
    for (size_t i = 0; i < rule->expr_node_count; ++i) {
        const maelys_datalog_arith_expr_node_t *n = &rule->expr_nodes[i];
        if (n->kind == MAELYS_DATALOG_ARITH_EXPR_INT_LITERAL ||
            n->kind == MAELYS_DATALOG_ARITH_EXPR_VAR) {
            if (n->term.kind != (n->kind == MAELYS_DATALOG_ARITH_EXPR_INT_LITERAL
                                     ? MAELYS_DATALOG_TERM_INT
                                     : MAELYS_DATALOG_TERM_VAR) ||
                !valid_term(r, &n->term, 1))
                return 0;
        } else if (n->kind >= MAELYS_DATALOG_ARITH_EXPR_ADD &&
                   n->kind <= MAELYS_DATALOG_ARITH_EXPR_MUL) {
            if (n->left >= i || n->right >= i)
                return 0; /* no cycles/forward edges */
            expanded[i] = expanded[n->left] + expanded[n->right];
        } else
            return 0;
        /* The reference evaluates recursively. Shared DAG edges must not turn
         * a bounded node array into exponentially many visits. Standard parser
         * trees already satisfy this expanded-tree bound. */
        if (++expanded[i] > MAELYS_DATALOG_MAX_ARITH_EXPR_NODES)
            return 0;
    }
    return 1;
}
static int valid_literal(const maelys_datalog_ruleset_t *r, const maelys_datalog_rule_t *rule,
                         const maelys_datalog_literal_t *l) {
    if (l->kind == MAELYS_DATALOG_LITERAL_ATOM || l->kind == MAELYS_DATALOG_LITERAL_NEGATED_ATOM)
        return valid_atom(r, &l->atom, 1);
    if (l->kind == MAELYS_DATALOG_LITERAL_COMPARISON) {
        if (l->op < MAELYS_DATALOG_CMP_EQ || l->op > MAELYS_DATALOG_CMP_GTE)
            return 0;
        if (l->has_arith_expr == 1)
            return l->lhs_expr_root < rule->expr_node_count &&
                   l->rhs_expr_root < rule->expr_node_count;
        if (l->has_arith_expr || !valid_term(r, &l->lhs, 1) || !valid_term(r, &l->rhs, 1))
            return 0;
        if (l->lhs.kind != MAELYS_DATALOG_TERM_VAR && l->rhs.kind != MAELYS_DATALOG_TERM_VAR) {
            if (l->lhs.kind != l->rhs.kind)
                return 0;
            if (l->lhs.kind != MAELYS_DATALOG_TERM_INT && l->op > MAELYS_DATALOG_CMP_NEQ)
                return 0;
        }
        return 1;
    }
    if (l->kind == MAELYS_DATALOG_LITERAL_FILTER) {
        if (l->filter_program_index >= r->filter_program_count ||
            (l->filter_value.kind != MAELYS_DATALOG_TERM_SYMBOL &&
             l->filter_value.kind != MAELYS_DATALOG_TERM_VAR) ||
            !valid_term(r, &l->filter_value, 1))
            return 0;
        return r->filter_programs[l->filter_program_index].kind == l->filter_kind;
    }
    return 0;
}

maelys_result_t maelys_datalog_validate_program(maelys_datalog_ruleset_t *r, const char *file,
                                                maelys_datalog_diagnostic_t *diag) {
    if (!r || !r->loaded || !r->registry.frozen)
        return MAELYS_ERR_INVALID_STATE;
    size_t line = 0, column = 0;
    if (r->registry.count > MAELYS_DATALOG_MAX_PREDICATES ||
        r->rule_count > MAELYS_DATALOG_MAX_RULES || r->fact_count > MAELYS_DATALOG_MAX_RULE_FACTS ||
        r->filter_program_count > MAELYS_DATALOG_MAX_FILTER_PROGRAMS ||
        r->filter_pattern_pool_used > MAELYS_DATALOG_FILTER_PATTERN_POOL_BYTES)
        goto malformed;
    for (size_t i = 0; i < r->filter_program_count; ++i) {
        const maelys_datalog_filter_program_t *f = &r->filter_programs[i];
        if (f->pattern_length > MAELYS_DATALOG_MAX_FILTER_PATTERN_BYTES ||
            f->pattern_offset > r->filter_pattern_pool_used ||
            f->pattern_length > r->filter_pattern_pool_used - f->pattern_offset)
            goto malformed;
        maelys_result_t rc = maelys_datalog_filter_validate(
            (maelys_datalog_filter_kind_t)f->kind, r->filter_pattern_pool + f->pattern_offset,
            f->pattern_length);
        if (rc != MAELYS_OK) {
            maelys_datalog_diagnostic_set(diag, MAELYS_DATALOG_DIAG_PARSER_INVALID_FILTER,
                                          "validate", file, 0, 0, "invalid filter program",
                                          "check the provider and pattern");
            return rc;
        }
    }
    for (size_t i = 0; i < r->fact_count; ++i) {
        if (!valid_atom(r, &r->facts[i], 0))
            goto malformed;
        const maelys_datalog_predicate_def_t *d =
            maelys_datalog_predicate_registry_get(&r->registry, r->facts[i].predicate_id);
        if (!(d->kind_flags & MAELYS_DATALOG_PRED_KIND_POLICY_FACT))
            goto malformed;
    }
    for (size_t i = 0; i < r->rule_count; ++i) {
        const maelys_datalog_rule_t *rule = &r->rules[i];
        line = r->rule_sources[i].line;
        column = r->rule_sources[i].column;
        if (rule->rule_id != i + 1u || rule->body_count == 0 ||
            rule->body_count > MAELYS_DATALOG_MAX_BODY_LITERALS || !valid_atom(r, &rule->head, 1) ||
            !valid_expressions(r, rule))
            goto malformed;
        const maelys_datalog_predicate_def_t *head =
            maelys_datalog_predicate_registry_get(&r->registry, rule->head.predicate_id);
        if (!(head->kind_flags & MAELYS_DATALOG_PRED_KIND_IDB) ||
            (head->kind_flags & (MAELYS_DATALOG_PRED_KIND_EDB | MAELYS_DATALOG_PRED_KIND_POLICY_FACT)))
            goto malformed;
        for (size_t j = 0; j < rule->body_count; ++j) {
            if (!valid_literal(r, rule, &rule->body[j]))
                goto malformed;
            if (rule->body[j].kind == MAELYS_DATALOG_LITERAL_NEGATED_ATOM)
                r->negation_supported = 1;
        }
        maelys_result_t rc = maelys_datalog_validate_rule(r, rule, file, line, column, diag);
        if (rc != MAELYS_OK)
            return rc;
    }
    return maelys_datalog_assign_strata(r, file, line, column, diag);
malformed:
    maelys_datalog_diagnostic_set(
        diag, MAELYS_DATALOG_DIAG_MALFORMED_PROGRAM, "validate", file, line, column,
        "malformed intermediate program",
        "check term kinds, predicate roles, expression indices and program bounds");
    return MAELYS_ERR_INVALID_FIELD;
}
