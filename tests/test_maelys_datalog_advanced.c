/* SPDX-License-Identifier: MPL-2.0 */
#include "maelys/datalog_advanced.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while(0)
#define OK(x) CHECK((x) == MAELYS_DATALOG_STATUS_OK)
static unsigned calls;
static const maelys_datalog_predicate_t predicates[] = {
    {"seed",1,MAELYS_DATALOG_PREDICATE_EDB},
    {"blocked",1,MAELYS_DATALOG_PREDICATE_EDB},
    {"allow",1,MAELYS_DATALOG_PREDICATE_IDB|MAELYS_DATALOG_PREDICATE_QUERY}
};
static maelys_datalog_status_t install(maelys_datalog_domain_builder_t *b) {
    ++calls;
    for(size_t i=0;i<3;++i) {
        maelys_datalog_status_t rc=maelys_datalog_domain_builder_add(b,&predicates[i]);
        if(rc)return rc;
    }
    return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t invalid_install(maelys_datalog_domain_builder_t *b) {
    ++calls;
    (void)maelys_datalog_domain_builder_add(b,NULL);
    (void)maelys_datalog_domain_builder_add(b,&predicates[0]);
    return MAELYS_DATALOG_STATUS_OK; /* ignored failure remains fatal */
}
static maelys_datalog_status_t obsolete_lower(const char *s,size_t n,maelys_datalog_program_builder_t *b,maelys_datalog_diagnostic_t *d) {
    (void)s;(void)n;(void)b;(void)d;++calls;return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t obsolete_prepare(const maelys_datalog_program_t *p,const maelys_datalog_backend_storage_t *storage,void **o) {
    (void)storage;
    (void)p;(void)o;++calls;return MAELYS_DATALOG_STATUS_OK;
}
static int diagnostics_and_callbacks(void) {
    struct { maelys_datalog_diagnostic_t d; unsigned char tail[32]; } box;
    memset(&box,0xa7,sizeof(box));
    OK(maelys_datalog_diagnostic_init(&box,sizeof(box)));
    CHECK(box.d.struct_size==sizeof(box));
    for(size_t i=0;i<sizeof(box.tail);++i) CHECK(box.tail[i]==0xa7);
    maelys_datalog_domain_t dom={"advanced",NULL,0,NULL,0};
    OK(maelys_datalog_domain_register_advanced(&dom,"owned metadata",install));
    OK(maelys_datalog_domain_register_advanced(&dom,"owned metadata",install));
    CHECK(maelys_datalog_domain_register_advanced(&dom,"other",install)==MAELYS_DATALOG_STATUS_INVALID_FIELD);
    CHECK(calls==0);
    maelys_datalog_policy_t *p=(void *)1;
    const char source[]="allow(X) :- seed(X), not(blocked(X)).";
    unsigned char snapshot[sizeof(box)];
    box.d.struct_size=sizeof(box.d)-1;memcpy(snapshot,&box,sizeof(box));
    CHECK(maelys_datalog_policy_load_inline("advanced","p",source,strlen(source),&p,&box.d)==MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL);
    CHECK(calls==0 && !memcmp(snapshot,&box,sizeof(box)));
    box.d.struct_size=sizeof(box);box.d.abi_version=99;memcpy(snapshot,&box,sizeof(box));
    CHECK(maelys_datalog_policy_load_inline("advanced","p",source,strlen(source),&p,&box.d)==MAELYS_DATALOG_STATUS_UNSUPPORTED);
    CHECK(calls==0 && !memcmp(snapshot,&box,sizeof(box)));
    OK(maelys_datalog_diagnostic_init(&box,sizeof(box)));
    maelys_datalog_frontend_t old={1u,sizeof(old),"old","old.v1",obsolete_lower};
    CHECK(maelys_datalog_policy_load_frontend("advanced","p",source,strlen(source),&old,&p,&box.d)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    CHECK(calls==0 && !p);
    OK(maelys_datalog_policy_load_inline("advanced","p",source,strlen(source),&p,&box.d));
    CHECK(calls==1);
    maelys_datalog_backend_t old_backend=*maelys_datalog_backend_reference();
    old_backend.abi_version=3;old_backend.prepare=obsolete_prepare;
    maelys_datalog_session_options_t options={MAELYS_DATALOG_BACKEND_ABI_VERSION,sizeof(options),&old_backend,0,0};
    maelys_datalog_session_t *s=(void *)1;
    CHECK(maelys_datalog_session_create_ex(p,0,&options,&s)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    CHECK(calls==1 && !s);
    OK(maelys_datalog_policy_free(p));
    CHECK(maelys_datalog_policy_load_inline("advanced","p","allow(X) :- missing(X).",23,&p,&box.d)==MAELYS_DATALOG_STATUS_INVALID_FIELD);
    CHECK(box.d.status==MAELYS_DATALOG_STATUS_INVALID_FIELD && box.d.code==MAELYS_DATALOG_DIAG_PARSER_UNKNOWN_PREDICATE);
    CHECK(box.d.present & MAELYS_DATALOG_DIAGNOSTIC_LOCATION);
    CHECK(box.d.present & MAELYS_DATALOG_DIAGNOSTIC_PREDICATE);
    CHECK(!strcmp(box.d.predicate,"missing"));
    for(size_t i=0;i<sizeof(box.tail);++i)CHECK(box.tail[i]==0xa7);
    dom.name="advanced_bad";
    OK(maelys_datalog_domain_register_advanced(&dom,NULL,invalid_install));
    CHECK(maelys_datalog_policy_load_inline(dom.name,"p",source,strlen(source),&p,&box.d)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    CHECK(!p);
    return 0;
}
static int structured(void) {
    const char source[]="allow(X) :- seed(X), not(blocked(X)).";
    maelys_datalog_policy_t *p=NULL; maelys_datalog_session_t *s=NULL;
    OK(maelys_datalog_policy_load_inline("advanced","p",source,strlen(source),&p,NULL));
    maelys_datalog_session_config_t *config=NULL;
    OK(maelys_datalog_session_config_create(&config));
    OK(maelys_datalog_session_config_set_backend(config,maelys_datalog_backend_reference()));
    OK(maelys_datalog_session_config_set_requirements(config,MAELYS_DATALOG_CAP_NEGATION,0));
    OK(maelys_datalog_session_create_configured(p,0,config,&s));
    OK(maelys_datalog_session_config_free(config));
    OK(maelys_datalog_policy_free(p));
    maelys_datalog_value_t one={MAELYS_DATALOG_VALUE_INTEGER,{.integer=1}}, two={MAELYS_DATALOG_VALUE_INTEGER,{.integer=2}};
    maelys_datalog_fact_t facts[]={{"seed",1,{one}},{"seed",1,{two}},{"blocked",1,{two}}};
    maelys_datalog_result_t *result=NULL;
    OK(maelys_datalog_session_solve(s,facts,3,&result,NULL));
    maelys_datalog_filter_statistics_t stats;
    OK(maelys_datalog_result_filter_statistics(result,&stats)); CHECK(!stats.evaluations && !stats.cost_units);
    for(unsigned kind=1;kind<=2;++kind) {
        size_t bytes,alignment;
        OK(maelys_datalog_result_explanation_storage_requirements(result,(maelys_datalog_explanation_kind_t)kind,&bytes,&alignment));
        void *arena=malloc(bytes);CHECK(arena);
        maelys_datalog_prepared_explanation_t *e=NULL;
        OK(maelys_datalog_result_prepare_explanation(result,(maelys_datalog_explanation_kind_t)kind,"allow",kind==1?&one:&two,1,arena,bytes,&e));
        maelys_datalog_explanation_info_t info;
        OK(maelys_datalog_prepared_explanation_info(e,&info));
        if(kind==1) {
            CHECK(info.found && !info.truncated && info.step_count==1 && info.premise_count==2);
            maelys_datalog_explanation_step_view_t step;
            OK(maelys_datalog_prepared_explanation_step(e,0,&step));CHECK(!strcmp(step.fact.predicate,"allow") && step.fact.terms[0].as.integer==1);
            maelys_datalog_explanation_premise_view_t premise;
            OK(maelys_datalog_prepared_explanation_premise(e,step.premise_begin,&premise));
            CHECK(premise.kind==MAELYS_DATALOG_EXPLANATION_PREMISE_POSITIVE_FACT && premise.origin==MAELYS_DATALOG_EXPLANATION_ORIGIN_EDB);
            OK(maelys_datalog_prepared_explanation_premise(e,step.premise_begin+1,&premise));
            CHECK(premise.kind==MAELYS_DATALOG_EXPLANATION_PREMISE_NEGATED_ABSENCE && !strcmp(premise.atom.predicate,"blocked"));
            CHECK(maelys_datalog_prepared_explanation_step(e,1,&step)==MAELYS_DATALOG_STATUS_NOT_FOUND);
        } else {
            CHECK(info.false_status==MAELYS_DATALOG_WHY_FALSE_STATUS_COMPLETE && info.diagnostic_count==1);
            maelys_datalog_explanation_obstacle_view_t obstacle;
            OK(maelys_datalog_prepared_explanation_obstacle(e,0,&obstacle));
            CHECK(obstacle.obstacle_kind==MAELYS_DATALOG_WHY_FALSE_OBSTACLE_NEGATIVE_CONTRADICTED);
            CHECK(!strcmp(obstacle.pattern.predicate,"blocked") && obstacle.pattern.terms[0].as.integer==2);
            CHECK(obstacle.support_count==1 && !strcmp(obstacle.supports[0].fact.predicate,"seed"));
        }
        CHECK(maelys_datalog_prepared_explanation_info(e,arena)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
        CHECK(maelys_datalog_result_free(result)==MAELYS_DATALOG_STATUS_INVALID_STATE);
        OK(maelys_datalog_prepared_explanation_release(e));
        CHECK(maelys_datalog_prepared_explanation_info(e,&info)==MAELYS_DATALOG_STATUS_INVALID_STATE);
        free(arena);
    }
    OK(maelys_datalog_result_free(result)); OK(maelys_datalog_session_free(s));
    maelys_datalog_decision_t d;
    const maelys_datalog_decision_t expected[]={MAELYS_DATALOG_DECISION_DENY_DEFAULT,MAELYS_DATALOG_DECISION_ALLOW,MAELYS_DATALOG_DECISION_REDUCED,MAELYS_DATALOG_DECISION_REDUCED,MAELYS_DATALOG_DECISION_DENY,MAELYS_DATALOG_DECISION_DENY_CONFLICT,MAELYS_DATALOG_DECISION_DENY_CONFLICT,MAELYS_DATALOG_DECISION_DENY_CONFLICT};
    for(unsigned i=0;i<8;++i){OK(maelys_datalog_decision_from_presence(i&1,(i>>1)&1,(i>>2)&1,&d));CHECK(d==expected[i]);}
    CHECK(maelys_datalog_decision_from_presence(2,0,0,&d)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    return 0;
}
static int owned_policy_and_bundle(void) {
    size_t bytes, alignment;
    OK(maelys_datalog_policy_storage_requirements(&bytes,&alignment));
    unsigned char *arena=malloc(bytes+32), *snapshot=malloc(bytes+32); CHECK(arena && snapshot);
    CHECK((uintptr_t)arena % alignment==0);
    memset(arena,0xa5,bytes+32);memcpy(snapshot,arena,bytes+32);
    const char source[]="allow(X) :- seed(X).";
    maelys_datalog_policy_t *p=(void *)1;
    CHECK(maelys_datalog_policy_load_frontend_in(arena,bytes-1,"advanced","one",source,strlen(source),NULL,&p,NULL)==MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL);
    CHECK(!p && !memcmp(arena,snapshot,bytes+32));
    CHECK(maelys_datalog_policy_load_frontend_in(arena+1,bytes,"advanced","one",source,strlen(source),NULL,&p,NULL)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    CHECK(!p && !memcmp(arena,snapshot,bytes+32));
    OK(maelys_datalog_policy_load_frontend_in(arena,bytes,"advanced","one",source,strlen(source),NULL,&p,NULL));
    CHECK((void *)p==(void *)arena);
    OK(maelys_datalog_policy_free(p));
    CHECK(maelys_datalog_policy_free(p)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    const char json[]="{\"policy_set_id\":\"advanced\",\"policy_set_version\":\"1\",\"manifest_version\":\"1\",\"policies\":[{\"policy_id\":\"p0\",\"domain\":\"advanced\",\"file\":\"ignored.dl\",\"sha256\":\"59b5e9ba254730e97e2498721cf5deca65f7a1b585051b2b58d7c6d9c2db7cb5\",\"mode\":\"enforce\",\"enabled\":true,\"description\":\"bounded\",\"queries\":[{\"name\":\"allow\",\"arity\":1}]},{\"policy_id\":\"p1\",\"domain\":\"advanced\",\"file\":\"ignored.dl\",\"sha256\":\"5cd989dbca2f32f8d8c87858bd2bfa145cb71d02100930e678c99eaf3a3cfa3e\",\"mode\":\"enforce\",\"enabled\":true,\"description\":\"bounded\",\"queries\":[{\"name\":\"allow\",\"arity\":1}]}],\"capabilities\":[],\"default_profile\":\"MAELYS-DATALOG-v2\",\"strict_loading\":true,\"fail_closed\":true,\"created_for\":\"test\"}";
    const char second[]="allow(X) :- blocked(X).";
    maelys_datalog_policy_bundle_entry_t bundle[]={{"p1",second,sizeof(second)-1},{"p0",source,sizeof(source)-1}};
    OK(maelys_datalog_policy_load_manifest_text_in(arena,bytes,json,sizeof(json)-1,bundle,2,0,&p,NULL));
    size_t count;OK(maelys_datalog_policy_count(p,&count)); CHECK(count==2);
    maelys_datalog_policy_t *allocated=NULL;
    OK(maelys_datalog_policy_load_manifest_text(json,sizeof(json)-1,bundle,2,0,&allocated,NULL));
    char a[65],b[65];OK(maelys_datalog_policy_fingerprint(p,a));OK(maelys_datalog_policy_fingerprint(allocated,b));CHECK(!strcmp(a,b));
    OK(maelys_datalog_policy_free(allocated));
    maelys_datalog_value_t v={MAELYS_DATALOG_VALUE_INTEGER,{.integer=7}};
    maelys_datalog_fact_t f={"seed",1,{v}};
    for(size_t i=0;i<2;++i) {
        maelys_datalog_session_t *session=NULL;maelys_datalog_result_t *result=NULL;
        OK(maelys_datalog_session_create(p,i,&session));OK(maelys_datalog_session_solve(session,&f,1,&result,NULL));
        int present=-1;OK(maelys_datalog_result_query(result,"allow",&v,1,&present));CHECK(present==(i==0));
        OK(maelys_datalog_result_free(result));OK(maelys_datalog_session_free(session));
    }
    OK(maelys_datalog_policy_free(p));
    maelys_datalog_diagnostic_t d=MAELYS_DATALOG_DIAGNOSTIC_INIT;
    CHECK(maelys_datalog_policy_load_manifest_text_in(arena,bytes,json,sizeof(json)-1,bundle,1,0,&p,&d)==MAELYS_DATALOG_STATUS_NOT_FOUND);
    CHECK(!p && d.status==MAELYS_DATALOG_STATUS_NOT_FOUND && d.code==MAELYS_DATALOG_DIAG_MANIFEST_POLICY_NOT_FOUND);
    OK(maelys_datalog_policy_load_frontend_in(arena,bytes,"advanced","one",source,strlen(source),NULL,&p,NULL));
    OK(maelys_datalog_policy_free(p));
    for(size_t i=bytes;i<bytes+32;++i)CHECK(arena[i]==0xa5);
    free(snapshot);free(arena);
    return 0;
}
static int context_configuration(void) {
    maelys_datalog_context_t *ctx=NULL;
    maelys_datalog_session_config_t *config=NULL;
    OK(maelys_datalog_context_create(&ctx));OK(maelys_datalog_session_config_create(&config));
    CHECK(maelys_datalog_session_config_set_context(config,ctx,NULL)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    OK(maelys_datalog_context_seal(ctx,NULL));
    OK(maelys_datalog_session_config_set_context(config,ctx,NULL));
    CHECK(maelys_datalog_session_config_set_context(config,ctx,"missing")==MAELYS_DATALOG_STATUS_NOT_FOUND);
    OK(maelys_datalog_session_config_set_explanation_workspace(config,MAELYS_DATALOG_EXPLAIN_TRUE));
    maelys_datalog_policy_t *p=NULL;
    const char source[]="allow(X) :- seed(X).";
    OK(maelys_datalog_context_load_inline(ctx,NULL,"advanced","p",source,strlen(source),&p,NULL));
    OK(maelys_datalog_context_free(ctx)); /* config and policy retain context */
    maelys_datalog_session_t *session=NULL;
    OK(maelys_datalog_session_create_configured(p,0,config,&session));
    OK(maelys_datalog_session_config_free(config));OK(maelys_datalog_policy_free(p));
    maelys_datalog_value_t value={MAELYS_DATALOG_VALUE_INTEGER,{.integer=8}};
    maelys_datalog_fact_t fact={"seed",1,{value}};maelys_datalog_result_t *result=NULL;
    OK(maelys_datalog_session_solve(session,&fact,1,&result,NULL));
    size_t needed=0;OK(maelys_datalog_result_explain_true_text(result,"allow",&value,1,NULL,0,&needed));CHECK(needed>0);
    OK(maelys_datalog_result_free(result));OK(maelys_datalog_session_free(session));
    return 0;
}
static int aggregate_views(void) {
    const char *operators[]={"count","min","max","sum"};
    const unsigned kinds[]={MAELYS_DATALOG_EXPLANATION_PREMISE_COUNT,MAELYS_DATALOG_EXPLANATION_PREMISE_MIN,MAELYS_DATALOG_EXPLANATION_PREMISE_MAX,MAELYS_DATALOG_EXPLANATION_PREMISE_SUM};
    const int values[]={2,2,3,5};
    const unsigned obstacles[]={MAELYS_DATALOG_WHY_FALSE_OBSTACLE_COUNT_MISMATCH,MAELYS_DATALOG_WHY_FALSE_OBSTACLE_MIN_MISMATCH,MAELYS_DATALOG_WHY_FALSE_OBSTACLE_MAX_MISMATCH,MAELYS_DATALOG_WHY_FALSE_OBSTACLE_SUM_MISMATCH};
    for(size_t i=0;i<4;++i) {
        char source[96];snprintf(source,sizeof(source),"allow(N) :- %s(V,seed(V),N).",operators[i]);
        maelys_datalog_policy_t *p=NULL;maelys_datalog_session_t *s=NULL;maelys_datalog_result_t *r=NULL;
        OK(maelys_datalog_policy_load_inline("advanced","aggregates",source,strlen(source),&p,NULL));
        OK(maelys_datalog_session_create(p,0,&s));OK(maelys_datalog_policy_free(p));
        maelys_datalog_value_t two={MAELYS_DATALOG_VALUE_INTEGER,{.integer=2}}, three={MAELYS_DATALOG_VALUE_INTEGER,{.integer=3}};
        maelys_datalog_fact_t facts[]={{"seed",1,{two}},{"seed",1,{three}}};
        OK(maelys_datalog_session_solve(s,facts,2,&r,NULL));
        for(unsigned kind=1;kind<=2;++kind) {
            size_t bytes,alignment;OK(maelys_datalog_result_explanation_storage_requirements(r,(maelys_datalog_explanation_kind_t)kind,&bytes,&alignment));
            void *arena=malloc(bytes);CHECK(arena);
            maelys_datalog_value_t q={MAELYS_DATALOG_VALUE_INTEGER,{.integer=kind==1?values[i]:9}};
            maelys_datalog_prepared_explanation_t *e=NULL;
            OK(maelys_datalog_result_prepare_explanation(r,(maelys_datalog_explanation_kind_t)kind,"allow",&q,1,arena,bytes,&e));
            if(kind==1) {
                maelys_datalog_explanation_premise_view_t premise;OK(maelys_datalog_prepared_explanation_premise(e,0,&premise));
                CHECK(premise.kind==kinds[i] && premise.aggregate_value==(unsigned)values[i] && !strcmp(premise.atom.predicate,"seed"));
                CHECK(premise.atom.terms[0].kind==MAELYS_DATALOG_IR_VARIABLE);
            } else {
                maelys_datalog_explanation_obstacle_view_t obstacle;OK(maelys_datalog_prepared_explanation_obstacle(e,0,&obstacle));
                CHECK(obstacle.obstacle_kind==obstacles[i] && obstacle.lhs.as.integer==values[i] && obstacle.rhs.as.integer==9);
                CHECK(!strcmp(obstacle.pattern.predicate,"seed") && obstacle.unbound_term_mask==1);
            }
            OK(maelys_datalog_prepared_explanation_release(e));free(arena);
        }
        OK(maelys_datalog_result_free(r));OK(maelys_datalog_session_free(s));
    }
    return 0;
}
/* Real public solves, not fabricated compact diagnostics. Both profiles must
 * distinguish a relation limit from the global IDB limit and remain reusable. */
static int capacity_rejections(void) {
    size_t per_pred, global;
    OK(maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_FACTS_PER_PRED,&per_pred));
    OK(maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_IDB_FACTS,&global));
    const maelys_datalog_predicate_t sg_preds[]={
        {"parent",2,MAELYS_DATALOG_PREDICATE_EDB},
        {"person",1,MAELYS_DATALOG_PREDICATE_IDB},
        {"same",2,MAELYS_DATALOG_PREDICATE_IDB}};
    const maelys_datalog_domain_t sg={"diagnostic_sg",sg_preds,3,NULL,0};
    OK(maelys_datalog_domain_register(&sg));
    const char source[]="person(X) :- parent(X, _). person(X) :- parent(_, X). "
        "same(X,X) :- person(X). same(X,Y) :- parent(X,P), same(P,Q), parent(Y,Q).";
    maelys_datalog_policy_t *p=NULL;maelys_datalog_session_t *session=NULL;
    maelys_datalog_result_t *result=NULL;
    maelys_datalog_diagnostic_t diag=MAELYS_DATALOG_DIAGNOSTIC_INIT;
    OK(maelys_datalog_policy_load_inline(sg.name,"p",source,strlen(source),&p,&diag));
    OK(maelys_datalog_session_create(p,0,&session));OK(maelys_datalog_policy_free(p));
    size_t leaves=1;while(leaves*leaves+1<=per_pred)++leaves;
    maelys_datalog_fact_t *facts=calloc(per_pred,sizeof(*facts));CHECK(facts);
    for(size_t i=0;i<leaves;++i){
        facts[i].predicate="parent";facts[i].arity=2;
        facts[i].terms[0].kind=facts[i].terms[1].kind=MAELYS_DATALOG_VALUE_INTEGER;
        facts[i].terms[0].as.integer=(int64_t)i+1;facts[i].terms[1].as.integer=0;
    }
    CHECK(maelys_datalog_session_solve(session,facts,leaves,&result,&diag)==MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
    CHECK(!result && diag.code==MAELYS_DATALOG_DIAG_SOLVE_IDB_OVERFLOW);
    CHECK(diag.present & MAELYS_DATALOG_DIAGNOSTIC_CAPACITY);
    CHECK(diag.present & MAELYS_DATALOG_DIAGNOSTIC_PREDICATE);
    CHECK(!strcmp(diag.predicate,"same") && diag.arity==2);
    CHECK(diag.limit_kind==MAELYS_DATALOG_LIMIT_MAX_FACTS_PER_PRED);
    CHECK(diag.limit==per_pred && diag.observed_count==per_pred+1);
    OK(maelys_datalog_session_solve(session,facts,1,&result,&diag));
    CHECK(diag.source==MAELYS_DATALOG_DIAGNOSTIC_NONE && !diag.present && !diag.limit && !diag.predicate[0]);
    OK(maelys_datalog_result_free(result));OK(maelys_datalog_session_free(session));

    size_t outputs=global/per_pred+1;
    maelys_datalog_predicate_t *preds=calloc(outputs+1,sizeof(*preds));
    char (*names)[32]=calloc(outputs,sizeof(*names));char *rules=calloc(outputs,48);
    CHECK(preds && names && rules);
    preds[0]=(maelys_datalog_predicate_t){"seed",1,MAELYS_DATALOG_PREDICATE_EDB};
    size_t used=0;
    for(size_t i=0;i<outputs;++i){
        snprintf(names[i],sizeof(names[i]),"g%zu",i);
        preds[i+1]=(maelys_datalog_predicate_t){names[i],1,MAELYS_DATALOG_PREDICATE_IDB};
        used+=(size_t)snprintf(rules+used,outputs*48-used,"%s(X) :- seed(X).\n",names[i]);
    }
    const maelys_datalog_domain_t gd={"diagnostic_global",preds,outputs+1,NULL,0};
    OK(maelys_datalog_domain_register(&gd));
    OK(maelys_datalog_policy_load_inline(gd.name,"p",rules,used,&p,&diag));
    OK(maelys_datalog_session_create(p,0,&session));OK(maelys_datalog_policy_free(p));
    for(size_t i=0;i<per_pred;++i){facts[i].predicate="seed";facts[i].arity=1;
        facts[i].terms[0].kind=MAELYS_DATALOG_VALUE_INTEGER;facts[i].terms[0].as.integer=(int64_t)i;}
    CHECK(maelys_datalog_session_solve(session,facts,per_pred,&result,&diag)==MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
    CHECK(!result && diag.code==MAELYS_DATALOG_DIAG_SOLVE_IDB_OVERFLOW);
    CHECK(diag.present & MAELYS_DATALOG_DIAGNOSTIC_CAPACITY);
    CHECK(diag.present & MAELYS_DATALOG_DIAGNOSTIC_PREDICATE);
    CHECK(diag.limit_kind==MAELYS_DATALOG_LIMIT_MAX_IDB_FACTS);
    CHECK(diag.limit==global && diag.observed_count==global+1);
    CHECK(diag.predicate[0] && diag.arity==1);
    OK(maelys_datalog_session_solve(session,facts,1,&result,&diag));
    OK(maelys_datalog_result_free(result));OK(maelys_datalog_session_free(session));
    free(rules);free(names);free(preds);free(facts);
    return 0;
}
int main(void) {
    return capacity_rejections() || diagnostics_and_callbacks() || structured() ||
        owned_policy_and_bundle() || context_configuration() || aggregate_views();
}
