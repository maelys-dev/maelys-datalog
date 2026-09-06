/* SPDX-License-Identifier: MPL-2.0 */
#include <maelys/datalog_backend.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
#define OK(x) CHECK((x) == MAELYS_DATALOG_STATUS_OK)
extern const maelys_datalog_frontend_t *example_arrow_frontend(void);
extern const maelys_datalog_backend_t *example_naive_backend(void);

static maelys_datalog_public_value_t symbol(const char *s) {
    maelys_datalog_public_value_t v = {0};
    v.kind = MAELYS_DATALOG_VALUE_SYMBOL;
    v.as.symbol = s;
    return v;
}
static maelys_datalog_public_fact_t fact(const char *p, const char *value) {
    maelys_datalog_public_fact_t f = {0};
    f.predicate = p;
    f.arity = 1u;
    f.terms[0] = symbol(value);
    return f;
}
static maelys_datalog_ir_atom_t atom(const char *p, uint32_t variable) {
    maelys_datalog_ir_atom_t a = {0};
    a.predicate = p;
    a.arity = 1u;
    a.terms[0].kind = MAELYS_DATALOG_IR_VARIABLE;
    a.terms[0].as.variable = variable;
    return a;
}
static maelys_datalog_session_options_t options(const maelys_datalog_backend_t *b) {
    maelys_datalog_session_options_t o = {MAELYS_DATALOG_BACKEND_ABI_VERSION, sizeof(o), b, 0, 0};
    return o;
}
static maelys_datalog_status_t load(const char *source, const maelys_datalog_frontend_t *frontend,
                                    maelys_datalog_policy_t **out,
                                    maelys_datalog_public_diagnostic_t *diag) {
    return maelys_datalog_policy_load_frontend("compiler", "compiler.test", source, strlen(source),
                                               frontend, out, diag);
}
static int setup(void) {
    const maelys_datalog_public_predicate_t predicates[] = {
        {"seed", 1, MAELYS_DATALOG_PREDICATE_EDB},
        {"extra", 1, MAELYS_DATALOG_PREDICATE_EDB},
        {"edge", 2, MAELYS_DATALOG_PREDICATE_EDB},
        {"blocked", 1, MAELYS_DATALOG_PREDICATE_EDB},
        {"base", 1, MAELYS_DATALOG_PREDICATE_POLICY_FACT | MAELYS_DATALOG_PREDICATE_QUERY},
        {"allow", 1, MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY},
        {"reach", 2, MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY},
        {"aux", 1, MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY},
        {"helper", 1, MAELYS_DATALOG_PREDICATE_IDB}};
    const char *const atoms[] = {"alice", "bob", "carol"};
    maelys_datalog_public_domain_t d = {"compiler", predicates,
                                        sizeof(predicates) / sizeof(predicates[0]), atoms, 3};
    OK(maelys_datalog_domain_register(&d));
    d.name = "compiler_other";
    OK(maelys_datalog_domain_register(&d));
    return 0;
}
static int frontend_and_backend_cross_product(void) {
    const maelys_datalog_frontend_t *frontends[] = {maelys_datalog_frontend_datalog(),
                                                    example_arrow_frontend()};
    const char *sources[] = {"\nallow(X) :- seed(X).\n", "# custom language\nallow <- seed\n"};
    const maelys_datalog_backend_t *backends[] = {maelys_datalog_backend_reference(),
                                                  example_naive_backend()};
    char execution[2][65];
    for (size_t i = 0; i < 2; ++i) {
        maelys_datalog_policy_t *policy = NULL;
        OK(load(sources[i], frontends[i], &policy, NULL));
        maelys_datalog_session_t *sessions[2];
        for (size_t j = 0; j < 2; ++j) {
            maelys_datalog_session_options_t o = options(backends[j]);
            OK(maelys_datalog_session_create_ex(policy, 0u, &o, &sessions[j]));
        }
        OK(maelys_datalog_policy_free(policy));
        for (size_t j = 0; j < 2; ++j) {
            const maelys_datalog_program_t *program;
            OK(maelys_datalog_session_program(sessions[j], &program));
            maelys_datalog_ir_rule_t rule;
            OK(maelys_datalog_program_rule(program, 0u, &rule));
            CHECK(rule.source.line == 2u && rule.source.column == 1u);
            CHECK(!strcmp(rule.head.predicate, "allow") && rule.body_count == 1);
            maelys_datalog_program_info_t info;
            OK(maelys_datalog_program_info(program, &info));
            CHECK(info.required_capabilities == MAELYS_DATALOG_CAP_POSITIVE);
            for (size_t k = 0; k < info.predicate_count; ++k) {
                maelys_datalog_public_predicate_t pred;
                OK(maelys_datalog_program_predicate(program, k, &pred));
                CHECK(pred.name && pred.arity <= 4u);
            }
            maelys_datalog_public_predicate_t sentinel = {"sentinel", 77u, 0};
            CHECK(maelys_datalog_program_predicate(program, info.predicate_count, &sentinel) ==
                  MAELYS_DATALOG_STATUS_NOT_FOUND);
            CHECK(sentinel.arity == 77u);
            for (size_t repeat = 0; repeat < 2; ++repeat) {
                char input_text[] = "alice";
                maelys_datalog_public_fact_t input = fact("seed", input_text);
                maelys_datalog_result_t *result = NULL;
                OK(maelys_datalog_session_solve(sessions[j], &input, 1u, &result, NULL));
                memset(input_text, 'x', 5u); /* result doesn't borrow source/EDB */
                CHECK(maelys_datalog_session_free(sessions[j]) ==
                      MAELYS_DATALOG_STATUS_INVALID_STATE);
                maelys_datalog_result_t *blocked_result = (void *)(uintptr_t)1;
                CHECK(maelys_datalog_session_solve(sessions[j], NULL, 0u, &blocked_result, NULL) ==
                      MAELYS_DATALOG_STATUS_INVALID_STATE);
                CHECK(blocked_result == NULL);
                int present = -1;
                maelys_datalog_public_value_t value = symbol("alice");
                OK(maelys_datalog_result_query(result, "allow", &value, 1u, &present));
                CHECK(present == 1);
                maelys_datalog_public_fact_view_t f;
                size_t count = 0;
                OK(maelys_datalog_result_enumerate(result, "allow", 1u, &f, 1u, &count));
                CHECK(count == 1);
                const char *text;
                size_t length;
                OK(maelys_datalog_result_symbol_text(result, f.terms[0].as.symbol_id, &text,
                                                     &length));
                CHECK(length == 5u && !strcmp(text, "alice"));
                size_t required = 777u;
                maelys_datalog_status_t rc = maelys_datalog_result_explain_true_text(
                    result, "allow", &value, 1u, NULL, 0u, &required);
                if (j == 0)
                    CHECK(rc == MAELYS_DATALOG_STATUS_OK && required > 0);
                else
                    CHECK(rc == MAELYS_DATALOG_STATUS_UNSUPPORTED && required == 777u);
                OK(maelys_datalog_result_free(result));
            }
            OK(maelys_datalog_session_execution_fingerprint(sessions[j], execution[j]));
            OK(maelys_datalog_session_free(sessions[j]));
        }
        CHECK(strcmp(execution[0], execution[1]));
    }
    return 0;
}

static maelys_datalog_status_t fixture_lower(const char *source, size_t length,
                                             maelys_datalog_program_builder_t *b,
                                             maelys_datalog_public_diagnostic_t *diag) {
    (void)length;
    (void)diag;
    maelys_datalog_ir_rule_t r = {0};
    r.head = atom("allow", 0);
    r.body_count = 1;
    r.body[0].kind = MAELYS_DATALOG_IR_ATOM;
    r.body[0].atom = atom("seed", 0);
    r.source = (maelys_datalog_source_location_t){3, 2};
    if (!strcmp(source, "expr_expansion")) {
        r.body_count = 2;
        r.body[1].kind = MAELYS_DATALOG_IR_COMPARISON;
        r.body[1].comparison = MAELYS_DATALOG_IR_EQ;
        r.body[1].has_arithmetic = 1;
        r.body[1].lhs_expression = r.body[1].rhs_expression = 31;
        r.expression_count = 32;
        r.expressions[0].kind = MAELYS_DATALOG_IR_EXPR_INTEGER;
        r.expressions[0].term.kind = MAELYS_DATALOG_IR_INTEGER;
        for (size_t i = 1; i < r.expression_count; ++i) {
            r.expressions[i].kind = MAELYS_DATALOG_IR_EXPR_ADD;
            r.expressions[i].left = r.expressions[i].right = (uint32_t)i - 1u;
        }
    } else if (!strcmp(source, "unsafe"))
        r.head.terms[0].as.variable = 1;
    else if (!strcmp(source, "base_head"))
        r.head.predicate = "seed";
    else if (!strcmp(source, "unknown"))
        r.body[0].atom.predicate = "absent";
    else if (!strcmp(source, "wide_variable"))
        r.head.terms[0].as.variable = UINT32_MAX;
    else if (!strcmp(source, "unknown_atom")) {
        r.head.terms[0].kind = MAELYS_DATALOG_IR_SYMBOL;
        r.head.terms[0].as.symbol = "not_declared";
    } else if (!strcmp(source, "bad_bool")) {
        r.head.terms[0].kind = MAELYS_DATALOG_IR_BOOLEAN;
        r.head.terms[0].as.boolean = 2;
    } else if (!strcmp(source, "wide_integer")) {
        r.head.terms[0].kind = MAELYS_DATALOG_IR_INTEGER;
        r.head.terms[0].as.integer = INT64_MAX;
    } else if (!strcmp(source, "wide_arity"))
        r.head.arity = SIZE_MAX;
    else if (!strcmp(source, "wide_body"))
        r.body_count = SIZE_MAX;
    else if (!strcmp(source, "unknown_literal"))
        r.body[0].kind = (maelys_datalog_ir_literal_kind_t)99;
    else if (!strcmp(source, "empty_body"))
        r.body_count = 0;
    else if (!strcmp(source, "fact_edb") || !strcmp(source, "fact_variable")) {
        maelys_datalog_ir_atom_t a = atom(!strcmp(source, "fact_edb") ? "seed" : "base", 0);
        if (!strcmp(source, "fact_edb")) {
            a.terms[0].kind = MAELYS_DATALOG_IR_SYMBOL;
            a.terms[0].as.symbol = "alice";
        }
        (void)maelys_datalog_program_add_fact(b, &a);
    } else if (!strcmp(source, "unsafe_negation") || !strcmp(source, "negative_cycle")) {
        r.body_count = 2;
        r.body[1].kind = MAELYS_DATALOG_IR_NEGATION;
        r.body[1].atom = atom("aux", !strcmp(source, "unsafe_negation") ? 1u : 0u);
        if (!strcmp(source, "negative_cycle")) {
            (void)maelys_datalog_program_add_rule(b, &r);
            r.head.predicate = "aux";
            r.body[1].atom.predicate = "allow";
        }
    } else if (!strcmp(source, "expr_cycle") || !strcmp(source, "expr_index")) {
        r.body_count = 2;
        r.body[1].kind = MAELYS_DATALOG_IR_COMPARISON;
        r.body[1].comparison = MAELYS_DATALOG_IR_EQ;
        r.body[1].has_arithmetic = 1;
        r.expression_count = 1;
        r.expressions[0].kind = MAELYS_DATALOG_IR_EXPR_ADD;
        if (!strcmp(source, "expr_index"))
            r.body[1].lhs_expression = UINT32_MAX;
    } else if (!strcmp(source, "missing_filter") || !strcmp(source, "wrong_filter_version") ||
               !strcmp(source, "filter_type")) {
        r.body_count = 2;
        maelys_datalog_ir_literal_t *l = &r.body[1];
        l->kind = MAELYS_DATALOG_IR_FILTER;
        l->filter_name = !strcmp(source, "missing_filter") ? "missing" : "starts_with";
        if (!strcmp(source, "wrong_filter_version"))
            l->filter_semantic_id = "wrong.v1";
        l->pattern = (const unsigned char *)"a";
        l->pattern_length = 1;
        l->filter_value.kind =
            !strcmp(source, "filter_type") ? MAELYS_DATALOG_IR_INTEGER : MAELYS_DATALOG_IR_VARIABLE;
    }
    /* Intentionally swallow builder errors: the host must still fail closed. */
    (void)maelys_datalog_program_add_rule(b, &r);
    return MAELYS_DATALOG_STATUS_OK;
}
static const maelys_datalog_frontend_t fixture = {1u, sizeof(fixture), "fixture", "test.fixture.v1",
                                                  fixture_lower};
static int common_validation(void) {
    const char *invalid[] = {
        "unsafe",     "base_head",    "unknown",        "wide_variable",   "unknown_atom",
        "bad_bool",   "wide_integer", "wide_arity",     "wide_body",       "unknown_literal",
        "empty_body", "fact_edb",     "fact_variable",  "unsafe_negation", "negative_cycle",
        "expr_cycle", "expr_index",   "expr_expansion", "missing_filter",  "wrong_filter_version",
        "filter_type"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        maelys_datalog_policy_t *p = (void *)(uintptr_t)1;
        maelys_datalog_public_diagnostic_t d;
        CHECK(load(invalid[i], &fixture, &p, &d) != MAELYS_DATALOG_STATUS_OK);
        CHECK(p == NULL && d.source == MAELYS_DATALOG_DIAGNOSTIC_LOAD);
        if (!strcmp(invalid[i], "unsafe"))
            CHECK(d.line == 3 && d.column == 2);
    }
    maelys_datalog_policy_t *p = NULL;
    maelys_datalog_public_diagnostic_t d;
    CHECK(load("# comment\nallow ? seed\n", example_arrow_frontend(), &p, &d) ==
          MAELYS_DATALOG_STATUS_INVALID_FIELD);
    CHECK(p == NULL && d.line == 2 && d.column == 1 && !strcmp(d.phase, "arrow"));
    OK(load("allow <- seed", example_arrow_frontend(), &p, &d));
    OK(maelys_datalog_policy_free(p));
    return 0;
}
static int capabilities_and_identity(void) {
    const char *sources[] = {"allow(X) :- seed(X), not(blocked(X)).", "allow(X) :- seed(X), X = X.",
                             "allow(X) :- seed(X), X + 1 > 0.",
                             "allow(X) :- seed(X), starts_with(X, \"a\")."};
    const uint64_t features[] = {MAELYS_DATALOG_CAP_NEGATION, MAELYS_DATALOG_CAP_COMPARISONS,
                                 MAELYS_DATALOG_CAP_ARITHMETIC, MAELYS_DATALOG_CAP_FILTERS};
    for (size_t i = 0; i < 4; ++i) {
        maelys_datalog_policy_t *p;
        maelys_datalog_session_t *s = NULL;
        OK(load(sources[i], NULL, &p, NULL));
        maelys_datalog_session_options_t o = options(example_naive_backend());
        CHECK(maelys_datalog_session_create_ex(p, 0, &o, &s) == MAELYS_DATALOG_STATUS_UNSUPPORTED);
        CHECK(s == NULL);
        OK(maelys_datalog_session_create(p, 0, &s));
        const maelys_datalog_program_t *program;
        maelys_datalog_program_info_t info;
        OK(maelys_datalog_session_program(s, &program));
        OK(maelys_datalog_program_info(program, &info));
        CHECK(info.required_capabilities & features[i]);
        maelys_datalog_ir_rule_t r;
        OK(maelys_datalog_program_rule(program, 0, &r));
        if (i == 2)
            CHECK(r.expression_count > 0 && r.body[1].has_arithmetic);
        if (i == 3)
            CHECK(!strcmp(r.body[1].filter_name, "starts_with") && r.body[1].pattern_length == 1);
        OK(maelys_datalog_session_free(s));
        OK(maelys_datalog_policy_free(p));
    }
    maelys_datalog_frontend_t f = *example_arrow_frontend();
    char hashes[2][65];
    for (size_t i = 0; i < 2; ++i) {
        if (i)
            f.semantic_id = "example.arrow.v2";
        maelys_datalog_policy_t *p;
        OK(load("allow <- seed", &f, &p, NULL));
        OK(maelys_datalog_policy_fingerprint(p, hashes[i]));
        OK(maelys_datalog_policy_free(p));
    }
    CHECK(strcmp(hashes[0], hashes[1]));
    maelys_datalog_policy_t *p;
    OK(load("allow(X) :- seed(X).", NULL, &p, NULL));
    maelys_datalog_session_options_t o = options(example_naive_backend());
    o.required_capabilities = MAELYS_DATALOG_CAP_EXPLAIN_TRUE;
    maelys_datalog_session_t *s = NULL;
    CHECK(maelys_datalog_session_create_ex(p, 0, &o, &s) == MAELYS_DATALOG_STATUS_UNSUPPORTED);
    o = options(maelys_datalog_backend_reference());
    o.work_limit = 1;
    CHECK(maelys_datalog_session_create_ex(p, 0, &o, &s) == MAELYS_DATALOG_STATUS_UNSUPPORTED);
    o = options(example_naive_backend());
    o.work_limit = 1;
    OK(maelys_datalog_session_create_ex(p, 0, &o, &s));
    maelys_datalog_public_fact_t input = fact("seed", "alice");
    maelys_datalog_result_t *result = NULL;
    CHECK(maelys_datalog_session_solve(s, &input, 1, &result, NULL) ==
          MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
    CHECK(result == NULL);
    OK(maelys_datalog_session_free(s));
    OK(maelys_datalog_policy_free(p));
    return 0;
}
static int graph_differential(void) {
    const char *source = "reach(X,Y) :- edge(X,Y). reach(X,Z) :- reach(X,Y), edge(Y,Z). "
                         "allow(Y) :- seed(X), reach(X,Y). helper(X) :- edge(X,X).";
    maelys_datalog_policy_t *p;
    OK(load(source, NULL, &p, NULL));
    maelys_datalog_session_t *sessions[2];
    OK(maelys_datalog_session_create(p, 0, &sessions[0]));
    maelys_datalog_session_options_t o = options(example_naive_backend());
    OK(maelys_datalog_session_create_ex(p, 0, &o, &sessions[1]));
    OK(maelys_datalog_policy_free(p));
    uint32_t random = 12345;
    const char *nodes[] = {"n0", "n1", "n2", "n3", "n4", "n5"};
    for (size_t trial = 0; trial < 20; ++trial) {
        int reachable[6][6] = {{0}};
        maelys_datalog_public_fact_t inputs[40];
        size_t count = 0;
        inputs[count++] = fact("seed", "n0");
        for (size_t a = 0; a < 6; ++a)
            for (size_t b = 0; b < 6; ++b) {
                random = random * 1664525u + 1013904223u;
                if (trial == 0 || (random >> 29u) < 3u) {
                    maelys_datalog_public_fact_t f = fact("edge", nodes[a]);
                    f.arity = 2;
                    f.terms[1] = symbol(nodes[b]);
                    inputs[count++] = f;
                    reachable[a][b] = 1;
                }
            }
        for (size_t k = 0; k < 6; ++k)
            for (size_t a = 0; a < 6; ++a)
                for (size_t b = 0; b < 6; ++b)
                    reachable[a][b] |= reachable[a][k] && reachable[k][b];
        maelys_datalog_public_fact_view_t views[2][36];
        size_t totals[2];
        for (size_t backend = 0; backend < 2; ++backend) {
            if (backend)
                for (size_t i = 0; i < count / 2; ++i) {
                    maelys_datalog_public_fact_t tmp = inputs[i];
                    inputs[i] = inputs[count - 1 - i];
                    inputs[count - 1 - i] = tmp;
                }
            maelys_datalog_result_t *result;
            OK(maelys_datalog_session_solve(sessions[backend], inputs, count, &result, NULL));
            memset(views[backend], 0, sizeof(views[backend]));
            OK(maelys_datalog_result_enumerate(result, "reach", 2, views[backend], 36,
                                               &totals[backend]));
            for (size_t a = 0; a < 6; ++a)
                for (size_t b = 0; b < 6; ++b) {
                    maelys_datalog_public_value_t query[] = {symbol(nodes[a]), symbol(nodes[b])};
                    int present;
                    OK(maelys_datalog_result_query(result, "reach", query, 2, &present));
                    CHECK(present == reachable[a][b]);
                }
            size_t ignored;
            CHECK(maelys_datalog_result_enumerate(result, "helper", 1, NULL, 0, &ignored) ==
                  MAELYS_DATALOG_STATUS_INVALID_FIELD);
            OK(maelys_datalog_result_free(result));
        }
        CHECK(totals[0] == totals[1] &&
              !memcmp(views[0], views[1], totals[0] * sizeof(views[0][0])));
    }
    OK(maelys_datalog_session_free(sessions[0]));
    OK(maelys_datalog_session_free(sessions[1]));
    return 0;
}

static int compiled_identity(void) {
    const char *source = "allow(X) :- seed(X).";
    char compiled[2][65], execution[4][65], authority[2][65];
    for (size_t i = 0; i < 2; ++i) {
        maelys_datalog_policy_t *p;
        maelys_datalog_session_t *s;
        OK(maelys_datalog_policy_load_frontend(i ? "compiler_other" : "compiler", "compiler.test",
                                               source, strlen(source), NULL, &p, NULL));
        OK(maelys_datalog_session_create(p, 0, &s));
        const maelys_datalog_program_t *program;
        OK(maelys_datalog_session_program(s, &program));
        OK(maelys_datalog_program_fingerprint(program, compiled[i]));
        OK(maelys_datalog_session_fingerprint(s, authority[i]));
        OK(maelys_datalog_session_execution_fingerprint(s, execution[i]));
        OK(maelys_datalog_session_free(s));
        OK(maelys_datalog_policy_free(p));
    }
    CHECK(!strcmp(authority[0], authority[1])); /* Legacy source authority is preserved. */
    CHECK(strcmp(compiled[0], compiled[1]) && strcmp(execution[0], execution[1]));
    maelys_datalog_policy_t *p;
    OK(load(source, NULL, &p, NULL));
    maelys_datalog_backend_t b = *example_naive_backend();
    for (size_t i = 0; i < 4; ++i) {
        b.semantic_id = i >= 2 ? "example.naive.v2" : "example.naive.v1";
        maelys_datalog_session_options_t o = options(&b);
        o.work_limit = i == 3 ? 100 : 200;
        maelys_datalog_session_t *s;
        OK(maelys_datalog_session_create_ex(p, 0, &o, &s));
        OK(maelys_datalog_session_execution_fingerprint(s, execution[i]));
        OK(maelys_datalog_session_free(s));
    }
    CHECK(!strcmp(execution[0], execution[1]));
    CHECK(strcmp(execution[1], execution[2]) && strcmp(execution[2], execution[3]));
    OK(maelys_datalog_policy_free(p));
    char name[64], id[128];
    memset(name, 'a', sizeof(name));
    name[63] = 0;
    memset(id, 'b', sizeof(id));
    id[127] = 0;
    maelys_datalog_frontend_t f = *example_arrow_frontend();
    f.name = name;
    f.semantic_id = id;
    OK(load("allow <- seed", &f, &p, NULL));
    OK(maelys_datalog_policy_free(p));
    return 0;
}

static const maelys_datalog_program_t *roundtrip_program;
static maelys_datalog_status_t roundtrip_lower(const char *source, size_t length,
                                               maelys_datalog_program_builder_t *builder,
                                               maelys_datalog_public_diagnostic_t *diag) {
    (void)source;
    (void)length;
    (void)diag;
    maelys_datalog_program_info_t info;
    maelys_datalog_status_t rc = maelys_datalog_program_info(roundtrip_program, &info);
    if (rc)
        return rc;
    for (size_t i = 0; i < info.fact_count; ++i) {
        maelys_datalog_ir_atom_t f;
        rc = maelys_datalog_program_fact(roundtrip_program, i, &f);
        if (!rc)
            rc = maelys_datalog_program_add_fact(builder, &f);
        if (rc)
            return rc;
    }
    for (size_t i = 0; i < info.rule_count; ++i) {
        maelys_datalog_ir_rule_t r;
        rc = maelys_datalog_program_rule(roundtrip_program, i, &r);
        if (!rc)
            rc = maelys_datalog_program_add_rule(builder, &r);
        if (rc)
            return rc;
    }
    return MAELYS_DATALOG_STATUS_OK;
}
static int ir_roundtrip(void) {
    const char *sources[] = {"base(\"alice\"). base(7). base(true). allow(X) :- base(X), seed(X).",
                             "base(\"alice\"). allow(X) :- base(X), seed(N), N + 1 > 2, "
                             "not(blocked(X)), starts_with(X, \"a\")."};
    const maelys_datalog_frontend_t f = {1, sizeof(f), "roundtrip", "test.roundtrip.v1",
                                         roundtrip_lower};
    for (size_t i = 0; i < 2; ++i) {
        maelys_datalog_policy_t *p;
        maelys_datalog_session_t *original, *copy;
        maelys_datalog_public_diagnostic_t diagnostic;
        maelys_datalog_status_t loaded = load(sources[i], NULL, &p, &diagnostic);
        if (loaded)
            fprintf(stderr, "roundtrip %zu: %s (%s)\n", i, diagnostic.message, diagnostic.hint);
        OK(loaded);
        OK(maelys_datalog_session_create(p, 0, &original));
        OK(maelys_datalog_policy_free(p));
        OK(maelys_datalog_session_program(original, &roundtrip_program));
        OK(load("copied typed IR", &f, &p, NULL));
        maelys_datalog_session_options_t o = options(i ? NULL : example_naive_backend());
        OK(maelys_datalog_session_create_ex(p, 0, &o, &copy));
        OK(maelys_datalog_policy_free(p));
        OK(maelys_datalog_session_free(original));
        roundtrip_program = NULL;
        maelys_datalog_public_fact_t inputs[] = {fact("seed", "alice"), fact("seed", "alice"),
                                                 fact("seed", "alice")};
        inputs[1].terms[0].kind = MAELYS_DATALOG_VALUE_INTEGER;
        inputs[1].terms[0].as.integer = 7;
        inputs[2].terms[0].kind = MAELYS_DATALOG_VALUE_BOOLEAN;
        inputs[2].terms[0].as.boolean = 1;
        maelys_datalog_result_t *result;
        OK(maelys_datalog_session_solve(copy, i ? &inputs[1] : inputs, i ? 1 : 3, &result, NULL));
        size_t count;
        OK(maelys_datalog_result_enumerate(result, "allow", 1, NULL, 0, &count));
        CHECK(count == (i ? 1u : 3u));
        int present;
        maelys_datalog_public_value_t value = symbol("alice");
        OK(maelys_datalog_result_query(result, "base", &value, 1, &present));
        CHECK(present);
        OK(maelys_datalog_result_free(result));
        OK(maelys_datalog_session_free(copy));
    }
    return 0;
}

static size_t destroys, result_destroys;
static maelys_datalog_status_t fake_prepare(const maelys_datalog_program_t *p, void **out) {
    (void)p;
    *out = malloc(1);
    return *out ? MAELYS_DATALOG_STATUS_OK : MAELYS_DATALOG_STATUS_INTERNAL;
}
static maelys_datalog_status_t failed_prepare(const maelys_datalog_program_t *p, void **out) {
    maelys_datalog_status_t rc = fake_prepare(p, out);
    return rc ? rc : MAELYS_DATALOG_STATUS_UNSUPPORTED;
}
static void fake_destroy(void *state) {
    ++destroys;
    free(state);
}
static void fake_destroy_result(void *state, void *result) {
    (void)state;
    ++result_destroys;
    free(result);
}
static maelys_datalog_status_t bad_emit(void *state, const maelys_datalog_public_fact_t *facts,
                                        size_t count, maelys_datalog_backend_output_t *out,
                                        void **result, maelys_datalog_public_diagnostic_t *diag) {
    (void)state;
    (void)facts;
    (void)count;
    (void)diag;
    *result = malloc(1);
    maelys_datalog_public_fact_t f = fact("seed", "alice");
    (void)maelys_datalog_backend_emit(out, &f); /* forbidden base predicate */
    return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t ignored_budget(void *state,
                                              const maelys_datalog_public_fact_t *facts,
                                              size_t count, maelys_datalog_backend_output_t *out,
                                              void **result,
                                              maelys_datalog_public_diagnostic_t *diag) {
    (void)state;
    (void)facts;
    (void)count;
    (void)diag;
    (void)result;
    (void)maelys_datalog_backend_charge(out, UINT64_MAX);
    return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t partial_failure(void *state,
                                               const maelys_datalog_public_fact_t *facts,
                                               size_t count, maelys_datalog_backend_output_t *out,
                                               void **result,
                                               maelys_datalog_public_diagnostic_t *diag) {
    (void)state;
    (void)facts;
    (void)count;
    (void)diag;
    *result = malloc(1);
    maelys_datalog_public_fact_t f = fact("allow", "alice");
    (void)maelys_datalog_backend_emit(out, &f);
    return (maelys_datalog_status_t)1234;
}
static maelys_datalog_status_t forged_symbol(void *state, const maelys_datalog_public_fact_t *facts,
                                             size_t count, maelys_datalog_backend_output_t *out,
                                             void **result,
                                             maelys_datalog_public_diagnostic_t *diag) {
    (void)state;
    (void)facts;
    (void)count;
    (void)diag;
    (void)result;
    maelys_datalog_public_fact_t f = fact("allow", "unavailable");
    (void)maelys_datalog_backend_emit(out, &f);
    return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_backend_t fake_backend(void) {
    maelys_datalog_backend_t b = {1,
                                  sizeof(b),
                                  "fake",
                                  "test.fake.v1",
                                  MAELYS_DATALOG_CAP_POSITIVE | MAELYS_DATALOG_CAP_WORK_LIMIT,
                                  fake_prepare,
                                  bad_emit,
                                  NULL,
                                  fake_destroy_result,
                                  fake_destroy};
    return b;
}
static int filter_mode;
static maelys_datalog_status_t filter_prepare(const maelys_datalog_program_t *p, void **out) {
    maelys_datalog_ir_rule_t *r = calloc(1, sizeof(*r));
    *out = r;
    return r ? maelys_datalog_program_rule(p, 0, r) : MAELYS_DATALOG_STATUS_INTERNAL;
}
static maelys_datalog_status_t filter_solve(void *state, const maelys_datalog_public_fact_t *facts,
                                            size_t count, maelys_datalog_backend_output_t *out,
                                            void **result,
                                            maelys_datalog_public_diagnostic_t *diag) {
    (void)result;
    (void)diag;
    const maelys_datalog_ir_rule_t *r = state;
    const maelys_datalog_ir_literal_t *l = &r->body[1];
    for (size_t i = 0; i < count; ++i) {
        const char *value = facts[i].terms[0].as.symbol;
        int matched = 0;
        maelys_datalog_status_t rc = maelys_datalog_backend_filter(
            out, l->filter_name, filter_mode == 1 ? "wrong.v1" : l->filter_semantic_id,
            (const unsigned char *)value, strlen(value),
            filter_mode == 2 ? (const unsigned char *)"z" : l->pattern, l->pattern_length,
            &matched);
        if (rc)
            return MAELYS_DATALOG_STATUS_OK; /* Host must remember the error. */
        if (matched) {
            maelys_datalog_public_fact_t derived = facts[i];
            derived.predicate = r->head.predicate;
            rc = maelys_datalog_backend_emit(out, &derived);
            if (rc)
                return rc;
            rc = maelys_datalog_backend_emit(out, &derived);
            if (rc)
                return rc; /* Dedup. */
        }
    }
    return MAELYS_DATALOG_STATUS_OK;
}
static int shared_filter_service(void) {
    maelys_datalog_policy_t *p;
    OK(load("allow(X) :- seed(X), starts_with(X, \"a\").", NULL, &p, NULL));
    maelys_datalog_backend_t b = fake_backend();
    b.prepare = filter_prepare;
    b.solve = filter_solve;
    b.capabilities = MAELYS_DATALOG_CAP_POSITIVE | MAELYS_DATALOG_CAP_FILTERS;
    maelys_datalog_session_options_t o = options(&b);
    maelys_datalog_session_t *s;
    OK(maelys_datalog_session_create_ex(p, 0, &o, &s));
    OK(maelys_datalog_policy_free(p));
    maelys_datalog_public_fact_t inputs[] = {fact("seed", "alice"), fact("seed", "bob")};
    const maelys_datalog_status_t expected[] = {MAELYS_DATALOG_STATUS_OK,
                                                MAELYS_DATALOG_STATUS_UNSUPPORTED,
                                                MAELYS_DATALOG_STATUS_INVALID_FIELD};
    for (filter_mode = 0; filter_mode < 3; ++filter_mode) {
        maelys_datalog_result_t *result = NULL;
        CHECK(maelys_datalog_session_solve(s, inputs, 2, &result, NULL) == expected[filter_mode]);
        if (!filter_mode) {
            size_t count;
            OK(maelys_datalog_result_enumerate(result, "allow", 1, NULL, 0, &count));
            CHECK(count == 1);
            OK(maelys_datalog_result_free(result));
        } else
            CHECK(result == NULL);
    }
    OK(maelys_datalog_session_free(s));
    return 0;
}
static int backend_failures_are_atomic(void) {
    maelys_datalog_policy_t *p;
    OK(load("allow(X) :- seed(X).", NULL, &p, NULL));
    maelys_datalog_backend_t b = fake_backend();
    maelys_datalog_session_options_t o = options(&b);
    maelys_datalog_session_t *s = NULL;
    b.prepare = failed_prepare;
    size_t before = destroys;
    CHECK(maelys_datalog_session_create_ex(p, 0, &o, &s) == MAELYS_DATALOG_STATUS_UNSUPPORTED);
    CHECK(s == NULL && destroys == before + 1);
    maelys_datalog_status_t (*callbacks[])(void *, const maelys_datalog_public_fact_t *, size_t,
                                           maelys_datalog_backend_output_t *, void **,
                                           maelys_datalog_public_diagnostic_t *) = {
        bad_emit, ignored_budget, partial_failure, forged_symbol};
    maelys_datalog_status_t expected[] = {
        MAELYS_DATALOG_STATUS_INVALID_FIELD, MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE,
        MAELYS_DATALOG_STATUS_INTERNAL, MAELYS_DATALOG_STATUS_INVALID_FIELD};
    for (size_t i = 0; i < 4; ++i) {
        b = fake_backend();
        b.solve = callbacks[i];
        OK(maelys_datalog_session_create_ex(p, 0, &o, &s));
        before = result_destroys;
        for (size_t n = 0; n < 2; ++n) {
            maelys_datalog_result_t *result = (void *)(uintptr_t)1;
            maelys_datalog_public_fact_t input = fact("seed", "alice");
            CHECK(maelys_datalog_session_solve(s, &input, 1u, &result, NULL) == expected[i]);
            CHECK(result == NULL);
        }
        CHECK(result_destroys == before + 2);
        OK(maelys_datalog_session_free(s));
    }
    OK(maelys_datalog_policy_free(p));
    return 0;
}
static int descriptor_validation(void) {
    maelys_datalog_policy_t *p = NULL;
    maelys_datalog_frontend_t f = *example_arrow_frontend();
    f.abi_version = 999u;
    CHECK(load("allow <- seed", &f, &p, NULL) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    CHECK(!p);
    f = *example_arrow_frontend();
    f.lower = NULL;
    CHECK(load("allow <- seed", &f, &p, NULL) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    OK(load("allow(X) :- seed(X).", NULL, &p, NULL));
    maelys_datalog_backend_t b = fake_backend();
    maelys_datalog_session_options_t o = options(&b);
    maelys_datalog_session_t *s = NULL;
    b.abi_version = 42;
    CHECK(maelys_datalog_session_create_ex(p, 0, &o, &s) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    b = fake_backend();
    b.capabilities |= MAELYS_DATALOG_CAP_EXPLAIN_TRUE;
    CHECK(maelys_datalog_session_create_ex(p, 0, &o, &s) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    b = fake_backend();
    b.name = "bad/name";
    CHECK(maelys_datalog_session_create_ex(p, 0, &o, &s) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    o = options(NULL);
    o.required_capabilities = UINT64_MAX;
    CHECK(maelys_datalog_session_create_ex(p, 0, &o, &s) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    OK(maelys_datalog_policy_free(p));
    return 0;
}
int main(void) {
    if (setup())
        return 1;
    const struct {
        const char *name;
        int (*test)(void);
    } tests[] = {{"frontend_backend_cross_product", frontend_and_backend_cross_product},
                 {"common_validation_and_sticky_builder_errors", common_validation},
                 {"capabilities_identity_and_work_limit", capabilities_and_identity},
                 {"differential_recursive_graphs", graph_differential},
                 {"compiled_identity_and_descriptor_limits", compiled_identity},
                 {"ir_roundtrip_all_language_features", ir_roundtrip},
                 {"shared_filter_service_and_output_dedup", shared_filter_service},
                 {"atomic_backend_failures_and_cleanup", backend_failures_are_atomic},
                 {"descriptor_validation", descriptor_validation}};
    int failures = 0;
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        int rc = tests[i].test();
        printf("compiler/%s: %s\n", tests[i].name, rc ? "FAILED" : "OK");
        failures += !!rc;
    }
    return failures ? 1 : 0;
}
