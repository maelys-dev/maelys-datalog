/* SPDX-License-Identifier: MPL-2.0 */
#include <maelys/datalog_backend_transactions.h>
#include <maelys/datalog_advanced.h>
#include <maelys/datalog_window.h>
#include "input_provider.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define OK(x) do{int rc_=(x);if(rc_){fprintf(stderr,"%d %s: %d\n",__LINE__,#x,rc_);abort();}}while(0)
static size_t allocations,frees;
static int forbidden;
void *maelys_test_malloc(size_t n){++allocations;assert(!forbidden);return malloc(n);}
void *maelys_test_calloc(size_t n,size_t z){++allocations;assert(!forbidden);return calloc(n,z);}
void *maelys_test_realloc(void *p,size_t n){++allocations;assert(!forbidden);return realloc(p,n);}
void maelys_test_free(void *p){if(p){++frees;assert(!forbidden);}free(p);}
void *maelys_test_memset(void *p,int c,size_t n){return memset(p,c,n);}
static const maelys_datalog_predicate_t predicates[]={MAELYS_DATALOG_EDB("event",2),MAELYS_DATALOG_EDB("record",1),
    MAELYS_DATALOG_IDB_QUERY("copy",2),MAELYS_DATALOG_IDB_QUERY("out",1)};
static const char *words[]={"zebra","alpha","gamma","delta","epsilon","theta","omega","beta"};
static maelys_datalog_policy_t *policy;
typedef struct {maelys_datalog_session_t *session;maelys_datalog_session_inputs_t *inputs;void *provider,*storage;} fixture;
static maelys_datalog_input_base_t base(fixture *f) {maelys_datalog_input_base_t b;OK(maelys_datalog_session_inputs_base(f->inputs,&b));return b;}
static input_fixture_observation observation(fixture *f) {input_fixture_observation o;input_fixture_observe(f->provider,&o);return o;}
static maelys_datalog_fact_t fact(size_t key) {
    maelys_datalog_fact_t f={.predicate="record",.arity=1};
    if(key<8){f.terms[0].kind=MAELYS_DATALOG_VALUE_INTEGER;f.terms[0].as.integer=(int64_t)key;}
    else if(key<16){f.terms[0].kind=MAELYS_DATALOG_VALUE_SYMBOL;f.terms[0].as.symbol=words[key-8];}
    else{f.terms[0].kind=MAELYS_DATALOG_VALUE_BOOLEAN;f.terms[0].as.boolean=(int)(key-16);}
    return f;
}
static void setup(fixture *f,int delta) {
    memset(f,0,sizeof(*f));maelys_datalog_session_config_t *c=NULL;OK(maelys_datalog_session_config_create(&c));
    if(delta)OK(maelys_datalog_session_config_set_backend_v7(c,input_fixture_transactions()));
    else OK(maelys_datalog_session_config_set_backend_v6(c,input_fixture_snapshot()));
    maelys_datalog_session_resource_request_t q=MAELYS_DATALOG_RESOURCE_REQUEST_INIT;
    q.capacity_mask=MAELYS_DATALOG_CAPACITY_INPUT_FACTS|MAELYS_DATALOG_CAPACITY_DERIVED_FACTS;
    q.input_facts=32;q.derived_facts=32;OK(maelys_datalog_session_config_set_resources(c,&q));
    maelys_datalog_session_storage_plan_t plan=MAELYS_DATALOG_SESSION_PLAN_INIT;
    OK(maelys_datalog_session_storage_requirements_configured(policy,0,c,&plan,NULL));
    f->provider=malloc(plan.backend_bytes);assert(f->provider);
    maelys_datalog_backend_storage_t storage={sizeof(storage),f->provider,plan.backend_bytes,_Alignof(max_align_t)};
    OK(maelys_datalog_session_config_set_backend_storage(c,&storage));
    OK(maelys_datalog_session_create_configured(policy,0,c,&f->session));OK(maelys_datalog_session_config_free(c));
    maelys_datalog_input_options_t o=MAELYS_DATALOG_INPUT_OPTIONS_INIT;
    o.fact_capacity=32;o.addition_capacity=8;o.removal_capacity=8;o.symbols=words;o.symbol_count=8;
    size_t bytes,alignment;OK(maelys_datalog_session_inputs_storage_requirements(f->session,&o,&bytes,&alignment));
    f->storage=malloc(bytes);assert(f->storage);size_t before=allocations;
    forbidden=1;OK(maelys_datalog_session_inputs_init(f->session,&o,f->storage,bytes,&f->inputs));forbidden=0;assert(allocations==before);
}
static void close_fixture(fixture *f) {
    forbidden=1;OK(maelys_datalog_session_inputs_free(f->inputs));forbidden=0;
    OK(maelys_datalog_session_free(f->session));free(f->storage);free(f->provider);
}
static void compare(maelys_datalog_result_t *a,maelys_datalog_session_t *oracle,const maelys_datalog_fact_t *facts,size_t n) {
    maelys_datalog_result_t *b=NULL;OK(maelys_datalog_session_solve(oracle,facts,n,&b,NULL));
    size_t ca,cb;OK(maelys_datalog_result_derived_fact_count(a,&ca));OK(maelys_datalog_result_derived_fact_count(b,&cb));assert(ca==cb);
    for(size_t p=2;p<4;++p) {
        maelys_datalog_fact_view_t fa[32],fb[32];size_t na,nb;
        OK(maelys_datalog_result_enumerate(a,predicates[p].name,predicates[p].arity,fa,32,&na));
        OK(maelys_datalog_result_enumerate(b,predicates[p].name,predicates[p].arity,fb,32,&nb));assert(na==nb);
        for(size_t i=0;i<na;++i)for(size_t j=0;j<predicates[p].arity;++j) {
            const maelys_datalog_term_view_t *x=&fa[i].terms[j],*y=&fb[i].terms[j];assert(x->kind==y->kind);
            if(x->kind==MAELYS_DATALOG_VALUE_INTEGER)assert(x->as.integer==y->as.integer);
            else if(x->kind==MAELYS_DATALOG_VALUE_BOOLEAN)assert(x->as.boolean==y->as.boolean);
            else {const char *sx,*sy;size_t nx,ny;assert(x->as.symbol_id==y->as.symbol_id);
                OK(maelys_datalog_result_symbol_text(a,x->as.symbol_id,&sx,&nx));OK(maelys_datalog_result_symbol_text(b,y->as.symbol_id,&sy,&ny));assert(nx==ny&&!memcmp(sx,sy,nx));}
        }
    }
    OK(maelys_datalog_result_free(b));
}
static void generated(int delta) {
    fixture f;setup(&f,delta);maelys_datalog_session_t *oracle=NULL;OK(maelys_datalog_session_create(policy,0,&oracle));
    uint32_t seed=UINT32_C(0x741c2903);int live[18]={0};size_t before=allocations,before_free=frees;forbidden=1;
    for(size_t tx=0;tx<1200;++tx) {
        maelys_datalog_fact_t add[2],remove[2],expected[18];size_t n=0;
        seed=seed*1664525u+1013904223u;size_t x=(seed>>8)%18;
        seed=seed*1664525u+1013904223u;size_t y=(seed>>8)%18;
        remove[0]=fact(x);remove[1]=fact(y);live[x]=live[y]=0;
        seed=seed*1664525u+1013904223u;x=(seed>>8)%18;add[0]=add[1]=fact(x);live[x]=1;
        for(size_t k=0;k<18;++k)if(live[k])expected[n++]=fact(k);
        input_fixture_observation old=observation(&f);maelys_datalog_input_base_t prior=base(&f);maelys_datalog_result_t *r=NULL;
        if(tx%17==0)OK(maelys_datalog_session_inputs_replace(f.inputs,prior,expected,n,&r,NULL));
        else OK(maelys_datalog_session_inputs_apply(f.inputs,prior,add,2,remove,2,&r,NULL));
        compare(r,oracle,expected,n);OK(maelys_datalog_result_free(r));
        input_fixture_observation now=observation(&f);maelys_datalog_input_base_t current=base(&f);
        assert(current.generation==prior.generation+1 && now.retained==n && now.commits==old.commits+1);
        if(delta) { assert(!memcmp(&now.base,&current,sizeof(current)));if(tx%17)assert(now.delivered-old.delivered<=3); }
        else assert(now.delivered-old.delivered==n);
        if(tx%31==0) {
            input_fixture_fault(f.provider,1);assert(maelys_datalog_session_inputs_apply(f.inputs,current,remove,1,NULL,0,&r,NULL)==MAELYS_DATALOG_STATUS_INVALID_FIELD && !r);
            input_fixture_observation failed=observation(&f);maelys_datalog_input_base_t after=base(&f);
            assert(!memcmp(&after,&current,sizeof(current)) && failed.digest==now.digest && failed.commits==now.commits && failed.aborts==now.aborts+1);
            input_fixture_fault(f.provider,0);
        }
    }
    forbidden=0;assert(allocations==before && frees==before_free);OK(maelys_datalog_session_free(oracle));close_fixture(&f);
    printf("backend input oracle: mode=%d seed=741c2903 transactions=1200 PASS\n",delta);
}
static void errors(void) {
    fixture f;setup(&f,1);maelys_datalog_result_t *r=NULL;maelys_datalog_fact_t a=fact(8);maelys_datalog_input_base_t initial=base(&f);
    assert(maelys_datalog_session_solve(f.session,&a,1,&r,NULL)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    for(unsigned fault=1;fault<=3;++fault) {
        input_fixture_fault(f.provider,fault);
        assert(maelys_datalog_session_inputs_apply(f.inputs,initial,&a,1,NULL,0,&r,NULL)!=MAELYS_DATALOG_STATUS_OK && !r);
        input_fixture_observation o=observation(&f);assert(!o.commits && !o.base.incarnation && !o.retained && o.aborts==fault);
        maelys_datalog_input_base_t b=base(&f);assert(!memcmp(&b,&initial,sizeof(b)));
    }
    input_fixture_fault(f.provider,0);OK(maelys_datalog_session_inputs_apply(f.inputs,initial,&a,1,NULL,0,&r,NULL));
    size_t bytes,alignment;OK(maelys_datalog_result_explanation_storage_requirements(r,MAELYS_DATALOG_EXPLAIN_TRUE,&bytes,&alignment));
    void *storage=malloc(bytes);assert(storage);maelys_datalog_prepared_explanation_t *e=NULL;
    OK(maelys_datalog_result_prepare_explanation(r,MAELYS_DATALOG_EXPLAIN_TRUE,"out",a.terms,1,storage,bytes,&e));
    assert(maelys_datalog_result_free(r)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    char text[128];OK(maelys_datalog_prepared_explanation_write_text(e,text,sizeof(text)));assert(strstr(text,"committed"));
    OK(maelys_datalog_prepared_explanation_release(e));free(storage);OK(maelys_datalog_result_free(r));
    maelys_datalog_input_base_t b=base(&f);assert(maelys_datalog_session_inputs_apply(f.inputs,initial,NULL,0,NULL,0,&r,NULL)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    fixture other;setup(&other,1);assert(maelys_datalog_session_inputs_apply(other.inputs,b,NULL,0,NULL,0,&r,NULL)==MAELYS_DATALOG_STATUS_INVALID_STATE);close_fixture(&other);
    OK(maelys_datalog_session_inputs_apply(f.inputs,b,NULL,0,NULL,0,&r,NULL));OK(maelys_datalog_result_free(r));
    assert(base(&f).generation==b.generation+1);close_fixture(&f);
}
static void negotiation(void) {
    maelys_datalog_session_config_t *c=NULL;OK(maelys_datalog_session_config_create(&c));
    maelys_datalog_backend_v7_t b=*input_fixture_transactions();
    b.abi_version=6;assert(maelys_datalog_session_config_set_backend_v7(c,&b)==MAELYS_DATALOG_STATUS_UNSUPPORTED);
    b=*input_fixture_transactions();b.struct_size--;assert(maelys_datalog_session_config_set_backend_v7(c,&b)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    b=*input_fixture_transactions();b.solve=NULL;assert(maelys_datalog_session_config_set_backend_v7(c,&b)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    b=*input_fixture_transactions();b.commit=NULL;assert(maelys_datalog_session_config_set_backend_v7(c,&b)!=MAELYS_DATALOG_STATUS_OK);
    b=*input_fixture_transactions();b.resource_features|=MAELYS_DATALOG_RESOURCE_CALLER_ALLOCATOR;assert(maelys_datalog_session_config_set_backend_v7(c,&b)==MAELYS_DATALOG_STATUS_UNSUPPORTED);
    OK(maelys_datalog_session_config_set_backend_v7(c,input_fixture_transactions()));
    maelys_datalog_session_storage_plan_t delta=MAELYS_DATALOG_SESSION_PLAN_INIT,snapshot=MAELYS_DATALOG_SESSION_PLAN_INIT;
    OK(maelys_datalog_session_storage_requirements_configured(policy,0,c,&delta,NULL));
    maelys_datalog_session_t *s=NULL;OK(maelys_datalog_session_create_configured(policy,0,c,&s));
    char identity[65],other[65];OK(maelys_datalog_session_execution_fingerprint(s,identity));
    maelys_datalog_result_t *r=NULL;assert(maelys_datalog_session_solve(s,NULL,0,&r,NULL)==MAELYS_DATALOG_STATUS_INVALID_STATE);OK(maelys_datalog_session_free(s));
    OK(maelys_datalog_session_config_set_backend_v6(c,input_fixture_snapshot()));
    OK(maelys_datalog_session_storage_requirements_configured(policy,0,c,&snapshot,NULL));assert(snapshot.host_bytes>delta.host_bytes);
    OK(maelys_datalog_session_create_configured(policy,0,c,&s));OK(maelys_datalog_session_execution_fingerprint(s,other));assert(strcmp(identity,other));
    OK(maelys_datalog_session_solve(s,NULL,0,&r,NULL));OK(maelys_datalog_result_free(r));OK(maelys_datalog_session_free(s));
    OK(maelys_datalog_session_config_set_backend_v7(c,input_fixture_transactions()));OK(maelys_datalog_session_config_set_backend_v7(c,NULL));
    OK(maelys_datalog_session_create_configured(policy,0,c,&s));OK(maelys_datalog_session_solve(s,NULL,0,&r,NULL));OK(maelys_datalog_result_free(r));OK(maelys_datalog_session_free(s));
    OK(maelys_datalog_session_config_free(c));
}
static void windows(int grouped) {
    fixture f[2];setup(&f[0],1);setup(&f[1],1);maelys_datalog_window_t *w=NULL;maelys_datalog_group_window_t *g=NULL;
    maelys_datalog_window_options_t o={sizeof(o),4,MAELYS_DATALOG_WINDOW_EXPIRATION};
    maelys_datalog_group_window_capacities_t capacity={2,4,8,128};size_t bytes,alignment;
    if(grouped)OK(maelys_datalog_group_window_storage_requirements_configured(&capacity,&o,&bytes,&alignment));
    else OK(maelys_datalog_window_storage_requirements_configured(2,128,&o,&bytes,&alignment));
    void *storage=malloc(bytes);assert(storage);
    if(grouped)OK(maelys_datalog_group_window_init_configured(storage,bytes,&capacity,&o,0,f[0].session,f[1].session,&g,NULL));
    else OK(maelys_datalog_window_init_configured(storage,bytes,2,128,&o,0,f[0].session,f[1].session,&w,NULL));
    maelys_datalog_fact_t record=fact(8);uint32_t id=99;
    if(grouped)OK(maelys_datalog_group_window_replace_static(g,&record,1,NULL));else OK(maelys_datalog_window_replace_static(w,&record,1,NULL));
    maelys_datalog_value_t value={.kind=MAELYS_DATALOG_VALUE_INTEGER,.as.integer=7};
    if(grouped) {
        OK(maelys_datalog_group_window_push_until(g,&record,1,10,&id,NULL));
    } else {
        OK(maelys_datalog_window_push_until(w,"event",&value,1,10,&id,NULL));
    }
    assert(id==0);
    maelys_datalog_result_t *r=NULL;if(grouped)OK(maelys_datalog_group_window_result(g,&r));else OK(maelys_datalog_window_result(w,&r));
    size_t ebytes;OK(maelys_datalog_result_explanation_storage_requirements(r,MAELYS_DATALOG_EXPLAIN_TRUE,&ebytes,&alignment));
    void *earea=malloc(ebytes);assert(earea);maelys_datalog_prepared_explanation_t *explanation=NULL;
    OK(maelys_datalog_result_prepare_explanation(r,MAELYS_DATALOG_EXPLAIN_TRUE,"out",record.terms,1,earea,ebytes,&explanation));
    input_fixture_observation before[2]={observation(&f[0]),observation(&f[1])};size_t expired=99;
    int rc=grouped?maelys_datalog_group_window_expire(g,10,&expired,NULL):maelys_datalog_window_expire(w,10,&expired,NULL);
    assert(rc==MAELYS_DATALOG_STATUS_INVALID_STATE && expired==99);
    size_t aborts=0;
    for(size_t i=0;i<2;++i) {input_fixture_observation after=observation(&f[i]);assert(!memcmp(&after.base,&before[i].base,sizeof(after.base)) && after.digest==before[i].digest && after.commits==before[i].commits);aborts+=after.aborts-before[i].aborts;}
    assert(aborts==1);OK(maelys_datalog_prepared_explanation_release(explanation));free(earea);
    if(grouped)OK(maelys_datalog_group_window_expire(g,10,&expired,NULL));else OK(maelys_datalog_window_expire(w,10,&expired,NULL));assert(expired==1);
    if(grouped)OK(maelys_datalog_group_window_result(g,&r));else OK(maelys_datalog_window_result(w,&r));
    int present;OK(maelys_datalog_result_query(r,"out",record.terms,1,&present));assert(present); /* static supplier survives */
    for(size_t i=0;i<2;++i){input_fixture_observation after=observation(&f[i]);assert(!after.deltas);}
    if(grouped)OK(maelys_datalog_group_window_free(g));else OK(maelys_datalog_window_free(w));free(storage);close_fixture(&f[0]);close_fixture(&f[1]);
}
int main(void) {
    const maelys_datalog_domain_t d={"delivery",predicates,4,NULL,0};OK(maelys_datalog_domain_register(&d));
    const char *source="copy(I,X) :- event(I,X). out(X) :- record(X).";
    OK(maelys_datalog_policy_load_inline(d.name,"delivery",source,strlen(source),&policy,NULL));
    negotiation();generated(0);generated(1);errors();windows(0);windows(1);OK(maelys_datalog_policy_free(policy));
    puts("backend input negotiation, complete outputs, bases, abort/retry, sticky errors, leases and both windows PASS");return 0;
}
