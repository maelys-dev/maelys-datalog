#include "src/core/maelys_datalog_parser.h"
#include "src/compiler/maelys_datalog_program_internal.h"
#include "src/core/maelys_datalog_pipeline_testing.h"

#include "src/core/maelys_datalog_lexer.h"
#include "src/core/maelys_datalog_filter.h"
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
    unsigned flags;
    maelys_datalog_parse_origin_t *origin;
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

static int token_is_contextual_or(const maelys_datalog_token_t *tok) {
    return tok && tok->kind == MAELYS_DATALOG_TOKEN_PREDICATE &&
           tok->len == 2u && memcmp(tok->text, "or", 2u) == 0;
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

static maelys_result_t validate_policy_symbol_constant(parser_t *p) {
    const int declared = maelys_datalog_predicate_registry_atom_allowed(
        &p->ruleset->registry, p->tok.text);
    if (!declared &&
        !(p->flags & MAELYS_DATALOG_PARSE_ALLOW_UNDECLARED_POLICY_ATOMS)) {
        parser_diag(p,
                    MAELYS_DATALOG_DIAG_PARSER_UNKNOWN_ATOM,
                    "unknown atom",
                    "register the atom in the domain registry");
        return MAELYS_ERR_INVALID_FIELD;
    }
    const size_t max_policy_atom_bytes =
        sizeof(p->ruleset->registry.atoms[0]) - 1u;
    if (!declared && p->tok.len > max_policy_atom_bytes) {
        parser_diag(p,
                    MAELYS_DATALOG_DIAG_LEXER_STRING_TOO_LONG,
                    "policy atom exceeds capacity",
                    "limit policy atoms to 63 UTF-8 bytes");
        maelys_datalog_diagnostic_set_limit(
            p->diag, p->tok.len, max_policy_atom_bytes);
        return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    }
    maelys_datalog_symbol_id_t sid;
    int found = 0;
    maelys_result_t rc = maelys_datalog_symbol_lookup_readonly(
        &p->ruleset->symbols, p->tok.text, p->tok.len, &sid, &found);
    if (rc != MAELYS_OK) return rc;
    if (!found && p->ruleset->symbols.count >= MAELYS_DATALOG_MAX_ATOMS) {
        parser_diag(p,
                    MAELYS_DATALOG_DIAG_LEXER_STRING_TOO_LONG,
                    "policy atom count exceeds capacity",
                    "limit each ruleset to 256 distinct policy atoms");
        maelys_datalog_diagnostic_set_limit(
            p->diag, p->ruleset->symbols.count + 1u, MAELYS_DATALOG_MAX_ATOMS);
        return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    }
    return MAELYS_OK;
}

static maelys_result_t intern_policy_symbol_constant(
    parser_t *p,
    maelys_datalog_term_t *term) {
    maelys_result_t rc = validate_policy_symbol_constant(p);
    if (rc != MAELYS_OK) return rc;
    maelys_datalog_symbol_id_t sid;
    rc = maelys_datalog_symbol_intern(
        &p->ruleset->symbols, p->tok.text, p->tok.len, &sid);
    if (rc != MAELYS_OK) return rc;
    term->kind = MAELYS_DATALOG_TERM_SYMBOL;
    term->as.symbol = sid;
    return MAELYS_OK;
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
        maelys_result_t rc = intern_policy_symbol_constant(p, term);
        if (rc != MAELYS_OK) return rc;
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

static maelys_result_t parse_filter_literal(
    parser_t *p,
    const maelys_datalog_filter_definition_t *definition,
    maelys_datalog_literal_t *lit) {
    if (!p || !definition || !lit) return MAELYS_ERR_INVALID_ARGUMENT;
    maelys_result_t rc = next(p);
    if (rc != MAELYS_OK) return rc;
    if (p->tok.kind != MAELYS_DATALOG_TOKEN_LPAREN) {
        parser_diag(p,
                    MAELYS_DATALOG_DIAG_PARSER_INVALID_FILTER,
                    "expected filter argument list",
                    "use filter(Value, \"constant pattern\")");
        return MAELYS_ERR_INVALID_FIELD;
    }
    rc = next(p);
    if (rc != MAELYS_OK) return rc;
    if (p->tok.kind != MAELYS_DATALOG_TOKEN_VARIABLE &&
        p->tok.kind != MAELYS_DATALOG_TOKEN_STRING) {
        parser_diag(p,
                    MAELYS_DATALOG_DIAG_PARSER_INVALID_FILTER,
                    "filter value must be symbolic",
                    "bind a symbol variable in a positive atom or use an allowed symbol");
        return MAELYS_ERR_INVALID_FIELD;
    }
    maelys_datalog_term_t value;
    memset(&value, 0, sizeof(value));
    char value_text[MAELYS_DATALOG_MAX_STRING_BYTES];
    size_t value_length = 0u;
    int value_requires_intern = 0;
    if (p->tok.kind == MAELYS_DATALOG_TOKEN_VARIABLE) {
        value.kind = MAELYS_DATALOG_TERM_VAR;
        value.as.variable = (unsigned)(p->tok.text[0] - 'A');
    } else {
        rc = validate_policy_symbol_constant(p);
        if (rc != MAELYS_OK) return rc;
        value_length = p->tok.len;
        if (value_length != 0u) {
            memcpy(value_text, p->tok.text, value_length);
        }
        value_requires_intern = 1;
    }
    rc = next(p);
    if (rc != MAELYS_OK) return rc;
    if (p->tok.kind != MAELYS_DATALOG_TOKEN_COMMA) {
        parser_diag(p,
                    MAELYS_DATALOG_DIAG_PARSER_ARITY_MISMATCH,
                    "filter arity mismatch",
                    "filters require exactly a value and a constant pattern");
        return MAELYS_ERR_INVALID_FIELD;
    }
    rc = next(p);
    if (rc != MAELYS_OK) return rc;
    if (p->tok.kind != MAELYS_DATALOG_TOKEN_STRING) {
        parser_diag(p,
                    MAELYS_DATALOG_DIAG_PARSER_INVALID_FILTER,
                    "filter pattern must be a string constant",
                    "write the pattern as a quoted constant");
        return MAELYS_ERR_INVALID_FIELD;
    }
    if (p->tok.len > MAELYS_DATALOG_MAX_FILTER_PATTERN_BYTES) {
        parser_diag(p,
                    MAELYS_DATALOG_DIAG_PARSER_INVALID_FILTER,
                    "filter pattern exceeds capacity",
                    "shorten the constant pattern");
        maelys_datalog_diagnostic_set_limit(
            p->diag, p->tok.len, MAELYS_DATALOG_MAX_FILTER_PATTERN_BYTES);
        return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    }
    if (p->ruleset->filter_program_count >= MAELYS_DATALOG_MAX_FILTER_PROGRAMS) {
        parser_diag(p,
                    MAELYS_DATALOG_DIAG_PARSER_INVALID_FILTER,
                    "filter program capacity exceeded",
                    "reduce the number of filter literals");
        maelys_datalog_diagnostic_set_limit(
            p->diag,
            p->ruleset->filter_program_count + 1u,
            MAELYS_DATALOG_MAX_FILTER_PROGRAMS);
        return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    }
    if (p->tok.len > MAELYS_DATALOG_FILTER_PATTERN_POOL_BYTES -
                         p->ruleset->filter_pattern_pool_used) {
        parser_diag(p,
                    MAELYS_DATALOG_DIAG_PARSER_INVALID_FILTER,
                    "filter pattern pool capacity exceeded",
                    "reduce the total pattern bytes");
        maelys_datalog_diagnostic_set_limit(
            p->diag,
            p->ruleset->filter_pattern_pool_used + p->tok.len,
            MAELYS_DATALOG_FILTER_PATTERN_POOL_BYTES);
        return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    }
    unsigned char pattern[MAELYS_DATALOG_MAX_FILTER_PATTERN_BYTES];
    const size_t pattern_length = p->tok.len;
    if (pattern_length != 0u) memcpy(pattern, p->tok.text, pattern_length);
    rc = next(p);
    if (rc != MAELYS_OK) return rc;
    if (p->tok.kind != MAELYS_DATALOG_TOKEN_RPAREN) {
        parser_diag(p,
                    MAELYS_DATALOG_DIAG_PARSER_ARITY_MISMATCH,
                    "filter arity mismatch",
                    "filters require exactly a value and a constant pattern");
        return MAELYS_ERR_INVALID_FIELD;
    }
    rc = next(p);
    if (rc != MAELYS_OK) return rc;

    if (value_requires_intern) {
        maelys_datalog_symbol_id_t sid;
        rc = maelys_datalog_symbol_intern(
            &p->ruleset->symbols, value_text, value_length, &sid);
        if (rc != MAELYS_OK) return rc;
        value.kind = MAELYS_DATALOG_TERM_SYMBOL;
        value.as.symbol = sid;
    }

    const size_t program_index = p->ruleset->filter_program_count;
    maelys_datalog_filter_program_t *program =
        &p->ruleset->filter_programs[program_index];
    memset(program, 0, sizeof(*program));
    program->kind = (uint8_t)definition->kind;
    program->pattern_offset = (uint32_t)p->ruleset->filter_pattern_pool_used;
    program->pattern_length = (uint16_t)pattern_length;
    if (pattern_length != 0u) {
        memcpy(p->ruleset->filter_pattern_pool + program->pattern_offset,
               pattern,
               pattern_length);
    }
    p->ruleset->filter_pattern_pool_used += pattern_length;
    p->ruleset->filter_program_count++;
    lit->kind = MAELYS_DATALOG_LITERAL_FILTER;
    lit->filter_kind = (uint8_t)definition->kind;
    lit->filter_program_index = (uint16_t)program_index;
    lit->filter_value = value;
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
        char name[64];
        if (p->tok.len < sizeof(name)) {
            memcpy(name, p->tok.text, p->tok.len);
            name[p->tok.len] = '\0';
            if (!predicate_name_exists(&p->ruleset->registry, name)) {
                const maelys_datalog_filter_definition_t *definition =
                    maelys_datalog_filter_by_name_in(p->ruleset->modules, name);
                if (definition) return parse_filter_literal(p, definition, lit);
            }
        }
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

static void clear_staged_rules(maelys_datalog_ruleset_t *ruleset,
                               size_t base,
                               size_t count) {
    if (!ruleset || base >= MAELYS_DATALOG_MAX_RULES) return;
    size_t available = MAELYS_DATALOG_MAX_RULES - base;
    if (count > available) count = available;
    memset(&ruleset->rules[base], 0, count * sizeof(ruleset->rules[0]));
}

static maelys_result_t normalize_anonymous_variables(parser_t *p,
                                                     maelys_datalog_rule_t *rule) {
    unsigned next_anonymous = MAELYS_DATALOG_NAMED_VARIABLE_COUNT;
    for (size_t i = 0; i < rule->body_count; i++) {
        maelys_datalog_literal_t *literal = &rule->body[i];
        if (literal->kind != MAELYS_DATALOG_LITERAL_ATOM &&
            literal->kind != MAELYS_DATALOG_LITERAL_NEGATED_ATOM) {
            continue;
        }
        for (size_t t = 0; t < literal->atom.arity; t++) {
            maelys_datalog_term_t *term = &literal->atom.terms[t];
            if (term->kind != MAELYS_DATALOG_TERM_VAR ||
                term->as.variable < MAELYS_DATALOG_NAMED_VARIABLE_COUNT) {
                continue;
            }
            if (next_anonymous >= MAELYS_DATALOG_MAX_RULE_VARIABLES) {
                parser_diag(p,
                            MAELYS_DATALOG_DIAG_PARSER_TOO_MANY_VARIABLES,
                            "rule variable limit exceeded",
                            "reduce anonymous variables or split the rule");
                maelys_datalog_diagnostic_set_limit(
                    p->diag,
                    (size_t)next_anonymous + 1u,
                    MAELYS_DATALOG_MAX_RULE_VARIABLES);
                return MAELYS_ERR_PAYLOAD_TOO_LARGE;
            }
            term->as.variable = next_anonymous++;
        }
    }
    return MAELYS_OK;
}

static void parser_rule_expansion_overflow(parser_t *p, size_t requested) {
    parser_diag(p,
                MAELYS_DATALOG_DIAG_PARSER_RULE_BODY_LITERAL_OVERFLOW,
                "OR expansion exceeds rule capacity",
                "reduce OR alternatives or split the policy");
    maelys_datalog_diagnostic_set_limit(p->diag,
                                        requested,
                                        MAELYS_DATALOG_MAX_RULES);
}

static maelys_result_t parse_clause(parser_t *p) {
    const maelys_datalog_source_location_t source = {p->tok.line, p->tok.column};
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
        p->ruleset->program_validated = 0;
        p->ruleset->compiled_fingerprint[0] = 0;
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
    if (p->ruleset->rule_count >= MAELYS_DATALOG_MAX_RULES) {
        parser_rule_expansion_overflow(p, p->ruleset->rule_count + 1u);
        return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    }
    const size_t staged_base = p->ruleset->rule_count;
    const size_t filter_program_base = p->ruleset->filter_program_count;
    const size_t filter_pool_base = p->ruleset->filter_pattern_pool_used;
    const size_t staged_capacity = MAELYS_DATALOG_MAX_RULES - staged_base;
    size_t staged_count = 1u;
    maelys_datalog_rule_t *first = &p->ruleset->rules[staged_base];
    memset(first, 0, sizeof(*first));
    first->head = head;
    for (;;) {
        if (first->body_count >= MAELYS_DATALOG_MAX_BODY_LITERALS) {
            const maelys_datalog_predicate_def_t *def =
                maelys_datalog_predicate_registry_get(&p->ruleset->registry, head.predicate_id);
            parser_diag(p,
                        MAELYS_DATALOG_DIAG_PARSER_RULE_BODY_LITERAL_OVERFLOW,
                        "rule body literal limit exceeded",
                        "split rule into IDB helper predicates");
            if (def) maelys_datalog_diagnostic_set_predicate(p->diag, def->name, def->arity);
            maelys_datalog_diagnostic_set_limit(p->diag,
                                                first->body_count + 1u,
                                                MAELYS_DATALOG_MAX_BODY_LITERALS);
            rc = MAELYS_ERR_PAYLOAD_TOO_LARGE;
            goto fail_staged_clause;
        }

        const size_t body_index = first->body_count;
        const uint8_t expr_count_before = first->expr_node_count;
        const unsigned anonymous_before = p->anonymous_var_count;
        maelys_datalog_rule_t first_before = *first;

        rc = parse_literal(p, first, &first->body[body_index]);
        if (rc != MAELYS_OK) goto fail_staged_clause;

        if (first->body[body_index].kind == MAELYS_DATALOG_LITERAL_ATOM &&
            token_is_contextual_or(&p->tok)) {
            maelys_datalog_fact_t alternatives[MAELYS_DATALOG_MAX_RULES];
            size_t alternative_count = 1u;
            unsigned max_anonymous = p->anonymous_var_count;
            alternatives[0] = first->body[body_index].atom;

            while (token_is_contextual_or(&p->tok)) {
                if (alternative_count >= staged_capacity / staged_count) {
                    parser_rule_expansion_overflow(
                        p,
                        staged_base + staged_count * (alternative_count + 1u));
                    rc = MAELYS_ERR_PAYLOAD_TOO_LARGE;
                    goto fail_staged_clause;
                }
                rc = next(p);
                if (rc != MAELYS_OK) goto fail_staged_clause;
                p->anonymous_var_count = anonymous_before;
                int has_anonymous = 0;
                rc = parse_atom(p,
                                &alternatives[alternative_count],
                                MAELYS_DATALOG_TERM_CTX_BODY_ATOM,
                                &has_anonymous);
                if (rc != MAELYS_OK) goto fail_staged_clause;
                if (p->anonymous_var_count > max_anonymous) {
                    max_anonymous = p->anonymous_var_count;
                }
                alternative_count++;
            }
            p->anonymous_var_count = max_anonymous;

            const size_t expanded_count = staged_count * alternative_count;
            for (size_t i = staged_count; i-- > 0u;) {
                maelys_datalog_rule_t base_rule =
                    i == 0u ? first_before : p->ruleset->rules[staged_base + i];
                for (size_t a = 0; a < alternative_count; a++) {
                    maelys_datalog_rule_t *expanded =
                        &p->ruleset->rules[staged_base + i * alternative_count + a];
                    *expanded = base_rule;
                    maelys_datalog_literal_t *literal =
                        &expanded->body[expanded->body_count++];
                    memset(literal, 0, sizeof(*literal));
                    literal->kind = MAELYS_DATALOG_LITERAL_ATOM;
                    literal->lhs_expr_root = MAELYS_DATALOG_ARITH_EXPR_NO_NODE;
                    literal->rhs_expr_root = MAELYS_DATALOG_ARITH_EXPR_NO_NODE;
                    literal->atom = alternatives[a];
                }
            }
            staged_count = expanded_count;
            first = &p->ruleset->rules[staged_base];
        } else {
            first->body_count++;
            for (size_t i = 1u; i < staged_count; i++) {
                maelys_datalog_rule_t *candidate =
                    &p->ruleset->rules[staged_base + i];
                candidate->body[body_index] = first->body[body_index];
                for (uint8_t e = expr_count_before; e < first->expr_node_count; e++) {
                    candidate->expr_nodes[e] = first->expr_nodes[e];
                }
                candidate->expr_node_count = first->expr_node_count;
                candidate->body_count++;
            }
        }

        if (p->tok.kind == MAELYS_DATALOG_TOKEN_COMMA) {
            rc = next(p);
            if (rc != MAELYS_OK) goto fail_staged_clause;
            continue;
        }
        break;
    }
    if (p->tok.kind != MAELYS_DATALOG_TOKEN_DOT) {
        parser_diag(p,
                    MAELYS_DATALOG_DIAG_PARSER_EXPECTED_DOT,
                    "expected clause terminator",
                    "terminate facts and rules with a dot");
        rc = MAELYS_ERR_INVALID_FIELD;
        goto fail_staged_clause;
    }

    for (size_t i = 0; i < staged_count; i++) {
        maelys_datalog_rule_t *candidate = &p->ruleset->rules[staged_base + i];
        candidate->rule_id = staged_base + i + 1u;
        rc = normalize_anonymous_variables(p, candidate);
        if (rc != MAELYS_OK) goto fail_staged_clause;
    }
    for (size_t i = 0u; i < staged_count; ++i) {
        p->ruleset->rule_sources[staged_base + i] = source;
        if (p->origin)
            p->origin->rules[staged_base + i] = (maelys_datalog_clause_origin_t){
                staged_base, filter_program_base, filter_pool_base, p->ruleset->fact_count,
                {p->tok.line, p->tok.column}};
    }
    p->ruleset->rule_count = staged_base + staged_count;
    p->ruleset->program_validated = 0;
    p->ruleset->compiled_fingerprint[0] = 0;
    return next(p);

fail_staged_clause:
    clear_staged_rules(p->ruleset, staged_base, staged_capacity);
    if (p->ruleset->filter_pattern_pool_used > filter_pool_base) {
        memset(p->ruleset->filter_pattern_pool + filter_pool_base,
               0,
               p->ruleset->filter_pattern_pool_used - filter_pool_base);
    }
    if (p->ruleset->filter_program_count > filter_program_base) {
        memset(&p->ruleset->filter_programs[filter_program_base],
               0,
               (p->ruleset->filter_program_count - filter_program_base) *
                   sizeof(p->ruleset->filter_programs[0]));
    }
    p->ruleset->filter_pattern_pool_used = filter_pool_base;
    p->ruleset->filter_program_count = filter_program_base;
    return rc;
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
    return maelys_datalog_parse_ruleset_ex_with_flags(
        ruleset, src, len, file_path, 0u, out_diag);
}

maelys_result_t maelys_datalog_parse_ruleset_ex_with_flags(
    maelys_datalog_ruleset_t *ruleset,
    const char *src,
    size_t len,
    const char *file_path,
    unsigned flags,
    maelys_datalog_diagnostic_t *out_diag) {
    maelys_datalog_parse_origin_t origin = {0};
    maelys_result_t rc = maelys_datalog_parse_only(
        ruleset, src, len, file_path, flags, &origin, out_diag);
    if (rc != MAELYS_OK) return rc;
    return maelys_datalog_validate_program(ruleset, file_path, &origin, out_diag);
}

/* Validation runs once after the whole source is parsed, but a clause-local
 * error in an earlier clause must still win over a later parse error, as it
 * did when every clause was validated as soon as it was parsed. */
static maelys_result_t earliest_clause_error(maelys_datalog_ruleset_t *ruleset,
                                             const char *file_path,
                                             const maelys_datalog_parse_origin_t *origin,
                                             maelys_datalog_diagnostic_t *out_diag,
                                             maelys_result_t parse_rc) {
    if (!origin || ruleset->rule_count == 0) return parse_rc;
    maelys_datalog_diagnostic_t prefix;
    maelys_datalog_diagnostic_clear(&prefix);
    maelys_result_t rc = maelys_datalog_validate_parsed_prefix(ruleset, file_path, origin, &prefix);
    if (rc == MAELYS_OK) return parse_rc;
    if (out_diag) *out_diag = prefix;
    return rc;
}

maelys_result_t maelys_datalog_parse_only(
    maelys_datalog_ruleset_t *ruleset, const char *src, size_t len, const char *file_path,
    unsigned flags, maelys_datalog_parse_origin_t *origin, maelys_datalog_diagnostic_t *out_diag) {
    if (!ruleset || !ruleset->loaded) return MAELYS_ERR_INVALID_STATE;
    MAELYS_DATALOG_COUNT_PIPELINE(parses);
    if (flags & ~MAELYS_DATALOG_PARSE_ALLOW_UNDECLARED_POLICY_ATOMS) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
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
    p.flags = flags;
    p.origin = origin;
    maelys_result_t rc = maelys_datalog_lexer_init_ex(&p.lexer, src, len, file_path, out_diag);
    if (rc != MAELYS_OK) return rc;
    rc = next(&p);
    if (rc != MAELYS_OK) return rc;
    while (p.tok.kind != MAELYS_DATALOG_TOKEN_EOF) {
        rc = parse_clause(&p);
        if (rc != MAELYS_OK)
            return earliest_clause_error(ruleset, file_path, origin, out_diag, rc);
    }
    if (origin) origin->eof = (maelys_datalog_source_location_t){p.tok.line, p.tok.column};
    return MAELYS_OK;
}
