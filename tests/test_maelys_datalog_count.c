/* SPDX-License-Identifier: MPL-2.0 */
#include <maelys/datalog_extension.h>
#include "tests/helpers/test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
#define OK(c) REQUIRE((c) == MAELYS_DATALOG_STATUS_OK)
static maelys_datalog_value_t integer(int64_t n) {
    maelys_datalog_value_t v = {.kind = MAELYS_DATALOG_VALUE_INTEGER}; v.as.integer = n; return v;
}
static maelys_datalog_value_t symbol(const char *s) {
    maelys_datalog_value_t v = {.kind = MAELYS_DATALOG_VALUE_SYMBOL}; v.as.symbol = s; return v;
}
static maelys_datalog_fact_t fact(const char *p, size_t n,
    maelys_datalog_value_t a, maelys_datalog_value_t b, maelys_datalog_value_t c) {
    maelys_datalog_fact_t f = {.predicate = p, .arity = n};
    f.terms[0] = a; f.terms[1] = b; f.terms[2] = c; return f;
}
static int setup(void) {
    const maelys_datalog_predicate_t predicates[] = {
        {"group", 1, MAELYS_DATALOG_PREDICATE_EDB},
        {"raw", 3, MAELYS_DATALOG_PREDICATE_EDB},
        {"edge", 2, MAELYS_DATALOG_PREDICATE_EDB},
        {"count", 1, MAELYS_DATALOG_PREDICATE_EDB},
        {"min", 1, MAELYS_DATALOG_PREDICATE_EDB},
        {"max", 1, MAELYS_DATALOG_PREDICATE_EDB},
        {"sum", 1, MAELYS_DATALOG_PREDICATE_EDB},
        {"blocked", 1, MAELYS_DATALOG_PREDICATE_EDB},
        {"expected", 2, MAELYS_DATALOG_PREDICATE_EDB},
        {"base", 1, MAELYS_DATALOG_PREDICATE_POLICY_FACT},
        {"out", 2, MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY},
        {"project", 2, MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY},
        {"path", 2, MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY},
        {"summary", 1, MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY},
        {"allow", 1, MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY}
    };
    const char *atoms[] = {"a", "b", "c", "z", "keep", "drop", "1"};
    maelys_datalog_domain_t d = {"aggregates", predicates,
        sizeof(predicates)/sizeof(predicates[0]), atoms, sizeof(atoms)/sizeof(atoms[0])};
    OK(maelys_datalog_domain_register(&d));
    return 0;
}
static maelys_datalog_status_t load(const char *source, maelys_datalog_policy_t **out) {
    maelys_datalog_public_diagnostic_t d;
    maelys_datalog_status_t rc = maelys_datalog_policy_load_inline(
        "aggregates", "count.test", source, strlen(source), out, &d);
    if (rc) fprintf(stderr, "load %d: %s [%s]\n", rc, d.message, source);
    return rc;
}
static int session(const char *source, maelys_datalog_session_t **out) {
    maelys_datalog_policy_t *p = NULL;
    OK(load(source, &p));
    OK(maelys_datalog_session_create(p, 0, out));
    OK(maelys_datalog_policy_free(p));
    return 0;
}
static int present(maelys_datalog_result_t *r, const char *p, size_t n,
    maelys_datalog_value_t a, maelys_datalog_value_t b, int expected) {
    maelys_datalog_value_t values[2] = {a,b}; int actual = -1;
    OK(maelys_datalog_result_query(r, p, values, n, &actual));
    REQUIRE(actual == expected); return 0;
}
static int solve(maelys_datalog_session_t *s, const maelys_datalog_fact_t *f,
    size_t n, maelys_datalog_result_t **out) {
    maelys_datalog_public_diagnostic_t d;
    int rc = maelys_datalog_session_solve(s, f, n, out, &d);
    if (rc) fprintf(stderr, "solve %d: %s\n", rc, d.message);
    REQUIRE(rc == 0); return 0;
}
static int grouped_empty_distinct_and_reuse(void) {
    maelys_datalog_session_t *s = NULL;
    REQUIRE(session("out(G,N) :- N >= 0, count(I,raw(I,G,_),N), group(G). "
        "allow(G) :- out(G,N), N >= 2, not(blocked(G)). ", &s) == 0);
    maelys_datalog_value_t a = symbol("a"), b = symbol("b"), z = integer(0);
    maelys_datalog_fact_t facts[] = {
        fact("group",1,a,z,z), fact("group",1,b,z,z),
        fact("raw",3,integer(1),a,symbol("keep")), fact("raw",3,integer(1),a,symbol("drop")),
        fact("raw",3,integer(2),a,symbol("keep")), fact("raw",3,integer(2),a,symbol("keep"))};
    char canonical[4096] = {0};
    for (int pass = 0; pass < 4; ++pass) {
        maelys_datalog_result_t *r = NULL;
        REQUIRE(solve(s, facts, sizeof(facts)/sizeof(facts[0]), &r) == 0);
        REQUIRE(present(r,"out",2,a,integer(2),1) == 0);
        REQUIRE(present(r,"out",2,b,integer(0),1) == 0);
        REQUIRE(present(r,"allow",1,a,z,1) == 0);
        maelys_datalog_result_t *blocked = NULL;
        REQUIRE(maelys_datalog_session_solve(s,NULL,0,&blocked,NULL) == MAELYS_DATALOG_STATUS_INVALID_STATE);
        REQUIRE(blocked == NULL);
        char text[4096]; size_t bytes = 0;
        maelys_datalog_value_t q[] = {a,integer(2)};
        OK(maelys_datalog_result_explain_true_text(r,"out",q,2,text,sizeof(text),&bytes));
        REQUIRE(strstr(text,"kind=count origin=edb pattern=\"raw\"(?8,\"a\",?26) projected=?8 value=2 parent=-"));
        if (!pass) memcpy(canonical,text,bytes+1); else REQUIRE(!strcmp(text,canonical));
        char short_text[2] = {'x','y'}; size_t short_bytes = 0;
        REQUIRE(maelys_datalog_result_explain_true_text(r,"out",q,2,short_text,sizeof(short_text),&short_bytes) == MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
        REQUIRE(short_text[0] == 0 && short_bytes == bytes);
        q[1] = integer(3);
        OK(maelys_datalog_result_explain_false_text(r,"out",q,2,text,sizeof(text),&bytes));
        REQUIRE(strstr(text,"obstacle=count-mismatch") && strstr(text,"observed=2 expected=3"));
        OK(maelys_datalog_result_free(r));
        for (size_t i=0;i<3;++i) { maelys_datalog_fact_t t=facts[i];facts[i]=facts[5-i];facts[5-i]=t; }
    }
    maelys_datalog_result_t *r = NULL;
    maelys_datalog_fact_t empty_group = fact("group",1,a,z,z);
    REQUIRE(solve(s,&empty_group,1,&r) == 0);
    REQUIRE(present(r,"out",2,a,z,1) == 0);
    REQUIRE(present(r,"allow",1,a,z,0) == 0);
    OK(maelys_datalog_result_free(r));
    REQUIRE(solve(s,NULL,0,&r) == 0);
    size_t total = 99; OK(maelys_datalog_result_derived_fact_count(r,&total)); REQUIRE(total == 0);
    OK(maelys_datalog_result_free(r)); OK(maelys_datalog_session_free(s)); return 0;
}
static int typed_global_and_policy_counts(void) {
    maelys_datalog_session_t *s = NULL;
    REQUIRE(session("summary(N) :- count(I,raw(I,_,_),N). ",&s) == 0);
    maelys_datalog_value_t boolean = {.kind=MAELYS_DATALOG_VALUE_BOOLEAN,.as.boolean=1};
    maelys_datalog_fact_t f[] = {
        fact("raw",3,integer(1),symbol("a"),integer(0)),
        fact("raw",3,boolean,symbol("a"),integer(0)),
        fact("raw",3,symbol("1"),symbol("b"),integer(0)),
        fact("raw",3,integer(1),symbol("b"),integer(1))};
    maelys_datalog_result_t *r = NULL;
    REQUIRE(solve(s,f,4,&r) == 0);
    REQUIRE(present(r,"summary",1,integer(3),integer(0),1) == 0);
    OK(maelys_datalog_result_free(r));
    REQUIRE(solve(s,NULL,0,&r) == 0);
    REQUIRE(present(r,"summary",1,integer(0),integer(0),1) == 0);
    OK(maelys_datalog_result_free(r)); OK(maelys_datalog_session_free(s));
    REQUIRE(session("base(1). base(1). base(true). base(\"1\"). summary(N) :- count(I,base(I),N). ",&s) == 0);
    REQUIRE(solve(s,NULL,0,&r) == 0);
    REQUIRE(present(r,"summary",1,integer(3),integer(0),1) == 0);
    OK(maelys_datalog_result_free(r)); OK(maelys_datalog_session_free(s)); return 0;
}
static int recursion_negation_and_multiple_counts(void) {
    maelys_datalog_session_t *s = NULL;
    REQUIRE(session("path(X,Y) :- edge(X,Y). path(X,Z) :- path(X,Y),edge(Y,Z). "
        "out(G,N) :- group(G),count(I,path(G,I),N). "
        "project(G,N) :- group(G),count(I,path(G,I),N),count(J,edge(G,J),M),N >= M. "
        "allow(G) :- group(G),not(out(G,0)). ",&s) == 0);
    maelys_datalog_fact_t f[] = {
        fact("group",1,integer(1),integer(0),integer(0)),
        fact("group",1,integer(4),integer(0),integer(0)),
        fact("edge",2,integer(1),integer(2),integer(0)),
        fact("edge",2,integer(2),integer(3),integer(0)),
        fact("edge",2,integer(1),integer(3),integer(0))};
    maelys_datalog_result_t *r = NULL;
    REQUIRE(solve(s,f,5,&r) == 0);
    REQUIRE(present(r,"out",2,integer(1),integer(2),1) == 0);
    REQUIRE(present(r,"project",2,integer(4),integer(0),1) == 0);
    REQUIRE(present(r,"allow",1,integer(1),integer(0),1) == 0);
    REQUIRE(present(r,"allow",1,integer(4),integer(0),0) == 0);
    char text[4096]; size_t bytes;
    maelys_datalog_value_t q[] = {integer(1),integer(2)};
    OK(maelys_datalog_result_explain_true_text(r,"out",q,2,text,sizeof(text),&bytes));
    REQUIRE(strstr(text,"kind=count origin=idb"));
    q[0] = integer(4);
    OK(maelys_datalog_result_explain_false_text(r,"allow",q,1,text,sizeof(text),&bytes));
    REQUIRE(strstr(text,"negative-contradicted"));
    OK(maelys_datalog_result_free(r)); OK(maelys_datalog_session_free(s)); return 0;
}
static int binding_and_syntax_rejections(void) {
    const char *invalid[] = {
        "out(G,N) :- count(I,raw(I,G,_),N). ",
        "out(I,N) :- count(I,raw(I,_,_),N). ",
        "summary(N) :- count(I,raw(I,_,_),N),I > 0.",
        "summary(N) :- count(I,raw(I,_,_),N),group(I). ",
        "summary(N) :- count(I,raw(I,_,_),N),not(blocked(I)). ",
        "summary(N) :- count(I,raw(I,_,_),N),starts_with(I,\"a\"). ",
        "summary(N) :- count(I,raw(I,_,_),I). ",
        "summary(N) :- count(I,raw(N,_,_),N). ",
        "summary(N) :- count(I,raw(I,N,_),N). ",
        "summary(N) :- count(_,raw(_,_,_),N). ",
        "summary(N) :- count(I,raw(I,_,_),_). ",
        "summary(N) :- count(I,raw(I,_,_),0). ",
        "summary(N) :- count(I,raw(I,_,_),N,0). ",
        "summary(N) :- count(I,unknown(I),N). ",
        "summary(N) :- count(I,raw(I,_),N). ",
        "summary(N) :- count(I,summary(I),N). ",
        "summary(N) :- count(I,allow(I),N). allow(N) :- summary(N). ",
        "summary(N) :- count(I,raw(I,_,_),N) or group(N). ",
        "summary(N) :- count(I,raw(I,_,_),N). // invalid comment",
    };
    for (size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
        maelys_datalog_policy_t *p = (void *)(uintptr_t)1;
        maelys_datalog_public_diagnostic_t d;
        int rc = maelys_datalog_policy_load_inline("aggregates","bad",invalid[i],strlen(invalid[i]),&p,&d);
        if (!rc) fprintf(stderr,"unexpected acceptance: %s\n",invalid[i]);
        REQUIRE(rc != 0 && p == NULL);
    }
    maelys_datalog_session_t *s = NULL;
    REQUIRE(session("summary(N) :- count(N). ",&s) == 0);
    maelys_datalog_fact_t f = fact("count",1,integer(7),integer(0),integer(0));
    maelys_datalog_result_t *r = NULL; REQUIRE(solve(s,&f,1,&r) == 0);
    REQUIRE(present(r,"summary",1,integer(7),integer(0),1) == 0);
    OK(maelys_datalog_result_free(r)); OK(maelys_datalog_session_free(s));
    REQUIRE(session("out(G,N) :- group(G) or blocked(G), count(I,raw(I,G,_),N). ",&s) == 0);
    OK(maelys_datalog_session_free(s)); return 0;
}
static int capacity_failure_and_reuse(void) {
    maelys_datalog_session_t *s = NULL;
    REQUIRE(session("summary(N) :- count(I,raw(I,_,_),N). ",&s) == 0);
    size_t capacity = 0; OK(maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_FACTS_PER_PRED,&capacity));
    maelys_datalog_fact_t *f = calloc(capacity+1,sizeof(*f)); REQUIRE(f);
    for (size_t i=0;i<=capacity;++i) f[i] = fact("raw",3,integer((int64_t)i),integer(0),integer(0));
    maelys_datalog_result_t *r = NULL; REQUIRE(solve(s,f,capacity,&r) == 0);
    REQUIRE(present(r,"summary",1,integer((int64_t)capacity),integer(0),1) == 0);
    OK(maelys_datalog_result_free(r)); r = NULL;
    REQUIRE(maelys_datalog_session_solve(s,f,capacity+1,&r,NULL) == MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE && r == NULL);
    REQUIRE(solve(s,f,1,&r) == 0);
    REQUIRE(present(r,"summary",1,integer(1),integer(0),1) == 0);
    OK(maelys_datalog_result_free(r)); OK(maelys_datalog_session_free(s)); free(f); return 0;
}
static int prepare_calls;
static maelys_datalog_status_t forbidden_prepare(const maelys_datalog_program_t *p, void **state) { (void)p;(void)state;++prepare_calls;return MAELYS_DATALOG_STATUS_INTERNAL; }
static int backend_gate_and_ir(void) {
    maelys_datalog_policy_t *p = NULL; OK(load("summary(N) :- count(I,raw(I,_,_),N). ",&p));
    maelys_datalog_backend_t b = *maelys_datalog_backend_reference();
    b.name = "without_count"; b.semantic_id = "tests.without_count.v1";
    b.capabilities &= ~MAELYS_DATALOG_CAP_AGGREGATES; b.prepare = forbidden_prepare;
    maelys_datalog_session_options_t o = {MAELYS_DATALOG_BACKEND_ABI_VERSION,sizeof(o),&b,0,0};
    maelys_datalog_session_t *s = NULL;
    REQUIRE(maelys_datalog_session_create_ex(p,0,&o,&s) == MAELYS_DATALOG_STATUS_UNSUPPORTED);
    REQUIRE(!s && prepare_calls == 0);
    OK(maelys_datalog_session_create(p,0,&s)); OK(maelys_datalog_policy_free(p));
    const maelys_datalog_program_t *program; OK(maelys_datalog_session_program(s,&program));
    maelys_datalog_program_info_t info; OK(maelys_datalog_program_info(program,&info));
    REQUIRE(info.required_capabilities == (MAELYS_DATALOG_CAP_POSITIVE|MAELYS_DATALOG_CAP_AGGREGATES));
    maelys_datalog_ir_rule_t rule; OK(maelys_datalog_program_rule(program,0,&rule));
    REQUIRE(rule.body_count == 1 && rule.body[0].kind == MAELYS_DATALOG_IR_COUNT);
    REQUIRE(!strcmp(rule.body[0].atom.predicate,"raw") && rule.body[0].lhs.as.variable == 8 && rule.body[0].rhs.as.variable == 13);
    OK(maelys_datalog_session_free(s)); return 0;
}
static const maelys_datalog_program_t *roundtrip_source;
static maelys_datalog_status_t lower_count(const char *source, size_t length,
    maelys_datalog_program_builder_t *builder, maelys_datalog_public_diagnostic_t *diag) {
    (void)length; (void)diag;
    maelys_datalog_ir_rule_t r;
    maelys_datalog_status_t rc = maelys_datalog_program_rule(roundtrip_source,0,&r);
    if (rc) return rc;
    switch (source[0]) {
        case '1': r.body[0].lhs.as.variable = 32; break;
        case '2': r.body[0].rhs.kind = MAELYS_DATALOG_IR_INTEGER; break;
        case '3': r.body[0].rhs = r.body[0].lhs; break;
        case '4': r.body[0].lhs.as.variable = 0; break;
        case '5': r.body[0].atom.terms[1] = r.body[0].rhs; break;
        case '6': r.body[0].atom.terms[1].as.variable = 1; break;
        case '7': r.body[0].rhs.as.variable = 26; break;
        case '8':
            r.body_count = 2;
            r.body[1].kind = MAELYS_DATALOG_IR_ATOM;
            r.body[1].atom.predicate = "group";
            r.body[1].atom.arity = 1;
            r.body[1].atom.terms[0] = r.body[0].atom.terms[1];
            break;
    }
    return maelys_datalog_program_add_rule(builder,&r);
}
static int frontend_roundtrip_and_malformed_ir(void) {
    maelys_datalog_session_t *s = NULL;
    REQUIRE(session("summary(N) :- count(I,raw(I,_,_),N).",&s) == 0);
    OK(maelys_datalog_session_program(s,&roundtrip_source));
    maelys_datalog_frontend_t f = {MAELYS_DATALOG_PROGRAM_ABI_VERSION,sizeof(f),
        "count_ir","tests.count-ir.v1",lower_count};
    for (char variant='0'; variant<='8'; ++variant) {
        char input[] = {variant,0}; maelys_datalog_policy_t *p = NULL;
        int rc = maelys_datalog_policy_load_frontend("aggregates","count.ir",input,1,&f,&p,NULL);
        if (variant != '0') { REQUIRE(rc != 0 && !p); continue; }
        REQUIRE(rc == 0 && p);
        maelys_datalog_session_t *other = NULL; OK(maelys_datalog_session_create(p,0,&other));
        OK(maelys_datalog_policy_free(p));
        maelys_datalog_result_t *r = NULL; REQUIRE(solve(other,NULL,0,&r) == 0);
        REQUIRE(present(r,"summary",1,integer(0),integer(0),1) == 0);
        OK(maelys_datalog_result_free(r)); OK(maelys_datalog_session_free(other));
    }
    OK(maelys_datalog_session_free(s)); roundtrip_source=NULL; return 0;
}
static int planner_calls;
static maelys_datalog_status_t old_planner(const maelys_datalog_join_candidate_t *c,
    size_t n, size_t *out) {
    ++planner_calls;
    if (!n) return MAELYS_DATALOG_STATUS_INTERNAL;
    for (size_t i=0;i<n;++i)
        if (c[i].kind < MAELYS_DATALOG_JOIN_ATOM || c[i].kind > MAELYS_DATALOG_JOIN_FILTER)
            return MAELYS_DATALOG_STATUS_UNSUPPORTED;
    *out=n-1; return MAELYS_DATALOG_STATUS_OK;
}
static int old_planner_compatibility(void) {
    const char *names[]={"count","min","max","sum"};
    for(unsigned op=0;op<4;++op) {
    maelys_datalog_context_t *c = NULL; OK(maelys_datalog_context_create(&c));
    maelys_datalog_planner_module_t p = {MAELYS_DATALOG_MODULE_ABI_VERSION,sizeof(p),
        "old_planner","tests.old-planner.v1",old_planner};
    maelys_datalog_extension_t e = {0};
    e.abi_version=MAELYS_DATALOG_EXTENSION_ABI_VERSION;e.struct_size=sizeof(e);
    e.name="count_planner";e.semantic_id="tests.count-planner.v1";e.planners=&p;e.planner_count=1;
    OK(maelys_datalog_context_register(c,&e)); OK(maelys_datalog_context_seal(c,"old_planner"));
    char source[160];snprintf(source,sizeof(source),"out(G,N) :- N >= 0,%s(I,raw(I,G,_),N),group(G).",names[op]);
    maelys_datalog_policy_t *policy = NULL;
    OK(maelys_datalog_context_load_inline(c,NULL,"aggregates","planner",source,strlen(source),&policy,NULL));
    maelys_datalog_session_t *s = NULL;OK(maelys_datalog_session_create(policy,0,&s));
    OK(maelys_datalog_policy_free(policy));OK(maelys_datalog_context_free(c));
    maelys_datalog_fact_t input[] = {fact("group",1,symbol("a"),integer(0),integer(0)),
        fact("raw",3,integer(0),symbol("a"),integer(0))};
    maelys_datalog_result_t *r = NULL; REQUIRE(solve(s,input,2,&r) == 0);
    REQUIRE(present(r,"out",2,symbol("a"),integer(op==0?1:0),1) == 0);
    REQUIRE(planner_calls > 0);OK(maelys_datalog_result_free(r));OK(maelys_datalog_session_free(s));
    }
    return 0;
}
static uint32_t random_state = UINT32_C(9834721);
static uint32_t draw(void) { random_state=random_state*UINT32_C(1664525)+UINT32_C(1013904223);return random_state; }
static int snapshot_oracle(void) {
    maelys_datalog_session_t *s = NULL;
    REQUIRE(session("project(I,G) :- raw(I,G,T),T >= 2,not(blocked(I)). "
        "out(G,N) :- group(G),count(I,project(I,G),N). "
        "allow(G) :- out(G,N),N >= 3.",&s) == 0);
    for (size_t pass=0;pass<100;++pass) {
        unsigned char seen[3][10] = {{0}};
        maelys_datalog_fact_t inputs[44];
        for (size_t g=0;g<3;++g) inputs[g]=fact("group",1,integer((int64_t)g),integer(0),integer(0));
        inputs[3]=fact("blocked",1,integer(5),integer(0),integer(0));
        size_t count=draw()%41;
        for (size_t i=0;i<count;++i) {
            uint32_t id=draw()%10, group=draw()%3, tag=draw()%4;
            inputs[i+4]=fact("raw",3,integer(id),integer(group),integer(tag));
            if (tag>=2 && id!=5) seen[group][id]=1;
        }
        maelys_datalog_result_t *r = NULL;REQUIRE(solve(s,inputs,count+4,&r) == 0);
        for (size_t g=0;g<3;++g) {
            size_t expected=0;for(size_t id=0;id<10;++id) expected+=seen[g][id];
            REQUIRE(present(r,"out",2,integer((int64_t)g),integer((int64_t)expected),1) == 0);
            REQUIRE(present(r,"allow",1,integer((int64_t)g),integer(0),expected>=3) == 0);
        }
        maelys_datalog_fact_view_t rows[3];size_t n=0;
        OK(maelys_datalog_result_enumerate(r,"out",2,rows,3,&n));REQUIRE(n==3);
        OK(maelys_datalog_result_free(r));
    }
    OK(maelys_datalog_session_free(s));return 0;
}
static int derived_capacity_failure_and_reuse(void) {
    maelys_datalog_session_t *s = NULL;
    REQUIRE(session("out(G,N) :- group(G),count(I,raw(I,_,_),N). "
        "out(G,N) :- group(G),count(I,base(I),N).",&s) == 0);
    size_t capacity=0;OK(maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_FACTS_PER_PRED,&capacity));
    maelys_datalog_fact_t *f=calloc(capacity+1,sizeof(*f));REQUIRE(f);
    for(size_t i=0;i<capacity;++i) f[i]=fact("group",1,integer((int64_t)i),integer(0),integer(0));
    f[capacity]=fact("raw",3,integer(1),integer(0),integer(0));
    maelys_datalog_result_t *r=NULL;
    REQUIRE(maelys_datalog_session_solve(s,f,capacity+1,&r,NULL)==MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE && !r);
    REQUIRE(solve(s,f,1,&r)==0);REQUIRE(present(r,"out",2,integer(0),integer(0),1)==0);
    OK(maelys_datalog_result_free(r));OK(maelys_datalog_session_free(s));free(f);return 0;
}

static int prebound_outputs_and_repeated_projection(void) {
    maelys_datalog_session_t *s = NULL;
    REQUIRE(session("out(G,N) :- expected(G,N),count(I,raw(I,G,_),N). "
        "project(G,N) :- group(G),count(I,raw(I,G,_),N),count(J,edge(G,J),N). "
        "summary(N) :- count(I,edge(I,I),N).",&s) == 0);
    maelys_datalog_value_t a=symbol("a"),b=symbol("b"),z=integer(0);
    maelys_datalog_value_t truth={.kind=MAELYS_DATALOG_VALUE_BOOLEAN,.as.boolean=1};
    maelys_datalog_fact_t f[]={
        fact("group",1,a,z,z),fact("group",1,b,z,z),
        fact("expected",2,a,integer(1),z),fact("expected",2,a,truth,z),
        fact("expected",2,b,integer(1),z),
        fact("raw",3,integer(7),a,z),
        fact("edge",2,a,a,z),fact("edge",2,b,a,z)};
    maelys_datalog_result_t *r=NULL;REQUIRE(solve(s,f,sizeof(f)/sizeof(f[0]),&r)==0);
    REQUIRE(present(r,"out",2,a,integer(1),1)==0);
    REQUIRE(present(r,"out",2,a,truth,0)==0);
    REQUIRE(present(r,"out",2,b,integer(1),0)==0);
    REQUIRE(present(r,"project",2,a,integer(1),1)==0);
    REQUIRE(present(r,"project",2,b,z,0)==0);
    REQUIRE(present(r,"summary",1,integer(1),z,1)==0);
    char text[4096];size_t bytes=0;
    maelys_datalog_value_t q[]={a,truth};
    OK(maelys_datalog_result_explain_false_text(r,"out",q,2,text,sizeof(text),&bytes));
    REQUIRE(strstr(text,"observed=1 expected=true"));
    OK(maelys_datalog_result_free(r));OK(maelys_datalog_session_free(s));return 0;
}

/* The numeric operators share scope/strata rules but not count's distinct-value
 * semantics. The oracle below deliberately deduplicates complete input tuples. */
static const char *numeric_names[] = {"min", "max", "sum"};
static const uint64_t numeric_caps[] = {MAELYS_DATALOG_CAP_MIN, MAELYS_DATALOG_CAP_MAX, MAELYS_DATALOG_CAP_SUM};
static const unsigned numeric_ir[] = {MAELYS_DATALOG_IR_MIN, MAELYS_DATALOG_IR_MAX, MAELYS_DATALOG_IR_SUM};

static int prepared_matches(maelys_datalog_result_t *result,
    maelys_datalog_explanation_kind_t kind, const maelys_datalog_value_t *query,
    const char *expected) {
    size_t bytes=0,alignment=0;
    OK(maelys_datalog_result_explanation_storage_requirements(result,kind,&bytes,&alignment));
    void *storage=malloc(bytes+alignment);REQUIRE(storage);
    void *aligned=(void *)(((uintptr_t)storage+alignment-1u)&~(uintptr_t)(alignment-1u));
    maelys_datalog_prepared_explanation_t *prepared=NULL;
    OK(maelys_datalog_result_prepare_explanation(result,kind,"out",query,2,aligned,bytes,&prepared));
    REQUIRE(maelys_datalog_result_free(result)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    size_t required=0;OK(maelys_datalog_prepared_explanation_text_size(prepared,&required));
    REQUIRE(required==strlen(expected));char text[4096];
    REQUIRE(maelys_datalog_prepared_explanation_write_text(prepared,text,1)==MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE && !text[0]);
    OK(maelys_datalog_prepared_explanation_write_text(prepared,text,sizeof(text)));REQUIRE(!strcmp(text,expected));
    OK(maelys_datalog_prepared_explanation_release(prepared));free(storage);return 0;
}

static int numeric_groups_and_explanations(void) {
    for (size_t op = 0; op < 3; ++op) {
        char source[256];
        snprintf(source,sizeof(source),"out(G,N) :- N >= 0,%s(V,raw(_,G,V),N),group(G).",numeric_names[op]);
        maelys_datalog_session_t *s = NULL; REQUIRE(session(source,&s) == 0);
        maelys_datalog_value_t a=symbol("a"),b=symbol("b"),z=integer(0);
        maelys_datalog_fact_t f[]={fact("group",1,a,z,z),fact("group",1,b,z,z),
            fact("raw",3,integer(1),a,integer(10)),fact("raw",3,integer(2),a,integer(10)),
            fact("raw",3,integer(3),a,integer(7)),fact("raw",3,integer(1),a,integer(10)),
            /* Nonmatching groups do not impose a global column type. */
            fact("raw",3,integer(4),symbol("c"),symbol("keep"))};
        const int expected[] = {7,10,27};
        char canonical[4096]={0};
        for (int pass=0;pass<2;++pass) {
            maelys_datalog_result_t *r=NULL; REQUIRE(solve(s,f,7,&r)==0);
            REQUIRE(present(r,"out",2,a,integer(expected[op]),1)==0);
            REQUIRE(present(r,"out",2,b,z,op==2)==0);
            REQUIRE(present(r,"out",2,symbol("z"),z,0)==0);
            maelys_datalog_value_t q[]={a,integer(expected[op])};
            char text[4096],needle[64];size_t bytes=0;
            OK(maelys_datalog_result_explain_true_text(r,"out",q,2,text,sizeof(text),&bytes));
            snprintf(needle,sizeof(needle),"kind=%s origin=edb",numeric_names[op]); REQUIRE(strstr(text,needle));
            if (!pass) memcpy(canonical,text,bytes+1); else REQUIRE(!strcmp(text,canonical));
            REQUIRE(prepared_matches(r,MAELYS_DATALOG_EXPLAIN_TRUE,q,text)==0);
            char short_text[2]={'x','y'};size_t required=0;
            REQUIRE(maelys_datalog_result_explain_true_text(r,"out",q,2,short_text,2,&required)==MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
            REQUIRE(!short_text[0] && required==bytes);
            q[1]=integer(99);
            OK(maelys_datalog_result_explain_false_text(r,"out",q,2,text,sizeof(text),&bytes));
            snprintf(needle,sizeof(needle),"obstacle=%s-mismatch",numeric_names[op]); REQUIRE(strstr(text,needle));
            REQUIRE(strstr(text,"expected=99"));
            REQUIRE(prepared_matches(r,MAELYS_DATALOG_EXPLAIN_FALSE,q,text)==0);
            q[0]=b;q[1]=z;
            if (op<2) {
                OK(maelys_datalog_result_explain_false_text(r,"out",q,2,text,sizeof(text),&bytes));
                snprintf(needle,sizeof(needle),"obstacle=%s-empty",numeric_names[op]); REQUIRE(strstr(text,needle));
                REQUIRE(!strstr(text," observed="));
                REQUIRE(prepared_matches(r,MAELYS_DATALOG_EXPLAIN_FALSE,q,text)==0);
            }
            OK(maelys_datalog_result_free(r));
            for (size_t i=0;i<3;++i) { maelys_datalog_fact_t t=f[i];f[i]=f[6-i];f[6-i]=t; }
        }
        maelys_datalog_result_t *r=NULL;REQUIRE(solve(s,NULL,0,&r)==0);
        size_t n=99;OK(maelys_datalog_result_derived_fact_count(r,&n));REQUIRE(n==0);
        OK(maelys_datalog_result_free(r));OK(maelys_datalog_session_free(s));
    }
    return 0;
}
static int numeric_error_atomicity_and_boundaries(void) {
    for (size_t op=0;op<3;++op) {
        char source[160];snprintf(source,sizeof(source),"summary(N) :- %s(V,raw(_,_,V),N).",numeric_names[op]);
        maelys_datalog_session_t *s=NULL;REQUIRE(session(source,&s)==0);
        maelys_datalog_value_t z=integer(0),max=integer(INT32_MAX);
        maelys_datalog_value_t bad[]={symbol("1"),{.kind=MAELYS_DATALOG_VALUE_BOOLEAN,.as.boolean=1}};
        maelys_datalog_fact_t f[]={fact("raw",3,z,z,max),fact("raw",3,integer(1),z,z)};
        for (size_t k=0;k<2;++k) {
            f[1].terms[2]=bad[k];maelys_datalog_result_t *r=(void *)(uintptr_t)1;
            REQUIRE(maelys_datalog_session_solve(s,f,2,&r,NULL)==MAELYS_DATALOG_STATUS_INVALID_FIELD && !r);
            /* A rejection releases the result lease and every working binding. */
            REQUIRE(solve(s,f,1,&r)==0);REQUIRE(present(r,"summary",1,max,z,1)==0);OK(maelys_datalog_result_free(r));
        }
        f[1].terms[2]=integer(1);maelys_datalog_result_t *r=NULL;
        if (op==2) REQUIRE(maelys_datalog_session_solve(s,f,2,&r,NULL)==MAELYS_DATALOG_STATUS_INVALID_FIELD && !r);
        else { REQUIRE(solve(s,f,2,&r)==0);REQUIRE(present(r,"summary",1,op?max:integer(1),z,1)==0);OK(maelys_datalog_result_free(r)); }
        f[1]=f[0];REQUIRE(solve(s,f,2,&r)==0);REQUIRE(present(r,"summary",1,max,z,1)==0);OK(maelys_datalog_result_free(r));
        REQUIRE(solve(s,NULL,0,&r)==0);REQUIRE(present(r,"summary",1,z,z,op==2)==0);OK(maelys_datalog_result_free(r));
        OK(maelys_datalog_session_free(s));
    }
    return 0;
}
static int numeric_policy_and_frozen_idb(void) {
    for(size_t op=0;op<3;++op) {
        char source[512];
        snprintf(source,sizeof(source),"base(10). base(10). base(3). summary(N) :- %s(V,base(V),N). "
            "path(X,Y) :- edge(X,Y). path(X,Z) :- path(X,Y),edge(Y,Z). "
            "out(G,N) :- group(G),%s(V,path(G,V),N). "
            "allow(G) :- group(G),not(out(G,0)).",numeric_names[op],numeric_names[op]);
        maelys_datalog_session_t *s=NULL;REQUIRE(session(source,&s)==0);
        maelys_datalog_value_t z=integer(0),a=integer(1);
        maelys_datalog_fact_t f[]={fact("group",1,a,z,z),fact("edge",2,a,integer(2),z),
            fact("edge",2,integer(2),integer(3),z),fact("edge",2,a,integer(3),z)};
        maelys_datalog_result_t *r=NULL;REQUIRE(solve(s,f,4,&r)==0);
        const int policy[]={3,10,13},idb[]={2,3,5};
        REQUIRE(present(r,"summary",1,integer(policy[op]),z,1)==0);
        REQUIRE(present(r,"out",2,a,integer(idb[op]),1)==0);
        REQUIRE(present(r,"allow",1,a,z,1)==0);
        char text[4096];size_t bytes=0;maelys_datalog_value_t q[]={a,integer(idb[op])};
        OK(maelys_datalog_result_explain_true_text(r,"out",q,2,text,sizeof(text),&bytes));REQUIRE(strstr(text,"origin=idb"));
        q[1]=integer(99);OK(maelys_datalog_result_explain_false_text(r,"out",q,2,text,sizeof(text),&bytes));REQUIRE(strstr(text,"-mismatch"));
        OK(maelys_datalog_result_free(r));OK(maelys_datalog_session_free(s));
    }
    return 0;
}
static int numeric_snapshot_oracle(void) {
    maelys_datalog_session_t *s=NULL;
    REQUIRE(session("out(G,N) :- group(G),min(V,raw(_,G,V),N). "
        "project(G,N) :- group(G),max(V,raw(_,G,V),N). "
        "path(G,N) :- group(G),sum(V,raw(_,G,V),N).",&s)==0);
    for(size_t pass=0;pass<100;++pass) {
        unsigned char seen[3][5][4]={{{0}}};maelys_datalog_fact_t f[43];
        for(size_t g=0;g<3;++g)f[g]=fact("group",1,integer((int64_t)g),integer(0),integer(0));
        size_t n=draw()%41;
        for(size_t i=0;i<n;++i) {
            unsigned id=draw()%5,g=draw()%3,v=draw()%4;seen[g][id][v]=1;
            f[i+3]=fact("raw",3,integer(id),integer(g),integer(v));
        }
        maelys_datalog_result_t *r=NULL;REQUIRE(solve(s,f,n+3,&r)==0);
        for(size_t g=0;g<3;++g) {
            unsigned lo=4,hi=0,sum=0,found=0;
            for(unsigned id=0;id<5;++id)for(unsigned v=0;v<4;++v)if(seen[g][id][v]) {
                found=1;if(v<lo)lo=v;if(v>hi)hi=v;sum+=v;
            }
            REQUIRE(present(r,"out",2,integer((int64_t)g),integer(lo),found)==0);
            REQUIRE(present(r,"project",2,integer((int64_t)g),integer(hi),found)==0);
            REQUIRE(present(r,"path",2,integer((int64_t)g),integer(sum),1)==0);
        }
        OK(maelys_datalog_result_free(r));
    }
    OK(maelys_datalog_session_free(s));return 0;
}
static int numeric_ir_and_capabilities(void) {
    for(size_t op=0;op<3;++op) {
        char source[160];snprintf(source,sizeof(source),"summary(N) :- %s(I,raw(I,_,_),N).",numeric_names[op]);
        maelys_datalog_policy_t *p=NULL;OK(load(source,&p));
        maelys_datalog_backend_t b=*maelys_datalog_backend_reference();
        b.name="count_only";b.semantic_id="tests.count-only.v1";b.capabilities&=~numeric_caps[op];b.prepare=forbidden_prepare;
        maelys_datalog_session_options_t o={MAELYS_DATALOG_BACKEND_ABI_VERSION,sizeof(o),&b,0,0};
        maelys_datalog_session_t *s=NULL;
        REQUIRE(maelys_datalog_session_create_ex(p,0,&o,&s)==MAELYS_DATALOG_STATUS_UNSUPPORTED && !s && !prepare_calls);
        OK(maelys_datalog_session_create(p,0,&s));OK(maelys_datalog_policy_free(p));
        OK(maelys_datalog_session_program(s,&roundtrip_source));
        maelys_datalog_program_info_t info;OK(maelys_datalog_program_info(roundtrip_source,&info));
        REQUIRE(info.required_capabilities==(MAELYS_DATALOG_CAP_POSITIVE|numeric_caps[op]));
        maelys_datalog_ir_rule_t rule;OK(maelys_datalog_program_rule(roundtrip_source,0,&rule));REQUIRE(rule.body[0].kind==numeric_ir[op]);
        maelys_datalog_frontend_t frontend={MAELYS_DATALOG_PROGRAM_ABI_VERSION,sizeof(frontend),"numeric_ir","tests.numeric-ir.v1",lower_count};
        for(char variant='0';variant<='8';++variant) {
            char input[]={variant,0};p=NULL;
            int rc=maelys_datalog_policy_load_frontend("aggregates","numeric.ir",input,1,&frontend,&p,NULL);
            if(variant!='0') { REQUIRE(rc && !p);continue; }
            REQUIRE(!rc && p);maelys_datalog_session_t *other=NULL;OK(maelys_datalog_session_create(p,0,&other));
            OK(maelys_datalog_policy_free(p));maelys_datalog_result_t *r=NULL;
            maelys_datalog_fact_t fact_input=fact("raw",3,integer(7),integer(0),integer(0));
            REQUIRE(solve(other,&fact_input,1,&r)==0);REQUIRE(present(r,"summary",1,integer(7),integer(0),1)==0);
            OK(maelys_datalog_result_free(r));OK(maelys_datalog_session_free(other));
        }
        OK(maelys_datalog_session_free(s));roundtrip_source=NULL;
    }
    return 0;
}
static int numeric_scope_and_strata_rejections(void) {
    const char *invalid[]={"out(G,N) :- %s(V,raw(_,G,V),N).", "out(V,N) :- %s(V,raw(_,_,V),N).",
        "summary(N) :- %s(V,raw(_,_,V),N),group(V).", "summary(N) :- %s(V,summary(V),N).",
        "summary(N) :- %s(V,allow(V),N). allow(N) :- summary(N).", "summary(N) :- %s(V,raw(_,_,V),V).",
        "summary(N) :- %s(V,raw(N,_,V),N).", "summary(N) :- %s(_,raw(_,_,_),N)."};
    for(size_t op=0;op<3;++op)for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
        char source[200];snprintf(source,sizeof(source),invalid[i],numeric_names[op]);
        maelys_datalog_policy_t *p=NULL;
        REQUIRE(maelys_datalog_policy_load_inline("aggregates","bad",source,strlen(source),&p,NULL)!=0 && !p);
    }
    return 0;
}

static int numeric_capacity_and_bindings(void) {
    size_t capacity=0;OK(maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_FACTS_PER_PRED,&capacity));
    maelys_datalog_fact_t *facts=calloc(capacity+1,sizeof(*facts));REQUIRE(facts);
    for(size_t i=0;i<=capacity;++i)facts[i]=fact("raw",3,integer((int64_t)i),integer(0),integer(1));
    for(size_t op=0;op<3;++op) {
        char source[256];snprintf(source,sizeof(source),"summary(N) :- %s(V,raw(_,_,V),N).",numeric_names[op]);
        maelys_datalog_session_t *s=NULL;REQUIRE(session(source,&s)==0);maelys_datalog_result_t *r=NULL;
        REQUIRE(solve(s,facts,capacity,&r)==0);
        REQUIRE(present(r,"summary",1,integer(op==2?(int64_t)capacity:1),integer(0),1)==0);OK(maelys_datalog_result_free(r));
        REQUIRE(maelys_datalog_session_solve(s,facts,capacity+1,&r,NULL)==MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE && !r);
        REQUIRE(solve(s,facts,1,&r)==0);REQUIRE(present(r,"summary",1,integer(1),integer(0),1)==0);
        OK(maelys_datalog_result_free(r));OK(maelys_datalog_session_free(s));
        /* Ordinary homonyms, repeated projection and prebound typed outputs. */
        snprintf(source,sizeof(source),"summary(N) :- %s(N). out(G,N) :- expected(G,N),%s(V,edge(V,V),N).",numeric_names[op],numeric_names[op]);
        REQUIRE(session(source,&s)==0);
        maelys_datalog_value_t z=integer(0),truth={.kind=MAELYS_DATALOG_VALUE_BOOLEAN,.as.boolean=1};
        maelys_datalog_fact_t f[]={fact(numeric_names[op],1,integer(9),z,z),
            fact("expected",2,symbol("a"),integer(1),z),fact("expected",2,symbol("a"),truth,z),
            fact("edge",2,integer(1),integer(1),z),fact("edge",2,integer(2),integer(3),z)};
        REQUIRE(solve(s,f,5,&r)==0);REQUIRE(present(r,"summary",1,integer(9),z,1)==0);
        REQUIRE(present(r,"out",2,symbol("a"),integer(1),1)==0);REQUIRE(present(r,"out",2,symbol("a"),truth,0)==0);
        char text[4096];size_t bytes;maelys_datalog_value_t q[]={symbol("a"),truth};
        OK(maelys_datalog_result_explain_false_text(r,"out",q,2,text,sizeof(text),&bytes));REQUIRE(strstr(text,"observed=1 expected=true"));
        OK(maelys_datalog_result_free(r));OK(maelys_datalog_session_free(s));
    }
    free(facts);
    maelys_datalog_session_t *s=NULL;
    REQUIRE(session("allow(G) :- group(G),min(V,raw(_,G,V),N),max(W,raw(_,G,W),N).",&s)==0);
    maelys_datalog_fact_t f=fact("group",1,symbol("a"),integer(0),integer(0));maelys_datalog_result_t *r=NULL;
    REQUIRE(solve(s,&f,1,&r)==0);char text[4096];size_t bytes;maelys_datalog_value_t q=symbol("a");
    OK(maelys_datalog_result_explain_false_text(r,"allow",&q,1,text,sizeof(text),&bytes));REQUIRE(strstr(text,"min-empty"));
    OK(maelys_datalog_result_free(r));OK(maelys_datalog_session_free(s));return 0;
}

int main(int argc, char **argv) {
    if (setup()) return 1;
    const test_case_t cases[] = {
        {"numeric/capacity_and_bindings",TEST_MODE_NON_BLOCKING,numeric_capacity_and_bindings},
        {"numeric/groups_and_explanations",TEST_MODE_NON_BLOCKING,numeric_groups_and_explanations},
        {"numeric/error_atomicity_and_boundaries",TEST_MODE_NON_BLOCKING,numeric_error_atomicity_and_boundaries},
        {"numeric/policy_and_frozen_idb",TEST_MODE_NON_BLOCKING,numeric_policy_and_frozen_idb},
        {"numeric/snapshot_oracle",TEST_MODE_NON_BLOCKING,numeric_snapshot_oracle},
        {"numeric/ir_and_capabilities",TEST_MODE_NON_BLOCKING,numeric_ir_and_capabilities},
        {"numeric/scope_and_strata_rejections",TEST_MODE_NON_BLOCKING,numeric_scope_and_strata_rejections},

        {"count/frontend_roundtrip_malformed",TEST_MODE_NON_BLOCKING,frontend_roundtrip_and_malformed_ir},
        {"count/old_planner",TEST_MODE_NON_BLOCKING,old_planner_compatibility},
        {"count/snapshot_oracle",TEST_MODE_NON_BLOCKING,snapshot_oracle},
        {"count/derived_overflow_reuse",TEST_MODE_NON_BLOCKING,derived_capacity_failure_and_reuse},
        {"count/grouped_empty_distinct_reuse",TEST_MODE_NON_BLOCKING,grouped_empty_distinct_and_reuse},
        {"count/typed_global_policy",TEST_MODE_NON_BLOCKING,typed_global_and_policy_counts},
        {"count/recursive_source_negation",TEST_MODE_NON_BLOCKING,recursion_negation_and_multiple_counts},
        {"count/rejections_and_homonyms",TEST_MODE_NON_BLOCKING,binding_and_syntax_rejections},
        {"count/capacity_atomic_reuse",TEST_MODE_NON_BLOCKING,capacity_failure_and_reuse},
        {"count/backend_gate_ir",TEST_MODE_NON_BLOCKING,backend_gate_and_ir},
        {"count/prebound_repeated_projection",TEST_MODE_NON_BLOCKING,prebound_outputs_and_repeated_projection}
    };
    return test_main("count",cases,(int)(sizeof(cases)/sizeof(cases[0])),argc,argv);
}
