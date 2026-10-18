/* SPDX-License-Identifier: MPL-2.0 */
#include <maelys/datalog.h>
#include <maelys/datalog_backend.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while (0)
#define OK(x) CHECK((x)==MAELYS_DATALOG_STATUS_OK)
static maelys_datalog_status_t load(const char *src, maelys_datalog_policy_t **p, maelys_datalog_diagnostic_t *d) {
    return maelys_datalog_policy_load_inline("wording", "wording", src, strlen(src), p, d);
}
static int aggregate_author_case(const char *name, const char *source) {
    maelys_datalog_policy_t *p=NULL;
    maelys_datalog_diagnostic_t d=MAELYS_DATALOG_DIAGNOSTIC_INIT;
    CHECK(load(source,&p,&d)==MAELYS_DATALOG_STATUS_INVALID_FIELD); CHECK(!p);
    CHECK(d.code==MAELYS_DATALOG_DIAG_PARSER_UNSAFE_VARIABLE);
    CHECK(!strcmp(d.message,"aggregate result variable must differ from the projected variable and must not occur in the source atom"));
    CHECK(strstr(d.hint,"count(U, member(G,U), N)"));
    CHECK(!strstr(d.message,"malformed") && !strstr(d.hint,"backend"));
    printf("%s: PASS\n",name); return 0;
}
static int aggregate_same_projection_result(void) {
    return aggregate_author_case("aggregate_same_projection_result", "out(U):-seed(G),count(U,member(G,U),U).");
}
static int aggregate_projection_absent(void) {
    return aggregate_author_case("aggregate_projection_absent", "out(N):-seed(G),count(U,member(G,X),N).");
}
static int aggregate_result_in_source(void) {
    return aggregate_author_case("aggregate_result_in_source", "out(N):-seed(N),count(U,member(N,U),N).");
}
static int aggregate_authors_never_malformed(void) {
    const char *ops[]={"count","min","max","sum"};
    const char *args[]={"U,member(G,U),U", "U,member(G,X),N", "U,member(N,U),N"};
    for (size_t i=0;i<4;++i) for (size_t j=0;j<3;++j) {
        char src[200]; snprintf(src,sizeof(src),"out(1):-seed(G),%s(%s).",ops[i],args[j]);
        CHECK(!aggregate_author_case("aggregate_author_not_malformed",src));
    }
    return 0;
}
static int aggregate_cycle_without_negation(void) {
    maelys_datalog_policy_t *p=NULL;
    maelys_datalog_diagnostic_t d=MAELYS_DATALOG_DIAGNOSTIC_INIT;
    CHECK(load("out(N):-count(U,out(U),N).",&p,&d)==MAELYS_DATALOG_STATUS_INVALID_FIELD);
    CHECK(!p && d.code==MAELYS_DATALOG_DIAG_POLICY_NOT_STRATIFIABLE);
    CHECK(strstr(d.message,"stratification cycle") && !strstr(d.message,"negation"));
    return 0;
}
static int input_capacity_case(int which) {
    maelys_datalog_policy_t *p=NULL; maelys_datalog_session_t *s=NULL; maelys_datalog_result_t *r=NULL;
    maelys_datalog_diagnostic_t d=MAELYS_DATALOG_DIAGNOSTIC_INIT;
    OK(load("out(X):-seed(X).",&p,&d)); OK(maelys_datalog_session_create(p,0,&s));
    size_t n=which==0 ? 129 : which==1 ? 33 : 1;
    maelys_datalog_fact_t *facts=calloc(n,sizeof(*facts)); CHECK(facts);
    char (*text)[1026]=calloc(n*4,sizeof(*text)); CHECK(text);
    for(size_t i=0;i<n;++i) {
        facts[i].predicate=which==0 ? "quad" : "seed"; facts[i].arity=which==0 ? 4 : 1;
        for(size_t j=0;j<facts[i].arity;++j) {
            char *v=text[i*4+j];
            if(which==0) snprintf(v,1026,"s%04zu",(n-1-i)*4+j);
            else { memset(v,'x',which==1 ? 1024 : 1025); if(which==1) { char prefix[32]; snprintf(prefix,sizeof(prefix),"%04zu",i); memcpy(v,prefix,4); } }
            facts[i].terms[j]=(maelys_datalog_value_t){.kind=MAELYS_DATALOG_VALUE_SYMBOL,.as.symbol=v};
        }
    }
    CHECK(maelys_datalog_session_solve(s,facts,n,&r,&d)==MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
    CHECK(!r && d.code==MAELYS_DATALOG_DIAG_OPERATION_REJECTED);
    CHECK(d.present & MAELYS_DATALOG_DIAGNOSTIC_CAPACITY);
    maelys_datalog_limit_t kind=which==0 ? MAELYS_DATALOG_LIMIT_MAX_SYMBOLS : which==1 ? MAELYS_DATALOG_LIMIT_STRING_POOL_BYTES : MAELYS_DATALOG_LIMIT_MAX_STRING_BYTES;
    size_t limit=0; OK(maelys_datalog_limit_get(kind,&limit)); CHECK(maelys_datalog_limit_name(kind));
    CHECK(d.limit_kind==kind && d.limit==limit);
    CHECK(d.observed_count==(which==0 ? 513u : which==1 ? 32800u : 1025u));
    if(which==0) CHECK(strstr(d.message,"index 0 (quad), term 0: symbol count limit (512)"));
    if(which==1) CHECK(strstr(d.message,"symbol storage byte limit (32768)"));
    if(which==2) CHECK(strstr(d.message,"symbol exceeds 1024 bytes"));
    /* Failure must leave the session usable and no dictionary insertion visible. */
    maelys_datalog_fact_t retry={.predicate="seed",.arity=1,.terms={{.kind=MAELYS_DATALOG_VALUE_SYMBOL,.as.symbol="retry"}}};
    OK(maelys_datalog_session_solve(s,&retry,1,&r,&d)); OK(maelys_datalog_result_free(r));
    OK(maelys_datalog_session_free(s)); OK(maelys_datalog_policy_free(p)); free(text);free(facts);return 0;
}
static int input_symbol_count_capacity(void) { return input_capacity_case(0); }
static int input_dictionary_bytes_capacity(void) { return input_capacity_case(1); }
static int input_symbol_length_status_and_capacity(void) { return input_capacity_case(2); }
static int rule_capacity_case(int expansion) {
    char src[5000]="";
    if(expansion) { strcpy(src,"out(X):-seed(X)");for(size_t i=1;i<129;++i)strcat(src," or seed(X)");strcat(src,"."); }
    else for(size_t i=0;i<129;++i) strcat(src,"out(X):-seed(X).\n");
    maelys_datalog_policy_t *p=NULL; maelys_datalog_diagnostic_t d=MAELYS_DATALOG_DIAGNOSTIC_INIT;
    CHECK(load(src,&p,&d)==MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE); CHECK(!p);
    CHECK(d.code==MAELYS_DATALOG_DIAG_PARSER_RULE_BODY_LITERAL_OVERFLOW);
    CHECK(!strcmp(d.message,expansion ? "OR expansion exceeds rule capacity" : "rule capacity exceeded"));
    CHECK(d.limit_kind==MAELYS_DATALOG_LIMIT_MAX_RULES && d.observed_count==129 && d.limit==128);
    return 0;
}
static int ordinary_rule_capacity_message(void) { return rule_capacity_case(0); }
static int or_expansion_capacity_message(void) { return rule_capacity_case(1); }
static int recursion_depth_wording(void) {
    maelys_datalog_policy_t *p=NULL; maelys_datalog_session_t *s=NULL; maelys_datalog_result_t *r=NULL;
    maelys_datalog_diagnostic_t d=MAELYS_DATALOG_DIAGNOSTIC_INIT;
    OK(load("out(X):-seed(X). out(Y):-out(X),edge(X,Y).",&p,&d)); OK(maelys_datalog_session_create(p,0,&s));
    maelys_datalog_fact_t f[10]={0}; f[0].predicate="seed";f[0].arity=1;
    f[0].terms[0]=(maelys_datalog_value_t){.kind=MAELYS_DATALOG_VALUE_INTEGER,.as.integer=0};
    for(size_t i=1;i<10;++i) { f[i].predicate="edge";f[i].arity=2;for(size_t j=0;j<2;++j)f[i].terms[j]=(maelys_datalog_value_t){.kind=MAELYS_DATALOG_VALUE_INTEGER,.as.integer=(int64_t)(i+j-1)}; }
    CHECK(maelys_datalog_session_solve(s,f,10,&r,&d)==MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
    CHECK(!r && d.code==MAELYS_DATALOG_DIAG_SOLVE_MAX_DEPTH);
    CHECK(!strcmp(d.message,"recursive evaluation exceeds iteration depth limit"));
    CHECK(strstr(d.hint,"fixed point")); CHECK(d.present & MAELYS_DATALOG_DIAGNOSTIC_DEPTH);
    CHECK(d.depth==10 && d.depth_limit==10);
    OK(maelys_datalog_session_free(s)); OK(maelys_datalog_policy_free(p));return 0;
}
static int twelve_rules_load_without_depth_error(void) {
    char src[1000]="out(X):-p10(X).\n";size_t used=strlen(src);
    for(size_t i=10;i>0;--i)used+=(size_t)snprintf(src+used,sizeof(src)-used,"p%zu(X):-p%zu(X).\n",i,i-1);
    strcat(src,"p0(X):-seed(X).");
    maelys_datalog_policy_t *p=NULL;maelys_datalog_diagnostic_t d=MAELYS_DATALOG_DIAGNOSTIC_INIT;
    OK(load(src,&p,&d)); OK(maelys_datalog_policy_free(p));return 0;
}
static int explanation_failure_fields(const maelys_datalog_diagnostic_t *d,
    maelys_datalog_status_t status, const char *message) {
    CHECK(d->status==status && d->code==MAELYS_DATALOG_DIAG_OPERATION_REJECTED);
    CHECK(!strcmp(d->message,message)); CHECK(d->hint[0]);
    CHECK(d->present & MAELYS_DATALOG_DIAGNOSTIC_PREDICATE);
    CHECK(!strcmp(d->predicate,"out") && d->arity==1);return 0;
}
static maelys_datalog_status_t callback_failure;
static int callback_at_requirements;
static maelys_datalog_status_t diagnosed_requirements(void *state, void *result,
    maelys_datalog_explanation_kind_t kind, size_t *bytes, size_t *alignment) {
    if(callback_at_requirements) return callback_failure;
    return maelys_datalog_backend_reference()->explanation_storage_requirements(state,result,kind,bytes,alignment);
}
static maelys_datalog_status_t diagnosed_prepare(void *state, void *result,
    maelys_datalog_explanation_kind_t kind, const char *predicate,
    const maelys_datalog_value_t *terms, size_t arity, void *storage, size_t bytes, size_t *required) {
    (void)state;(void)result;(void)kind;(void)predicate;(void)terms;(void)arity;
    (void)storage;(void)bytes;(void)required; return callback_failure;
}
static int explanation_callback_rejections(void) {
    maelys_datalog_policy_t *p=NULL;maelys_datalog_session_t *s=NULL;maelys_datalog_result_t *r=NULL;
    maelys_datalog_session_config_t *c=NULL;maelys_datalog_diagnostic_t d=MAELYS_DATALOG_DIAGNOSTIC_INIT;
    OK(load("out(X):-seed(X).",&p,&d));OK(maelys_datalog_session_config_create(&c));
    maelys_datalog_backend_t backend=*maelys_datalog_backend_reference();
    backend.explanation_storage_requirements=diagnosed_requirements;backend.explanation_prepare=diagnosed_prepare;
    OK(maelys_datalog_session_config_set_backend(c,&backend));OK(maelys_datalog_session_create_configured(p,0,c,&s));
    maelys_datalog_fact_t f={.predicate="seed",.arity=1,.terms={{.kind=MAELYS_DATALOG_VALUE_INTEGER,.as.integer=1}}};
    OK(maelys_datalog_session_solve(s,&f,1,&r,&d));
    size_t bytes=0,alignment=0;callback_at_requirements=0;
    OK(maelys_datalog_result_explanation_storage_requirements(r,MAELYS_DATALOG_EXPLAIN_TRUE,&bytes,&alignment));
    void *storage=malloc(bytes);CHECK(storage);maelys_datalog_prepared_explanation_t *prepared=NULL;
    for(callback_at_requirements=0;callback_at_requirements<2;++callback_at_requirements) {
        const maelys_datalog_status_t cases[]={MAELYS_DATALOG_STATUS_NOT_FOUND,MAELYS_DATALOG_STATUS_UNSUPPORTED};
        for(size_t i=0;i<2;++i) {
            callback_failure=cases[i];size_t required=777;
            CHECK(maelys_datalog_result_prepare_explanation_diagnosed(r,MAELYS_DATALOG_EXPLAIN_TRUE,"out",f.terms,1,storage,bytes,&prepared,&d)==callback_failure);
            CHECK(!prepared);CHECK(!explanation_failure_fields(&d,callback_failure,i ? "explanation kind not supported by the backend" : "query symbol absent from the result"));
            CHECK(maelys_datalog_result_explain_text_diagnosed(r,MAELYS_DATALOG_EXPLAIN_TRUE,"out",f.terms,1,NULL,0,&required,&d)==callback_failure);
            CHECK(required==777);CHECK(!explanation_failure_fields(&d,callback_failure,i ? "explanation kind not supported by the backend" : "query symbol absent from the result"));
        }
    }
    free(storage);OK(maelys_datalog_result_free(r));OK(maelys_datalog_session_free(s));
    OK(maelys_datalog_session_config_free(c));OK(maelys_datalog_policy_free(p));return 0;
}
static int explanation_diagnosed_failures(void) {
    maelys_datalog_policy_t *p=NULL;maelys_datalog_session_t *s=NULL;maelys_datalog_result_t *r=NULL;
    maelys_datalog_diagnostic_t d=MAELYS_DATALOG_DIAGNOSTIC_INIT;
    OK(load("out(X):-seed(X).",&p,&d)); OK(maelys_datalog_session_create(p,0,&s));
    maelys_datalog_fact_t fact={.predicate="seed",.arity=1,.terms={{.kind=MAELYS_DATALOG_VALUE_SYMBOL,.as.symbol="present"}}};
    OK(maelys_datalog_session_solve(s,&fact,1,&r,&d));size_t n=0,a=0,required=777;
    OK(maelys_datalog_result_explanation_storage_requirements(r,MAELYS_DATALOG_EXPLAIN_TRUE,&n,&a));
    void *storage=malloc(n);CHECK(storage);maelys_datalog_prepared_explanation_t *out=NULL;
    maelys_datalog_value_t missing={.kind=MAELYS_DATALOG_VALUE_SYMBOL,.as.symbol="missing"};
    for(int k=MAELYS_DATALOG_EXPLAIN_TRUE;k<=MAELYS_DATALOG_EXPLAIN_FALSE;k*=2) {
        CHECK(maelys_datalog_result_prepare_explanation_diagnosed(r,k,"out",&missing,1,storage,n,&out,&d)==MAELYS_DATALOG_STATUS_NOT_FOUND);
        CHECK(!out);CHECK(!explanation_failure_fields(&d,MAELYS_DATALOG_STATUS_NOT_FOUND,"query symbol absent from the result"));
        CHECK(!strcmp(d.hint,"query a symbol the result contains"));
        CHECK(maelys_datalog_result_explain_text_diagnosed(r,k,"out",&missing,1,NULL,0,&required,&d)==MAELYS_DATALOG_STATUS_NOT_FOUND);
        CHECK(required==777);CHECK(!explanation_failure_fields(&d,MAELYS_DATALOG_STATUS_NOT_FOUND,"query symbol absent from the result"));
    }
    CHECK(maelys_datalog_result_prepare_explanation_diagnosed(r,MAELYS_DATALOG_EXPLAIN_TRUE,"out",fact.terms,1,NULL,n,&out,&d)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    CHECK(!explanation_failure_fields(&d,MAELYS_DATALOG_STATUS_INVALID_ARGUMENT,"explanation preparation failed"));
    CHECK(maelys_datalog_result_explain_text_diagnosed(r,MAELYS_DATALOG_EXPLAIN_TRUE,"seed",fact.terms,1,NULL,0,&required,&d)==MAELYS_DATALOG_STATUS_INVALID_FIELD);
    CHECK(!strcmp(d.message,"unknown or non-query predicate") && !strcmp(d.predicate,"seed"));
    OK(maelys_datalog_result_prepare_explanation_diagnosed(r,MAELYS_DATALOG_EXPLAIN_TRUE,"out",fact.terms,1,storage,n,&out,&d));
    CHECK(d.code==MAELYS_DATALOG_DIAG_NONE && !d.present && !d.message[0]);
    OK(maelys_datalog_prepared_explanation_release(out));
    size_t legacy=0,diagnosed=0;
    OK(maelys_datalog_result_explain_true_text(r,"out",fact.terms,1,NULL,0,&legacy));
    OK(maelys_datalog_result_explain_text_diagnosed(r,MAELYS_DATALOG_EXPLAIN_TRUE,"out",fact.terms,1,NULL,0,&diagnosed,NULL));
    CHECK(legacy==diagnosed);
    char *old_text=malloc(legacy+1),*new_text=malloc(legacy+1); CHECK(old_text && new_text);
    OK(maelys_datalog_result_explain_true_text(r,"out",fact.terms,1,old_text,legacy+1,&legacy));
    OK(maelys_datalog_result_explain_text_diagnosed(r,MAELYS_DATALOG_EXPLAIN_TRUE,"out",fact.terms,1,new_text,legacy+1,&diagnosed,&d));
    CHECK(!strcmp(old_text,new_text));free(old_text);free(new_text);
    free(storage);OK(maelys_datalog_result_free(r));OK(maelys_datalog_session_free(s));
    maelys_datalog_session_config_t *c=NULL;OK(maelys_datalog_session_config_create(&c));
    OK(maelys_datalog_session_config_set_explanation_workspace(c,MAELYS_DATALOG_EXPLAIN_TRUE));
    OK(maelys_datalog_session_create_configured(p,0,c,&s));OK(maelys_datalog_session_solve(s,&fact,1,&r,&d));
    CHECK(maelys_datalog_result_explain_text_diagnosed(r,MAELYS_DATALOG_EXPLAIN_FALSE,"out",fact.terms,1,NULL,0,&required,&d)==MAELYS_DATALOG_STATUS_UNSUPPORTED);
    CHECK(!explanation_failure_fields(&d,MAELYS_DATALOG_STATUS_UNSUPPORTED,"explanation kind not reserved at prepare"));
    OK(maelys_datalog_result_free(r));OK(maelys_datalog_session_free(s));OK(maelys_datalog_session_config_free(c));
    OK(maelys_datalog_policy_free(p));return 0;
}

int main(int argc, char **argv) {
    maelys_datalog_predicate_t predicates[18]={
        {"seed",1,MAELYS_DATALOG_PREDICATE_EDB},{"member",2,MAELYS_DATALOG_PREDICATE_EDB},
        {"quad",4,MAELYS_DATALOG_PREDICATE_EDB},{"edge",2,MAELYS_DATALOG_PREDICATE_EDB},
        {"out",1,MAELYS_DATALOG_PREDICATE_IDB|MAELYS_DATALOG_PREDICATE_QUERY}};
    char names[12][32];for(size_t i=0;i<12;++i){snprintf(names[i],sizeof(names[i]),"p%zu",i);predicates[5+i]=(maelys_datalog_predicate_t){names[i],1,MAELYS_DATALOG_PREDICATE_IDB};}
    maelys_datalog_domain_t domain={"wording",predicates,17,NULL,0}; OK(maelys_datalog_domain_register(&domain));
    const struct {const char *name;int (*run)(void);} tests[]={
#define CASE(n) {#n,n}
        CASE(aggregate_same_projection_result),CASE(aggregate_projection_absent),CASE(aggregate_result_in_source),
        CASE(aggregate_authors_never_malformed),CASE(aggregate_cycle_without_negation),
        CASE(input_symbol_count_capacity),CASE(input_dictionary_bytes_capacity),CASE(input_symbol_length_status_and_capacity),
        CASE(ordinary_rule_capacity_message),CASE(or_expansion_capacity_message),CASE(recursion_depth_wording),
        CASE(twelve_rules_load_without_depth_error),CASE(explanation_diagnosed_failures),CASE(explanation_callback_rejections)
    };
    CHECK(argc==1 || argc==2); int ran=0;
    for(size_t i=0;i<sizeof(tests)/sizeof(*tests);++i) {
        if(argc==2 && strcmp(argv[1],tests[i].name)) continue;
        ran=1;CHECK(!tests[i].run());printf("%s PASS\n",tests[i].name);
    }
    CHECK(ran);return 0;
}
