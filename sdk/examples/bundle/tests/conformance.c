/* SPDX-License-Identifier: MPL-2.0 */
#include "extension.h"
#include "maelys_conformance.h"

static int context(const maelys_datalog_extension_t *e, maelys_datalog_context_t **out) {
    MC_OK(maelys_datalog_context_create(out));
    MC_OK(maelys_datalog_context_register(*out, e));
    MC_OK(maelys_datalog_context_seal(*out, NULL));
    return 0;
}
static maelys_datalog_public_fact_t candidate(const char *name) {
    maelys_datalog_public_fact_t f = {0};
    f.predicate = "candidate";
    f.arity = 1;
    f.terms[0].kind = MAELYS_DATALOG_VALUE_SYMBOL;
    f.terms[0].as.symbol = name;
    return f;
}
static int end_to_end(void) {
    maelys_datalog_extension_t e = example_bundle_extension();
    MC_REQUIRE(e.frontend_count == 1 && e.filter_count == 1);
    MC_REQUIRE(!e.backend_count && !e.planner_count);
    maelys_datalog_context_t *c;
    MC_REQUIRE(!context(&e, &c));
    char source[] = "# accounts\n  permit \"alice\"\npermit \"carol\"\n";
    maelys_datalog_policy_t *p = NULL;
    /* Registration does not implicitly select the frontend. */
    MC_REQUIRE(maelys_datalog_context_load_inline(c, NULL, "bundle_example", "accounts", source,
                                                  strlen(source), &p,
                                                  NULL) != MAELYS_DATALOG_STATUS_OK);
    MC_REQUIRE(!p);
    MC_OK(maelys_datalog_context_load_inline(c, e.frontends->name, "bundle_example", "accounts",
                                             source, strlen(source), &p, NULL));
    memset(source, 'z', sizeof(source)); /* Nothing retained from caller source. */
    maelys_datalog_session_t *s;
    MC_OK(maelys_datalog_context_session_create(c, p, 0, NULL,
                                                MAELYS_DATALOG_CAP_FILTERS |
                                                    MAELYS_DATALOG_CAP_EXPLAIN_TRUE |
                                                    MAELYS_DATALOG_CAP_EXPLAIN_FALSE,
                                                0, &s));
    MC_OK(maelys_datalog_policy_free(p));
    MC_OK(maelys_datalog_context_free(c)); /* Session keeps the bundled catalogue. */

    const maelys_datalog_program_t *program;
    maelys_datalog_program_info_t info;
    maelys_datalog_ir_rule_t rule;
    MC_OK(maelys_datalog_session_program(s, &program));
    MC_OK(maelys_datalog_program_info(program, &info));
    MC_REQUIRE(info.rule_count == 2 && (info.required_capabilities & MAELYS_DATALOG_CAP_FILTERS));
    MC_OK(maelys_datalog_program_rule(program, 0, &rule));
    MC_REQUIRE(rule.source.line == 2 && rule.source.column == 3);
    MC_REQUIRE(rule.body_count == 2 && rule.body[1].kind == MAELYS_DATALOG_IR_FILTER);
    MC_REQUIRE(!strcmp(rule.body[1].filter_name, e.filters->name));
    MC_REQUIRE(!strcmp(rule.body[1].filter_semantic_id, e.filters->semantic_id));
    MC_REQUIRE(rule.body[1].pattern_length == 5 && !memcmp(rule.body[1].pattern, "alice", 5));
    MC_OK(maelys_datalog_program_rule(program, 1, &rule));
    MC_REQUIRE(rule.source.line == 3 && rule.source.column == 1);

    maelys_datalog_public_fact_t facts[] = {candidate("alice"), candidate("bob"),
                                            candidate("carol")};
    maelys_datalog_result_t *r;
    MC_OK(maelys_datalog_session_solve(s, facts, 3, &r, NULL));
    for (size_t i = 0; i < 3; ++i) {
        int present = -1;
        MC_OK(maelys_datalog_result_query(r, "allow", facts[i].terms, 1, &present));
        MC_REQUIRE(present == (i != 1));
    }
    size_t count = 0;
    MC_OK(maelys_datalog_result_enumerate(r, "allow", 1, NULL, 0, &count));
    MC_REQUIRE(count == 2);
    char proof[8192];
    size_t required = 0;
    MC_OK(maelys_datalog_result_explain_true_text(r, "allow", facts[0].terms, 1, proof,
                                                  sizeof(proof), &required));
    MC_REQUIRE(strstr(proof, e.filters->semantic_id));
    MC_OK(maelys_datalog_result_explain_false_text(r, "allow", facts[1].terms, 1, proof,
                                                   sizeof(proof), &required));
    MC_REQUIRE(strstr(proof, e.filters->semantic_id));
    MC_OK(maelys_datalog_result_free(r));
    MC_OK(maelys_datalog_session_free(s));
    puts("bundle/end_to_end: selection, IR locations, copied source, solve, proofs and lifetime "
         "PASS");
    return 0;
}
static int dependency_rejection(void) {
    for (int incompatible = 0; incompatible < 2; ++incompatible) {
        maelys_datalog_extension_t e = example_bundle_extension();
        maelys_datalog_filter_module_t wrong = *e.filters;
        wrong.semantic_id = "example.incompatible.v1";
        e.filters = incompatible ? &wrong : NULL;
        e.filter_count = incompatible ? 1 : 0;
        maelys_datalog_context_t *c;
        MC_REQUIRE(!context(&e, &c));
        const char *source = "permit \"alice\"";
        maelys_datalog_policy_t *p = NULL;
        MC_REQUIRE(maelys_datalog_context_load_inline(c, "permit", "bundle_example", "missing",
                                                      source, strlen(source), &p,
                                                      NULL) == MAELYS_DATALOG_STATUS_UNSUPPORTED);
        MC_REQUIRE(!p);
        MC_OK(maelys_datalog_context_free(c));
    }
    puts("bundle/dependencies: missing filter and incompatible semantic ID rejected PASS");
    return 0;
}
static int atomic_registration(void) {
    maelys_datalog_extension_t e = example_bundle_extension();
    maelys_datalog_frontend_t bad = *e.frontends;
    bad.abi_version = UINT32_MAX;
    e.frontends = &bad;
    maelys_datalog_context_t *c;
    MC_OK(maelys_datalog_context_create(&c));
    size_t filters_before, frontends_before, n;
    MC_OK(maelys_datalog_context_component_count(c, MAELYS_DATALOG_EXTENSION_FILTER,
                                                 &filters_before));
    MC_OK(maelys_datalog_context_component_count(c, MAELYS_DATALOG_EXTENSION_FRONTEND,
                                                 &frontends_before));
    MC_REQUIRE(maelys_datalog_context_register(c, &e) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    MC_OK(maelys_datalog_context_component_count(c, MAELYS_DATALOG_EXTENSION_FILTER, &n));
    MC_REQUIRE(n == filters_before);
    MC_OK(maelys_datalog_context_component_count(c, MAELYS_DATALOG_EXTENSION_FRONTEND, &n));
    MC_REQUIRE(n == frontends_before);
    e = example_bundle_extension();
    MC_OK(maelys_datalog_context_register(c, &e)); /* Package name was not reserved. */
    MC_OK(maelys_datalog_context_component_count(c, MAELYS_DATALOG_EXTENSION_FILTER, &n));
    MC_REQUIRE(n == filters_before + 1);
    MC_OK(maelys_datalog_context_component_count(c, MAELYS_DATALOG_EXTENSION_FRONTEND, &n));
    MC_REQUIRE(n == frontends_before + 1);
    MC_OK(maelys_datalog_context_free(c));
    puts("bundle/registration: all-or-nothing publication and retry PASS");
    return 0;
}
static int syntax_and_validation(void) {
    maelys_datalog_extension_t e = example_bundle_extension();
    maelys_datalog_context_t *c;
    MC_REQUIRE(!context(&e, &c));
    const char *bad[] = {"permit",
                         "permitfoo \"alice\"",
                         "permit alice",
                         "permit \"alice",
                         "permit \"alice\" extra",
                         "permit \"a\\b\"",
                         "permit \"alice\"\npermit bob"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        maelys_datalog_policy_t *p = NULL;
        maelys_datalog_public_diagnostic_t diag;
        MC_REQUIRE(maelys_datalog_context_load_inline(c, "permit", "bundle_example", "syntax",
                                                      bad[i], strlen(bad[i]), &p, &diag) ==
                   MAELYS_DATALOG_STATUS_INVALID_FIELD);
        MC_REQUIRE(!p && diag.line == (i == 6 ? 2u : 1u) && diag.column > 0);
        MC_REQUIRE(!strcmp(diag.phase, "permit"));
    }
    maelys_datalog_policy_t *p = NULL;
    maelys_datalog_public_diagnostic_t diag;
    const char *source = "permit \"alice\"";
    MC_REQUIRE(maelys_datalog_context_load_inline(c, "permit", "bundle_bad_domain", "invalid_ir",
                                                  source, strlen(source), &p,
                                                  &diag) == MAELYS_DATALOG_STATUS_INVALID_FIELD);
    MC_REQUIRE(!p && diag.code == MAELYS_DATALOG_DIAG_MALFORMED_PROGRAM);
    MC_OK(maelys_datalog_context_free(c));
    puts("bundle/validation: syntax, atomic load errors and mandatory IR validation PASS");
    return 0;
}
int main(void) {
    const maelys_datalog_public_predicate_t predicates[] = {
        {"candidate", 1, MAELYS_DATALOG_PREDICATE_EDB},
        {"allow", 1, MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY}};
    const maelys_datalog_public_domain_t domain = {"bundle_example", predicates, 2, NULL, 0};
    MC_OK(maelys_datalog_domain_register(&domain));
    const maelys_datalog_public_predicate_t wrong[] = {
        {"candidate", 1, MAELYS_DATALOG_PREDICATE_EDB}, {"allow", 1, MAELYS_DATALOG_PREDICATE_EDB}};
    const maelys_datalog_public_domain_t invalid = {"bundle_bad_domain", wrong, 2, NULL, 0};
    MC_OK(maelys_datalog_domain_register(&invalid));
    maelys_datalog_extension_t e = example_bundle_extension();
    const maelys_conformance_filter_case_t cases[] = {
        {(const unsigned char *)"alice", (const unsigned char *)"alice", 5, 5, 0, 0, 0, 1},
        {(const unsigned char *)"bob", (const unsigned char *)"alice", 3, 5, 0, 0, 0, 0},
        {NULL, NULL, 0, 0, 0, 0, 0, 1},
        {NULL, NULL, 0, 1, MAELYS_DATALOG_STATUS_INVALID_ARGUMENT, 0, 0, 0},
        {NULL, (const unsigned char *)"x", 1, 1, 0, 0, MAELYS_DATALOG_STATUS_INVALID_ARGUMENT, 0},
        {NULL, (const unsigned char *)"", 0, SIZE_MAX, 0, MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE,
         0, 0}};
    MC_REQUIRE(!maelys_conformance_filter(e.filters, cases, sizeof(cases) / sizeof(cases[0])));
    MC_REQUIRE(!end_to_end());
    MC_REQUIRE(!dependency_rejection());
    MC_REQUIRE(!atomic_registration());
    MC_REQUIRE(!syntax_and_validation());
    puts("bundle: frontend + filter conformance PASS");
    return 0;
}
