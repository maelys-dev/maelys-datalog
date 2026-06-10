#include "src/core/maelys_datalog_parser.h"
#include "src/core/maelys_datalog_domain_registry.h"
#include "examples/domains/maelys_datalog_example_domains.h"
#include "src/core/maelys_datalog_predicate_registry.h"
#include "tests/helpers/test_framework.h"

#include <stdio.h>
#include <string.h>

static int parse_text_ex(const char *src, maelys_datalog_diagnostic_t *diag);
static int init_parser_ruleset(maelys_datalog_ruleset_t *r);

static int parse_text(const char *src) {
    maelys_datalog_diagnostic_t diag;
    return parse_text_ex(src, &diag);
}

static int init_parser_ruleset(maelys_datalog_ruleset_t *r) {
    memset(r, 0, sizeof(*r));
    if (maelys_datalog_ruleset_init(r, "policy.test", "graph",
                                    "0000000000000000000000000000000000000000000000000000000000000000", 1) != MAELYS_OK) {
        return -999;
    }
    if (maelys_datalog_example_domains_install() != MAELYS_OK) return -999;
    if (maelys_datalog_domain_registry_install("graph", &r->registry) != MAELYS_OK) return -999;
    maelys_datalog_predicate_registry_add_domain(&r->registry, "parent", 2, MAELYS_DATALOG_PRED_KIND_EDB);
    maelys_datalog_predicate_registry_add_domain(&r->registry, "q", 2, MAELYS_DATALOG_PRED_KIND_EDB);
    maelys_datalog_predicate_registry_add_domain(&r->registry, "r", 2, MAELYS_DATALOG_PRED_KIND_EDB);
    maelys_datalog_predicate_registry_add_domain(&r->registry, "blocked", 1, MAELYS_DATALOG_PRED_KIND_EDB);
    maelys_datalog_predicate_registry_add_domain(&r->registry, "backend", 1, MAELYS_DATALOG_PRED_KIND_EDB);
    maelys_datalog_predicate_registry_add_domain(&r->registry, "backend_class", 2, MAELYS_DATALOG_PRED_KIND_EDB);
    maelys_datalog_predicate_registry_add_domain(&r->registry, "backend_channel", 2, MAELYS_DATALOG_PRED_KIND_EDB);
    maelys_datalog_predicate_registry_add_domain(&r->registry, "backend_format", 2, MAELYS_DATALOG_PRED_KIND_EDB);
    maelys_datalog_predicate_registry_add_domain(&r->registry, "blocked_backend", 2, MAELYS_DATALOG_PRED_KIND_EDB);
    maelys_datalog_predicate_registry_add_domain(&r->registry, "wide", 4, MAELYS_DATALOG_PRED_KIND_EDB);
    maelys_datalog_predicate_registry_add_domain(&r->registry, "named", 2, MAELYS_DATALOG_PRED_KIND_EDB);
    maelys_datalog_predicate_registry_add_domain(&r->registry, "ancestor", 2,
                                                 MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY);
    maelys_datalog_predicate_registry_add_domain(&r->registry, "has_backend", 1,
                                                 MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY);
    maelys_datalog_predicate_registry_add_domain(&r->registry, "p", 1,
                                                 MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY);
    maelys_datalog_predicate_registry_add_domain(&r->registry, "bad", 1,
                                                 MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY);
    maelys_datalog_predicate_registry_add_domain(&r->registry, "bad", 2,
                                                 MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY);
    maelys_datalog_predicate_registry_add_domain(&r->registry, "allowed_backend_tuple", 3,
                                                 MAELYS_DATALOG_PRED_KIND_POLICY_FACT);
    maelys_datalog_predicate_registry_add_manifest_predicate(&r->registry, "allow", 1,
                                                             MAELYS_DATALOG_PRED_KIND_IDB);
    maelys_datalog_predicate_registry_add_manifest_predicate(&r->registry, "blocked_redaction_ok", 1,
                                                             MAELYS_DATALOG_PRED_KIND_IDB);
    maelys_datalog_predicate_registry_add_manifest_predicate(&r->registry, "reduce", 1,
                                                             MAELYS_DATALOG_PRED_KIND_IDB);
    maelys_datalog_predicate_registry_add_atom(&r->registry, "proj-1");
    maelys_datalog_predicate_registry_add_atom(&r->registry, "cli_pivot");
    maelys_datalog_predicate_registry_add_atom(&r->registry, "stdin");
    maelys_datalog_predicate_registry_add_atom(&r->registry, "text");
    maelys_datalog_predicate_registry_add_atom(&r->registry, "a");
    maelys_datalog_predicate_registry_add_atom(&r->registry, "b");
    maelys_datalog_predicate_registry_add_atom(&r->registry, "c");
    maelys_datalog_predicate_registry_freeze(&r->registry);
    return MAELYS_OK;
}

static int parse_text_ex(const char *src, maelys_datalog_diagnostic_t *diag) {
    maelys_datalog_ruleset_t r;
    if (init_parser_ruleset(&r) != MAELYS_OK) return -999;
    int rc = maelys_datalog_parse_ruleset_ex(&r, src, strlen(src), "test.dl", diag);
    maelys_datalog_ruleset_clear(&r);
    return rc;
}

static void fill_chars(char *buf, size_t len, char c) {
    memset(buf, c, len);
    buf[len] = '\0';
}

static int init_ruleset_with_predicate(maelys_datalog_ruleset_t *r,
                                       const char *predicate,
                                       size_t arity,
                                       unsigned kind_flags) {
    memset(r, 0, sizeof(*r));
    if (maelys_datalog_ruleset_init(r, "policy.test", "custom",
                                    "0000000000000000000000000000000000000000000000000000000000000000", 1) != MAELYS_OK) {
        return -999;
    }
    if (maelys_datalog_predicate_registry_add_domain(&r->registry, predicate, arity, kind_flags) != MAELYS_OK) {
        maelys_datalog_ruleset_clear(r);
        return -999;
    }
    if (maelys_datalog_predicate_registry_add_manifest_predicate(&r->registry, "p", 1,
                                                                 MAELYS_DATALOG_PRED_KIND_IDB |
                                                                     MAELYS_DATALOG_PRED_KIND_QUERY) != MAELYS_OK) {
        maelys_datalog_ruleset_clear(r);
        return -999;
    }
    if (maelys_datalog_predicate_registry_freeze(&r->registry) != MAELYS_OK) {
        maelys_datalog_ruleset_clear(r);
        return -999;
    }
    return MAELYS_OK;
}

static int file_contains(const char *path, const char *needle) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char buf[8192];
    size_t n = fread(buf, 1, sizeof(buf) - 1u, f);
    fclose(f);
    buf[n] = '\0';
    return strstr(buf, needle) != NULL;
}

static int test_parser_accepts_valid_fact_rule_and_recursion(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, parse_text("allowed_backend_tuple(\"cli_pivot\", \"stdin\", \"text\")."), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, parse_text("allow(P) :- blocked(P), blocked(P)."), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, parse_text("ancestor(X, Y) :- parent(X, Y).\nancestor(X, Z) :- parent(X, Y), ancestor(Y, Z)."), "%d");
    TEST_END();
}

static int test_parser_rejects_safety_unknowns_and_types(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, parse_text("allow(P) :- blocked(\"proj-1\")."), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, parse_text("magic_allow(P) :- blocked(P)."), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, parse_text("blocked(\"unsafe_root_shell\")."), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, parse_text("allow(P) :- blocked(P), \"a\" < \"b\"."), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, parse_text("allow(P) :- blocked(P), \"a\" = 1."), "%d");
    TEST_END();
}

static int test_parser_rejects_unsupported_constructs(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_ERR_UNSUPPORTED, parse_text("?- blocked(\"proj-1\")."), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_UNSUPPORTED, parse_text(".input blocked."), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_UNSUPPORTED, parse_text("~blocked(\"proj-1\")."), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_UNSUPPORTED, parse_text("allow(P) :- blocked(P); audit_enabled(P)."), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, parse_text("allow(_) :- blocked(\"proj-1\")."), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_UNSUPPORTED, parse_text("blocked(1.5)."), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_UNSUPPORTED, parse_text("allow(P) :- blocked(P), P MATCHES \"x\"."), "%d");
    TEST_END();
}

static int test_datalog_negation_safety_check_unsafe_variable(void) {
    TEST_BEGIN();
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      parse_text_ex("bad(X) :- not(p(X)).", &diag),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_PARSER_UNSAFE_VARIABLE, diag.code, "%d");
    TEST_END();
}

static int test_datalog_negation_not_in_head_rejected(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      parse_text("not(p(X)) :- blocked(X)."),
                      "%d");
    TEST_END();
}

static int test_datalog_negation_no_wildcard_in_not(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      parse_text("p(X) :- q(X, \"a\"), not(q(_, X))."),
                      "%d");
    TEST_END();
}

static int test_datalog_negation_not_space_rejected(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      parse_text("p(X) :- q(X, \"a\"), not p(X)."),
                      "%d");
    TEST_END();
}

static int test_datalog_negation_assign_strata_correct(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_parser_ruleset(&r), "%d");
    const char *src =
        "p(X) :- q(X, \"a\").\n"
        "bad(X) :- q(X, \"a\"), not(p(X)).";
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_parse_ruleset_ex(&r, src, strlen(src), "test.dl", NULL),
                      "%d");
    maelys_datalog_predicate_id_t p_id = 0;
    maelys_datalog_predicate_id_t bad_id = 0;
    TEST_ASSERT_TRUE(maelys_datalog_predicate_registry_find(&r.registry, "p", 1, &p_id));
    TEST_ASSERT_TRUE(maelys_datalog_predicate_registry_find(&r.registry, "bad", 1, &bad_id));
    TEST_ASSERT_TRUE(r.negation_supported);
    TEST_ASSERT_TRUE(r.strata_assigned);
    TEST_ASSERT_EQUAL((uint32_t)0u, r.strata[p_id], "%u");
    TEST_ASSERT_EQUAL((uint32_t)1u, r.strata[bad_id], "%u");
    TEST_ASSERT_EQUAL((uint32_t)1u, r.max_stratum, "%u");
    maelys_datalog_ruleset_clear(&r);
    TEST_END();
}

static int test_datalog_negation_negative_cycle_detected(void) {
    TEST_BEGIN();
    maelys_datalog_diagnostic_t diag;
    const char *src =
        "p(X) :- q(X, \"a\"), not(bad(X)).\n"
        "bad(X) :- q(X, \"a\"), not(p(X)).";
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, parse_text_ex(src, &diag), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_POLICY_NOT_STRATIFIABLE, diag.code, "%d");
    TEST_END();
}

static int test_parser_has_no_domain_predicate_special_case(void) {
    TEST_BEGIN();
    char parent_domain[32];
    char parent_domains_fn[64];
    snprintf(parent_domain, sizeof(parent_domain), "%s_%s", "context", "projection");
    snprintf(parent_domains_fn, sizeof(parent_domains_fn), "%s_%s_%s", "maelys_datalog", "maelys", "domains");
    TEST_ASSERT_FALSE(file_contains("src/core/maelys_datalog_parser.c", parent_domain));
    TEST_ASSERT_FALSE(file_contains("src/core/maelys_datalog_parser.c", parent_domains_fn));
    TEST_END();
}

static int test_parser_uses_registry_flags_not_names(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    memset(&r, 0, sizeof(r));
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_ruleset_init(&r, "policy.test", "custom",
        "0000000000000000000000000000000000000000000000000000000000000000", 1), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_predicate_registry_add_domain(&r.registry, "thing", 1,
        MAELYS_DATALOG_PRED_KIND_EDB), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_predicate_registry_add_manifest_predicate(&r.registry, "permit", 1,
        MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_predicate_registry_freeze(&r.registry), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_parse_ruleset(&r, "permit(X) :- thing(X).", strlen("permit(X) :- thing(X).")), "%d");
    maelys_datalog_ruleset_clear(&r);
    TEST_END();
}

static int test_parser_diag_unknown_predicate(void) {
    TEST_BEGIN();
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, parse_text_ex("magic_allow(P) :- blocked(P).", &diag), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_PARSER_UNKNOWN_PREDICATE, diag.code, "%d");
    TEST_ASSERT_EQUAL_STRING("magic_allow", diag.predicate);
    TEST_ASSERT_EQUAL((size_t)1u, diag.arity, "%zu");
    TEST_END();
}

static int test_parser_diag_arity_mismatch(void) {
    TEST_BEGIN();
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, parse_text_ex("blocked(\"proj-1\", \"a\").", &diag), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_PARSER_ARITY_MISMATCH, diag.code, "%d");
    TEST_ASSERT_EQUAL_STRING("blocked", diag.predicate);
    TEST_ASSERT_EQUAL((size_t)2u, diag.arity, "%zu");
    TEST_END();
}

static int test_parser_diag_rule_head_edb_forbidden(void) {
    TEST_BEGIN();
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, parse_text_ex("blocked(P) :- allow(P).", &diag), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_PARSER_RULE_HEAD_EDB_FORBIDDEN, diag.code, "%d");
    TEST_ASSERT_EQUAL_STRING("blocked", diag.predicate);
    TEST_END();
}

static int test_parser_diag_fact_uses_non_base_predicate(void) {
    TEST_BEGIN();
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, parse_text_ex("allow(\"a\").", &diag), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_PARSER_FACT_USES_NON_BASE_PREDICATE, diag.code, "%d");
    TEST_ASSERT_EQUAL_STRING("allow", diag.predicate);
    TEST_END();
}

static int test_parser_diag_rule_body_literal_overflow(void) {
    TEST_BEGIN();
    maelys_datalog_diagnostic_t diag;
    const char *src =
        "allow(P) :- blocked(P), blocked(P), blocked(P), blocked(P), "
        "blocked(P), blocked(P), blocked(P), blocked(P), blocked(P).";
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE, parse_text_ex(src, &diag), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_PARSER_RULE_BODY_LITERAL_OVERFLOW, diag.code, "%d");
    TEST_ASSERT_EQUAL_STRING("allow", diag.predicate);
    TEST_ASSERT_EQUAL((size_t)9u, diag.count, "%zu");
    TEST_ASSERT_EQUAL((size_t)MAELYS_DATALOG_MAX_BODY_LITERALS, diag.limit, "%zu");
    TEST_ASSERT_TRUE(strstr(diag.hint, "split rule") != NULL);
    TEST_END();
}

static int test_parser_diag_unsafe_variable(void) {
    TEST_BEGIN();
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, parse_text_ex("allow(P) :- blocked(\"proj-1\").", &diag), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_PARSER_UNSAFE_VARIABLE, diag.code, "%d");
    TEST_ASSERT_EQUAL_STRING("allow", diag.predicate);
    TEST_END();
}

static int test_parser_diag_unknown_atom(void) {
    TEST_BEGIN();
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, parse_text_ex("blocked(\"unsafe_root_shell\").", &diag), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_PARSER_UNKNOWN_ATOM, diag.code, "%d");
    TEST_ASSERT_EQUAL_STRING("unsafe_root_shell", diag.token);
    TEST_END();
}

static int test_parser_predicate_name_at_max_length_accepted(void) {
    TEST_BEGIN();
    char pred[64];
    fill_chars(pred, 63u, 'a');
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_ruleset_with_predicate(&r, pred, 1, MAELYS_DATALOG_PRED_KIND_EDB), "%d");
    char src[96];
    snprintf(src, sizeof(src), "p(X) :- %s(X).", pred);
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_parse_ruleset_ex(&r, src, strlen(src), "test.dl", NULL), "%d");
    maelys_datalog_ruleset_clear(&r);
    TEST_END();
}

static int test_parser_predicate_name_over_max_length_rejected(void) {
    TEST_BEGIN();
    char pred[65];
    fill_chars(pred, 64u, 'a');
    char src[96];
    snprintf(src, sizeof(src), "%s(X).", pred);
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, parse_text(src), "%d");
    TEST_END();
}

static int test_parser_predicate_name_over_max_length_has_diagnostic(void) {
    TEST_BEGIN();
    char pred[65];
    fill_chars(pred, 64u, 'a');
    char src[96];
    snprintf(src, sizeof(src), "%s(X).", pred);
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, parse_text_ex(src, &diag), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_PARSER_UNKNOWN_PREDICATE, diag.code, "%d");
    TEST_ASSERT_TRUE(diag.message[0] != '\0');
    TEST_ASSERT_TRUE(diag.token[0] != '\0');
    TEST_END();
}

static int test_parser_unknown_token_in_body_has_diagnostic(void) {
    TEST_BEGIN();
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, parse_text_ex("allow(P) :- .", &diag), "%d");
    TEST_ASSERT_TRUE(diag.code != MAELYS_DATALOG_DIAG_NONE);
    TEST_ASSERT_TRUE(diag.message[0] != '\0');
    TEST_END();
}

static int test_parser_unknown_token_in_body_returns_invalid(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, parse_text("allow(P) :- ."), "%d");
    TEST_END();
}

static int test_parser_ruleset_init_policy_id_exact_length_accepted(void) {
    TEST_BEGIN();
    char policy_id[128];
    fill_chars(policy_id, 127u, 'p');
    maelys_datalog_ruleset_t r;
    memset(&r, 0, sizeof(r));
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_ruleset_init(&r, policy_id, "graph",
                                                  "0000000000000000000000000000000000000000000000000000000000000000", 1),
                      "%d");
    TEST_ASSERT_EQUAL_STRING(policy_id, r.policy_id);
    maelys_datalog_ruleset_clear(&r);
    TEST_END();
}

static int test_parser_ruleset_init_policy_id_too_long_rejected(void) {
    TEST_BEGIN();
    char policy_id[129];
    fill_chars(policy_id, 128u, 'p');
    maelys_datalog_ruleset_t r;
    memset(&r, 0, sizeof(r));
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE,
                      maelys_datalog_ruleset_init(&r, policy_id, "graph",
                                                  "0000000000000000000000000000000000000000000000000000000000000000", 1),
                      "%d");
    TEST_END();
}

static int test_parser_ruleset_init_sha256_too_long_rejected(void) {
    TEST_BEGIN();
    char sha256[66];
    fill_chars(sha256, 65u, '0');
    maelys_datalog_ruleset_t r;
    memset(&r, 0, sizeof(r));
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE,
                      maelys_datalog_ruleset_init(&r, "policy.test", "graph", sha256, 1),
                      "%d");
    TEST_END();
}

static int test_parser_ruleset_init_domain_too_long_rejected(void) {
    TEST_BEGIN();
    char domain[65];
    fill_chars(domain, 64u, 'd');
    maelys_datalog_ruleset_t r;
    memset(&r, 0, sizeof(r));
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE,
                      maelys_datalog_ruleset_init(&r, "policy.test", domain,
                                                  "0000000000000000000000000000000000000000000000000000000000000000", 1),
                      "%d");
    TEST_END();
}

static int test_parser_unsafe_variable_detected_single(void) {
    TEST_BEGIN();
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, parse_text_ex("allow(P) :- blocked(\"proj-1\").", &diag), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_PARSER_UNSAFE_VARIABLE, diag.code, "%d");
    TEST_END();
}

static int test_parser_unsafe_variable_detected_multiple(void) {
    TEST_BEGIN();
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, parse_text_ex("ancestor(X, Y) :- parent(X, Z).", &diag), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_PARSER_UNSAFE_VARIABLE, diag.code, "%d");
    TEST_ASSERT_EQUAL_STRING("ancestor", diag.predicate);
    TEST_END();
}

static int test_parser_safe_rule_all_head_vars_bound(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, parse_text("ancestor(X, Y) :- parent(X, Y)."), "%d");
    TEST_END();
}

static int test_parser_anonymous_variable_excluded_from_head_check(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, parse_text("has_backend(P) :- blocked_backend(P, _)."), "%d");
    TEST_END();
}

static int test_parser_valid_rules_unchanged(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, parse_text("allowed_backend_tuple(\"cli_pivot\", \"stdin\", \"text\").\n"
                                            "allow(P) :- blocked(P), blocked(P).\n"
                                            "ancestor(X, Z) :- parent(X, Y), ancestor(Y, Z)."), "%d");
    TEST_END();
}

static int test_parser_comparison_valid_still_accepted(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, parse_text("allow(P) :- blocked(P), P = P."), "%d");
    TEST_END();
}

static int test_datalog_parser_rejects_symbol_ordinal_static(void) {
    TEST_BEGIN();
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      parse_text_ex("allow(P) :- blocked(P), \"a\" < \"b\".", &diag),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_PARSER_INVALID_COMPARISON, diag.code, "%d");
    TEST_END();
}

static int test_datalog_parser_rejects_bool_ordinal_static(void) {
    TEST_BEGIN();
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      parse_text_ex("allow(P) :- blocked(P), true > false.", &diag),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_PARSER_INVALID_COMPARISON, diag.code, "%d");
    TEST_END();
}

static int test_datalog_parser_rejects_kind_mismatch_static(void) {
    TEST_BEGIN();
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      parse_text_ex("allow(P) :- blocked(P), \"a\" = 1.", &diag),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_PARSER_INVALID_COMPARISON, diag.code, "%d");
    TEST_END();
}

static int test_datalog_parser_rejects_underscore_in_comparison(void) {
    TEST_BEGIN();
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      parse_text_ex("allow(P) :- blocked(P), _ = P.", &diag),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_PARSER_ANONYMOUS_VARIABLE_IN_COMPARISON, diag.code, "%d");
    TEST_END();
}

static int test_parser_wildcard_in_body_still_accepted(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, parse_text("has_backend(P) :- blocked_backend(P, _)."), "%d");
    TEST_END();
}

static int test_datalog_policy_fact_allowed_as_direct_dl_fact(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, parse_text("allowed_backend_tuple(\"cli_pivot\", \"stdin\", \"text\")."), "%d");
    TEST_END();
}

static int test_datalog_policy_fact_rejected_as_rule_head(void) {
    TEST_BEGIN();
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      parse_text_ex("allowed_backend_tuple(K, N, F) :- backend_class(B, K), backend_channel(B, N), backend_format(B, F).", &diag),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_PARSER_RULE_HEAD_EDB_FORBIDDEN, diag.code, "%d");
    TEST_ASSERT_EQUAL_STRING("allowed_backend_tuple", diag.predicate);
    TEST_END();
}

static int test_datalog_edb_rejected_as_direct_dl_fact(void) {
    TEST_BEGIN();
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, parse_text_ex("backend(1).", &diag), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_PARSER_FACT_USES_NON_BASE_PREDICATE, diag.code, "%d");
    TEST_ASSERT_EQUAL_STRING("backend", diag.predicate);
    TEST_END();
}

static int test_datalog_policy_fact_allowed_in_rule_body(void) {
    TEST_BEGIN();
    const char *src =
        "allow(B) :- backend(B), backend_class(B, K), backend_channel(B, N), backend_format(B, F), allowed_backend_tuple(K, N, F).";
    TEST_ASSERT_EQUAL(MAELYS_OK, parse_text(src), "%d");
    TEST_END();
}

static int test_datalog_idb_still_allowed_in_rule_body(void) {
    TEST_BEGIN();
    const char *src =
        "allow(P) :- blocked(P), blocked_redaction_ok(P), reduce(P).";
    TEST_ASSERT_EQUAL(MAELYS_OK, parse_text(src), "%d");
    TEST_END();
}

static int test_datalog_policy_fact_parser_does_not_mutate_frozen_registry(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    memset(&r, 0, sizeof(r));
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_ruleset_init(&r, "policy.test", "custom",
        "0000000000000000000000000000000000000000000000000000000000000000", 1), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_predicate_registry_add_domain(&r.registry, "runtime_fact", 1,
                                                                              MAELYS_DATALOG_PRED_KIND_EDB), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_predicate_registry_freeze(&r.registry), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_parse_ruleset(&r, "runtime_fact(1).", strlen("runtime_fact(1).")),
                      "%d");
    maelys_datalog_predicate_id_t pid = 0;
    TEST_ASSERT_TRUE(maelys_datalog_predicate_registry_find(&r.registry, "runtime_fact", 1, &pid));
    const maelys_datalog_predicate_def_t *def =
        maelys_datalog_predicate_registry_get(&r.registry, pid);
    TEST_ASSERT_NOT_NULL(def);
    TEST_ASSERT_TRUE(def->kind_flags & MAELYS_DATALOG_PRED_KIND_EDB);
    TEST_ASSERT_FALSE(def->kind_flags & MAELYS_DATALOG_PRED_KIND_POLICY_FACT);
    maelys_datalog_ruleset_clear(&r);
    TEST_END();
}

static int symbol_table_contains_prefix(const maelys_datalog_ruleset_t *r, const char *prefix) {
    size_t prefix_len = strlen(prefix);
    for (size_t i = 0; i < r->symbols.count; i++) {
        const char *s = r->symbols.storage + r->symbols.entries[i].offset;
        if (strncmp(s, prefix, prefix_len) == 0) return 1;
    }
    return 0;
}

static int test_datalog_wildcard_accepted_in_body_atom(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, parse_text("has_backend(P) :- blocked_backend(P, _)."), "%d");
    TEST_END();
}

static int test_datalog_wildcard_multiple_occurrences_are_distinct(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_parser_ruleset(&r), "%d");
    const char *src = "p(X) :- q(X, _), r(X, _).";
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_parse_ruleset_ex(&r, src, strlen(src), "test.dl", NULL), "%d");
    TEST_ASSERT_EQUAL((size_t)1u, r.rule_count, "%zu");
    unsigned a = r.rules[0].body[0].atom.terms[1].as.variable;
    unsigned b = r.rules[0].body[1].atom.terms[1].as.variable;
    TEST_ASSERT_TRUE(a >= MAELYS_DATALOG_NAMED_VARIABLE_COUNT);
    TEST_ASSERT_TRUE(b >= MAELYS_DATALOG_NAMED_VARIABLE_COUNT);
    TEST_ASSERT_TRUE(a != b);
    maelys_datalog_ruleset_clear(&r);
    TEST_END();
}

static int test_datalog_wildcard_multiple_in_same_atom(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, parse_text("p(X) :- edge(_, _), q(X, \"a\")."), "%d");
    TEST_END();
}

static int test_datalog_wildcard_each_occurrence_gets_fresh_variable(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_parser_ruleset(&r), "%d");
    const char *src = "p(X) :- edge(_, _), q(X, \"a\").";
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_parse_ruleset_ex(&r, src, strlen(src), "test.dl", NULL), "%d");
    TEST_ASSERT_EQUAL((size_t)1u, r.rule_count, "%zu");
    unsigned a = r.rules[0].body[0].atom.terms[0].as.variable;
    unsigned b = r.rules[0].body[0].atom.terms[1].as.variable;
    TEST_ASSERT_TRUE(a >= MAELYS_DATALOG_NAMED_VARIABLE_COUNT);
    TEST_ASSERT_TRUE(b >= MAELYS_DATALOG_NAMED_VARIABLE_COUNT);
    TEST_ASSERT_TRUE(a != b);
    maelys_datalog_ruleset_clear(&r);
    TEST_END();
}

static int test_datalog_wildcard_rejected_in_rule_head(void) {
    TEST_BEGIN();
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, parse_text_ex("ancestor(_, X) :- parent(X, X).", &diag), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_PARSER_ANONYMOUS_VARIABLE_IN_HEAD, diag.code, "%d");
    TEST_END();
}

static int test_datalog_wildcard_rejected_in_head_before_safety(void) {
    TEST_BEGIN();
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, parse_text_ex("bad(_) :- blocked(P).", &diag), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_PARSER_ANONYMOUS_VARIABLE_IN_HEAD, diag.code, "%d");
    TEST_ASSERT_FALSE(diag.code == MAELYS_DATALOG_DIAG_PARSER_UNSAFE_VARIABLE);
    TEST_END();
}

static int test_datalog_wildcard_rejected_in_comparison(void) {
    TEST_BEGIN();
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, parse_text_ex("bad(P) :- blocked(P), _ = P.", &diag), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_PARSER_ANONYMOUS_VARIABLE_IN_COMPARISON, diag.code, "%d");
    TEST_END();
}

static int test_datalog_wildcard_rejected_in_comparison_before_lowering(void) {
    TEST_BEGIN();
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, parse_text_ex("bad(P) :- blocked(P), P = _.", &diag), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_PARSER_ANONYMOUS_VARIABLE_IN_COMPARISON, diag.code, "%d");
    TEST_END();
}

static int test_datalog_wildcard_rejected_in_direct_fact(void) {
    TEST_BEGIN();
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, parse_text_ex("allowed_backend_tuple(_, \"stdin\", \"text\").", &diag), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_PARSER_ANONYMOUS_VARIABLE_IN_FACT, diag.code, "%d");
    TEST_END();
}

static int test_datalog_wildcard_head_candidate_rejected_without_commit(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_parser_ruleset(&r), "%d");
    size_t before_symbols = r.symbols.count;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_parse_ruleset_ex(&r, "bad(_) :- blocked(P).", strlen("bad(_) :- blocked(P)."), "test.dl", &diag),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_PARSER_ANONYMOUS_VARIABLE_IN_HEAD, diag.code, "%d");
    TEST_ASSERT_EQUAL((size_t)0u, r.rule_count, "%zu");
    TEST_ASSERT_EQUAL((size_t)0u, r.fact_count, "%zu");
    TEST_ASSERT_EQUAL(before_symbols, r.symbols.count, "%zu");
    maelys_datalog_ruleset_clear(&r);
    TEST_END();
}

static int test_datalog_wildcard_direct_fact_candidate_checks_kind_before_groundness(void) {
    TEST_BEGIN();
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, parse_text_ex("backend_class(1, _).", &diag), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_PARSER_FACT_USES_NON_BASE_PREDICATE, diag.code, "%d");
    TEST_END();
}

static int test_datalog_wildcard_candidate_allocation_does_not_leak(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_parser_ruleset(&r), "%d");
    size_t before = r.symbols.count;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_parse_ruleset(&r, "bad(_) :- blocked(P).", strlen("bad(_) :- blocked(P).")),
                      "%d");
    TEST_ASSERT_EQUAL(before, r.symbols.count, "%zu");
    TEST_ASSERT_FALSE(symbol_table_contains_prefix(&r, "_G"));
    maelys_datalog_ruleset_clear(&r);
    TEST_END();
}

static int test_datalog_wildcard_next_rule_state_not_corrupted_after_candidate_reject(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, parse_text("bad(_) :- blocked(P)."), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, parse_text("has_backend(P) :- blocked_backend(P, _)."), "%d");
    TEST_END();
}

static int test_datalog_wildcard_direct_policy_fact_rejected_as_non_ground(void) {
    TEST_BEGIN();
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, parse_text_ex("allowed_backend_tuple(_, \"stdin\", \"text\").", &diag), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_PARSER_ANONYMOUS_VARIABLE_IN_FACT, diag.code, "%d");
    TEST_ASSERT_EQUAL_STRING("allowed_backend_tuple", diag.predicate);
    TEST_END();
}

static int test_datalog_wildcard_direct_edb_fact_rejected_by_kind_before_groundness(void) {
    TEST_BEGIN();
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, parse_text_ex("backend_class(1, _).", &diag), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_PARSER_FACT_USES_NON_BASE_PREDICATE, diag.code, "%d");
    TEST_ASSERT_EQUAL_STRING("backend_class", diag.predicate);
    TEST_END();
}

static int test_datalog_wildcard_does_not_bind_head_variable(void) {
    TEST_BEGIN();
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, parse_text_ex("bad(P) :- blocked_backend(_, _).", &diag), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_PARSER_UNSAFE_VARIABLE, diag.code, "%d");
    TEST_END();
}

static int test_datalog_wildcard_named_head_variable_still_safe(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, parse_text("has_backend(P) :- blocked_backend(P, _)."), "%d");
    TEST_END();
}

static int test_datalog_wildcard_anonymous_ids_start_after_named_variables(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_parser_ruleset(&r), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_parse_ruleset(&r, "has_backend(P) :- blocked_backend(P, _).",
                                                   strlen("has_backend(P) :- blocked_backend(P, _).")),
                      "%d");
    unsigned id = r.rules[0].body[0].atom.terms[1].as.variable;
    TEST_ASSERT_TRUE(id >= MAELYS_DATALOG_NAMED_VARIABLE_COUNT);
    TEST_ASSERT_TRUE(id < MAELYS_DATALOG_MAX_RULE_VARIABLES);
    maelys_datalog_ruleset_clear(&r);
    TEST_END();
}

static int test_datalog_wildcard_capacity_overflow_fails_closed(void) {
    TEST_BEGIN();
    maelys_datalog_diagnostic_t diag;
    const char *src = "p(X) :- wide(_, _, _, _), edge(_, _), q(X, _).";
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE, parse_text_ex(src, &diag), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_PARSER_TOO_MANY_VARIABLES, diag.code, "%d");
    TEST_ASSERT_EQUAL((size_t)MAELYS_DATALOG_MAX_RULE_VARIABLES, diag.limit, "%zu");
    TEST_END();
}

static int test_datalog_wildcard_no_global_symbol_growth_on_repeated_loads(void) {
    TEST_BEGIN();
    for (size_t i = 0; i < 3; i++) {
        maelys_datalog_ruleset_t r;
        TEST_ASSERT_EQUAL(MAELYS_OK, init_parser_ruleset(&r), "%d");
        TEST_ASSERT_EQUAL(MAELYS_OK,
                          maelys_datalog_parse_ruleset(&r, "has_backend(P) :- blocked_backend(P, _).",
                                                       strlen("has_backend(P) :- blocked_backend(P, _).")),
                          "%d");
        TEST_ASSERT_EQUAL((size_t)0u, r.symbols.count, "%zu");
        maelys_datalog_ruleset_clear(&r);
    }
    TEST_END();
}

static int test_datalog_wildcard_generated_names_not_in_symbol_table(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t r;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_parser_ruleset(&r), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_parse_ruleset(&r, "has_backend(P) :- blocked_backend(P, _).",
                                                   strlen("has_backend(P) :- blocked_backend(P, _).")),
                      "%d");
    TEST_ASSERT_FALSE(symbol_table_contains_prefix(&r, "_G"));
    maelys_datalog_ruleset_clear(&r);
    TEST_END();
}

int main(int argc, char **argv) {
    test_case_t cases[] = {
        {"maelys_datalog_parser/valid_fact_rule_recursion", TEST_MODE_NON_BLOCKING, test_parser_accepts_valid_fact_rule_and_recursion},
        {"maelys_datalog_parser/rejects_safety_unknowns_types", TEST_MODE_NON_BLOCKING, test_parser_rejects_safety_unknowns_and_types},
        {"maelys_datalog_parser/rejects_unsupported_constructs", TEST_MODE_NON_BLOCKING, test_parser_rejects_unsupported_constructs},
        {"maelys_datalog_parser/negation_safety_check_unsafe_variable", TEST_MODE_NON_BLOCKING, test_datalog_negation_safety_check_unsafe_variable},
        {"maelys_datalog_parser/negation_not_in_head_rejected", TEST_MODE_NON_BLOCKING, test_datalog_negation_not_in_head_rejected},
        {"maelys_datalog_parser/negation_no_wildcard_in_not", TEST_MODE_NON_BLOCKING, test_datalog_negation_no_wildcard_in_not},
        {"maelys_datalog_parser/negation_not_space_rejected", TEST_MODE_NON_BLOCKING, test_datalog_negation_not_space_rejected},
        {"maelys_datalog_parser/negation_assign_strata_correct", TEST_MODE_NON_BLOCKING, test_datalog_negation_assign_strata_correct},
        {"maelys_datalog_parser/negation_negative_cycle_detected", TEST_MODE_NON_BLOCKING, test_datalog_negation_negative_cycle_detected},
        {"maelys_datalog_parser/no_domain_predicate_special_case", TEST_MODE_NON_BLOCKING, test_parser_has_no_domain_predicate_special_case},
        {"maelys_datalog_parser/uses_registry_flags_not_names", TEST_MODE_NON_BLOCKING, test_parser_uses_registry_flags_not_names},
        {"maelys_datalog_parser/diag_unknown_predicate", TEST_MODE_NON_BLOCKING, test_parser_diag_unknown_predicate},
        {"maelys_datalog_parser/diag_arity_mismatch", TEST_MODE_NON_BLOCKING, test_parser_diag_arity_mismatch},
        {"maelys_datalog_parser/diag_rule_head_edb_forbidden", TEST_MODE_NON_BLOCKING, test_parser_diag_rule_head_edb_forbidden},
        {"maelys_datalog_parser/diag_fact_uses_non_base_predicate", TEST_MODE_NON_BLOCKING, test_parser_diag_fact_uses_non_base_predicate},
        {"maelys_datalog_parser/diag_rule_body_literal_overflow", TEST_MODE_NON_BLOCKING, test_parser_diag_rule_body_literal_overflow},
        {"maelys_datalog_parser/diag_unsafe_variable", TEST_MODE_NON_BLOCKING, test_parser_diag_unsafe_variable},
        {"maelys_datalog_parser/diag_unknown_atom", TEST_MODE_NON_BLOCKING, test_parser_diag_unknown_atom},
        {"maelys_datalog_parser/predicate_name_at_max_length_accepted", TEST_MODE_NON_BLOCKING, test_parser_predicate_name_at_max_length_accepted},
        {"maelys_datalog_parser/predicate_name_over_max_length_rejected", TEST_MODE_NON_BLOCKING, test_parser_predicate_name_over_max_length_rejected},
        {"maelys_datalog_parser/predicate_name_over_max_length_has_diagnostic", TEST_MODE_NON_BLOCKING, test_parser_predicate_name_over_max_length_has_diagnostic},
        {"maelys_datalog_parser/unknown_token_in_body_has_diagnostic", TEST_MODE_NON_BLOCKING, test_parser_unknown_token_in_body_has_diagnostic},
        {"maelys_datalog_parser/unknown_token_in_body_returns_invalid", TEST_MODE_NON_BLOCKING, test_parser_unknown_token_in_body_returns_invalid},
        {"maelys_datalog_parser/ruleset_init_policy_id_exact_length_accepted", TEST_MODE_NON_BLOCKING, test_parser_ruleset_init_policy_id_exact_length_accepted},
        {"maelys_datalog_parser/ruleset_init_policy_id_too_long_rejected", TEST_MODE_NON_BLOCKING, test_parser_ruleset_init_policy_id_too_long_rejected},
        {"maelys_datalog_parser/ruleset_init_sha256_too_long_rejected", TEST_MODE_NON_BLOCKING, test_parser_ruleset_init_sha256_too_long_rejected},
        {"maelys_datalog_parser/ruleset_init_domain_too_long_rejected", TEST_MODE_NON_BLOCKING, test_parser_ruleset_init_domain_too_long_rejected},
        {"maelys_datalog_parser/unsafe_variable_detected_single", TEST_MODE_NON_BLOCKING, test_parser_unsafe_variable_detected_single},
        {"maelys_datalog_parser/unsafe_variable_detected_multiple", TEST_MODE_NON_BLOCKING, test_parser_unsafe_variable_detected_multiple},
        {"maelys_datalog_parser/safe_rule_all_head_vars_bound", TEST_MODE_NON_BLOCKING, test_parser_safe_rule_all_head_vars_bound},
        {"maelys_datalog_parser/anonymous_variable_excluded_from_head_check", TEST_MODE_NON_BLOCKING, test_parser_anonymous_variable_excluded_from_head_check},
        {"maelys_datalog_parser/valid_rules_unchanged", TEST_MODE_NON_BLOCKING, test_parser_valid_rules_unchanged},
        {"maelys_datalog_parser/comparison_valid_still_accepted", TEST_MODE_NON_BLOCKING, test_parser_comparison_valid_still_accepted},
        {"maelys_datalog_parser/rejects_symbol_ordinal_static", TEST_MODE_NON_BLOCKING, test_datalog_parser_rejects_symbol_ordinal_static},
        {"maelys_datalog_parser/rejects_bool_ordinal_static", TEST_MODE_NON_BLOCKING, test_datalog_parser_rejects_bool_ordinal_static},
        {"maelys_datalog_parser/rejects_kind_mismatch_static", TEST_MODE_NON_BLOCKING, test_datalog_parser_rejects_kind_mismatch_static},
        {"maelys_datalog_parser/rejects_underscore_in_comparison", TEST_MODE_NON_BLOCKING, test_datalog_parser_rejects_underscore_in_comparison},
        {"maelys_datalog_parser/wildcard_in_body_still_accepted", TEST_MODE_NON_BLOCKING, test_parser_wildcard_in_body_still_accepted},
        {"maelys_datalog_parser/policy_fact_allowed_as_direct_dl_fact", TEST_MODE_NON_BLOCKING, test_datalog_policy_fact_allowed_as_direct_dl_fact},
        {"maelys_datalog_parser/policy_fact_rejected_as_rule_head", TEST_MODE_NON_BLOCKING, test_datalog_policy_fact_rejected_as_rule_head},
        {"maelys_datalog_parser/edb_rejected_as_direct_dl_fact", TEST_MODE_NON_BLOCKING, test_datalog_edb_rejected_as_direct_dl_fact},
        {"maelys_datalog_parser/policy_fact_allowed_in_rule_body", TEST_MODE_NON_BLOCKING, test_datalog_policy_fact_allowed_in_rule_body},
        {"maelys_datalog_parser/idb_still_allowed_in_rule_body", TEST_MODE_NON_BLOCKING, test_datalog_idb_still_allowed_in_rule_body},
        {"maelys_datalog_parser/policy_fact_parser_does_not_mutate_frozen_registry", TEST_MODE_NON_BLOCKING, test_datalog_policy_fact_parser_does_not_mutate_frozen_registry},
        {"maelys_datalog_parser/wildcard_accepted_in_body_atom", TEST_MODE_NON_BLOCKING, test_datalog_wildcard_accepted_in_body_atom},
        {"maelys_datalog_parser/wildcard_multiple_occurrences_are_distinct", TEST_MODE_NON_BLOCKING, test_datalog_wildcard_multiple_occurrences_are_distinct},
        {"maelys_datalog_parser/wildcard_multiple_in_same_atom", TEST_MODE_NON_BLOCKING, test_datalog_wildcard_multiple_in_same_atom},
        {"maelys_datalog_parser/wildcard_each_occurrence_gets_fresh_variable", TEST_MODE_NON_BLOCKING, test_datalog_wildcard_each_occurrence_gets_fresh_variable},
        {"maelys_datalog_parser/wildcard_rejected_in_rule_head", TEST_MODE_NON_BLOCKING, test_datalog_wildcard_rejected_in_rule_head},
        {"maelys_datalog_parser/wildcard_rejected_in_head_before_safety", TEST_MODE_NON_BLOCKING, test_datalog_wildcard_rejected_in_head_before_safety},
        {"maelys_datalog_parser/wildcard_rejected_in_comparison", TEST_MODE_NON_BLOCKING, test_datalog_wildcard_rejected_in_comparison},
        {"maelys_datalog_parser/wildcard_rejected_in_comparison_before_lowering", TEST_MODE_NON_BLOCKING, test_datalog_wildcard_rejected_in_comparison_before_lowering},
        {"maelys_datalog_parser/wildcard_rejected_in_direct_fact", TEST_MODE_NON_BLOCKING, test_datalog_wildcard_rejected_in_direct_fact},
        {"maelys_datalog_parser/wildcard_head_candidate_rejected_without_commit", TEST_MODE_NON_BLOCKING, test_datalog_wildcard_head_candidate_rejected_without_commit},
        {"maelys_datalog_parser/wildcard_direct_fact_candidate_checks_kind_before_groundness", TEST_MODE_NON_BLOCKING, test_datalog_wildcard_direct_fact_candidate_checks_kind_before_groundness},
        {"maelys_datalog_parser/wildcard_candidate_allocation_does_not_leak", TEST_MODE_NON_BLOCKING, test_datalog_wildcard_candidate_allocation_does_not_leak},
        {"maelys_datalog_parser/wildcard_next_rule_state_not_corrupted_after_candidate_reject", TEST_MODE_NON_BLOCKING, test_datalog_wildcard_next_rule_state_not_corrupted_after_candidate_reject},
        {"maelys_datalog_parser/wildcard_direct_policy_fact_rejected_as_non_ground", TEST_MODE_NON_BLOCKING, test_datalog_wildcard_direct_policy_fact_rejected_as_non_ground},
        {"maelys_datalog_parser/wildcard_direct_edb_fact_rejected_by_kind_before_groundness", TEST_MODE_NON_BLOCKING, test_datalog_wildcard_direct_edb_fact_rejected_by_kind_before_groundness},
        {"maelys_datalog_parser/wildcard_does_not_bind_head_variable", TEST_MODE_NON_BLOCKING, test_datalog_wildcard_does_not_bind_head_variable},
        {"maelys_datalog_parser/wildcard_named_head_variable_still_safe", TEST_MODE_NON_BLOCKING, test_datalog_wildcard_named_head_variable_still_safe},
        {"maelys_datalog_parser/wildcard_anonymous_ids_start_after_named_variables", TEST_MODE_NON_BLOCKING, test_datalog_wildcard_anonymous_ids_start_after_named_variables},
        {"maelys_datalog_parser/wildcard_capacity_overflow_fails_closed", TEST_MODE_NON_BLOCKING, test_datalog_wildcard_capacity_overflow_fails_closed},
        {"maelys_datalog_parser/wildcard_no_global_symbol_growth_on_repeated_loads", TEST_MODE_NON_BLOCKING, test_datalog_wildcard_no_global_symbol_growth_on_repeated_loads},
        {"maelys_datalog_parser/wildcard_generated_names_not_in_symbol_table", TEST_MODE_NON_BLOCKING, test_datalog_wildcard_generated_names_not_in_symbol_table},
    };
    return test_main("maelys_datalog_parser", cases, (int)(sizeof(cases) / sizeof(cases[0])), argc, argv);
}
