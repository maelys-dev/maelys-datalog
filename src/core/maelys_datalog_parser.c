#include "src/core/maelys_datalog_parser.h"

#include "src/core/maelys_datalog_lexer.h"
#include "src/core/maelys_datalog_predicate_registry.h"
#include "src/core/maelys_datalog_symbol_table.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    maelys_datalog_lexer_t lexer;
    maelys_datalog_token_t tok;
    maelys_datalog_ruleset_t *ruleset;
    const char *file_path;
    maelys_datalog_diagnostic_t *diag;
    unsigned anonymous_var_count;
} parser_t;

typedef enum {
    MAELYS_DATALOG_TERM_CTX_HEAD_CANDIDATE = 1,
    MAELYS_DATALOG_TERM_CTX_BODY_ATOM = 2,
    MAELYS_DATALOG_TERM_CTX_COMPARISON = 3
} maelys_datalog_term_parse_context_t;

#define MAELYS_DATALOG_ANONYMOUS_CANDIDATE_VAR MAELYS_DATALOG_MAX_RULE_VARIABLES

_Static_assert(MAELYS_DATALOG_MAX_RULE_VARIABLES <= 32,
               "variable bitmask requires MAX_RULE_VARIABLES <= 32");
_Static_assert(MAELYS_DATALOG_MAX_TERMS <= 255,
               "MAX_TERMS must fit in uint8_t atom->arity");

static maelys_result_t next(parser_t *p) {
    return maelys_datalog_lexer_next(&p->lexer, &p->tok);
}

static void parser_diag(parser_t *p,
                        maelys_datalog_diag_code_t code,
                        const char *message,
                        const char *hint) {
    maelys_datalog_diagnostic_set(p ? p->diag : NULL,
                                  code,
                                  "parser",
                                  p ? p->file_path : NULL,
                                  p ? p->tok.line : 0,
                                  p ? p->tok.column : 0,
                                  message,
                                  hint);
    if (p && p->diag && p->tok.text[0]) {
        snprintf(p->diag->token, sizeof(p->diag->token),
                 "%.*s", (int)p->tok.len, p->tok.text);
    }
}

static int predicate_name_exists(const maelys_datalog_predicate_registry_t *registry,
                                 const char *name) {
    if (!registry || !name) return 0;
    for (size_t i = 0; i < registry->count; i++) {
        if (strcmp(registry->defs[i].name, name) == 0) return 1;
    }
    return 0;
}

static int token_is_cmp(maelys_datalog_token_kind_t k) {
    return k == MAELYS_DATALOG_TOKEN_EQ || k == MAELYS_DATALOG_TOKEN_NEQ ||
           k == MAELYS_DATALOG_TOKEN_LT || k == MAELYS_DATALOG_TOKEN_LTE ||
           k == MAELYS_DATALOG_TOKEN_GT || k == MAELYS_DATALOG_TOKEN_GTE;
}

static maelys_result_t allocate_anonymous_variable(parser_t *p, maelys_datalog_term_t *term) {
    unsigned id = MAELYS_DATALOG_NAMED_VARIABLE_COUNT + p->anonymous_var_count;
    if (id >= MAELYS_DATALOG_MAX_RULE_VARIABLES) {
        parser_diag(p,
                    MAELYS_DATALOG_DIAG_PARSER_TOO_MANY_VARIABLES,
                    "rule variable limit exceeded",
                    "reduce anonymous variables or split the rule");
        maelys_datalog_diagnostic_set_limit(p->diag,
                                            (size_t)id + 1u,
                                            MAELYS_DATALOG_MAX_RULE_VARIABLES);
        return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    }
    term->kind = MAELYS_DATALOG_TERM_VAR;
    term->as.variable = id;
    p->anonymous_var_count++;
    return next(p);
}

static maelys_result_t parse_term(parser_t *p,
                                  maelys_datalog_term_t *term,
                                  maelys_datalog_term_parse_context_t context,
                                  int *has_anonymous) {
    memset(term, 0, sizeof(*term));
    if (p->tok.kind == MAELYS_DATALOG_TOKEN_VARIABLE) {
        term->kind = MAELYS_DATALOG_TERM_VAR;
        term->as.variable = (unsigned)(p->tok.text[0] - 'A');
        return next(p);
    }
    if (p->tok.kind == MAELYS_DATALOG_TOKEN_UNDERSCORE) {
        if (has_anonymous) *has_anonymous = 1;
        if (context == MAELYS_DATALOG_TERM_CTX_HEAD_CANDIDATE) {
            term->kind = MAELYS_DATALOG_TERM_VAR;
            term->as.variable = MAELYS_DATALOG_ANONYMOUS_CANDIDATE_VAR;
            return next(p);
        }
        if (context == MAELYS_DATALOG_TERM_CTX_BODY_ATOM) {
            return allocate_anonymous_variable(p, term);
        }
        parser_diag(p,
                    MAELYS_DATALOG_DIAG_PARSER_ANONYMOUS_VARIABLE_IN_COMPARISON,
                    "anonymous variable is not allowed in comparisons",
                    "use a named variable bound by a positive body atom");
        return MAELYS_ERR_INVALID_FIELD;
    }
    if (p->tok.kind == MAELYS_DATALOG_TOKEN_STRING) {
        if (!maelys_datalog_predicate_registry_atom_allowed(&p->ruleset->registry, p->tok.text)) {
            parser_diag(p,
                        MAELYS_DATALOG_DIAG_PARSER_UNKNOWN_ATOM,
                        "unknown atom",
                        "register the atom in the domain registry");
            return MAELYS_ERR_INVALID_FIELD;
        }
        maelys_datalog_symbol_id_t sid;
        maelys_result_t rc = maelys_datalog_symbol_intern(&p->ruleset->symbols, p->tok.text, p->tok.len, &sid);
        if (rc != MAELYS_OK) return rc;
        term->kind = MAELYS_DATALOG_TERM_SYMBOL;
        term->as.symbol = sid;
        return next(p);
    }
    if (p->tok.kind == MAELYS_DATALOG_TOKEN_INTEGER) {
        term->kind = MAELYS_DATALOG_TERM_INT;
        term->as.integer = p->tok.integer;
        return next(p);
    }
    if (p->tok.kind == MAELYS_DATALOG_TOKEN_BOOLEAN) {
        term->kind = MAELYS_DATALOG_TERM_BOOL;
        term->as.boolean = p->tok.boolean;
        return next(p);
    }
    parser_diag(p,
                MAELYS_DATALOG_DIAG_LEXER_INVALID_TOKEN,
                "expected term",
                "use a variable, string atom, integer, or boolean term");
    return MAELYS_ERR_INVALID_FIELD;
}

static maelys_result_t parse_atom(parser_t *p,
                                  maelys_datalog_fact_t *atom,
                                  maelys_datalog_term_parse_context_t context,
                                  int *has_anonymous) {
    if (p->tok.kind != MAELYS_DATALOG_TOKEN_PREDICATE) {
        parser_diag(p,
                    MAELYS_DATALOG_DIAG_PARSER_EXPECTED_PREDICATE,
                    "expected predicate",
                    "start facts and rule heads with a declared predicate");
        return MAELYS_ERR_INVALID_FIELD;
    }
    char pred[64];
    size_t pred_len = p->tok.len;
    if (pred_len >= sizeof(pred)) {
        parser_diag(p,
                    MAELYS_DATALOG_DIAG_PARSER_UNKNOWN_PREDICATE,
                    "predicate name too long",
                    "predicate names must be shorter than 64 characters");
        return MAELYS_ERR_INVALID_FIELD;
    }
    memcpy(pred, p->tok.text, pred_len);
    pred[pred_len] = '\0';
    size_t pred_line = p->tok.line;
    size_t pred_column = p->tok.column;
    maelys_result_t rc = next(p);
    if (rc != MAELYS_OK) return rc;
    if (p->tok.kind != MAELYS_DATALOG_TOKEN_LPAREN) {
        parser_diag(p,
                    MAELYS_DATALOG_DIAG_LEXER_INVALID_TOKEN,
                    "expected predicate argument list",
                    "add parentheses after the predicate name");
        maelys_datalog_diagnostic_set_predicate(p->diag, pred, 0);
        return MAELYS_ERR_INVALID_FIELD;
    }
    rc = next(p);
    if (rc != MAELYS_OK) return rc;
    maelys_datalog_term_t terms[MAELYS_DATALOG_MAX_TERMS];
    size_t arity = 0;
    if (p->tok.kind != MAELYS_DATALOG_TOKEN_RPAREN) {
        for (;;) {
            if (arity >= MAELYS_DATALOG_MAX_TERMS) {
                parser_diag(p,
                            MAELYS_DATALOG_DIAG_PARSER_ARITY_MISMATCH,
                            "predicate arity exceeds maximum",
                            "reduce predicate arity or update the accepted predicate declaration");
                maelys_datalog_diagnostic_set_predicate(p->diag, pred, arity + 1u);
                maelys_datalog_diagnostic_set_limit(p->diag, arity + 1u, MAELYS_DATALOG_MAX_TERMS);
                return MAELYS_ERR_PAYLOAD_TOO_LARGE;
            }
            rc = parse_term(p, &terms[arity++], context, has_anonymous);
            if (rc != MAELYS_OK) return rc;
            if (p->tok.kind == MAELYS_DATALOG_TOKEN_COMMA) {
                rc = next(p);
                if (rc != MAELYS_OK) return rc;
                continue;
            }
            break;
        }
    }
    if (p->tok.kind != MAELYS_DATALOG_TOKEN_RPAREN) {
        parser_diag(p,
                    MAELYS_DATALOG_DIAG_LEXER_INVALID_TOKEN,
                    "expected closing parenthesis",
                    "close the predicate argument list");
        maelys_datalog_diagnostic_set_predicate(p->diag, pred, arity);
        return MAELYS_ERR_INVALID_FIELD;
    }
    maelys_datalog_predicate_id_t pid;
    if (!maelys_datalog_predicate_registry_find(&p->ruleset->registry, pred, arity, &pid)) {
        maelys_datalog_diagnostic_set(p->diag,
                                      predicate_name_exists(&p->ruleset->registry, pred)
                                          ? MAELYS_DATALOG_DIAG_PARSER_ARITY_MISMATCH
                                          : MAELYS_DATALOG_DIAG_PARSER_UNKNOWN_PREDICATE,
                                      "parser",
                                      p->file_path,
                                      pred_line,
                                      pred_column,
                                      predicate_name_exists(&p->ruleset->registry, pred)
                                          ? "predicate arity mismatch"
                                          : "unknown predicate",
                                      predicate_name_exists(&p->ruleset->registry, pred)
                                          ? "check registry or manifest arity"
                                          : "declare predicate in the domain registry");
        maelys_datalog_diagnostic_set_predicate(p->diag, pred, arity);
        return MAELYS_ERR_INVALID_FIELD;
    }
    memset(atom, 0, sizeof(*atom));
    atom->predicate_id = pid;
    atom->arity = (uint8_t)arity;
    for (size_t i = 0; i < arity; i++) atom->terms[i] = terms[i];
    return next(p);
}

static maelys_datalog_cmp_op_t cmp_kind(maelys_datalog_token_kind_t k) {
    switch (k) {
        case MAELYS_DATALOG_TOKEN_EQ: return MAELYS_DATALOG_CMP_EQ;
        case MAELYS_DATALOG_TOKEN_NEQ: return MAELYS_DATALOG_CMP_NEQ;
        case MAELYS_DATALOG_TOKEN_LT: return MAELYS_DATALOG_CMP_LT;
        case MAELYS_DATALOG_TOKEN_LTE: return MAELYS_DATALOG_CMP_LTE;
        case MAELYS_DATALOG_TOKEN_GT: return MAELYS_DATALOG_CMP_GT;
        case MAELYS_DATALOG_TOKEN_GTE: return MAELYS_DATALOG_CMP_GTE;
        default: return 0;
    }
}

typedef struct {
    uint8_t root;
    maelys_datalog_term_t term;
    int has_simple_term;
    int is_composite;
} arith_operand_t;

static int token_starts_comparison_operand(maelys_datalog_token_kind_t kind) {
    return kind == MAELYS_DATALOG_TOKEN_VARIABLE ||
           kind == MAELYS_DATALOG_TOKEN_STRING ||
           kind == MAELYS_DATALOG_TOKEN_INTEGER ||
           kind == MAELYS_DATALOG_TOKEN_BOOLEAN ||
           kind == MAELYS_DATALOG_TOKEN_UNDERSCORE ||
           kind == MAELYS_DATALOG_TOKEN_LPAREN;
}

static maelys_result_t arith_expr_add_node(parser_t *p,
                                           maelys_datalog_rule_t *rule,
                                           maelys_datalog_arith_expr_kind_t kind,
                                           const maelys_datalog_term_t *term,
                                           uint8_t left,
                                           uint8_t right,
                                           uint8_t *out_index) {
    if (!p || !rule || !out_index) return MAELYS_ERR_INVALID_ARGUMENT;
    if (rule->expr_node_count >= MAELYS_DATALOG_MAX_ARITH_EXPR_NODES) {
        parser_diag(p,
                    MAELYS_DATALOG_DIAG_PARSER_INVALID_COMPARISON,
                    "arithmetic expression node limit exceeded",
                    "simplify the arithmetic expression");
        maelys_datalog_diagnostic_set_limit(p->diag,
                                            (size_t)rule->expr_node_count + 1u,
                                            MAELYS_DATALOG_MAX_ARITH_EXPR_NODES);
        return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    }
    uint8_t idx = rule->expr_node_count++;
    maelys_datalog_arith_expr_node_t *node = &rule->expr_nodes[idx];
    memset(node, 0, sizeof(*node));
    node->kind = kind;
    node->left = left;
    node->right = right;
    if (term) node->term = *term;
    *out_index = idx;
    return MAELYS_OK;
}

static maelys_result_t parse_arith_sum(parser_t *p,
                                       maelys_datalog_rule_t *rule,
                                       unsigned depth,
                                       uint8_t *out_root,
                                       int *out_composite);

static maelys_result_t parse_arith_atom(parser_t *p,
                                        maelys_datalog_rule_t *rule,
                                        unsigned depth,
                                        uint8_t *out_root,
                                        int *out_composite) {
    if (depth > MAELYS_DATALOG_MAX_ARITH_EXPR_DEPTH) {
        parser_diag(p,
                    MAELYS_DATALOG_DIAG_PARSER_INVALID_COMPARISON,
                    "arithmetic expression depth limit exceeded",
                    "reduce parentheses or split the expression");
        maelys_datalog_diagnostic_set_limit(p->diag,
                                            (size_t)depth,
                                            MAELYS_DATALOG_MAX_ARITH_EXPR_DEPTH);
        return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    }
    if (p->tok.kind == MAELYS_DATALOG_TOKEN_INTEGER ||
        p->tok.kind == MAELYS_DATALOG_TOKEN_VARIABLE) {
        maelys_datalog_term_t term;
        int has_anonymous = 0;
        maelys_result_t rc = parse_term(p, &term, MAELYS_DATALOG_TERM_CTX_COMPARISON, &has_anonymous);
        if (rc != MAELYS_OK) return rc;
        maelys_datalog_arith_expr_kind_t kind =
            term.kind == MAELYS_DATALOG_TERM_INT
                ? MAELYS_DATALOG_ARITH_EXPR_INT_LITERAL
                : MAELYS_DATALOG_ARITH_EXPR_VAR;
        *out_composite = 0;
        return arith_expr_add_node(p,
                                   rule,
                                   kind,
                                   &term,
                                   MAELYS_DATALOG_ARITH_EXPR_NO_NODE,
                                   MAELYS_DATALOG_ARITH_EXPR_NO_NODE,
                                   out_root);
    }
    if (p->tok.kind == MAELYS_DATALOG_TOKEN_UNDERSCORE) {
        parser_diag(p,
                    MAELYS_DATALOG_DIAG_PARSER_ANONYMOUS_VARIABLE_IN_COMPARISON,
                    "anonymous variable is not allowed in arithmetic expressions",
                    "use a named variable bound by a positive body atom");
        return MAELYS_ERR_INVALID_FIELD;
    }
    if (p->tok.kind == MAELYS_DATALOG_TOKEN_LPAREN) {
        maelys_result_t rc = next(p);
        if (rc != MAELYS_OK) return rc;
        int inner_composite = 0;
        rc = parse_arith_sum(p, rule, depth + 1u, out_root, &inner_composite);
        if (rc != MAELYS_OK) return rc;
        if (p->tok.kind != MAELYS_DATALOG_TOKEN_RPAREN) {
            parser_diag(p,
                        MAELYS_DATALOG_DIAG_LEXER_INVALID_TOKEN,
                        "expected ')' in arithmetic expression",
                        "close the parenthesized arithmetic expression");
            return MAELYS_ERR_INVALID_FIELD;
        }
        rc = next(p);
        if (rc != MAELYS_OK) return rc;
        *out_composite = 1;
        return MAELYS_OK;
    }
    parser_diag(p,
                MAELYS_DATALOG_DIAG_PARSER_INVALID_COMPARISON,
                "expected integer arithmetic expression",
                "use integer literals, variables, +, -, *, and parentheses");
    return MAELYS_ERR_INVALID_FIELD;
}

static maelys_result_t parse_arith_product(parser_t *p,
                                           maelys_datalog_rule_t *rule,
                                           unsigned depth,
                                           uint8_t *out_root,
                                           int *out_composite) {
    maelys_result_t rc = parse_arith_atom(p, rule, depth, out_root, out_composite);
    if (rc != MAELYS_OK) return rc;
    while (p->tok.kind == MAELYS_DATALOG_TOKEN_STAR) {
        rc = next(p);
        if (rc != MAELYS_OK) return rc;
        uint8_t rhs = MAELYS_DATALOG_ARITH_EXPR_NO_NODE;
        int rhs_composite = 0;
        rc = parse_arith_atom(p, rule, depth, &rhs, &rhs_composite);
        if (rc != MAELYS_OK) return rc;
        uint8_t root = MAELYS_DATALOG_ARITH_EXPR_NO_NODE;
        rc = arith_expr_add_node(p,
                                 rule,
                                 MAELYS_DATALOG_ARITH_EXPR_MUL,
                                 NULL,
                                 *out_root,
                                 rhs,
                                 &root);
        if (rc != MAELYS_OK) return rc;
        *out_root = root;
        *out_composite = 1;
        (void)rhs_composite;
    }
    return MAELYS_OK;
}

static maelys_result_t parse_arith_sum(parser_t *p,
                                       maelys_datalog_rule_t *rule,
                                       unsigned depth,
                                       uint8_t *out_root,
                                       int *out_composite) {
    maelys_result_t rc = parse_arith_product(p, rule, depth, out_root, out_composite);
    if (rc != MAELYS_OK) return rc;
    while (p->tok.kind == MAELYS_DATALOG_TOKEN_PLUS ||
           p->tok.kind == MAELYS_DATALOG_TOKEN_MINUS) {
        maelys_datalog_arith_expr_kind_t kind =
            p->tok.kind == MAELYS_DATALOG_TOKEN_PLUS
                ? MAELYS_DATALOG_ARITH_EXPR_ADD
                : MAELYS_DATALOG_ARITH_EXPR_SUB;
        rc = next(p);
        if (rc != MAELYS_OK) return rc;
        uint8_t rhs = MAELYS_DATALOG_ARITH_EXPR_NO_NODE;
        int rhs_composite = 0;
        rc = parse_arith_product(p, rule, depth, &rhs, &rhs_composite);
        if (rc != MAELYS_OK) return rc;
        uint8_t root = MAELYS_DATALOG_ARITH_EXPR_NO_NODE;
        rc = arith_expr_add_node(p, rule, kind, NULL, *out_root, rhs, &root);
        if (rc != MAELYS_OK) return rc;
        *out_root = root;
        *out_composite = 1;
        (void)rhs_composite;
    }
    return MAELYS_OK;
}

static maelys_result_t parse_comparison_operand(parser_t *p,
                                                maelys_datalog_rule_t *rule,
                                                arith_operand_t *out) {
    if (!p || !rule || !out) return MAELYS_ERR_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    out->root = MAELYS_DATALOG_ARITH_EXPR_NO_NODE;
    if (p->tok.kind == MAELYS_DATALOG_TOKEN_VARIABLE ||
        p->tok.kind == MAELYS_DATALOG_TOKEN_INTEGER ||
        p->tok.kind == MAELYS_DATALOG_TOKEN_LPAREN) {
        maelys_result_t rc = parse_arith_sum(p, rule, 1u, &out->root, &out->is_composite);
        if (rc != MAELYS_OK) return rc;
        const maelys_datalog_arith_expr_node_t *root = &rule->expr_nodes[out->root];
        if (!out->is_composite &&
            (root->kind == MAELYS_DATALOG_ARITH_EXPR_INT_LITERAL ||
             root->kind == MAELYS_DATALOG_ARITH_EXPR_VAR)) {
            out->term = root->term;
            out->has_simple_term = 1;
        }
        return MAELYS_OK;
    }
    int has_anonymous = 0;
    maelys_result_t rc = parse_term(p, &out->term, MAELYS_DATALOG_TERM_CTX_COMPARISON, &has_anonymous);
    if (rc != MAELYS_OK) return rc;
    out->has_simple_term = 1;
    return MAELYS_OK;
}

static maelys_result_t parse_literal(parser_t *p,
                                     maelys_datalog_rule_t *rule,
                                     maelys_datalog_literal_t *lit) {
    memset(lit, 0, sizeof(*lit));
    lit->lhs_expr_root = MAELYS_DATALOG_ARITH_EXPR_NO_NODE;
    lit->rhs_expr_root = MAELYS_DATALOG_ARITH_EXPR_NO_NODE;
    if (p->tok.kind == MAELYS_DATALOG_TOKEN_NOT) {
        maelys_result_t rc = next(p);
        if (rc != MAELYS_OK) return rc;
        if (p->tok.kind != MAELYS_DATALOG_TOKEN_LPAREN) {
            parser_diag(p,
                        MAELYS_DATALOG_DIAG_LEXER_INVALID_TOKEN,
                        "expected '(' after not",
                        "use not(predicate(X)) syntax");
            return MAELYS_ERR_INVALID_FIELD;
        }
        rc = next(p);
        if (rc != MAELYS_OK) return rc;
        lit->kind = MAELYS_DATALOG_LITERAL_NEGATED_ATOM;
        int has_anonymous = 0;
        rc = parse_atom(p, &lit->atom, MAELYS_DATALOG_TERM_CTX_BODY_ATOM, &has_anonymous);
        if (rc != MAELYS_OK) return rc;
        if (has_anonymous) {
            parser_diag(p,
                        MAELYS_DATALOG_DIAG_PARSER_ANONYMOUS_VARIABLE_IN_COMPARISON,
                        "anonymous variable not allowed in not()",
                        "bind variables via positive atoms before using not()");
            return MAELYS_ERR_INVALID_FIELD;
        }
        if (p->tok.kind != MAELYS_DATALOG_TOKEN_RPAREN) {
            parser_diag(p,
                        MAELYS_DATALOG_DIAG_LEXER_INVALID_TOKEN,
                        "expected ')' to close not()",
                        "close the not() form with a parenthesis");
            return MAELYS_ERR_INVALID_FIELD;
        }
        rc = next(p);
        if (rc != MAELYS_OK) return rc;
        p->ruleset->negation_supported = 1;
        return MAELYS_OK;
    }
    if (p->tok.kind == MAELYS_DATALOG_TOKEN_PREDICATE) {
        lit->kind = MAELYS_DATALOG_LITERAL_ATOM;
        int has_anonymous = 0;
        return parse_atom(p, &lit->atom, MAELYS_DATALOG_TERM_CTX_BODY_ATOM, &has_anonymous);
    }
    if (token_starts_comparison_operand(p->tok.kind)) {
        arith_operand_t lhs;
        arith_operand_t rhs;
        maelys_result_t rc = parse_comparison_operand(p, rule, &lhs);
        if (rc != MAELYS_OK) return rc;
        if (!token_is_cmp(p->tok.kind)) {
            parser_diag(p,
                        MAELYS_DATALOG_DIAG_PARSER_INVALID_COMPARISON,
                        "expected comparison operator",
                        "use a supported comparison between typed terms");
            return MAELYS_ERR_INVALID_FIELD;
        }
        lit->op = cmp_kind(p->tok.kind);
        rc = next(p);
        if (rc != MAELYS_OK) return rc;
        rc = parse_comparison_operand(p, rule, &rhs);
        if (rc != MAELYS_OK) return rc;
        const int uses_arith_expr = lhs.is_composite || rhs.is_composite;
        if (uses_arith_expr && (lhs.root == MAELYS_DATALOG_ARITH_EXPR_NO_NODE ||
                                rhs.root == MAELYS_DATALOG_ARITH_EXPR_NO_NODE)) {
            parser_diag(p,
                        MAELYS_DATALOG_DIAG_PARSER_INVALID_COMPARISON,
                        "arithmetic comparison requires integer expressions",
                        "do not mix arithmetic expressions with symbols or booleans");
            return MAELYS_ERR_INVALID_FIELD;
        }
        if (!uses_arith_expr) {
            if (!lhs.has_simple_term || !rhs.has_simple_term) {
                parser_diag(p,
                            MAELYS_DATALOG_DIAG_PARSER_INVALID_COMPARISON,
                            "expected comparison terms",
                            "use simple typed terms or integer arithmetic expressions");
                return MAELYS_ERR_INVALID_FIELD;
            }
            lit->lhs = lhs.term;
            lit->rhs = rhs.term;
        } else {
            lit->lhs = lhs.has_simple_term ? lhs.term : (maelys_datalog_term_t){0};
            lit->rhs = rhs.has_simple_term ? rhs.term : (maelys_datalog_term_t){0};
            lit->lhs_expr_root = lhs.root;
            lit->rhs_expr_root = rhs.root;
            lit->has_arith_expr = 1;
        }
        if (!lit->has_arith_expr &&
            lit->lhs.kind != MAELYS_DATALOG_TERM_VAR && lit->rhs.kind != MAELYS_DATALOG_TERM_VAR) {
            if (lit->lhs.kind != lit->rhs.kind) {
                parser_diag(p,
                            MAELYS_DATALOG_DIAG_PARSER_INVALID_COMPARISON,
                            "comparison term kind mismatch",
                            "compare terms of the same kind");
                return MAELYS_ERR_INVALID_FIELD;
            }
            if ((lit->lhs.kind == MAELYS_DATALOG_TERM_SYMBOL || lit->lhs.kind == MAELYS_DATALOG_TERM_BOOL) &&
                lit->op != MAELYS_DATALOG_CMP_EQ && lit->op != MAELYS_DATALOG_CMP_NEQ) {
                parser_diag(p,
                            MAELYS_DATALOG_DIAG_PARSER_INVALID_COMPARISON,
                            "invalid comparison operator for term kind",
                            "symbols and booleans support equality and inequality only");
                return MAELYS_ERR_INVALID_FIELD;
            }
        }
        lit->kind = MAELYS_DATALOG_LITERAL_COMPARISON;
        return MAELYS_OK;
    }
    parser_diag(p,
                MAELYS_DATALOG_DIAG_LEXER_INVALID_TOKEN,
                "unexpected token in rule body",
                "expected a predicate atom or comparison");
    return MAELYS_ERR_INVALID_FIELD;
}

static void vars_in_atom(const maelys_datalog_fact_t *a, uint32_t *mask) {
    for (size_t i = 0; i < a->arity; i++) {
        if (a->terms[i].kind == MAELYS_DATALOG_TERM_VAR &&
            a->terms[i].as.variable < MAELYS_DATALOG_MAX_RULE_VARIABLES) {
            *mask |= (1u << a->terms[i].as.variable);
        }
    }
}

static void vars_in_arith_expr(const maelys_datalog_rule_t *rule,
                               uint8_t root,
                               uint32_t *mask) {
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

static maelys_result_t validate_rule(parser_t *p, const maelys_datalog_rule_t *rule) {
    maelys_datalog_ruleset_t *r = p->ruleset;
    const maelys_datalog_predicate_def_t *head_def =
        maelys_datalog_predicate_registry_get(&r->registry, rule->head.predicate_id);
    if (!head_def ||
        (head_def->kind_flags & (MAELYS_DATALOG_PRED_KIND_EDB |
                                 MAELYS_DATALOG_PRED_KIND_POLICY_FACT))) {
        parser_diag(p,
                    MAELYS_DATALOG_DIAG_PARSER_RULE_HEAD_EDB_FORBIDDEN,
                    "base predicate used in rule head",
                    "EDB/runtime and policy fact predicates cannot appear in rule heads");
        if (head_def) maelys_datalog_diagnostic_set_predicate(p->diag, head_def->name, head_def->arity);
        return MAELYS_ERR_INVALID_FIELD;
    }
    uint32_t head_vars = 0;
    uint32_t body_vars = 0;
    vars_in_atom(&rule->head, &head_vars);
    for (size_t i = 0; i < rule->body_count; i++) {
        if (rule->body[i].kind == MAELYS_DATALOG_LITERAL_ATOM) {
            vars_in_atom(&rule->body[i].atom, &body_vars);
            if (rule->body[i].atom.predicate_id == rule->head.predicate_id) r->has_positive_recursion = 1;
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
                parser_diag(p,
                            MAELYS_DATALOG_DIAG_PARSER_UNSAFE_VARIABLE,
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
                parser_diag(p,
                            MAELYS_DATALOG_DIAG_PARSER_UNSAFE_VARIABLE,
                            "negated atom variable not bound by positive body atom",
                            "bind all not() variables in positive body atoms first");
                maelys_datalog_diagnostic_set_predicate(p->diag, head_def->name, head_def->arity);
                return MAELYS_ERR_INVALID_FIELD;
            }
        }
    }
    if (head_vars & ~body_vars) {
        parser_diag(p,
                    MAELYS_DATALOG_DIAG_PARSER_UNSAFE_VARIABLE,
                    "head variable not bound by positive body atom",
                    "bind every head variable in a positive body atom");
        maelys_datalog_diagnostic_set_predicate(p->diag, head_def->name, head_def->arity);
        return MAELYS_ERR_INVALID_FIELD;
    }
    return MAELYS_OK;
}

static maelys_result_t assign_strata(parser_t *p) {
    if (!p || !p->ruleset) return MAELYS_ERR_INVALID_ARGUMENT;
    maelys_datalog_ruleset_t *ruleset = p->ruleset;
    if (!ruleset->negation_supported) return MAELYS_OK;

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
            if (hpid >= MAELYS_DATALOG_MAX_PREDICATES) return MAELYS_ERR_INVALID_FIELD;
            uint32_t s = ruleset->strata[hpid];
            for (size_t i = 0; i < rule->body_count; i++) {
                const maelys_datalog_literal_t *literal = &rule->body[i];
                if (literal->kind != MAELYS_DATALOG_LITERAL_ATOM &&
                    literal->kind != MAELYS_DATALOG_LITERAL_NEGATED_ATOM) {
                    continue;
                }
                maelys_datalog_predicate_id_t bpid = literal->atom.predicate_id;
                if (bpid >= MAELYS_DATALOG_MAX_PREDICATES) return MAELYS_ERR_INVALID_FIELD;
                uint32_t ns = ruleset->strata[bpid];
                if (literal->kind == MAELYS_DATALOG_LITERAL_NEGATED_ATOM) {
                    if (ns >= MAELYS_DATALOG_MAX_STRATA) {
                        parser_diag(p,
                                    MAELYS_DATALOG_DIAG_POLICY_NOT_STRATIFIABLE,
                                    "negation stratum limit exceeded",
                                    "remove recursion through negation or reduce negation depth");
                        return MAELYS_ERR_INVALID_FIELD;
                    }
                    ns += 1u;
                }
                if (ns > s) s = ns;
            }
            if (s >= MAELYS_DATALOG_MAX_STRATA) {
                parser_diag(p,
                            MAELYS_DATALOG_DIAG_POLICY_NOT_STRATIFIABLE,
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
        parser_diag(p,
                    MAELYS_DATALOG_DIAG_POLICY_NOT_STRATIFIABLE,
                    "policy is not stratifiable",
                    "remove recursion through negation");
        return MAELYS_ERR_INVALID_FIELD;
    }

    uint32_t max = 0;
    for (size_t i = 0; i < MAELYS_DATALOG_MAX_PREDICATES; i++) {
        if (ruleset->strata[i] > max) max = ruleset->strata[i];
    }
    ruleset->max_stratum = max;
    ruleset->strata_assigned = 1;
    return MAELYS_OK;
}

static maelys_result_t copy_ruleset_identity(char *dst,
                                             size_t dst_size,
                                             const char *src) {
    int n = snprintf(dst, dst_size, "%s", src);
    if (n < 0 || (size_t)n >= dst_size) return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    return MAELYS_OK;
}

static maelys_result_t parse_clause(parser_t *p) {
    maelys_datalog_fact_t head;
    int head_has_anonymous = 0;
    p->anonymous_var_count = 0;
    maelys_result_t rc = parse_atom(p,
                                    &head,
                                    MAELYS_DATALOG_TERM_CTX_HEAD_CANDIDATE,
                                    &head_has_anonymous);
    if (rc != MAELYS_OK) return rc;
    if (p->tok.kind == MAELYS_DATALOG_TOKEN_DOT) {
        const maelys_datalog_predicate_def_t *def =
            maelys_datalog_predicate_registry_get(&p->ruleset->registry, head.predicate_id);
        if (!def || !(def->kind_flags & MAELYS_DATALOG_PRED_KIND_POLICY_FACT)) {
            parser_diag(p,
                        MAELYS_DATALOG_DIAG_PARSER_FACT_USES_NON_BASE_PREDICATE,
                        "direct fact uses non-policy predicate",
                        "direct .dl facts must use policy fact predicates");
            if (def) maelys_datalog_diagnostic_set_predicate(p->diag, def->name, def->arity);
            return MAELYS_ERR_INVALID_FIELD;
        }
        if (head_has_anonymous) {
            parser_diag(p,
                        MAELYS_DATALOG_DIAG_PARSER_ANONYMOUS_VARIABLE_IN_FACT,
                        "anonymous variable is not allowed in direct facts",
                        "direct .dl facts must be ground policy facts");
            if (def) maelys_datalog_diagnostic_set_predicate(p->diag, def->name, def->arity);
            return MAELYS_ERR_INVALID_FIELD;
        }
        if (p->ruleset->fact_count >= MAELYS_DATALOG_MAX_RULE_FACTS) return MAELYS_ERR_PAYLOAD_TOO_LARGE;
        for (size_t i = 0; i < head.arity; i++) {
            if (head.terms[i].kind == MAELYS_DATALOG_TERM_VAR) {
                parser_diag(p,
                            MAELYS_DATALOG_DIAG_PARSER_ANONYMOUS_VARIABLE_IN_FACT,
                            "variables are not allowed in direct facts",
                            "direct .dl facts must be ground policy facts");
                if (def) maelys_datalog_diagnostic_set_predicate(p->diag, def->name, def->arity);
                return MAELYS_ERR_INVALID_FIELD;
            }
        }
        p->ruleset->facts[p->ruleset->fact_count++] = head;
        return next(p);
    }
    if (p->tok.kind != MAELYS_DATALOG_TOKEN_NECK) {
        parser_diag(p,
                    MAELYS_DATALOG_DIAG_PARSER_EXPECTED_NECK,
                    "expected rule neck",
                    "end direct facts with a dot or introduce a rule body with :-");
        return MAELYS_ERR_INVALID_FIELD;
    }
    if (head_has_anonymous) {
        parser_diag(p,
                    MAELYS_DATALOG_DIAG_PARSER_ANONYMOUS_VARIABLE_IN_HEAD,
                    "anonymous variable is not allowed in rule heads",
                    "use named head variables bound by positive body atoms");
        const maelys_datalog_predicate_def_t *def =
            maelys_datalog_predicate_registry_get(&p->ruleset->registry, head.predicate_id);
        if (def) maelys_datalog_diagnostic_set_predicate(p->diag, def->name, def->arity);
        return MAELYS_ERR_INVALID_FIELD;
    }
    rc = next(p);
    if (rc != MAELYS_OK) return rc;
    if (p->ruleset->rule_count >= MAELYS_DATALOG_MAX_RULES) return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    maelys_datalog_rule_t rule;
    memset(&rule, 0, sizeof(rule));
    rule.head = head;
    rule.rule_id = p->ruleset->rule_count + 1u;
    for (;;) {
        if (rule.body_count >= MAELYS_DATALOG_MAX_BODY_LITERALS) {
            const maelys_datalog_predicate_def_t *def =
                maelys_datalog_predicate_registry_get(&p->ruleset->registry, head.predicate_id);
            parser_diag(p,
                        MAELYS_DATALOG_DIAG_PARSER_RULE_BODY_LITERAL_OVERFLOW,
                        "rule body literal limit exceeded",
                        "split rule into IDB helper predicates");
            if (def) maelys_datalog_diagnostic_set_predicate(p->diag, def->name, def->arity);
            maelys_datalog_diagnostic_set_limit(p->diag,
                                                rule.body_count + 1u,
                                                MAELYS_DATALOG_MAX_BODY_LITERALS);
            return MAELYS_ERR_PAYLOAD_TOO_LARGE;
        }
        rc = parse_literal(p, &rule, &rule.body[rule.body_count++]);
        if (rc != MAELYS_OK) return rc;
        if (p->tok.kind == MAELYS_DATALOG_TOKEN_COMMA) {
            rc = next(p);
            if (rc != MAELYS_OK) return rc;
            continue;
        }
        break;
    }
    if (p->tok.kind != MAELYS_DATALOG_TOKEN_DOT) {
        parser_diag(p,
                    MAELYS_DATALOG_DIAG_PARSER_EXPECTED_DOT,
                    "expected clause terminator",
                    "terminate facts and rules with a dot");
        return MAELYS_ERR_INVALID_FIELD;
    }
    rc = validate_rule(p, &rule);
    if (rc != MAELYS_OK) return rc;
    p->ruleset->rules[p->ruleset->rule_count++] = rule;
    return next(p);
}

maelys_result_t maelys_datalog_ruleset_init(maelys_datalog_ruleset_t *ruleset,
                                            const char *policy_id,
                                            const char *domain,
                                            const char *sha256,
                                            int test_only) {
    if (!ruleset || !policy_id || !domain || !sha256) return MAELYS_ERR_INVALID_ARGUMENT;
    if (ruleset->loaded) return MAELYS_ERR_INVALID_STATE;
    memset(ruleset, 0, sizeof(*ruleset));
    maelys_result_t rc = copy_ruleset_identity(ruleset->policy_id, sizeof(ruleset->policy_id), policy_id);
    if (rc != MAELYS_OK) return rc;
    rc = copy_ruleset_identity(ruleset->domain, sizeof(ruleset->domain), domain);
    if (rc != MAELYS_OK) return rc;
    rc = copy_ruleset_identity(ruleset->sha256, sizeof(ruleset->sha256), sha256);
    if (rc != MAELYS_OK) return rc;
    ruleset->positive_recursion_supported = 1;
    ruleset->negation_supported = 0;
    ruleset->negation_recursion_supported = 0;
    ruleset->test_only = test_only ? 1 : 0;
    maelys_datalog_symbol_table_init(&ruleset->symbols);
    maelys_datalog_predicate_registry_init_core(&ruleset->registry);
    ruleset->loaded = 1;
    return MAELYS_OK;
}

void maelys_datalog_ruleset_clear(maelys_datalog_ruleset_t *ruleset) {
    if (!ruleset) return;
    memset(ruleset, 0, sizeof(*ruleset));
}

maelys_result_t maelys_datalog_parse_ruleset(maelys_datalog_ruleset_t *ruleset,
                                             const char *src,
                                             size_t len) {
    return maelys_datalog_parse_ruleset_ex(ruleset, src, len, NULL, NULL);
}

maelys_result_t maelys_datalog_parse_ruleset_ex(maelys_datalog_ruleset_t *ruleset,
                                                const char *src,
                                                size_t len,
                                                const char *file_path,
                                                maelys_datalog_diagnostic_t *out_diag) {
    if (!ruleset || !ruleset->loaded) return MAELYS_ERR_INVALID_STATE;
    if (out_diag) maelys_datalog_diagnostic_clear(out_diag);
    if (!maelys_datalog_predicate_registry_is_frozen(&ruleset->registry)) {
        maelys_datalog_diagnostic_set(out_diag,
                                      MAELYS_DATALOG_DIAG_REGISTRY_MUTATION_AFTER_FREEZE,
                                      "registry",
                                      file_path,
                                      0,
                                      0,
                                      "predicate registry is not frozen",
                                      "freeze the registry before parsing policy text");
        return MAELYS_ERR_INVALID_STATE;
    }
    parser_t p;
    memset(&p, 0, sizeof(p));
    p.ruleset = ruleset;
    p.file_path = file_path;
    p.diag = out_diag;
    maelys_result_t rc = maelys_datalog_lexer_init_ex(&p.lexer, src, len, file_path, out_diag);
    if (rc != MAELYS_OK) return rc;
    rc = next(&p);
    if (rc != MAELYS_OK) return rc;
    while (p.tok.kind != MAELYS_DATALOG_TOKEN_EOF) {
        rc = parse_clause(&p);
        if (rc != MAELYS_OK) return rc;
    }
    if (ruleset->negation_supported) {
        rc = assign_strata(&p);
        if (rc != MAELYS_OK) return rc;
    }
    return MAELYS_OK;
}

/* Always returns 0. Maelys policy is fail-closed by design:
 * allow_projection must be explicitly derived by Datalog rules.
 * A ruleset can never implicitly allow everything.
 * This is intentional, not a missing implementation. */
int maelys_datalog_ruleset_has_allow_all(const maelys_datalog_ruleset_t *r) {
    (void)r;
    return 0;
}
