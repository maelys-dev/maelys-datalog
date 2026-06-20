#include "src/core/maelys_datalog_ruleset.h"

#include "common/maelys_sha256.h"
#include "src/core/maelys_datalog_symbol_table.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static maelys_result_t canonical_update(maelys_sha256_ctx_t *ctx, const char *text) {
    if (!ctx || !text) return MAELYS_ERR_INVALID_ARGUMENT;
    maelys_sha256_update(ctx, (const unsigned char *)text, strlen(text));
    return MAELYS_OK;
}

static maelys_result_t canonical_printf(maelys_sha256_ctx_t *ctx, const char *fmt, ...) {
    if (!ctx || !fmt) return MAELYS_ERR_INVALID_ARGUMENT;
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0 || (size_t)n >= sizeof(buf)) return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    maelys_sha256_update(ctx, (const unsigned char *)buf, (size_t)n);
    return MAELYS_OK;
}

static const maelys_datalog_predicate_def_t *canonical_predicate_def(
    const maelys_datalog_ruleset_t *ruleset,
    maelys_datalog_predicate_id_t id) {
    if (!ruleset || id >= ruleset->registry.count || id >= MAELYS_DATALOG_MAX_PREDICATES) return NULL;
    return &ruleset->registry.defs[id];
}

static maelys_result_t canonical_stream_term(maelys_sha256_ctx_t *ctx,
                                             const maelys_datalog_ruleset_t *ruleset,
                                             const maelys_datalog_term_t *term) {
    if (!ctx || !ruleset || !term) return MAELYS_ERR_INVALID_ARGUMENT;
    switch (term->kind) {
        case MAELYS_DATALOG_TERM_SYMBOL: {
            const char *text = maelys_datalog_symbol_text(&ruleset->symbols, term->as.symbol);
            if (!text) return MAELYS_ERR_INVALID_STATE;
            return canonical_printf(ctx, "sym:%s", text);
        }
        case MAELYS_DATALOG_TERM_INT:
            return canonical_printf(ctx, "int:%lld", term->as.integer);
        case MAELYS_DATALOG_TERM_BOOL:
            return canonical_printf(ctx, "bool:%d", term->as.boolean ? 1 : 0);
        case MAELYS_DATALOG_TERM_VAR:
            return canonical_printf(ctx, "var:%u", term->as.variable);
        default:
            return MAELYS_ERR_INVALID_STATE;
    }
}

static maelys_result_t canonical_stream_terms(maelys_sha256_ctx_t *ctx,
                                              const maelys_datalog_ruleset_t *ruleset,
                                              const maelys_datalog_term_t *terms,
                                              size_t arity) {
    if (!ctx || !ruleset || (!terms && arity > 0) || arity > MAELYS_DATALOG_MAX_TERMS) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < arity; i++) {
        if (i > 0) {
            maelys_result_t rc = canonical_update(ctx, ",");
            if (rc != MAELYS_OK) return rc;
        }
        maelys_result_t rc = canonical_stream_term(ctx, ruleset, &terms[i]);
        if (rc != MAELYS_OK) return rc;
    }
    return MAELYS_OK;
}

static maelys_result_t canonical_stream_atom(maelys_sha256_ctx_t *ctx,
                                             const maelys_datalog_ruleset_t *ruleset,
                                             const char *prefix,
                                             const maelys_datalog_fact_t *atom) {
    if (!ctx || !ruleset || !prefix || !atom || atom->arity > MAELYS_DATALOG_MAX_TERMS) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    const maelys_datalog_predicate_def_t *def = canonical_predicate_def(ruleset, atom->predicate_id);
    if (!def) return MAELYS_ERR_INVALID_STATE;
    maelys_result_t rc = canonical_printf(ctx, "%s%s/%u(", prefix, def->name, (unsigned)atom->arity);
    if (rc != MAELYS_OK) return rc;
    rc = canonical_stream_terms(ctx, ruleset, atom->terms, atom->arity);
    if (rc != MAELYS_OK) return rc;
    return canonical_update(ctx, ")\n");
}

static maelys_result_t canonical_stream_literal(maelys_sha256_ctx_t *ctx,
                                                const maelys_datalog_ruleset_t *ruleset,
                                                const maelys_datalog_rule_t *rule,
                                                const maelys_datalog_literal_t *literal) {
    if (!ctx || !ruleset || !rule || !literal) return MAELYS_ERR_INVALID_ARGUMENT;
    switch (literal->kind) {
        case MAELYS_DATALOG_LITERAL_ATOM:
            return canonical_stream_atom(ctx, ruleset, "body.atom=", &literal->atom);
        case MAELYS_DATALOG_LITERAL_NEGATED_ATOM:
            return canonical_stream_atom(ctx, ruleset, "body.not=", &literal->atom);
        case MAELYS_DATALOG_LITERAL_COMPARISON: {
            maelys_result_t rc = canonical_printf(ctx, "body.cmp=%u:", (unsigned)literal->op);
            if (rc != MAELYS_OK) return rc;
            if (literal->has_arith_expr) {
                rc = canonical_printf(ctx,
                                      "expr:%u:%u:nodes=%u:",
                                      (unsigned)literal->lhs_expr_root,
                                      (unsigned)literal->rhs_expr_root,
                                      (unsigned)rule->expr_node_count);
                if (rc != MAELYS_OK) return rc;
                for (uint8_t i = 0; i < rule->expr_node_count; i++) {
                    const maelys_datalog_arith_expr_node_t *node = &rule->expr_nodes[i];
                    rc = canonical_printf(ctx,
                                          "[%u:%u:%u:%u:",
                                          (unsigned)i,
                                          (unsigned)node->kind,
                                          (unsigned)node->left,
                                          (unsigned)node->right);
                    if (rc != MAELYS_OK) return rc;
                    if (node->kind == MAELYS_DATALOG_ARITH_EXPR_INT_LITERAL ||
                        node->kind == MAELYS_DATALOG_ARITH_EXPR_VAR) {
                        rc = canonical_stream_term(ctx, ruleset, &node->term);
                        if (rc != MAELYS_OK) return rc;
                    }
                    rc = canonical_update(ctx, "]");
                    if (rc != MAELYS_OK) return rc;
                }
            } else {
                rc = canonical_stream_term(ctx, ruleset, &literal->lhs);
                if (rc != MAELYS_OK) return rc;
                rc = canonical_update(ctx, ",");
                if (rc != MAELYS_OK) return rc;
                rc = canonical_stream_term(ctx, ruleset, &literal->rhs);
                if (rc != MAELYS_OK) return rc;
            }
            return canonical_update(ctx, "\n");
        }
        default:
            return MAELYS_ERR_INVALID_STATE;
    }
}

static maelys_result_t ruleset_stream_canonical(maelys_sha256_ctx_t *ctx,
                                                const maelys_datalog_ruleset_t *ruleset) {
    if (!ctx || !ruleset || !ruleset->loaded) return MAELYS_ERR_INVALID_ARGUMENT;
    maelys_result_t rc = canonical_printf(ctx, "policy_id=%s\n", ruleset->policy_id);
    if (rc != MAELYS_OK) return rc;
    rc = canonical_printf(ctx, "domain=%s\n", ruleset->domain);
    if (rc != MAELYS_OK) return rc;
    rc = canonical_printf(ctx, "profile=%s\n", MAELYS_DATALOG_PROFILE_NAME);
    if (rc != MAELYS_OK) return rc;

    for (size_t i = 0; i < ruleset->registry.count; i++) {
        const maelys_datalog_predicate_def_t *def = &ruleset->registry.defs[i];
        rc = canonical_printf(ctx, "pred=%s/%zu/%u\n", def->name, def->arity, def->kind_flags);
        if (rc != MAELYS_OK) return rc;
    }
    for (size_t i = 0; i < ruleset->fact_count; i++) {
        rc = canonical_stream_atom(ctx, ruleset, "fact=", &ruleset->facts[i]);
        if (rc != MAELYS_OK) return rc;
    }
    for (size_t i = 0; i < ruleset->rule_count; i++) {
        const maelys_datalog_rule_t *rule = &ruleset->rules[i];
        rc = canonical_printf(ctx, "rule=%zu\n", rule->rule_id);
        if (rc != MAELYS_OK) return rc;
        rc = canonical_stream_atom(ctx, ruleset, "head=", &rule->head);
        if (rc != MAELYS_OK) return rc;
        for (size_t j = 0; j < rule->body_count; j++) {
            rc = canonical_stream_literal(ctx, ruleset, rule, &rule->body[j]);
            if (rc != MAELYS_OK) return rc;
        }
    }
    for (size_t i = 0; i < ruleset->registry.count; i++) {
        const maelys_datalog_predicate_def_t *def = &ruleset->registry.defs[i];
        if ((def->kind_flags & MAELYS_DATALOG_PRED_KIND_QUERY) &&
            (def->kind_flags & MAELYS_DATALOG_PRED_KIND_IDB)) {
            rc = canonical_printf(ctx, "query=%s/%zu\n", def->name, def->arity);
            if (rc != MAELYS_OK) return rc;
        }
    }
    return MAELYS_OK;
}

maelys_result_t maelys_datalog_ruleset_finalize_sha256(maelys_datalog_ruleset_t *ruleset) {
    if (!ruleset) return MAELYS_ERR_INVALID_ARGUMENT;
    if (maelys_sha256_hex_is_lowercase(ruleset->sha256)) return MAELYS_OK;

    maelys_sha256_ctx_t ctx;
    maelys_sha256_init(&ctx);
    maelys_result_t rc = ruleset_stream_canonical(&ctx, ruleset);
    if (rc != MAELYS_OK) return rc;

    unsigned char digest[MAELYS_SHA256_DIGEST_BYTES];
    maelys_sha256_final(&ctx, digest);
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < MAELYS_SHA256_DIGEST_BYTES; i++) {
        ruleset->sha256[2u * i] = hex[(digest[i] >> 4) & 0x0fu];
        ruleset->sha256[2u * i + 1u] = hex[digest[i] & 0x0fu];
    }
    ruleset->sha256[MAELYS_SHA256_HEX_BYTES] = '\0';
    return MAELYS_OK;
}
