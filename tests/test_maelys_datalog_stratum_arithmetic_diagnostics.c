/* SPDX-License-Identifier: MPL-2.0 */
#include <maelys/datalog.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__func__,__LINE__,#x); return 1; } } while (0)
#define OK(x) CHECK((x) == MAELYS_DATALOG_STATUS_OK)
static int load(const char *source, maelys_datalog_policy_t **p, maelys_datalog_diagnostic_t *d) {
    int rc=maelys_datalog_policy_load_inline("diagnostics", "diagnostics", source, strlen(source), p, d);
    return rc;
}
static int strata_count_boundary(void) {
    const char *three = "a(X):-seed(X),not(seed(X)). b(X):-seed(X),not(a(X)). c(X):-seed(X),not(b(X)).";
    const char *four = "a(X):-seed(X),not(seed(X)). b(X):-seed(X),not(a(X)). c(X):-seed(X),not(b(X)). d(X):-seed(X),not(c(X)).";
    const char *five_reverse = "out(X):-seed(X),not(d(X)). d(X):-seed(X),not(c(X)). c(X):-seed(X),not(b(X)). b(X):-seed(X),not(a(X)). a(X):-seed(X),not(seed(X)).";
    maelys_datalog_policy_t *p=NULL;
    maelys_datalog_diagnostic_t d=MAELYS_DATALOG_DIAGNOSTIC_INIT;
    size_t limit=0;
    CHECK(MAELYS_DATALOG_LIMIT_MAX_RULE_FACTS==25);
    CHECK(MAELYS_DATALOG_LIMIT_MAX_STRATA==26);
    OK(maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_STRATA,&limit)); CHECK(limit==4);
    CHECK(!strcmp(maelys_datalog_limit_name(MAELYS_DATALOG_LIMIT_MAX_STRATA),"MAX_STRATA"));
    OK(load(three,&p,&d)); OK(maelys_datalog_policy_free(p)); p=NULL;
    const char *cases[]={four,five_reverse};
    for(size_t i=0;i<2;i++) {
        CHECK(load(cases[i],&p,&d)==MAELYS_DATALOG_STATUS_INVALID_FIELD); CHECK(!p);
        CHECK(d.code==MAELYS_DATALOG_DIAG_POLICY_NOT_STRATIFIABLE);
        CHECK(!strcmp(d.message,"negation depth exceeds stratum limit"));
        CHECK(d.present & MAELYS_DATALOG_DIAGNOSTIC_CAPACITY);
        CHECK(d.present & MAELYS_DATALOG_DIAGNOSTIC_DEPTH);
        CHECK(d.limit_kind==MAELYS_DATALOG_LIMIT_MAX_STRATA);
        CHECK(d.observed_count==5+i && d.limit==4);
        CHECK(d.depth==5+i && d.depth_limit==4);
    }
    puts("four strata accepted; five/six strata rejected with exact counts"); return 0;
}
static int cycle_is_not_depth(void) {
    const char *cases[]={"out(X):-a(X). a(X):-seed(X),not(b(X)). b(X):-a(X).",
        "b(X):-a(X). a(X):-seed(X),not(b(X)). out(X):-a(X).",
        "a(X):-seed(X),not(a(X))."};
    for(size_t i=0;i<3;i++) {
        maelys_datalog_policy_t *p=NULL;
        maelys_datalog_diagnostic_t d=MAELYS_DATALOG_DIAGNOSTIC_INIT;
        CHECK(load(cases[i],&p,&d)==MAELYS_DATALOG_STATUS_INVALID_FIELD); CHECK(!p);
        CHECK(d.code==MAELYS_DATALOG_DIAG_POLICY_NOT_STRATIFIABLE);
        CHECK(!strcmp(d.message,"negation cycle"));
        CHECK(d.present & MAELYS_DATALOG_DIAGNOSTIC_PREDICATE);
        CHECK(!strcmp(d.predicate,"a") || !strcmp(d.predicate,"b")); CHECK(d.arity==1);
        CHECK(!(d.present & (MAELYS_DATALOG_DIAGNOSTIC_CAPACITY|MAELYS_DATALOG_DIAGNOSTIC_DEPTH)));
    }
    maelys_datalog_policy_t *p=NULL; maelys_datalog_diagnostic_t d=MAELYS_DATALOG_DIAGNOSTIC_INIT;
    OK(load("a(X):-seed(X). a(X):-b(X). b(X):-a(X).",&p,&d));
    OK(maelys_datalog_policy_free(p));
    puts("negative cycles identify a cycle predicate, never the downstream out head"); return 0;
}
static int arithmetic_messages(void) {
    const char *sources[]={
        "out(X):-seed(X),X < 2147483647 * 2147483647 * 2147483647.",
        "out(X):-seed(X),X = true.",
        "out(X):-seed(X),X < X."};
    const char *messages[]={"arithmetic overflow","comparison operands have different kinds","ordering requires integer operands"};
    maelys_datalog_fact_t fact={.predicate="seed",.arity=1,.terms={{.kind=MAELYS_DATALOG_VALUE_INTEGER,.as.integer=1}}};
    for(size_t i=0;i<3;i++) {
        maelys_datalog_policy_t *p=NULL; maelys_datalog_session_t *s=NULL; maelys_datalog_result_t *r=NULL;
        maelys_datalog_diagnostic_t d=MAELYS_DATALOG_DIAGNOSTIC_INIT;
        OK(load(sources[i],&p,&d)); OK(maelys_datalog_session_create(p,0,&s));
        if(i==2) { fact.terms[0].kind=MAELYS_DATALOG_VALUE_SYMBOL; fact.terms[0].as.symbol="a"; }
        CHECK(maelys_datalog_session_solve(s,&fact,1,&r,&d)==MAELYS_DATALOG_STATUS_INVALID_FIELD);
        CHECK(!r); CHECK(d.code==MAELYS_DATALOG_DIAG_SOLVE_COMPARISON_TYPE_ERROR);
        if(strcmp(d.message,messages[i])) fprintf(stderr,"got: %s\n",d.message);
        CHECK(!strcmp(d.message,messages[i])); CHECK(d.present & MAELYS_DATALOG_DIAGNOSTIC_COMPARISON);
        CHECK(d.comparison_op==(i==1 ? 1 : 3)); /* EQ / LT */
        CHECK(d.lhs_kind==(i==2 ? 1 : 2)); /* SYMBOL / INT */
        CHECK(d.rhs_kind==(i==0 ? 2 : i==1 ? 3 : 1)); /* INT / BOOL / SYMBOL */
        CHECK(!(d.present & MAELYS_DATALOG_DIAGNOSTIC_CAPACITY));
        OK(maelys_datalog_session_free(s)); OK(maelys_datalog_policy_free(p));
    }
    puts("arithmetic overflow, mixed kinds, symbol ordering: three distinct messages"); return 0;
}
static int parser_wording_only(void) {
    const char *sources[]={"out(X):-seed(X),not(a(_)).", "out(X):-seed(X),_ = X.",
        "out(X):-seed(X),X = 1 + _.","allow():-true.","allow():-false."};
    const char *places[]={"not()","comparisons","arithmetic expressions","boolean constant is not a body literal","boolean constant is not a body literal"};
    for(size_t i=0;i<5;i++) {
        maelys_datalog_policy_t *p=NULL; maelys_datalog_diagnostic_t d=MAELYS_DATALOG_DIAGNOSTIC_INIT;
        CHECK(load(sources[i],&p,&d)==MAELYS_DATALOG_STATUS_INVALID_FIELD); CHECK(!p);
        CHECK(d.code==(i<3 ? MAELYS_DATALOG_DIAG_PARSER_ANONYMOUS_VARIABLE_IN_COMPARISON : MAELYS_DATALOG_DIAG_PARSER_INVALID_COMPARISON));
        if(!strstr(d.message,places[i])) fprintf(stderr,"got: %s\n",d.message);
        CHECK(strstr(d.message,places[i]));
    }
    maelys_datalog_policy_t *p=NULL; maelys_datalog_diagnostic_t d=MAELYS_DATALOG_DIAGNOSTIC_INIT;
    OK(load("allow():-true = true.",&p,&d)); OK(maelys_datalog_policy_free(p));
    puts("anonymous and boolean diagnostics retain codes and actual grammar"); return 0;
}
int main(int argc, char **argv) {
    maelys_datalog_predicate_t predicates[]={
        {"seed",1,MAELYS_DATALOG_PREDICATE_EDB},
        {"a",1,MAELYS_DATALOG_PREDICATE_IDB},{"b",1,MAELYS_DATALOG_PREDICATE_IDB},
        {"c",1,MAELYS_DATALOG_PREDICATE_IDB},{"d",1,MAELYS_DATALOG_PREDICATE_IDB},
        {"out",1,MAELYS_DATALOG_PREDICATE_IDB|MAELYS_DATALOG_PREDICATE_QUERY},
        {"allow",0,MAELYS_DATALOG_PREDICATE_IDB|MAELYS_DATALOG_PREDICATE_QUERY}};
    const char *atoms[]={"a","b"};
    maelys_datalog_domain_t domain={"diagnostics",predicates,7,atoms,2};
    OK(maelys_datalog_domain_register(&domain));
    if(argc==2 && !strcmp(argv[1],"--cycle")) return cycle_is_not_depth();
    CHECK(argc==1);
    CHECK(!strata_count_boundary()); CHECK(!cycle_is_not_depth());
    CHECK(!arithmetic_messages()); CHECK(!parser_wording_only()); return 0;
}
