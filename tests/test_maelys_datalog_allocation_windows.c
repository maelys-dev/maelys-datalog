/* SPDX-License-Identifier: MPL-2.0 */
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef memset
#include <maelys/datalog_backend.h>
#include <maelys/datalog_window.h>
#include <maelys/datalog_inputs.h>
#include "allocation_provider.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); abort(); } } while(0)
#define OK(x) CHECK((x)==MAELYS_DATALOG_STATUS_OK)
#define BAD MAELYS_DATALOG_STATUS_INVALID_ARGUMENT
#define NO MAELYS_DATALOG_STATUS_UNSUPPORTED
#define STATE MAELYS_DATALOG_STATUS_INVALID_STATE
#define FULL MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE
#define SMALL MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL
static int forbidden;
static size_t engine_calls;
void *maelys_test_malloc(size_t n) { CHECK(!forbidden); ++engine_calls; return malloc(n); }
void *maelys_test_calloc(size_t n,size_t w) { CHECK(!forbidden); ++engine_calls; return calloc(n,w); }
void *maelys_test_realloc(void *p,size_t n) { CHECK(!forbidden); ++engine_calls; return realloc(p,n); }
void maelys_test_free(void *p) { if(p) CHECK(!forbidden); free(p); }
void *maelys_test_memset(void *p,int c,size_t n) { return memset(p,c,n); }
typedef struct {
    void *p[32]; size_t n[32];
    size_t calls, releases, live, bytes, peak, fail;
} ledger;
static void *caller_acquire(void *context,size_t n,size_t alignment) {
    ledger *a=context;CHECK(alignment==_Alignof(max_align_t));++a->calls;
    if(a->calls==a->fail)return NULL;
    size_t i=0;while(i<32 && a->p[i])++i;CHECK(i<32);
    a->p[i]=malloc(n);CHECK(a->p[i]);a->n[i]=n;++a->live;a->bytes+=n;
    if(a->bytes>a->peak)a->peak=a->bytes;
    memset(a->p[i],0xa5,n);return a->p[i];
}
static void caller_release(void *context,void *p,size_t n,size_t alignment) {
    ledger *a=context;CHECK(alignment==_Alignof(max_align_t));size_t i=0;
    while(i<32 && a->p[i]!=p) { ++i; }
    CHECK(i<32 && a->n[i]==n);
    ++a->releases;--a->live;a->bytes-=n;a->p[i]=NULL;free(p);
}
static maelys_datalog_caller_allocator_t allocator(ledger *l) {
    maelys_datalog_caller_allocator_t a=MAELYS_DATALOG_CALLER_ALLOCATOR_INIT;
    a.context=l;a.acquire=caller_acquire;a.release=caller_release;return a;
}
static maelys_datalog_policy_t *policy;
typedef struct {
    ledger ledger;
    maelys_datalog_session_config_t *config;
    maelys_datalog_session_t *session;
    maelys_datalog_session_storage_plan_t plan;
    void *arena,*provider,*inputs_arena;
    maelys_datalog_session_inputs_t *inputs;
} fixture;
static void request(fixture *f,size_t cap) {
    maelys_datalog_caller_allocator_t a=allocator(&f->ledger);
    maelys_datalog_session_allocation_request_t q={.base=MAELYS_DATALOG_RESOURCE_REQUEST_INIT};
    q.base.struct_size=sizeof(q);q.base.memory_mode=MAELYS_DATALOG_MEMORY_BACKEND_ELASTIC;
    q.base.required_features=MAELYS_DATALOG_RESOURCE_CALLER_ALLOCATOR;
    q.base.capacity_mask=MAELYS_DATALOG_CAPACITY_ALL;
    q.base.input_facts=32;q.base.derived_facts=32;q.base.symbols=64;q.base.text_bytes=4096;
    q.execution_byte_cap=cap;q.allocator=&a;
    OK(maelys_datalog_session_config_set_resources(f->config,&q.base));
    /* Descriptor and request are copied, not borrowed. */
    memset(&a,0,sizeof(a));memset(&q,0,sizeof(q));
}
static void setup(fixture *f,int delta) {
    memset(f,0,sizeof(*f));OK(maelys_datalog_session_config_create(&f->config));
    if(delta)OK(maelys_datalog_session_config_set_backend_v7(f->config,allocation_fixture_transactions()));
    else OK(maelys_datalog_session_config_set_backend_v6(f->config,allocation_fixture_snapshot()));
    request(f,SIZE_MAX);f->plan=(maelys_datalog_session_storage_plan_t)MAELYS_DATALOG_SESSION_PLAN_INIT;
    OK(maelys_datalog_session_storage_requirements_configured(policy,0,f->config,&f->plan,NULL));
    f->provider=malloc(f->plan.backend_bytes);CHECK(f->provider);
    maelys_datalog_backend_storage_t b={sizeof(b),f->provider,f->plan.backend_bytes,_Alignof(max_align_t)};
    OK(maelys_datalog_session_config_set_backend_storage(f->config,&b));
    OK(maelys_datalog_session_storage_requirements_configured(policy,0,f->config,&f->plan,NULL));
    f->arena=malloc(f->plan.arena_bytes);CHECK(f->arena);
}
static maelys_datalog_status_t initialize(fixture *f,maelys_datalog_diagnostic_t *d) {
    forbidden=1;maelys_datalog_status_t rc=maelys_datalog_session_init_configured(f->arena,
        f->plan.arena_bytes,policy,0,f->config,&f->session,d);forbidden=0;return rc;
}
static void retained(fixture *f) {
    const char *words[]={"alpha","omega"};
    maelys_datalog_input_options_t o=MAELYS_DATALOG_INPUT_OPTIONS_INIT;
    o.fact_capacity=32;o.addition_capacity=0;o.removal_capacity=0;o.symbols=words;o.symbol_count=2;
    size_t n,a;OK(maelys_datalog_session_inputs_storage_requirements(f->session,&o,&n,&a));
    f->inputs_arena=malloc(n);CHECK(f->inputs_arena);
    forbidden=1;OK(maelys_datalog_session_inputs_init(f->session,&o,f->inputs_arena,n,&f->inputs));forbidden=0;
}
static void close_fixture(fixture *f) {
    forbidden=1;if(f->inputs)OK(maelys_datalog_session_inputs_free(f->inputs));
    if(f->session) { OK(maelys_datalog_session_free(f->session)); }
    forbidden=0;
    if(f->ledger.live)fprintf(stderr,"ledger leak: calls=%zu releases=%zu live=%zu bytes=%zu session=%p\n",f->ledger.calls,f->ledger.releases,f->ledger.live,f->ledger.bytes,(void *)f->session);
    CHECK(!f->ledger.live && !f->ledger.bytes);
    OK(maelys_datalog_session_config_free(f->config));free(f->provider);free(f->arena);free(f->inputs_arena);
}
static maelys_datalog_session_allocation_stats_t stats(fixture *f) {
    maelys_datalog_session_allocation_stats_t v=MAELYS_DATALOG_ALLOCATION_STATS_INIT;
    OK(maelys_datalog_session_get_allocation_stats(f->session,&v));
    CHECK(v.current_bytes==f->plan.total_execution_bytes+f->ledger.bytes);
    CHECK(v.remaining_bytes==v.cap_bytes-v.current_bytes && v.operation_peak_bytes>=v.current_bytes);
    return v;
}
typedef struct {
    maelys_datalog_window_t *w;
    maelys_datalog_group_window_t *g;
    void *arena;
} window_pair;
static maelys_datalog_status_t open_window(window_pair *w,fixture f[2],int grouped) {
    maelys_datalog_window_options_t o={sizeof(o),12,MAELYS_DATALOG_WINDOW_EXPIRATION};
    maelys_datalog_group_window_capacities_t c={4,12,24,2048};size_t n,a;
    if(grouped)OK(maelys_datalog_group_window_storage_requirements_configured(&c,&o,&n,&a));
    else OK(maelys_datalog_window_storage_requirements_configured(4,2048,&o,&n,&a));
    w->arena=malloc(n);CHECK(w->arena);w->w=NULL;w->g=NULL;
    return grouped?maelys_datalog_group_window_init_configured(w->arena,n,&c,&o,0,f[0].session,f[1].session,&w->g,NULL):
        maelys_datalog_window_init_configured(w->arena,n,4,2048,&o,0,f[0].session,f[1].session,&w->w,NULL);
}
static maelys_datalog_fact_t row(const char *p,int64_t id) {
    maelys_datalog_fact_t f={.predicate=p,.arity=2};
    f.terms[0]=(maelys_datalog_value_t){.kind=MAELYS_DATALOG_VALUE_INTEGER,.as.integer=id};
    f.terms[1]=(maelys_datalog_value_t){.kind=MAELYS_DATALOG_VALUE_SYMBOL,.as.symbol=id%2?"alpha":"omega"};return f;
}
static int replace(window_pair *w,const maelys_datalog_fact_t *f,size_t n) {
    return w->g?maelys_datalog_group_window_replace_static(w->g,f,n,NULL):maelys_datalog_window_replace_static(w->w,f,n,NULL);
}
static int push(window_pair *w,int64_t key,uint64_t time,uint32_t *id) {
    maelys_datalog_fact_t f=row("event",key),dup[2]={f,f};
    return w->g?maelys_datalog_group_window_push_until(w->g,dup,2,time,id,NULL):
        maelys_datalog_window_push_until(w->w,"event",f.terms+1,1,time,id,NULL);
}
static int expire(window_pair *w,uint64_t time,size_t *n) {
    return w->g?maelys_datalog_group_window_expire(w->g,time,n,NULL):maelys_datalog_window_expire(w->w,time,n,NULL);
}
static maelys_datalog_result_t *result(window_pair *w) {
    maelys_datalog_result_t *r=NULL;
    if(w->g)OK(maelys_datalog_group_window_result(w->g,&r));else OK(maelys_datalog_window_result(w->w,&r));return r;
}
static void close_window(window_pair *w) {
    if(w->g)OK(maelys_datalog_group_window_free(w->g));else OK(maelys_datalog_window_free(w->w));free(w->arena);
}
static void oracle(window_pair *w,maelys_datalog_session_t *reference) {
    const maelys_datalog_fact_t *f,*g=NULL;size_t n,ng=0;maelys_datalog_fact_t joined[32];
    if(w->g)OK(maelys_datalog_group_window_facts(w->g,&f,&n));
    else {OK(maelys_datalog_window_events(w->w,&f,&n));OK(maelys_datalog_window_static_facts(w->w,&g,&ng));}
    CHECK(n+ng<=32);memcpy(joined,f,n*sizeof(*f));if(ng)memcpy(joined+n,g,ng*sizeof(*g));
    maelys_datalog_result_t *expected=NULL,*r=result(w);OK(maelys_datalog_session_solve(reference,joined,n+ng,&expected,NULL));
    size_t a,b;OK(maelys_datalog_result_derived_fact_count(r,&a));OK(maelys_datalog_result_derived_fact_count(expected,&b));CHECK(a==b);
    const char *predicates[]={"seen","allow"};
    for(size_t p=0;p<2;++p) {
        maelys_datalog_fact_view_t x[32],y[32];
        OK(maelys_datalog_result_enumerate(r,predicates[p],2,x,32,&a));OK(maelys_datalog_result_enumerate(expected,predicates[p],2,y,32,&b));CHECK(a==b);
        for(size_t i=0;i<a;++i)for(size_t j=0;j<2;++j) {
            CHECK(x[i].terms[j].kind==y[i].terms[j].kind);
            if(x[i].terms[j].kind==MAELYS_DATALOG_VALUE_INTEGER)CHECK(x[i].terms[j].as.integer==y[i].terms[j].as.integer);
            else {const char *u,*v;size_t un,vn;OK(maelys_datalog_result_symbol_text(r,x[i].terms[j].as.symbol_id,&u,&un));
                OK(maelys_datalog_result_symbol_text(expected,y[i].terms[j].as.symbol_id,&v,&vn));CHECK(un==vn && !memcmp(u,v,un));}
        }
    }
    OK(maelys_datalog_result_free(expected));
}
typedef struct {
    unsigned char provider[4096];size_t provider_size;
    void *addresses[32];size_t sizes[32];unsigned char *images[32];
    maelys_datalog_session_allocation_stats_t stats;
} saved;
static void save(fixture *f,saved *s) {
    memset(s,0,sizeof(*s));s->provider_size=allocation_fixture_arena_image(f->provider,s->provider,sizeof(s->provider));CHECK(s->provider_size<=sizeof(s->provider));
    CHECK(allocation_fixture_scratch_valid(f->provider));
    s->stats=stats(f);
    for(size_t i=0;i<32;++i)if(f->ledger.p[i]) {
        s->addresses[i]=f->ledger.p[i];s->sizes[i]=f->ledger.n[i];s->images[i]=malloc(s->sizes[i]);CHECK(s->images[i]);
        memcpy(s->images[i],s->addresses[i],s->sizes[i]);
    }
}
static void unchanged(fixture *f,saved *s) {
    CHECK(allocation_fixture_scratch_valid(f->provider));
    unsigned char image[4096];CHECK(allocation_fixture_arena_image(f->provider,image,sizeof(image))==s->provider_size);
    CHECK(!memcmp(image,s->provider,s->provider_size));
    for(size_t i=0;i<32;++i) {
        CHECK(f->ledger.p[i]==s->addresses[i]);
        if(s->addresses[i])CHECK(f->ledger.n[i]==s->sizes[i] && !memcmp(s->addresses[i],s->images[i],s->sizes[i]));
    }
    maelys_datalog_session_allocation_stats_t v=stats(f);CHECK(v.current_bytes==s->stats.current_bytes && v.remaining_bytes==s->stats.remaining_bytes);
    /* Peak describes the failed operation and is intentionally not rolled back. */
}
static void discard(saved *s) {for(size_t i=0;i<32;++i)free(s->images[i]);}
static void unequal_caps(int grouped,int abi7) {
    fixture f[2];setup(&f[0],abi7);setup(&f[1],abi7);request(&f[1],SIZE_MAX-1);OK(initialize(&f[0],NULL));OK(initialize(&f[1],NULL));if(abi7){retained(&f[0]);retained(&f[1]);}
    size_t calls=f[0].ledger.calls+f[1].ledger.calls;saved s[2];save(&f[0],&s[0]);save(&f[1],&s[1]);window_pair w;
    CHECK(open_window(&w,f,grouped)==BAD);CHECK(!w.w && !w.g);CHECK(calls==f[0].ledger.calls+f[1].ledger.calls);
    for(size_t i=0;i<2;++i){unchanged(&f[i],&s[i]);discard(&s[i]);close_fixture(&f[i]);}free(w.arena);
}
static void bounded_banks(int grouped,int abi7) {
    fixture f[2];window_pair w;size_t cap=0;
    for(int pass=0;pass<2;++pass) {
        for(size_t i=0;i<2;++i){setup(&f[i],abi7);if(pass)request(&f[i],cap);OK(initialize(&f[i],NULL));if(abi7)retained(&f[i]);}
        OK(open_window(&w,f,grouped));
        if(!pass) {
            for(size_t i=0;i<2;++i){maelys_datalog_session_allocation_stats_t v=stats(&f[i]);if(v.operation_peak_bytes>cap)cap=v.operation_peak_bytes;}
        } else {
            saved before[2];for(size_t i=0;i<2;++i)save(&f[i],&before[i]);
            CHECK(before[0].stats.current_bytes!=before[1].stats.current_bytes);
            maelys_datalog_fact_t input[4];for(size_t i=0;i<4;++i)input[i]=row("user",(int64_t)i);
            maelys_datalog_result_t *old=result(&w);forbidden=1;CHECK(replace(&w,input,4)==FULL);forbidden=0;
            CHECK(result(&w)==old);for(size_t i=0;i<2;++i){unchanged(&f[i],&before[i]);discard(&before[i]);}
        }
        close_window(&w);for(size_t i=0;i<2;++i)close_fixture(&f[i]);
    }
}
static void shared_allocator(int grouped,int abi7) {
    fixture f[2];ledger common={0};maelys_datalog_caller_allocator_t caller=allocator(&common);
    for(size_t i=0;i<2;++i) {
        setup(&f[i],abi7);
        maelys_datalog_session_allocation_request_t q=MAELYS_DATALOG_ALLOCATION_REQUEST_INIT;
        q.base.capacity_mask=MAELYS_DATALOG_CAPACITY_ALL;q.base.input_facts=32;q.base.derived_facts=32;q.base.symbols=64;q.base.text_bytes=4096;
        q.execution_byte_cap=SIZE_MAX;q.allocator=&caller;
        OK(maelys_datalog_session_config_set_resources(f[i].config,&q.base));OK(initialize(&f[i],NULL));if(abi7)retained(&f[i]);
    }
    window_pair w;OK(open_window(&w,f,grouped));
    maelys_datalog_session_allocation_stats_t before[2],after;
    for(size_t i=0;i<2;++i){before[i]=(maelys_datalog_session_allocation_stats_t)MAELYS_DATALOG_ALLOCATION_STATS_INIT;OK(maelys_datalog_session_get_allocation_stats(f[i].session,&before[i]));}
    size_t bytes=common.bytes,blocks=common.live;common.fail=common.calls+2;
    maelys_datalog_fact_t input=row("user",1);forbidden=1;CHECK(replace(&w,&input,1)==SMALL);forbidden=0;
    CHECK(common.bytes==bytes && common.live==blocks);
    for(size_t i=0;i<2;++i){after=(maelys_datalog_session_allocation_stats_t)MAELYS_DATALOG_ALLOCATION_STATS_INIT;OK(maelys_datalog_session_get_allocation_stats(f[i].session,&after));CHECK(after.current_bytes==before[i].current_bytes && after.remaining_bytes>0);}
    common.fail=0;forbidden=1;OK(replace(&w,&input,1));forbidden=0;
    maelys_datalog_session_t *ref;OK(maelys_datalog_session_create(policy,0,&ref));oracle(&w,ref);OK(maelys_datalog_session_free(ref));
    close_window(&w);for(size_t i=0;i<2;++i)close_fixture(&f[i]);CHECK(!common.bytes && !common.live);
}
static void run_window(int grouped,int abi7) {
    fixture f[2];setup(&f[0],abi7);setup(&f[1],abi7);OK(initialize(&f[0],NULL));OK(initialize(&f[1],NULL));if(abi7){retained(&f[0]);retained(&f[1]);}
    char id0[65],id1[65];OK(maelys_datalog_session_execution_fingerprint(f[0].session,id0));OK(maelys_datalog_session_execution_fingerprint(f[1].session,id1));CHECK(!strcmp(id0,id1));
    window_pair w;OK(open_window(&w,f,grouped));maelys_datalog_session_t *ref;OK(maelys_datalog_session_create(policy,0,&ref));
    maelys_datalog_fact_t statics[5];for(size_t i=0;i<4;++i)statics[i]=row("user",(int64_t)i);statics[4]=row("event",0);
    forbidden=1;OK(replace(&w,statics,5));
    for(size_t i=0;i<4;++i){uint32_t id=99;OK(push(&w,(int64_t)i,10+i,&id));CHECK(id==i);oracle(&w,ref);}
    /* Give both provider banks the identical blocked state before the growth. */
    OK(replace(&w,statics,5));OK(replace(&w,statics,5));forbidden=0;
    saved before[2];for(size_t i=0;i<2;++i)save(&f[i],&before[i]);
    maelys_datalog_result_t *old=result(&w);size_t expired=99;
    for(size_t ordinal=1;ordinal<=3;++ordinal) {
        for(size_t i=0;i<2;++i)f[i].ledger.fail=f[i].ledger.calls+ordinal;
        forbidden=1;CHECK(expire(&w,20,&expired)==SMALL);forbidden=0;CHECK(expired==99 && result(&w)==old);
        for(size_t i=0;i<2;++i){f[i].ledger.fail=0;unchanged(&f[i],&before[i]);}
        oracle(&w,ref);
    }
    size_t ebytes,alignment;OK(maelys_datalog_result_explanation_storage_requirements(old,MAELYS_DATALOG_EXPLAIN_TRUE,&ebytes,&alignment));
    void *area=malloc(ebytes);CHECK(area);maelys_datalog_prepared_explanation_t *e;
    OK(maelys_datalog_result_prepare_explanation(old,MAELYS_DATALOG_EXPLAIN_TRUE,"seen",statics[0].terms,2,area,ebytes,&e));
    size_t acq=f[0].ledger.calls+f[1].ledger.calls,rel=f[0].ledger.releases+f[1].ledger.releases;
    forbidden=1;CHECK(expire(&w,20,&expired)==STATE);forbidden=0;CHECK(expired==99 && result(&w)==old);
    CHECK(f[0].ledger.calls+f[1].ledger.calls==acq+3);CHECK(f[0].ledger.releases+f[1].ledger.releases==rel+3);
    for(size_t i=0;i<2;++i){unchanged(&f[i],&before[i]);discard(&before[i]);}
    uint64_t now=99;int has_now=99;
    if(grouped)OK(maelys_datalog_group_window_expiry_watermark(w.g,&now,&has_now));else OK(maelys_datalog_window_expiry_watermark(w.w,&now,&has_now));CHECK(!has_now);
    OK(maelys_datalog_prepared_explanation_release(e));free(area);
    allocation_fixture_observation ob[2],after[2];for(size_t i=0;i<2;++i)allocation_fixture_observe(f[i].provider,&ob[i]);
    forbidden=1;OK(expire(&w,20,&expired));CHECK(expired==4);oracle(&w,ref);forbidden=0;
    size_t grew=0;for(size_t i=0;i<2;++i){allocation_fixture_observe(f[i].provider,&after[i]);grew+=after[i].output_bytes>ob[i].output_bytes;CHECK(stats(&f[i]).current_bytes<=SIZE_MAX);}CHECK(grew==1);
    int present=0;OK(maelys_datalog_result_query(result(&w),"allow",statics[0].terms,2,&present));CHECK(!present); /* static blocker survives */
    /* A no-op expiry preserves the live explanation/result and both charges. */
    old=result(&w);OK(maelys_datalog_result_explanation_storage_requirements(old,MAELYS_DATALOG_EXPLAIN_TRUE,&ebytes,&alignment));area=malloc(ebytes);CHECK(area);
    OK(maelys_datalog_result_prepare_explanation(old,MAELYS_DATALOG_EXPLAIN_TRUE,"seen",statics[0].terms,2,area,ebytes,&e));
    acq=f[0].ledger.calls+f[1].ledger.calls;for(size_t i=0;i<2;++i)save(&f[i],&before[i]);
    forbidden=1;OK(expire(&w,21,&expired));CHECK(!expired && result(&w)==old);forbidden=0;
    CHECK(acq==f[0].ledger.calls+f[1].ledger.calls);for(size_t i=0;i<2;++i){unchanged(&f[i],&before[i]);discard(&before[i]);}
    OK(maelys_datalog_prepared_explanation_release(e));free(area);
    forbidden=1;OK(replace(&w,statics,4));oracle(&w,ref);OK(maelys_datalog_result_query(result(&w),"allow",statics[0].terms,2,&present));CHECK(present);
    if(grouped) {
        uint32_t first,second;OK(push(&w,1,60,&first));OK(push(&w,1,80,&second));CHECK(second==first+1);
        OK(expire(&w,60,&expired));CHECK(expired==1);oracle(&w,ref);
        OK(maelys_datalog_result_query(result(&w),"allow",statics[1].terms,2,&present));CHECK(!present);
        OK(expire(&w,80,&expired));CHECK(expired==1);oracle(&w,ref);
        OK(maelys_datalog_result_query(result(&w),"allow",statics[1].terms,2,&present));CHECK(present);
    }
    uint32_t seed=UINT32_C(0xa110ca7e);
    for(size_t t=0;t<120;++t) {
        seed=seed*1664525u+1013904223u;uint32_t id=UINT32_MAX;
        if(t%5==0)OK(replace(&w,statics,4+(seed&1u)));
        else if(t%5==4)OK(expire(&w,100+t*4,&expired));
        else OK(push(&w,grouped?(int64_t)(seed%4):(int64_t)(4+t-t/5*2),102+t*4,&id));
        oracle(&w,ref);(void)stats(&f[0]);(void)stats(&f[1]);
    }
    forbidden=0;close_window(&w);OK(maelys_datalog_session_free(ref));close_fixture(&f[0]);close_fixture(&f[1]);
    printf("allocation windows: grouped=%d ABI=%d seed=a110ca7e 120 transactions PASS\n",grouped,abi7?7:6);
}
int main(void) {
    const maelys_datalog_predicate_t p[]={MAELYS_DATALOG_EDB("user",2),MAELYS_DATALOG_EDB("event",2),MAELYS_DATALOG_IDB_QUERY("seen",2),MAELYS_DATALOG_IDB_QUERY("allow",2)};
    const maelys_datalog_domain_t d={"allocation_windows",p,4,NULL,0};OK(maelys_datalog_domain_register(&d));
    const char *source="seen(I,X) :- user(I,X). allow(I,X) :- user(I,X), not(event(I,X)).";
    OK(maelys_datalog_policy_load_inline(d.name,"windows",source,strlen(source),&policy,NULL));
    for(int grouped=0;grouped<2;++grouped)for(int abi=0;abi<2;++abi){unequal_caps(grouped,abi);bounded_banks(grouped,abi);shared_allocator(grouped,abi);run_window(grouped,abi);}
    OK(maelys_datalog_policy_free(policy));return 0;
}
