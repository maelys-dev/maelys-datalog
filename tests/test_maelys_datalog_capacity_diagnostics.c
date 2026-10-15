/* SPDX-License-Identifier: MPL-2.0 */
#include <maelys/datalog.h>
#include <maelys/datalog_resources.h>
#include "src/core/maelys_datalog_parser.h"
#include "src/compiler/maelys_datalog_program_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while(0)
static maelys_datalog_internal_ruleset_t rules;
static char source[100000];
static const maelys_datalog_predicate_t predicates[] = {
    {"seed",1,MAELYS_DATALOG_PREDICATE_EDB},
    {"quad",4,MAELYS_DATALOG_PREDICATE_EDB},
    {"out",1,MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY}
};
static int capacity(const char *name, const maelys_datalog_diagnostic_t *diag,
    maelys_datalog_limit_t kind, size_t observed, size_t limit) {
    if (!(diag->present & MAELYS_DATALOG_DIAGNOSTIC_CAPACITY) ||
        !diag->limit_kind || diag->limit_kind != kind || diag->observed_count != observed || diag->limit != limit) {
        fprintf(stderr,"%s: got kind=%d count=%zu limit=%zu present=%llu; expected %d %zu %zu\n",name,
            diag->limit_kind,diag->observed_count,diag->limit,(unsigned long long)diag->present,kind,observed,limit);
        return 1;
    }
    printf("capacity: %s PASS\n",name); return 0;
}
static int load_case(const char *name, maelys_datalog_limit_t kind, size_t observed,
    maelys_datalog_diag_code_t code) {
    size_t limit=0; CHECK(!maelys_datalog_limit_get(kind,&limit));
    memset(&rules,0,sizeof(rules));
    CHECK(!maelys_datalog_ruleset_init(&rules,"capacity","capacity","unset",1));
    for(size_t i=0;i<sizeof(predicates)/sizeof(*predicates);i++)
        CHECK(!maelys_datalog_predicate_registry_add_domain(&rules.registry,predicates[i].name,predicates[i].arity,predicates[i].flags));
    CHECK(!maelys_datalog_predicate_registry_freeze(&rules.registry));
    maelys_datalog_internal_diagnostic_t internal;
    maelys_result_t rc=maelys_datalog_parse_ruleset_ex_with_flags(&rules,source,strlen(source),"capacity.dl",
        MAELYS_DATALOG_PARSE_ALLOW_UNDECLARED_POLICY_ATOMS,&internal);
    if(rc!=MAELYS_ERR_PAYLOAD_TOO_LARGE) fprintf(stderr,"%s rc=%d code=%d: %s\n",name,rc,internal.code,internal.message);
    CHECK(rc==MAELYS_ERR_PAYLOAD_TOO_LARGE);
    maelys_datalog_diagnostic_t diag=MAELYS_DATALOG_DIAGNOSTIC_INIT;
    maelys_datalog_copy_load_diagnostic(&diag,&internal,rc);
    CHECK(diag.code==code);
    return capacity(name,&diag,kind,observed,limit);
}
static void repeat(char *out, size_t n, char byte) { memset(out,byte,n);out[n]=0; }
static int loads(void) {
    char text[1100];repeat(text,1025,'x');snprintf(source,sizeof(source),"out(1):-seed(\"%s\").",text);
    CHECK(!load_case("lexer string +1",MAELYS_DATALOG_LIMIT_MAX_STRING_BYTES,1025,MAELYS_DATALOG_DIAG_LEXER_STRING_TOO_LONG));
    repeat(text,64,'x');snprintf(source,sizeof(source),"out(1):-seed(\"%s\").",text);
    CHECK(!load_case("policy atom bytes +1",MAELYS_DATALOG_LIMIT_MAX_POLICY_ATOM_BYTES,64,MAELYS_DATALOG_DIAG_LEXER_STRING_TOO_LONG));
    size_t used=0;
    for(size_t i=0;i<65;i++) used+=(size_t)snprintf(source+used,sizeof(source)-used,
        "out(1):-quad(\"v%zu\",\"v%zu\",\"v%zu\",\"v%zu\").\n",4*i,4*i+1,4*i+2,4*i+3);
    CHECK(!load_case("policy atoms +1",MAELYS_DATALOG_LIMIT_MAX_POLICY_ATOMS,257,MAELYS_DATALOG_DIAG_LEXER_STRING_TOO_LONG));
    source[0]=0;for(size_t i=0;i<129;i++)strcat(source,"out(X):-seed(X).\n");
    CHECK(!load_case("rules_129_names_MAX_RULES",MAELYS_DATALOG_LIMIT_MAX_RULES,129,MAELYS_DATALOG_DIAG_PARSER_RULE_BODY_LITERAL_OVERFLOW));
    strcpy(source,"out(X):-seed(X)");for(size_t i=1;i<129;i++)strcat(source," or seed(X)");strcat(source,".");
    CHECK(!load_case("OR expansion 129 names MAX_RULES",MAELYS_DATALOG_LIMIT_MAX_RULES,129,MAELYS_DATALOG_DIAG_PARSER_RULE_BODY_LITERAL_OVERFLOW));
    strcpy(source,"out(X):-seed(X),seed(X),seed(X),seed(X),seed(X),seed(X),seed(X),seed(X),seed(X).");
    CHECK(!load_case("body literals +1",MAELYS_DATALOG_LIMIT_MAX_BODY_LITERALS,9,MAELYS_DATALOG_DIAG_PARSER_RULE_BODY_LITERAL_OVERFLOW));
    strcpy(source,"out(1):-quad(1,2,3,4,5).");
    CHECK(!load_case("arity +1",MAELYS_DATALOG_LIMIT_MAX_ARITY,5,MAELYS_DATALOG_DIAG_PARSER_ARITY_MISMATCH));
    strcpy(source,"out(1):-quad(_,_,_,_),quad(_,_,_,_).");
    CHECK(!load_case("anonymous variables +1",MAELYS_DATALOG_LIMIT_MAX_RULE_VARIABLES,33,MAELYS_DATALOG_DIAG_PARSER_TOO_MANY_VARIABLES));
    strcpy(source,"out(X):-seed(X),X=1");for(size_t i=0;i<16;i++)strcat(source,"+1");strcat(source,".");
    CHECK(!load_case("expression nodes +1",MAELYS_DATALOG_LIMIT_MAX_ARITH_EXPR_NODES,33,MAELYS_DATALOG_DIAG_PARSER_INVALID_COMPARISON));
    strcpy(source,"out(X):-seed(X),X=(((((((((1))))))))).");
    CHECK(!load_case("expression depth +1",MAELYS_DATALOG_LIMIT_MAX_ARITH_EXPR_DEPTH,9,MAELYS_DATALOG_DIAG_PARSER_INVALID_COMPARISON));
    repeat(text,257,'x');snprintf(source,sizeof(source),"out(X):-seed(X),starts_with(X,\"%s\").",text);
    CHECK(!load_case("filter bytes +1",MAELYS_DATALOG_LIMIT_MAX_FILTER_PATTERN_BYTES,257,MAELYS_DATALOG_DIAG_PARSER_INVALID_FILTER));
    source[0]=0;size_t remaining=129;
    while(remaining){strcat(source,"out(X):-seed(X)");size_t count=remaining>7?7:remaining;for(size_t j=0;j<count;j++)strcat(source,",starts_with(X,\"\")");strcat(source,".\n");remaining-=count;}
    CHECK(!load_case("filter programs +1",MAELYS_DATALOG_LIMIT_MAX_FILTER_PROGRAMS,129,MAELYS_DATALOG_DIAG_PARSER_INVALID_FILTER));
    repeat(text,256,'x');used=0;for(size_t i=0;i<32;i++) used+=(size_t)snprintf(source+used,sizeof(source)-used,"out(X):-seed(X),starts_with(X,\"%s\").\n",text);
    strcat(source,"out(X):-seed(X),starts_with(X,\"x\").");
    CHECK(!load_case("filter pool +1",MAELYS_DATALOG_LIMIT_FILTER_PATTERN_POOL_BYTES,8193,MAELYS_DATALOG_DIAG_PARSER_INVALID_FILTER));
    return 0;
}
/* Public loader boundary: two relations keep each below SMALL's 64-fact ceiling. */
static int direct_facts_128_load_129_names_MAX_RULE_FACTS(void) {
    const maelys_datalog_predicate_t facts[] = {
        {"fixed_a", 1, MAELYS_DATALOG_PREDICATE_POLICY_FACT},
        {"fixed_b", 1, MAELYS_DATALOG_PREDICATE_POLICY_FACT}
    };
    const maelys_datalog_domain_t domain = {"direct_fact_capacity", facts, 2, NULL, 0};
    CHECK(maelys_datalog_domain_register(&domain) == MAELYS_DATALOG_STATUS_OK);
    size_t limit = 0;
    CHECK(MAELYS_DATALOG_LIMIT_EXECUTION_BYTES == 24);
    CHECK(MAELYS_DATALOG_LIMIT_MAX_RULE_FACTS == 25);
    CHECK(maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_RULE_FACTS, &limit) == MAELYS_DATALOG_STATUS_OK);
    CHECK(limit == 128);
    CHECK(strcmp(maelys_datalog_limit_name(MAELYS_DATALOG_LIMIT_MAX_RULE_FACTS), "MAX_RULE_FACTS") == 0);
    size_t used = 0;
    for (size_t i = 0; i < limit; ++i)
        used += (size_t)snprintf(source + used, sizeof(source) - used,
            "fixed_%c(%zu).\n", i < 64 ? 'a' : 'b', i % 64);
    maelys_datalog_policy_t *policy = NULL;
    maelys_datalog_diagnostic_t diag = MAELYS_DATALOG_DIAGNOSTIC_INIT;
    CHECK(maelys_datalog_policy_load_inline(domain.name, "direct_facts", source, used,
        &policy, &diag) == MAELYS_DATALOG_STATUS_OK);
    CHECK(policy != NULL);
    CHECK(maelys_datalog_policy_free(policy) == MAELYS_DATALOG_STATUS_OK);
    policy = NULL;
    used += (size_t)snprintf(source + used, sizeof(source) - used, "fixed_a(64).\n");
    CHECK(maelys_datalog_policy_load_inline(domain.name, "direct_facts", source, used,
        &policy, &diag) == MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
    CHECK(policy == NULL);
    CHECK(!capacity("direct_facts_129_names_MAX_RULE_FACTS", &diag,
        MAELYS_DATALOG_LIMIT_MAX_RULE_FACTS, 129, 128));
    CHECK(diag.code == MAELYS_DATALOG_DIAG_OPERATION_REJECTED);
    CHECK(strcmp(diag.phase, "parser") == 0);
    CHECK(diag.present & MAELYS_DATALOG_DIAGNOSTIC_LOCATION);
    CHECK(diag.line == 129 && diag.column == 12);
    printf("capacity: direct_facts_128_load PASS\n");
    return 0;
}
static int batches(void) {
    const maelys_datalog_domain_t domain={"capacity_inputs",predicates,3,NULL,0};CHECK(!maelys_datalog_domain_register(&domain));
    maelys_datalog_policy_t *policy=NULL;maelys_datalog_session_t *session=NULL;
    const char *program="out(X):-seed(X).";
    CHECK(!maelys_datalog_policy_load_inline(domain.name,"capacity",program,strlen(program),&policy,NULL));
    CHECK(!maelys_datalog_session_create(policy,0,&session));
    size_t edb=0,per=0;CHECK(!maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_EDB_FACTS,&edb));CHECK(!maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_FACTS_PER_PRED,&per));
    maelys_datalog_fact_t *facts=calloc(edb+1,sizeof(*facts));CHECK(facts);
    for(size_t i=0;i<=edb;i++){facts[i].predicate="seed";facts[i].arity=1;facts[i].terms[0].kind=MAELYS_DATALOG_VALUE_INTEGER;facts[i].terms[0].as.integer=(int64_t)i;}
    maelys_datalog_result_t *result=NULL;maelys_datalog_diagnostic_t diag=MAELYS_DATALOG_DIAGNOSTIC_INIT;
    CHECK(maelys_datalog_session_solve(session,facts,edb+1,&result,&diag)==MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
    CHECK(!capacity("input batch EDB +1",&diag,MAELYS_DATALOG_LIMIT_MAX_EDB_FACTS,edb+1,edb));CHECK(diag.code==MAELYS_DATALOG_DIAG_OPERATION_REJECTED);CHECK(strstr(diag.message,"before deduplication"));CHECK(!result);
    CHECK(maelys_datalog_session_solve(session,facts,per+1,&result,&diag)==MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
    CHECK(!capacity("input batch per predicate +1",&diag,MAELYS_DATALOG_LIMIT_MAX_FACTS_PER_PRED,per+1,per));CHECK(strstr(diag.message,"per-predicate fact limit"));CHECK(diag.code==MAELYS_DATALOG_DIAG_OPERATION_REJECTED);CHECK(!result);
    CHECK(!maelys_datalog_session_solve(session,facts,1,&result,&diag));CHECK(!maelys_datalog_result_free(result));CHECK(!maelys_datalog_session_free(session));
    maelys_datalog_session_config_t *config=NULL;CHECK(!maelys_datalog_session_config_create(&config));
    maelys_datalog_session_resource_request_t resources=MAELYS_DATALOG_RESOURCE_REQUEST_INIT;resources.capacity_mask=MAELYS_DATALOG_CAPACITY_INPUT_FACTS;resources.input_facts=1;
    CHECK(!maelys_datalog_session_config_set_resources(config,&resources));CHECK(!maelys_datalog_session_create_configured(policy,0,config,&session));
    CHECK(maelys_datalog_session_solve(session,facts,2,&result,&diag)==MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
    CHECK(!capacity("session input reservation +1",&diag,MAELYS_DATALOG_LIMIT_MAX_EDB_FACTS,2,1));CHECK(!result);
    CHECK(!maelys_datalog_session_free(session));CHECK(!maelys_datalog_session_config_free(config));CHECK(!maelys_datalog_policy_free(policy));free(facts);return 0;
}
int main(void) {
    CHECK(!maelys_datalog_limit_name(0));CHECK(!maelys_datalog_limit_name((maelys_datalog_limit_t)-1));CHECK(!maelys_datalog_limit_name((maelys_datalog_limit_t)999));
    CHECK(MAELYS_DATALOG_LIMIT_MAX_POLICY_ATOM_BYTES==14);
    for(int kind=1;kind<=MAELYS_DATALOG_LIMIT_MAX_FILTER_COST_UNITS;kind++){size_t limit=0;CHECK(maelys_datalog_limit_name((maelys_datalog_limit_t)kind));CHECK(!maelys_datalog_limit_get((maelys_datalog_limit_t)kind,&limit));CHECK(limit);}
    /* Known session dimensions and unknown identifiers have distinct statuses. */
    for(int kind=MAELYS_DATALOG_LIMIT_SESSION_ARENA_BYTES;kind<=MAELYS_DATALOG_LIMIT_EXECUTION_BYTES;kind++) {
        size_t value=777; CHECK(maelys_datalog_limit_name((maelys_datalog_limit_t)kind));
        CHECK(maelys_datalog_limit_get((maelys_datalog_limit_t)kind,&value)==MAELYS_DATALOG_STATUS_UNSUPPORTED); CHECK(value==777);
    }
    size_t value=777;
    CHECK(maelys_datalog_limit_get((maelys_datalog_limit_t)999,&value)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT); CHECK(value==777);
    CHECK(!loads());CHECK(!direct_facts_128_load_129_names_MAX_RULE_FACTS());CHECK(!batches());return 0;
}
