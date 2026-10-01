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
/* Exercise the opaque view's arbitrary-index contract, independently of the
 * provider's sequential traversal. Borrowed strings remain callback-local. */
static maelys_datalog_status_t inspecting_solve(void *state,const maelys_datalog_backend_input_t *packet,
    maelys_datalog_backend_output_t *out,void **result,maelys_datalog_diagnostic_t *diag) {
    for(size_t side=0;side<2;++side) {
        const maelys_datalog_backend_input_view_t *v=side?packet->additions:packet->removals;
        size_t n=side?packet->addition_count:packet->removal_count;
        maelys_datalog_fact_t expected[32];assert(n<=32);
        for(size_t i=0;i<n;++i)OK(maelys_datalog_backend_input_at(v,i,&expected[i]));
        for(size_t i=n;i>0;--i)for(size_t repeat=0;repeat<2;++repeat) {
            maelys_datalog_fact_t f;OK(maelys_datalog_backend_input_at(v,i-1,&f));
            assert(!memcmp(&f,&expected[i-1],sizeof(f)));
        }
    }
    return input_fixture_transactions()->solve(state,packet,out,result,diag);
}
static void setup(fixture *f,int delta) {
    maelys_datalog_backend_v7_t inspecting=*input_fixture_transactions();if(delta==2)inspecting.solve=inspecting_solve;
    memset(f,0,sizeof(*f));maelys_datalog_session_config_t *c=NULL;OK(maelys_datalog_session_config_create(&c));
    if(delta)OK(maelys_datalog_session_config_set_backend_v7(c,&inspecting));
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
    o.fact_capacity=32;o.addition_capacity=delta==2?0:8;o.removal_capacity=delta==2?0:8;o.symbols=words;o.symbol_count=8;
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
    unsigned char committed[2][32768],checked[32768];size_t committed_n[2];
    for(size_t i=0;i<2;++i){committed_n[i]=input_fixture_committed(f[i].provider,committed[i],sizeof(committed[i]));assert(committed_n[i]<=sizeof(committed[i]));}
    forbidden=1;
    int rc=grouped?maelys_datalog_group_window_expire(g,10,&expired,NULL):maelys_datalog_window_expire(w,10,&expired,NULL);
    assert(rc==MAELYS_DATALOG_STATUS_INVALID_STATE && expired==99);
    size_t aborts=0;
    for(size_t i=0;i<2;++i) {input_fixture_observation after=observation(&f[i]);assert(!memcmp(&after.base,&before[i].base,sizeof(after.base)) && after.digest==before[i].digest && after.commits==before[i].commits);aborts+=after.aborts-before[i].aborts;}
    assert(aborts==1);
    for(size_t i=0;i<2;++i){assert(input_fixture_committed(f[i].provider,checked,sizeof(checked))==committed_n[i]);assert(!memcmp(checked,committed[i],committed_n[i]));}
    OK(maelys_datalog_prepared_explanation_release(explanation));forbidden=0;free(earea);forbidden=1;
    if(grouped)OK(maelys_datalog_group_window_expire(g,10,&expired,NULL));else OK(maelys_datalog_window_expire(w,10,&expired,NULL));assert(expired==1);
    if(grouped)OK(maelys_datalog_group_window_result(g,&r));else OK(maelys_datalog_window_result(w,&r));
    int present;OK(maelys_datalog_result_query(r,"out",record.terms,1,&present));assert(present); /* static supplier survives */
    for(size_t i=0;i<2;++i){input_fixture_observation after=observation(&f[i]);assert(after.deltas && !after.snapshots);}
    if(grouped)OK(maelys_datalog_group_window_free(g));else OK(maelys_datalog_window_free(w));forbidden=0;free(storage);close_fixture(&f[0]);close_fixture(&f[1]);
}
typedef struct { maelys_datalog_fact_t facts[4];size_t n;uint64_t deadline; } window_event;
static int same_fact(const maelys_datalog_fact_t *a,const maelys_datalog_fact_t *b) {
    if(strcmp(a->predicate,b->predicate) || a->arity!=b->arity)return 0;
    for(size_t i=0;i<a->arity;++i) {
        if(a->terms[i].kind!=b->terms[i].kind)return 0;
        if(a->terms[i].kind==MAELYS_DATALOG_VALUE_SYMBOL) {if(strcmp(a->terms[i].as.symbol,b->terms[i].as.symbol))return 0;}
        else if(a->terms[i].kind==MAELYS_DATALOG_VALUE_INTEGER) {if(a->terms[i].as.integer!=b->terms[i].as.integer)return 0;}
        else if(!!a->terms[i].as.boolean!=!!b->terms[i].as.boolean)return 0;
    }
    return 1;
}
static int contains(const maelys_datalog_fact_t *set,size_t n,const maelys_datalog_fact_t *f) {
    for(size_t i=0;i<n;++i)if(same_fact(&set[i],f))return 1;return 0;
}
static size_t unique_facts(maelys_datalog_fact_t *out,const maelys_datalog_fact_t *raw,size_t n) {
    size_t count=0;for(size_t i=0;i<n;++i)if(!contains(out,count,&raw[i]))out[count++]=raw[i];return count;
}
static int window_action(maelys_datalog_window_t *w,maelys_datalog_group_window_t *g,int action,
    const maelys_datalog_fact_t *facts,size_t n,uint64_t time,uint32_t *id,size_t *expired) {
    if(action==0)return g?maelys_datalog_group_window_push_until(g,facts,n,time,id,NULL):
        maelys_datalog_window_push_until(w,"event",facts[0].terms+1,1,time,id,NULL);
    if(action==1)return g?maelys_datalog_group_window_replace_static(g,facts,n,NULL):
        maelys_datalog_window_replace_static(w,facts,n,NULL);
    return g?maelys_datalog_group_window_expire(g,time,expired,NULL):maelys_datalog_window_expire(w,time,expired,NULL);
}
static void window_sequence(int grouped,int abi7) {
    fixture f[2];setup(&f[0],abi7?2:0);setup(&f[1],abi7?2:0);
    maelys_datalog_session_t *oracle=NULL;OK(maelys_datalog_session_create(policy,0,&oracle));
    maelys_datalog_window_t *w=NULL;maelys_datalog_group_window_t *g=NULL;
    maelys_datalog_window_options_t o={sizeof(o),18,MAELYS_DATALOG_WINDOW_EXPIRATION};
    maelys_datalog_group_window_capacities_t cap={3,12,30,1024};size_t bytes,alignment;
    if(grouped)OK(maelys_datalog_group_window_storage_requirements_configured(&cap,&o,&bytes,&alignment));
    else OK(maelys_datalog_window_storage_requirements_configured(3,1024,&o,&bytes,&alignment));
    void *area=malloc(bytes);assert(area);
    if(grouped)OK(maelys_datalog_group_window_init_configured(area,bytes,&cap,&o,0,f[0].session,f[1].session,&g,NULL));
    else OK(maelys_datalog_window_init_configured(area,bytes,3,1024,&o,0,f[0].session,f[1].session,&w,NULL));
    maelys_datalog_result_t *result=NULL;
    if(grouped)OK(maelys_datalog_group_window_result(g,&result));else OK(maelys_datalog_window_result(w,&result));
    size_t ebytes;OK(maelys_datalog_result_explanation_storage_requirements(result,MAELYS_DATALOG_EXPLAIN_TRUE,&ebytes,&alignment));
    void *explain_area=malloc(ebytes);assert(explain_area);
    window_event events[3]={0};size_t ne=0,ns=0,bank_n[2]={0};unsigned active=0;
    maelys_datalog_fact_t statics[18],banks[2][32];uint32_t next=0,seed=UINT32_C(0x6ba72e91);uint64_t now=0;
    size_t allocations_before=allocations,frees_before=frees;forbidden=1;
    for(size_t tx=0;tx<240;++tx) {
        seed=seed*1664525u+1013904223u;
        int action=tx%10==4 || tx%10==8?1:(tx%10==5 || tx%10==9?2:0);
        maelys_datalog_fact_t incoming[18],raw[32],expected[32];size_t n=0,expected_n=0,expired_count=0;
        window_event proposed[3];memcpy(proposed,events,sizeof(events));size_t proposed_n=ne;
        uint64_t time=action==2?now+1:now+1+(seed%4);
        if(action==0) {
            if(proposed_n==3){memmove(proposed,proposed+1,2*sizeof(*proposed));--proposed_n;}
            n=grouped?4:1;
            for(size_t i=0;i<n;++i)incoming[i]=fact(((seed>>8)+(i%3))%18); /* deliberate duplicate */
            if(!grouped) {incoming[0].predicate="event";incoming[0].arity=2;incoming[0].terms[1]=incoming[0].terms[0];
                incoming[0].terms[0]=(maelys_datalog_value_t){.kind=MAELYS_DATALOG_VALUE_INTEGER,.as.integer=next};}
            proposed[proposed_n].n=n;proposed[proposed_n].deadline=time;
            memcpy(proposed[proposed_n++].facts,incoming,n*sizeof(*incoming));
        } else if(action==1) {
            n=12;for(size_t i=0;i<n;++i)incoming[i]=fact((i+tx)%18);
            if(ne)incoming[n++]=events[0].facts[0]; /* event + static support, also for occurrence windows */
        } else {
            proposed_n=0;for(size_t i=0;i<ne;++i)if(events[i].deadline>time)proposed[proposed_n++]=events[i];else ++expired_count;
        }
        size_t raw_n=action==1?n:ns;memcpy(raw,action==1?incoming:statics,raw_n*sizeof(*raw));
        for(size_t i=0;i<proposed_n;++i){memcpy(raw+raw_n,proposed[i].facts,proposed[i].n*sizeof(*raw));raw_n+=proposed[i].n;}
        expected_n=unique_facts(expected,raw,raw_n);
        unsigned candidate=1-active;int invokes=action!=2 || expired_count;
        input_fixture_observation before[2]={observation(&f[0]),observation(&f[1])};
        maelys_datalog_input_base_t base_before[2]={base(&f[0]),base(&f[1])};
        unsigned char image[2][32768],after_image[32768];size_t image_n[2];
        for(size_t i=0;i<2;++i){image_n[i]=input_fixture_committed(f[i].provider,image[i],sizeof(image[i]));assert(image_n[i]<=sizeof(image[i]));}
        /* Same candidate operation, rejected before publication, then retried. */
        if(invokes && tx%7==0) {
            uint32_t rejected_id=UINT32_MAX;size_t rejected_expired=SIZE_MAX;
            input_fixture_fault(f[candidate].provider,1+(unsigned)(tx%3));
            assert(window_action(w,g,action,incoming,n,time,&rejected_id,&rejected_expired)!=MAELYS_DATALOG_STATUS_OK);
            assert(rejected_id==UINT32_MAX && rejected_expired==SIZE_MAX);
            input_fixture_fault(f[candidate].provider,0);
            for(size_t i=0;i<2;++i) {
                assert(input_fixture_committed(f[i].provider,after_image,sizeof(after_image))==image_n[i]);
                assert(!memcmp(image[i],after_image,image_n[i]));
                maelys_datalog_input_base_t b=base(&f[i]);assert(!memcmp(&b,&base_before[i],sizeof(b)));
            }
            before[0]=observation(&f[0]);before[1]=observation(&f[1]);
        }
        uint32_t id=UINT32_MAX;size_t expired=SIZE_MAX;
        OK(window_action(w,g,action,incoming,n,time,&id,&expired));
        if(action==0){assert(id==next);++next;}
        if(action==1){ns=n;memcpy(statics,incoming,n*sizeof(*statics));}
        if(action==2){assert(expired==expired_count);now=time;}
        ne=proposed_n;memcpy(events,proposed,sizeof(events));
        if(invokes) {
            size_t changed=0;
            for(size_t i=0;i<expected_n;++i)changed+=!contains(banks[candidate],bank_n[candidate],&expected[i]);
            for(size_t i=0;i<bank_n[candidate];++i)changed+=!contains(expected,expected_n,&banks[candidate][i]);
            input_fixture_observation after=observation(&f[candidate]);
            assert(after.delivered-before[candidate].delivered==(abi7?changed:expected_n));
            assert(after.commits==before[candidate].commits+1);
            if(abi7)assert(after.deltas==before[candidate].deltas+1 && !after.snapshots);
            bank_n[candidate]=expected_n;memcpy(banks[candidate],expected,expected_n*sizeof(*expected));active=candidate;
        } else for(size_t i=0;i<2;++i){input_fixture_observation after=observation(&f[i]);assert(!memcmp(&after,&before[i],sizeof(after)));}
        maelys_datalog_result_t *old=result;
        if(grouped)OK(maelys_datalog_group_window_result(g,&result));else OK(maelys_datalog_window_result(w,&result));
        if(!invokes)assert(result==old);
        compare(result,oracle,raw,raw_n);
        for(size_t i=0;i<2;++i)assert(observation(&f[i]).retained==bank_n[i]);
    }
    if(grouped)OK(maelys_datalog_group_window_free(g));else OK(maelys_datalog_window_free(w));
    forbidden=0;assert(allocations==allocations_before && frees==frees_before);
    free(explain_area);free(area);OK(maelys_datalog_session_free(oracle));close_fixture(&f[0]);close_fixture(&f[1]);
    printf("window oracle: grouped=%d ABI=%d seed=6ba72e91 transactions=240 PASS\n",grouped,abi7?7:6);
}

int main(void) {
    const maelys_datalog_domain_t d={"delivery",predicates,4,NULL,0};OK(maelys_datalog_domain_register(&d));
    const char *source="copy(I,X) :- event(I,X). out(X) :- record(X).";
    OK(maelys_datalog_policy_load_inline(d.name,"delivery",source,strlen(source),&policy,NULL));
    negotiation();generated(0);generated(1);errors();windows(0);windows(1);for(int g=0;g<2;++g)for(int abi=0;abi<2;++abi)window_sequence(g,abi);OK(maelys_datalog_policy_free(policy));
    puts("backend input negotiation, complete outputs, bases, abort/retry, sticky errors, leases and both windows PASS");return 0;
}
