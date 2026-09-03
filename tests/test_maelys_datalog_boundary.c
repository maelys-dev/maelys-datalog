#include "src/core/maelys_datalog_edb.h"
#include "src/core/maelys_datalog_lexer.h"
#include "src/core/maelys_datalog_parser.h"
#include "src/core/maelys_datalog_predicate_registry.h"
#include "src/core/maelys_datalog_solver.h"
#include "src/core/maelys_datalog_symbol_table.h"
#include "tests/helpers/test_framework.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static maelys_result_t init_ruleset(maelys_datalog_ruleset_t *r) {
    memset(r, 0, sizeof(*r));
    return maelys_datalog_ruleset_init(r,
                                       "boundary.policy",
                                       "boundary",
                                       "0000000000000000000000000000000000000000000000000000000000000000",
                                       1);
}

static size_t bounded_strlen(const char *text, size_t cap) {
    size_t len = 0;
    while (len < cap && text[len] != '\0') len++;
    return len;
}

static int appendf(char *buf, size_t cap, size_t *len, const char *fmt, ...) {
    if (!buf || !len || *len >= cap) return 0;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *len, cap - *len, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= cap - *len) return 0;
    *len += (size_t)n;
    return 1;
}

static void pred_name(char *buf, size_t cap, const char *prefix, size_t index) {
    snprintf(buf, cap, "%s%03zu", prefix, index);
}

static maelys_datalog_term_t int_term(long long value) {
    maelys_datalog_term_t term;
    memset(&term, 0, sizeof(term));
    term.kind = MAELYS_DATALOG_TERM_INT;
    term.as.integer = value;
    return term;
}

static maelys_result_t add_pred(maelys_datalog_ruleset_t *r,
                                const char *name,
                                size_t arity,
                                unsigned kind) {
    return maelys_datalog_predicate_registry_add_domain(&r->registry, name, arity, kind);
}

static maelys_result_t add_edb_preds(maelys_datalog_ruleset_t *r,
                                     const char *prefix,
                                     size_t count) {
    char name[64];
    for (size_t i = 0; i < count; i++) {
        pred_name(name, sizeof(name), prefix, i);
        maelys_result_t rc = add_pred(r, name, 1u, MAELYS_DATALOG_PRED_KIND_EDB);
        if (rc != MAELYS_OK) return rc;
    }
    return MAELYS_OK;
}

static int init_parse_fixture(maelys_datalog_ruleset_t *r,
                              size_t edb_count,
                              size_t idb_count) {
    maelys_result_t rc = init_ruleset(r);
    if (rc != MAELYS_OK) return rc;
    rc = add_pred(r, "seed", 1u, MAELYS_DATALOG_PRED_KIND_EDB);
    if (rc != MAELYS_OK) return rc;
    rc = add_pred(r, "p", 1u, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY);
    if (rc != MAELYS_OK) return rc;
    if (edb_count > 0u) {
        rc = add_edb_preds(r, "e", edb_count);
        if (rc != MAELYS_OK) return rc;
    }
    char name[64];
    for (size_t i = 0; i < idb_count; i++) {
        pred_name(name, sizeof(name), "idb", i);
        rc = add_pred(r, name, 1u, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY);
        if (rc != MAELYS_OK) return rc;
    }
    return maelys_datalog_predicate_registry_freeze(&r->registry);
}

static int test_boundary_max_rules_at_limit(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_parse_fixture(&r, 0u, 0u), "%d");
    char src[8192];
    size_t len = 0;
    for (size_t i = 0; i < MAELYS_DATALOG_MAX_RULES; i++) {
        TEST_ASSERT_TRUE(appendf(src, sizeof(src), &len, "p(X) :- seed(X).\n"));
    }
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_parse_ruleset(&r, src, len), "%d");
    TEST_ASSERT_EQUAL((size_t)MAELYS_DATALOG_MAX_RULES, r.rule_count, "%zu");
    maelys_datalog_ruleset_clear(&r);
    TEST_END();
}

static int test_boundary_max_rules_over_limit(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_parse_fixture(&r, 0u, 0u), "%d");
    char src[8192];
    size_t len = 0;
    for (size_t i = 0; i < MAELYS_DATALOG_MAX_RULES + 1u; i++) {
        TEST_ASSERT_TRUE(appendf(src, sizeof(src), &len, "p(X) :- seed(X).\n"));
    }
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE, maelys_datalog_parse_ruleset(&r, src, len), "%d");
    TEST_ASSERT_EQUAL((size_t)MAELYS_DATALOG_MAX_RULES, r.rule_count, "%zu");
    maelys_datalog_ruleset_clear(&r);
    TEST_ASSERT_EQUAL(MAELYS_OK, init_parse_fixture(&r, 0u, 0u), "%d");
    TEST_ASSERT_EQUAL((size_t)0u, r.rule_count, "%zu");
    maelys_datalog_ruleset_clear(&r);
    TEST_END();
}

static int test_boundary_or_expansion_at_rule_limit(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_parse_fixture(&r, 0u, 0u), "%d");
    char src[4096];
    size_t len = 0;
    TEST_ASSERT_TRUE(appendf(src, sizeof(src), &len, "p(X) :- "));
    for (size_t i = 0; i < MAELYS_DATALOG_MAX_RULES; i++) {
        int ok = appendf(src,
                         sizeof(src),
                         &len,
                         "%sseed(X)",
                         i == 0u ? "" : " or ");
        TEST_ASSERT_TRUE(ok);
    }
    TEST_ASSERT_TRUE(appendf(src, sizeof(src), &len, ".\n"));
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_parse_ruleset(&r, src, len),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)MAELYS_DATALOG_MAX_RULES,
                      r.rule_count,
                      "%zu");
    for (size_t i = 0; i < r.rule_count; i++) {
        TEST_ASSERT_EQUAL(i + 1u, r.rules[i].rule_id, "%zu");
    }
    maelys_datalog_ruleset_clear(&r);
    TEST_END();
}

static int test_boundary_or_expansion_over_limit_is_clause_atomic(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_parse_fixture(&r, 0u, 0u), "%d");
    char src[4096];
    size_t len = 0;
    TEST_ASSERT_TRUE(appendf(src,
                             sizeof(src),
                             &len,
                             "p(X) :- seed(X).\np(X) :- "));
    for (size_t i = 0; i < MAELYS_DATALOG_MAX_RULES; i++) {
        int ok = appendf(src,
                         sizeof(src),
                         &len,
                         "%sseed(X)",
                         i == 0u ? "" : " or ");
        TEST_ASSERT_TRUE(ok);
    }
    TEST_ASSERT_TRUE(appendf(src, sizeof(src), &len, ".\n"));
    TEST_ASSERT_EQUAL(
        MAELYS_ERR_PAYLOAD_TOO_LARGE,
        maelys_datalog_parse_ruleset_ex(&r,
                                        src,
                                        len,
                                        "boundary_or.dl",
                                        &diag),
        "%d");
    TEST_ASSERT_EQUAL((size_t)1u, r.rule_count, "%zu");
    TEST_ASSERT_EQUAL((size_t)1u, r.rules[0].rule_id, "%zu");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_PARSER_RULE_BODY_LITERAL_OVERFLOW,
                      diag.code,
                      "%d");
    TEST_ASSERT_EQUAL((size_t)(MAELYS_DATALOG_MAX_RULES + 1u),
                      diag.count,
                      "%zu");
    TEST_ASSERT_EQUAL((size_t)MAELYS_DATALOG_MAX_RULES, diag.limit, "%zu");
    maelys_datalog_ruleset_clear(&r);
    TEST_END();
}

static int test_boundary_max_body_literals_at_limit(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_parse_fixture(&r, MAELYS_DATALOG_MAX_BODY_LITERALS, 0u), "%d");
    char src[512];
    size_t len = 0;
    TEST_ASSERT_TRUE(appendf(src, sizeof(src), &len, "p(X) :- "));
    for (size_t i = 0; i < MAELYS_DATALOG_MAX_BODY_LITERALS; i++) {
        int ok = appendf(src, sizeof(src), &len, "%se%03zu(X)", i ? ", " : "", i);
        TEST_ASSERT_TRUE(ok);
    }
    TEST_ASSERT_TRUE(appendf(src, sizeof(src), &len, ".\n"));
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_parse_ruleset(&r, src, len), "%d");
    TEST_ASSERT_EQUAL((size_t)1u, r.rule_count, "%zu");
    TEST_ASSERT_EQUAL((size_t)MAELYS_DATALOG_MAX_BODY_LITERALS, r.rules[0].body_count, "%zu");
    maelys_datalog_ruleset_clear(&r);
    TEST_END();
}

static int test_boundary_max_body_literals_over_limit(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_parse_fixture(&r, MAELYS_DATALOG_MAX_BODY_LITERALS + 1u, 0u), "%d");
    char src[512];
    size_t len = 0;
    TEST_ASSERT_TRUE(appendf(src, sizeof(src), &len, "p(X) :- "));
    for (size_t i = 0; i < MAELYS_DATALOG_MAX_BODY_LITERALS + 1u; i++) {
        int ok = appendf(src, sizeof(src), &len, "%se%03zu(X)", i ? ", " : "", i);
        TEST_ASSERT_TRUE(ok);
    }
    TEST_ASSERT_TRUE(appendf(src, sizeof(src), &len, ".\n"));
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE,
                      maelys_datalog_parse_ruleset_ex(&r, src, len, "boundary_body.dl", &diag),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_PARSER_RULE_BODY_LITERAL_OVERFLOW, diag.code, "%d");
    TEST_ASSERT_EQUAL((size_t)(MAELYS_DATALOG_MAX_BODY_LITERALS + 1u), diag.count, "%zu");
    TEST_ASSERT_EQUAL((size_t)MAELYS_DATALOG_MAX_BODY_LITERALS, diag.limit, "%zu");
    TEST_ASSERT_EQUAL((size_t)0u, r.rule_count, "%zu");
    maelys_datalog_ruleset_clear(&r);
    TEST_END();
}

static int test_boundary_named_variables_at_limit(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_TRUE(MAELYS_DATALOG_NAMED_VARIABLE_COUNT < MAELYS_DATALOG_MAX_RULE_VARIABLES);
    TEST_ASSERT_EQUAL(MAELYS_OK, init_ruleset(&r), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, add_pred(&r, "quad", 4u, MAELYS_DATALOG_PRED_KIND_EDB), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, add_pred(&r, "p", 1u, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_predicate_registry_freeze(&r.registry), "%d");
    const char *src =
        "p(A) :- quad(A,B,C,D), quad(E,F,G,H), quad(I,J,K,L), quad(M,N,O,P), "
        "quad(Q,R,S,T), quad(U,V,W,X), quad(Y,Z,A,B).\n";
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_parse_ruleset(&r, src, strlen(src)), "%d");
    TEST_ASSERT_EQUAL((size_t)1u, r.rule_count, "%zu");
    maelys_datalog_ruleset_clear(&r);
    TEST_END();
}

static int test_boundary_max_rule_variables_unreachable_for_named_vars(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL((size_t)26u, (size_t)MAELYS_DATALOG_NAMED_VARIABLE_COUNT, "%zu");
    TEST_ASSERT_EQUAL((size_t)32u, (size_t)MAELYS_DATALOG_MAX_RULE_VARIABLES, "%zu");
    TEST_ASSERT_TRUE(MAELYS_DATALOG_NAMED_VARIABLE_COUNT < MAELYS_DATALOG_MAX_RULE_VARIABLES);
    TEST_END();
}

static int test_boundary_max_predicates_at_limit(void) {
    TEST_BEGIN();
    maelys_datalog_predicate_registry_t registry;
    maelys_datalog_predicate_registry_init(&registry);
    char name[64];
    for (size_t i = 0; i < MAELYS_DATALOG_MAX_PREDICATES; i++) {
        pred_name(name, sizeof(name), "pred", i);
        TEST_ASSERT_EQUAL(MAELYS_OK,
                          maelys_datalog_predicate_registry_add_domain(&registry,
                                                                       name,
                                                                       1u,
                                                                       MAELYS_DATALOG_PRED_KIND_EDB),
                          "%d");
    }
    TEST_ASSERT_EQUAL((size_t)MAELYS_DATALOG_MAX_PREDICATES, registry.count, "%zu");
    for (size_t i = 0; i < MAELYS_DATALOG_MAX_PREDICATES; i++) {
        maelys_datalog_predicate_id_t id = 0;
        pred_name(name, sizeof(name), "pred", i);
        TEST_ASSERT_TRUE(maelys_datalog_predicate_registry_find(&registry, name, 1u, &id));
        TEST_ASSERT_EQUAL((unsigned)i, (unsigned)id, "%u");
    }
    TEST_END();
}

static int test_boundary_max_predicates_over_limit(void) {
    TEST_BEGIN();
    maelys_datalog_predicate_registry_t registry;
    maelys_datalog_predicate_registry_init(&registry);
    char name[64];
    for (size_t i = 0; i < MAELYS_DATALOG_MAX_PREDICATES; i++) {
        pred_name(name, sizeof(name), "pred", i);
        TEST_ASSERT_EQUAL(MAELYS_OK,
                          maelys_datalog_predicate_registry_add_domain(&registry,
                                                                       name,
                                                                       1u,
                                                                       MAELYS_DATALOG_PRED_KIND_EDB),
                          "%d");
    }
    pred_name(name, sizeof(name), "pred", MAELYS_DATALOG_MAX_PREDICATES);
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE,
                      maelys_datalog_predicate_registry_add_domain(&registry,
                                                                   name,
                                                                   1u,
                                                                   MAELYS_DATALOG_PRED_KIND_EDB),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)MAELYS_DATALOG_MAX_PREDICATES, registry.count, "%zu");
    TEST_END();
}

static int test_boundary_max_symbols_at_limit(void) {
    TEST_BEGIN();
    maelys_datalog_symbol_table_t symbols;
    maelys_datalog_symbol_table_init(&symbols);
    char text[32];
    for (size_t i = 0; i < MAELYS_DATALOG_MAX_SYMBOLS; i++) {
        snprintf(text, sizeof(text), "sym_%03zu", i);
        maelys_datalog_symbol_id_t id = 0;
        TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_symbol_intern(&symbols, text, strlen(text), &id), "%d");
        TEST_ASSERT_EQUAL((unsigned)(i + 1u), (unsigned)id, "%u");
    }
    TEST_ASSERT_EQUAL((size_t)MAELYS_DATALOG_MAX_SYMBOLS, symbols.count, "%zu");
    TEST_END();
}

static int test_boundary_max_symbols_over_limit(void) {
    TEST_BEGIN();
    maelys_datalog_symbol_table_t symbols;
    maelys_datalog_symbol_table_init(&symbols);
    char text[32];
    for (size_t i = 0; i < MAELYS_DATALOG_MAX_SYMBOLS; i++) {
        snprintf(text, sizeof(text), "sym_%03zu", i);
        maelys_datalog_symbol_id_t id = 0;
        TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_symbol_intern(&symbols, text, strlen(text), &id), "%d");
    }
    size_t used_before = symbols.used;
    maelys_datalog_symbol_id_t id = 0;
    snprintf(text, sizeof(text), "sym_%03zu", (size_t)MAELYS_DATALOG_MAX_SYMBOLS);
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE,
                      maelys_datalog_symbol_intern(&symbols, text, strlen(text), &id),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)MAELYS_DATALOG_MAX_SYMBOLS, symbols.count, "%zu");
    TEST_ASSERT_EQUAL(used_before, symbols.used, "%zu");
    TEST_END();
}

static int init_edb_registry(maelys_datalog_ruleset_t *r, size_t edb_pred_count) {
    maelys_result_t rc = init_ruleset(r);
    if (rc != MAELYS_OK) return rc;
    rc = add_edb_preds(r, "e", edb_pred_count);
    if (rc != MAELYS_OK) return rc;
    return maelys_datalog_predicate_registry_freeze(&r->registry);
}

static int test_boundary_max_edb_facts_at_limit(void) {
    TEST_BEGIN();
    const size_t pred_count = MAELYS_DATALOG_MAX_EDB_FACTS / MAELYS_DATALOG_MAX_FACTS_PER_PRED;
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_edb_registry(&r, pred_count), "%d");
    maelys_datalog_fact_t pool[MAELYS_DATALOG_MAX_EDB_FACTS];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, pool, MAELYS_DATALOG_MAX_EDB_FACTS, &r.symbols, &r.registry), "%d");
    char name[64];
    for (size_t p = 0; p < pred_count; p++) {
        pred_name(name, sizeof(name), "e", p);
        for (size_t i = 0; i < MAELYS_DATALOG_MAX_FACTS_PER_PRED; i++) {
            maelys_datalog_term_t term = int_term((long long)i);
            TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_add_fact(&edb, name, &term, 1u), "%d");
        }
    }
    TEST_ASSERT_EQUAL((size_t)MAELYS_DATALOG_MAX_EDB_FACTS, edb.fact_count, "%zu");
    TEST_ASSERT_EQUAL((size_t)0u, r.symbols.count, "%zu");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    TEST_ASSERT_EQUAL((size_t)MAELYS_DATALOG_MAX_EDB_FACTS, edb.fact_count, "%zu");
    maelys_datalog_ruleset_clear(&r);
    TEST_END();
}

static int test_boundary_max_edb_facts_over_limit(void) {
    TEST_BEGIN();
    const size_t pred_count = MAELYS_DATALOG_MAX_EDB_FACTS / MAELYS_DATALOG_MAX_FACTS_PER_PRED;
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_edb_registry(&r, pred_count + 1u), "%d");
    maelys_datalog_fact_t pool[MAELYS_DATALOG_MAX_EDB_FACTS];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, pool, MAELYS_DATALOG_MAX_EDB_FACTS, &r.symbols, &r.registry), "%d");
    char name[64];
    for (size_t p = 0; p < pred_count; p++) {
        pred_name(name, sizeof(name), "e", p);
        for (size_t i = 0; i < MAELYS_DATALOG_MAX_FACTS_PER_PRED; i++) {
            maelys_datalog_term_t term = int_term((long long)i);
            TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_add_fact(&edb, name, &term, 1u), "%d");
        }
    }
    size_t before = edb.fact_count;
    pred_name(name, sizeof(name), "e", pred_count);
    maelys_datalog_term_t term = int_term(1000);
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE, maelys_datalog_edb_add_fact(&edb, name, &term, 1u), "%d");
    TEST_ASSERT_EQUAL(before, edb.fact_count, "%zu");
    TEST_ASSERT_EQUAL((size_t)0u, r.symbols.count, "%zu");
    maelys_datalog_edb_clear(&edb);
    TEST_ASSERT_EQUAL((size_t)0u, edb.fact_count, "%zu");
    maelys_datalog_ruleset_clear(&r);
    TEST_END();
}

static int test_boundary_max_facts_per_pred_at_limit(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_edb_registry(&r, 1u), "%d");
    maelys_datalog_fact_t pool[MAELYS_DATALOG_MAX_FACTS_PER_PRED + 1u];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, pool, sizeof(pool) / sizeof(pool[0]), &r.symbols, &r.registry), "%d");
    for (size_t i = 0; i < MAELYS_DATALOG_MAX_FACTS_PER_PRED; i++) {
        maelys_datalog_term_t term = int_term((long long)i);
        TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_add_fact(&edb, "e000", &term, 1u), "%d");
    }
    TEST_ASSERT_EQUAL((size_t)MAELYS_DATALOG_MAX_FACTS_PER_PRED, edb.fact_count, "%zu");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    TEST_ASSERT_EQUAL((size_t)MAELYS_DATALOG_MAX_FACTS_PER_PRED, edb.fact_count, "%zu");
    maelys_datalog_ruleset_clear(&r);
    TEST_END();
}

static int test_boundary_max_facts_per_pred_over_limit(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_edb_registry(&r, 1u), "%d");
    maelys_datalog_fact_t pool[MAELYS_DATALOG_MAX_FACTS_PER_PRED + 1u];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, pool, sizeof(pool) / sizeof(pool[0]), &r.symbols, &r.registry), "%d");
    for (size_t i = 0; i < MAELYS_DATALOG_MAX_FACTS_PER_PRED; i++) {
        maelys_datalog_term_t term = int_term((long long)i);
        TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_add_fact(&edb, "e000", &term, 1u), "%d");
    }
    size_t before = edb.fact_count;
    maelys_datalog_term_t term = int_term(1000);
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE, maelys_datalog_edb_add_fact(&edb, "e000", &term, 1u), "%d");
    TEST_ASSERT_EQUAL(before, edb.fact_count, "%zu");
    maelys_datalog_edb_clear(&edb);
    TEST_ASSERT_EQUAL((size_t)0u, edb.fact_count, "%zu");
    maelys_datalog_ruleset_clear(&r);
    TEST_END();
}

static int test_boundary_max_arity_at_limit(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_ruleset(&r), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, add_pred(&r, "wide", MAELYS_DATALOG_MAX_ARITY, MAELYS_DATALOG_PRED_KIND_EDB), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_predicate_registry_freeze(&r.registry), "%d");
    maelys_datalog_fact_t pool[2];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, pool, 2u, &r.symbols, &r.registry), "%d");
    maelys_datalog_term_t terms[MAELYS_DATALOG_MAX_ARITY];
    for (size_t i = 0; i < MAELYS_DATALOG_MAX_ARITY; i++) terms[i] = int_term((long long)i);
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_add_fact(&edb, "wide", terms, MAELYS_DATALOG_MAX_ARITY), "%d");
    TEST_ASSERT_EQUAL((size_t)1u, edb.fact_count, "%zu");
    maelys_datalog_ruleset_clear(&r);
    TEST_END();
}

static int test_boundary_max_arity_over_limit(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_ruleset(&r), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, add_pred(&r, "wide", MAELYS_DATALOG_MAX_ARITY, MAELYS_DATALOG_PRED_KIND_EDB), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_predicate_registry_freeze(&r.registry), "%d");
    maelys_datalog_fact_t pool[2];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, pool, 2u, &r.symbols, &r.registry), "%d");
    maelys_datalog_term_t terms[MAELYS_DATALOG_MAX_ARITY + 1u];
    for (size_t i = 0; i < MAELYS_DATALOG_MAX_ARITY + 1u; i++) terms[i] = int_term((long long)i);
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_edb_add_fact(&edb, "wide", terms, MAELYS_DATALOG_MAX_ARITY + 1u),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, edb.fact_count, "%zu");
    maelys_datalog_ruleset_clear(&r);
    TEST_END();
}

static char *quoted_string_source(size_t payload_len, size_t *out_len) {
    char *src = malloc(payload_len + 3u);
    if (!src) return NULL;
    src[0] = '"';
    memset(src + 1u, 'a', payload_len);
    src[payload_len + 1u] = '"';
    src[payload_len + 2u] = '\0';
    if (out_len) *out_len = payload_len + 2u;
    return src;
}

static int test_boundary_max_string_bytes_below(void) {
    TEST_BEGIN();
    size_t len = 0;
    char *src = quoted_string_source(MAELYS_DATALOG_MAX_STRING_BYTES - 1u, &len);
    TEST_ASSERT_NOT_NULL(src);
    if (src) {
        TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_lexer_validate_ex(src, len, "boundary_string.dl", NULL), "%d");
        free(src);
    }
    TEST_END();
}

static int test_boundary_max_string_bytes_exact(void) {
    TEST_BEGIN();
    size_t len = 0;
    char *src = quoted_string_source(MAELYS_DATALOG_MAX_STRING_BYTES, &len);
    TEST_ASSERT_NOT_NULL(src);
    if (src) {
        TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_lexer_validate_ex(src, len, "boundary_string.dl", NULL), "%d");
        free(src);
    }
    TEST_END();
}

static int test_boundary_max_string_bytes_over(void) {
    TEST_BEGIN();
    size_t len = 0;
    maelys_datalog_diagnostic_t diag;
    char *src = quoted_string_source(MAELYS_DATALOG_MAX_STRING_BYTES + 1u, &len);
    TEST_ASSERT_NOT_NULL(src);
    if (src) {
        TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE,
                          maelys_datalog_lexer_validate_ex(src, len, "boundary_string.dl", &diag),
                          "%d");
        TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_LEXER_STRING_TOO_LONG, diag.code, "%d");
        TEST_ASSERT_EQUAL((size_t)(MAELYS_DATALOG_MAX_STRING_BYTES + 1u), diag.count, "%zu");
        TEST_ASSERT_EQUAL((size_t)MAELYS_DATALOG_MAX_STRING_BYTES, diag.limit, "%zu");
        free(src);
    }
    TEST_END();
}

static int test_boundary_idb_overflow_fails_closed(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_ruleset(&r), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, add_pred(&r, "seed", 1u, MAELYS_DATALOG_PRED_KIND_EDB), "%d");
    char name[64];
    for (size_t i = 0; i < 17u; i++) {
        pred_name(name, sizeof(name), "idb", i);
        TEST_ASSERT_EQUAL(MAELYS_OK, add_pred(&r, name, 1u, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY), "%d");
    }
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_predicate_registry_freeze(&r.registry), "%d");
    char src[1024];
    size_t len = 0;
    for (size_t i = 0; i < 17u; i++) {
        int ok = appendf(src, sizeof(src), &len, "idb%03zu(X) :- seed(X).\n", i);
        TEST_ASSERT_TRUE(ok);
    }
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_parse_ruleset(&r, src, len), "%d");
    maelys_datalog_fact_t pool[MAELYS_DATALOG_MAX_FACTS_PER_PRED];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, pool, MAELYS_DATALOG_MAX_FACTS_PER_PRED, &r.symbols, &r.registry), "%d");
    for (size_t i = 0; i < MAELYS_DATALOG_MAX_FACTS_PER_PRED; i++) {
        maelys_datalog_term_t term = int_term((long long)i);
        TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_add_fact(&edb, "seed", &term, 1u), "%d");
    }
    TEST_ASSERT_EQUAL((size_t)MAELYS_DATALOG_MAX_FACTS_PER_PRED, edb.fact_count, "%zu");
    TEST_ASSERT_EQUAL((size_t)0u, r.symbols.count, "%zu");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    maelys_datalog_solve_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE,
                      maelys_datalog_solve_once_ex(&r, &edb, &result, &diag),
                      "%d");
    TEST_ASSERT_NULL(result);
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_SOLVE_DIAG_IDB_OVERFLOW, diag.category, "%d");
    TEST_ASSERT_EQUAL((uint16_t)MAELYS_DATALOG_MAX_IDB_FACTS, diag.capacity, "%u");
    maelys_datalog_ruleset_clear(&r);
    TEST_END();
}

static int test_boundary_diagnostic_fields_stable(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_ruleset(&r), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_predicate_registry_freeze(&r.registry), "%d");
    char pred[96];
    memset(pred, 'p', sizeof(pred) - 1u);
    pred[sizeof(pred) - 1u] = '\0';
    char src[128];
    int n = snprintf(src, sizeof(src), "%s(X).", pred);
    TEST_ASSERT_TRUE(n > 0 && (size_t)n < sizeof(src));
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_parse_ruleset_ex(&r, src, strlen(src), "boundary_diag.dl", &diag),
                      "%d");
    TEST_ASSERT_TRUE(diag.code != MAELYS_DATALOG_DIAG_NONE);
    TEST_ASSERT_TRUE(bounded_strlen(diag.token, sizeof(diag.token)) < sizeof(diag.token));
    TEST_ASSERT_TRUE(bounded_strlen(diag.predicate, sizeof(diag.predicate)) < sizeof(diag.predicate));
    TEST_ASSERT_TRUE(bounded_strlen(diag.message, sizeof(diag.message)) < sizeof(diag.message));
    TEST_ASSERT_TRUE(bounded_strlen(diag.hint, sizeof(diag.hint)) < sizeof(diag.hint));
    TEST_ASSERT_EQUAL((size_t)95u, strlen(diag.token), "%zu");
    maelys_datalog_ruleset_clear(&r);
    TEST_END();
}

int main(int argc, char **argv) {
    tests_init_logging();
    const test_case_t cases[] = {
        {"maelys_datalog_boundary/max_rules_at_limit", TEST_MODE_NON_BLOCKING, test_boundary_max_rules_at_limit},
        {"maelys_datalog_boundary/max_rules_over_limit", TEST_MODE_NON_BLOCKING, test_boundary_max_rules_over_limit},
        {"maelys_datalog_boundary/or_expansion_at_rule_limit", TEST_MODE_NON_BLOCKING, test_boundary_or_expansion_at_rule_limit},
        {"maelys_datalog_boundary/or_expansion_over_limit_is_clause_atomic", TEST_MODE_NON_BLOCKING, test_boundary_or_expansion_over_limit_is_clause_atomic},
        {"maelys_datalog_boundary/max_body_literals_at_limit", TEST_MODE_NON_BLOCKING, test_boundary_max_body_literals_at_limit},
        {"maelys_datalog_boundary/max_body_literals_over_limit", TEST_MODE_NON_BLOCKING, test_boundary_max_body_literals_over_limit},
        {"maelys_datalog_boundary/named_variables_at_limit", TEST_MODE_NON_BLOCKING, test_boundary_named_variables_at_limit},
        {"maelys_datalog_boundary/max_rule_variables_unreachable_for_named_vars", TEST_MODE_NON_BLOCKING, test_boundary_max_rule_variables_unreachable_for_named_vars},
        {"maelys_datalog_boundary/max_predicates_at_limit", TEST_MODE_NON_BLOCKING, test_boundary_max_predicates_at_limit},
        {"maelys_datalog_boundary/max_predicates_over_limit", TEST_MODE_NON_BLOCKING, test_boundary_max_predicates_over_limit},
        {"maelys_datalog_boundary/max_symbols_at_limit", TEST_MODE_NON_BLOCKING, test_boundary_max_symbols_at_limit},
        {"maelys_datalog_boundary/max_symbols_over_limit", TEST_MODE_NON_BLOCKING, test_boundary_max_symbols_over_limit},
        {"maelys_datalog_boundary/max_edb_facts_at_limit", TEST_MODE_NON_BLOCKING, test_boundary_max_edb_facts_at_limit},
        {"maelys_datalog_boundary/max_edb_facts_over_limit", TEST_MODE_NON_BLOCKING, test_boundary_max_edb_facts_over_limit},
        {"maelys_datalog_boundary/max_facts_per_pred_at_limit", TEST_MODE_NON_BLOCKING, test_boundary_max_facts_per_pred_at_limit},
        {"maelys_datalog_boundary/max_facts_per_pred_over_limit", TEST_MODE_NON_BLOCKING, test_boundary_max_facts_per_pred_over_limit},
        {"maelys_datalog_boundary/max_arity_at_limit", TEST_MODE_NON_BLOCKING, test_boundary_max_arity_at_limit},
        {"maelys_datalog_boundary/max_arity_over_limit", TEST_MODE_NON_BLOCKING, test_boundary_max_arity_over_limit},
        {"maelys_datalog_boundary/max_string_bytes_below", TEST_MODE_NON_BLOCKING, test_boundary_max_string_bytes_below},
        {"maelys_datalog_boundary/max_string_bytes_exact", TEST_MODE_NON_BLOCKING, test_boundary_max_string_bytes_exact},
        {"maelys_datalog_boundary/max_string_bytes_over", TEST_MODE_NON_BLOCKING, test_boundary_max_string_bytes_over},
        {"maelys_datalog_boundary/idb_overflow_fails_closed", TEST_MODE_NON_BLOCKING, test_boundary_idb_overflow_fails_closed},
        {"maelys_datalog_boundary/diagnostic_fields_stable", TEST_MODE_NON_BLOCKING, test_boundary_diagnostic_fields_stable},
    };
    return test_main("maelys_datalog_boundary", cases, (int)(sizeof(cases) / sizeof(cases[0])), argc, argv);
}
