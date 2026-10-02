/* SPDX-License-Identifier: MPL-2.0 */
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef memset
#include <maelys/datalog_backend.h>
#include <maelys/datalog_window.h>
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
static maelys_datalog_fact_t fact(size_t i) {
    maelys_datalog_fact_t f={.predicate="seed",.arity=1};
    if(i%3==0){f.terms[0].kind=MAELYS_DATALOG_VALUE_SYMBOL;f.terms[0].as.symbol=i%2?"alpha":"omega";}
    else {f.terms[0].kind=MAELYS_DATALOG_VALUE_INTEGER;f.terms[0].as.integer=(int64_t)i;}
    return f;
}
typedef struct {
    ledger ledger;
    maelys_datalog_session_config_t *config;
    maelys_datalog_session_t *session;
    maelys_datalog_session_storage_plan_t plan;
    void *arena,*provider;
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
static void close_fixture(fixture *f) {
    forbidden=1;if(f->session)OK(maelys_datalog_session_free(f->session));forbidden=0;
    if(f->ledger.live)fprintf(stderr,"ledger leak: calls=%zu releases=%zu live=%zu bytes=%zu session=%p\n",f->ledger.calls,f->ledger.releases,f->ledger.live,f->ledger.bytes,(void *)f->session);
    CHECK(!f->ledger.live && !f->ledger.bytes);
    OK(maelys_datalog_session_config_free(f->config));free(f->provider);free(f->arena);
}
static maelys_datalog_session_allocation_stats_t stats(fixture *f) {
    maelys_datalog_session_allocation_stats_t v=MAELYS_DATALOG_ALLOCATION_STATS_INIT;
    OK(maelys_datalog_session_get_allocation_stats(f->session,&v));
    CHECK(v.current_bytes==f->plan.total_execution_bytes+f->ledger.bytes);
    CHECK(v.remaining_bytes==v.cap_bytes-v.current_bytes && v.operation_peak_bytes>=v.current_bytes);
    return v;
}
static void compare(maelys_datalog_result_t *r,maelys_datalog_session_t *oracle,
    const maelys_datalog_fact_t *facts,size_t n) {
    maelys_datalog_result_t *other=NULL;OK(maelys_datalog_session_solve(oracle,facts,n,&other,NULL));
    size_t a,b;OK(maelys_datalog_result_derived_fact_count(r,&a));OK(maelys_datalog_result_derived_fact_count(other,&b));CHECK(a==b);
    maelys_datalog_fact_view_t x[32],y[32];OK(maelys_datalog_result_enumerate(r,"seen",1,x,32,&a));
    OK(maelys_datalog_result_enumerate(other,"seen",1,y,32,&b));CHECK(a==b);
    for(size_t i=0;i<a;++i) {
        CHECK(x[i].terms[0].kind==y[i].terms[0].kind);
        if(x[i].terms[0].kind==MAELYS_DATALOG_VALUE_INTEGER)CHECK(x[i].terms[0].as.integer==y[i].terms[0].as.integer);
        else {const char *u,*v;size_t un,vn;OK(maelys_datalog_result_symbol_text(r,x[i].terms[0].as.symbol_id,&u,&un));
            OK(maelys_datalog_result_symbol_text(other,y[i].terms[0].as.symbol_id,&v,&vn));CHECK(un==vn && !memcmp(u,v,un));}
    }
    OK(maelys_datalog_result_free(other));
}
static void admission(void) {
    fixture f;setup(&f,0);size_t calls=f.ledger.calls;
    maelys_datalog_session_allocation_request_t q={.base=MAELYS_DATALOG_RESOURCE_REQUEST_INIT};
    q.base.struct_size=sizeof(q);q.execution_byte_cap=1;q.allocator=(void *)(uintptr_t)1;
    /* Unknown optional FIXED tail is never followed. */
    OK(maelys_datalog_session_config_set_resources(f.config,&q.base));
    q.base.required_features=MAELYS_DATALOG_RESOURCE_CALLER_ALLOCATOR;
    CHECK(maelys_datalog_session_config_set_resources(f.config,&q.base)==NO);
    q.base.memory_mode=MAELYS_DATALOG_MEMORY_BACKEND_ELASTIC;
    q.base.struct_size=sizeof(q.base);CHECK(maelys_datalog_session_config_set_resources(f.config,&q.base)==BAD);
    q.base.struct_size=sizeof(q);maelys_datalog_caller_allocator_t a=allocator(&f.ledger);q.allocator=&a;
    q.execution_byte_cap=0;CHECK(maelys_datalog_session_config_set_resources(f.config,&q.base)==BAD);q.execution_byte_cap=SIZE_MAX;
    a.struct_size=MAELYS_DATALOG_CALLER_ALLOCATOR_PREFIX_SIZE;CHECK(maelys_datalog_session_config_set_resources(f.config,&q.base)==BAD);
    a=allocator(&f.ledger);a.contract_version=2;CHECK(maelys_datalog_session_config_set_resources(f.config,&q.base)==NO);
    a=allocator(&f.ledger);a.required_features=1;CHECK(maelys_datalog_session_config_set_resources(f.config,&q.base)==NO);
    a=allocator(&f.ledger);a.reserved=1;CHECK(maelys_datalog_session_config_set_resources(f.config,&q.base)==BAD);
    a=allocator(&f.ledger);a.release=NULL;CHECK(maelys_datalog_session_config_set_resources(f.config,&q.base)==BAD);
    request(&f,SIZE_MAX);OK(maelys_datalog_session_config_set_backend_v6(f.config,NULL));
    maelys_datalog_session_storage_plan_t p=MAELYS_DATALOG_SESSION_PLAN_INIT,old=p;
    CHECK(maelys_datalog_session_storage_requirements_configured(policy,0,f.config,&p,NULL)==NO && !memcmp(&old,&p,sizeof(p)));
    OK(maelys_datalog_session_config_set_backend(f.config,maelys_datalog_backend_reference()));
    CHECK(maelys_datalog_session_create_configured(policy,0,f.config,&f.session)==NO && !f.session);
    OK(maelys_datalog_session_config_set_backend_v6(f.config,allocation_fixture_snapshot()));
    maelys_datalog_diagnostic_t d=MAELYS_DATALOG_DIAGNOSTIC_INIT;
    request(&f,f.plan.total_execution_bytes-1);CHECK(initialize(&f,&d)==FULL && !f.session);
    CHECK(!strcmp(d.phase,"backend-admit") && !strcmp(d.field,"execution_byte_cap") && d.limit==f.plan.total_execution_bytes-1);
    CHECK(d.observed_count==f.plan.total_execution_bytes && d.code==MAELYS_DATALOG_DIAG_NONE);
    request(&f,f.plan.total_execution_bytes);CHECK(initialize(&f,&d)==FULL && !f.session);
    CHECK(!strcmp(d.phase,"backend-acquire") && f.ledger.calls==calls);
    request(&f,SIZE_MAX);OK(initialize(&f,NULL));close_fixture(&f);
}
static void preparation_failures(void) {
    for(size_t ordinal=1;ordinal<=2;++ordinal) {
        fixture f;setup(&f,0);f.ledger.fail=ordinal;
        maelys_datalog_diagnostic_t d=MAELYS_DATALOG_DIAGNOSTIC_INIT;
        CHECK(initialize(&f,&d)==SMALL && !f.session);
        CHECK(!f.ledger.live && f.ledger.releases==ordinal-1 && f.ledger.calls==ordinal);
        CHECK(d.status==SMALL && d.code==MAELYS_DATALOG_DIAG_NONE && d.present==MAELYS_DATALOG_DIAGNOSTIC_CONTEXT);
        CHECK(!strcmp(d.phase,"backend-acquire") && !strcmp(d.field,"allocator") && strtoull(d.token,NULL,10)>0);
        f.ledger.fail=0;OK(initialize(&f,NULL));CHECK(f.ledger.live==2);close_fixture(&f);
    }
}
typedef struct {fixture *f;unsigned mode;size_t calls;int visited;} probe;
static void inspect_hook(void *context,const maelys_datalog_allocation_service_t *s) {
    probe *p=context;fixture *f=p->f;++p->visited;
    maelys_datalog_session_allocation_stats_t v=MAELYS_DATALOG_ALLOCATION_STATS_INIT,copy=v;
    CHECK(maelys_datalog_session_get_allocation_stats(f->session,&v)==STATE && !memcmp(&v,&copy,sizeof(v)));
    maelys_datalog_allocation_budget_t b=MAELYS_DATALOG_ALLOCATION_BUDGET_INIT;
    size_t calls=f->ledger.calls,live=f->ledger.live;
    for(size_t align=1;align<=s->maximum_alignment;align*=2) {
        OK(s->inspect(s->context,37,align,&b));size_t old=b.current_bytes;
        CHECK(b.required_charge==s->tracking_bytes+align-1+37 && b.fits);
        void *block=NULL;OK(s->acquire(s->context,37,align,&block,NULL));CHECK(block && (uintptr_t)block%align==0);
        memset(block,0xef,37);OK(s->inspect(s->context,37,align,&b));CHECK(b.current_bytes==old+b.required_charge);
        s->release(s->context,block);OK(s->inspect(s->context,37,align,&b));CHECK(b.current_bytes==old);
    }
    CHECK(f->ledger.live==live);p->calls=f->ledger.calls-calls;
    maelys_datalog_allocation_budget_t old=b;
    CHECK(s->inspect(s->context,0,1,&b)==BAD && !memcmp(&old,&b,sizeof(b)));
    CHECK(s->inspect(s->context,SIZE_MAX,1,&b)==BAD && !memcmp(&old,&b,sizeof(b)));
    CHECK(s->inspect(s->context,1,3,&b)==BAD && !memcmp(&old,&b,sizeof(b)));
    CHECK(s->inspect(s->context,1,s->maximum_alignment*2,&b)==BAD && !memcmp(&old,&b,sizeof(b)));
    b.struct_size=MAELYS_DATALOG_ALLOCATION_BUDGET_PREFIX_SIZE;old=b;
    CHECK(s->inspect(s->context,1,1,&b)==SMALL && !memcmp(&old,&b,sizeof(b)));
    b=(maelys_datalog_allocation_budget_t)MAELYS_DATALOG_ALLOCATION_BUDGET_INIT;
    b.contract_version=2;old=b;CHECK(s->inspect(s->context,1,1,&b)==NO && !memcmp(&old,&b,sizeof(b)));
    b=(maelys_datalog_allocation_budget_t)MAELYS_DATALOG_ALLOCATION_BUDGET_INIT;
    OK(s->inspect(s->context,1,1,&b));old=b;calls=f->ledger.calls;
    for(size_t i=0;i<5;++i) {
        maelys_datalog_allocation_budget_t too=MAELYS_DATALOG_ALLOCATION_BUDGET_INIT;
        OK(s->inspect(s->context,b.remaining_bytes,1,&too));CHECK(!too.fits);
    }
    OK(s->inspect(s->context,1,1,&b));CHECK(!memcmp(&old,&b,sizeof(b)) && f->ledger.calls==calls);
    if(p->mode) {
        void *block=(void *)(uintptr_t)1;size_t releases=f->ledger.releases;
        maelys_datalog_diagnostic_t d=MAELYS_DATALOG_DIAGNOSTIC_INIT;
        CHECK(s->acquire(s->context,b.remaining_bytes,1,&block,&d)==FULL && !block);
        CHECK(d.limit==b.cap_bytes && d.observed_count==b.current_bytes+s->tracking_bytes+b.remaining_bytes);
        CHECK(s->acquire(s->context,1,1,&block,&d)==FULL && !block);
        OK(s->inspect(s->context,1,1,&b));CHECK(f->ledger.calls==calls && f->ledger.releases==releases);
    }
}
static void inspection(void) {
    fixture f;setup(&f,0);request(&f,f.plan.total_execution_bytes+65536);OK(initialize(&f,NULL));
    probe p={.f=&f};allocation_fixture_hook(f.provider,inspect_hook,&p);
    maelys_datalog_fact_t x=fact(1);maelys_datalog_result_t *r=NULL;
    forbidden=1;OK(maelys_datalog_session_solve(f.session,&x,1,&r,NULL));OK(maelys_datalog_result_free(r));forbidden=0;
    CHECK(p.visited==1 && p.calls>0);
    p.mode=1;size_t old=stats(&f).current_bytes;
    forbidden=1;CHECK(maelys_datalog_session_solve(f.session,&x,1,&r,NULL)==FULL && !r);forbidden=0;
    CHECK(stats(&f).current_bytes==old);
    allocation_fixture_hook(f.provider,NULL,NULL);
    maelys_datalog_allocation_service_t service;allocation_fixture_service(f.provider,&service);
    maelys_datalog_allocation_budget_t b=MAELYS_DATALOG_ALLOCATION_BUDGET_INIT,copy=b;
    CHECK(service.inspect(service.context,1,1,&b)==STATE && !memcmp(&b,&copy,sizeof(b)));
    OK(maelys_datalog_session_solve(f.session,&x,1,&r,NULL));OK(maelys_datalog_result_free(r));close_fixture(&f);
}
static void telemetry_and_identity(void) {
    fixture a,b,c;setup(&a,0);setup(&b,0);setup(&c,0);
    size_t cap=a.plan.total_execution_bytes+65536;request(&a,cap);request(&b,cap);request(&c,cap+1);
    OK(initialize(&a,NULL));OK(initialize(&b,NULL));OK(initialize(&c,NULL));
    char x[65],y[65],z[65];OK(maelys_datalog_session_execution_fingerprint(a.session,x));
    OK(maelys_datalog_session_execution_fingerprint(b.session,y));OK(maelys_datalog_session_execution_fingerprint(c.session,z));
    CHECK(!strcmp(x,y) && strcmp(x,z));
    maelys_datalog_session_allocation_stats_t initial=stats(&a);CHECK(initial.operation_peak_bytes==initial.current_bytes);
    struct {maelys_datalog_session_allocation_stats_t v;unsigned char tail[13];} extended;
    memset(&extended,0xa5,sizeof(extended));extended.v=(maelys_datalog_session_allocation_stats_t)MAELYS_DATALOG_ALLOCATION_STATS_INIT;
    extended.v.struct_size=sizeof(extended);OK(maelys_datalog_session_get_allocation_stats(a.session,&extended.v));
    CHECK(extended.v.struct_size==sizeof(extended));for(size_t i=0;i<13;++i)CHECK(extended.tail[i]==0xa5);
    for(unsigned mode=0;mode<3;++mode) {
        maelys_datalog_session_allocation_stats_t v=initial;
        if(!mode)v.struct_size=MAELYS_DATALOG_ALLOCATION_STATS_PREFIX_SIZE;
        else if(mode==1)v.contract_version=2;else v.reserved=1;
        maelys_datalog_session_allocation_stats_t old=v;
        CHECK(maelys_datalog_session_get_allocation_stats(a.session,&v)==(mode==0?SMALL:mode==1?NO:BAD));CHECK(!memcmp(&old,&v,sizeof(v)));
    }
    maelys_datalog_session_t *fixed;OK(maelys_datalog_session_create(policy,0,&fixed));
    maelys_datalog_session_allocation_stats_t v=initial,old=v;CHECK(maelys_datalog_session_get_allocation_stats(fixed,&v)==NO && !memcmp(&old,&v,sizeof(v)));
    OK(maelys_datalog_session_free(fixed));
    maelys_datalog_fact_t f[8];for(size_t i=0;i<8;++i)f[i]=fact(i);
    maelys_datalog_result_t *r;OK(maelys_datalog_session_solve(a.session,f,8,&r,NULL));
    maelys_datalog_session_allocation_stats_t pending=stats(&a);CHECK(pending.current_bytes>initial.current_bytes);
    size_t releases=a.ledger.releases;OK(maelys_datalog_result_free(r));
    v=stats(&a);CHECK(v.operation_peak_bytes==pending.operation_peak_bytes && v.current_bytes<pending.current_bytes && a.ledger.releases==releases+2);
    OK(maelys_datalog_session_execution_fingerprint(a.session,y));CHECK(!strcmp(x,y));
    close_fixture(&a);close_fixture(&b);close_fixture(&c);
}
static void failures_and_reuse(void) {
    fixture f;setup(&f,0);OK(initialize(&f,NULL));maelys_datalog_fact_t x[16];for(size_t i=0;i<16;++i)x[i]=fact(i);
    maelys_datalog_result_t *r=NULL;OK(maelys_datalog_session_solve(f.session,x,1,&r,NULL));OK(maelys_datalog_result_free(r));
    for(unsigned swallowed=0;swallowed<2;++swallowed)for(size_t ordinal=1;ordinal<=2;++ordinal) {
        unsigned char saved[8192],after[8192];size_t n=allocation_fixture_committed(f.provider,saved,sizeof(saved));CHECK(n<sizeof(saved));
        size_t charge=stats(&f).current_bytes,live=f.ledger.live,releases=f.ledger.releases;
        f.ledger.fail=f.ledger.calls+ordinal;allocation_fixture_fault(f.provider,swallowed?4:0);
        maelys_datalog_diagnostic_t d=MAELYS_DATALOG_DIAGNOSTIC_INIT;
        forbidden=1;CHECK(maelys_datalog_session_solve(f.session,x,16,&r,&d)==SMALL && !r);forbidden=0;
        CHECK(!strcmp(d.field,"allocator") && f.ledger.live==live && f.ledger.releases==releases+ordinal-1);
        CHECK(stats(&f).current_bytes==charge);
        CHECK(allocation_fixture_committed(f.provider,after,sizeof(after))==n && !memcmp(saved,after,n));
        f.ledger.fail=0;allocation_fixture_fault(f.provider,0);
        forbidden=1;OK(maelys_datalog_session_solve(f.session,x,1,&r,NULL));OK(maelys_datalog_result_free(r));forbidden=0;
    }
    for(unsigned fault=1;fault<=5;++fault) {
        if(fault==4)continue;
        size_t charge=stats(&f).current_bytes;allocation_fixture_fault(f.provider,fault);
        forbidden=1;CHECK(maelys_datalog_session_solve(f.session,x,16,&r,NULL)!=0 && !r);forbidden=0;
        CHECK(stats(&f).current_bytes==charge);
    }
    allocation_fixture_fault(f.provider,0);maelys_datalog_session_t *oracle;OK(maelys_datalog_session_create(policy,0,&oracle));
    uint32_t seed=UINT32_C(0xa119019);size_t before=engine_calls;
    forbidden=1;
    for(size_t tx=0;tx<512;++tx) {
        seed=seed*1664525u+1013904223u;size_t n=(seed>>16)%17;
        OK(maelys_datalog_session_solve(f.session,x,n,&r,NULL));compare(r,oracle,x,n);
        OK(maelys_datalog_result_free(r));CHECK(f.ledger.live==2);(void)stats(&f);
    }
    forbidden=0;CHECK(engine_calls==before);OK(maelys_datalog_session_free(oracle));close_fixture(&f);
}
static void abi7(void) {
    fixture f;setup(&f,1);OK(initialize(&f,NULL));maelys_datalog_input_options_t o=MAELYS_DATALOG_INPUT_OPTIONS_INIT;
    o.fact_capacity=32;o.addition_capacity=8;o.removal_capacity=8;
    size_t bytes,alignment;OK(maelys_datalog_session_inputs_storage_requirements(f.session,&o,&bytes,&alignment));
    void *storage=malloc(bytes);maelys_datalog_session_inputs_t *inputs;OK(maelys_datalog_session_inputs_init(f.session,&o,storage,bytes,&inputs));
    maelys_datalog_input_base_t base,after;OK(maelys_datalog_session_inputs_base(inputs,&base));
    maelys_datalog_fact_t x=fact(1);maelys_datalog_result_t *r;
    forbidden=1;OK(maelys_datalog_session_inputs_apply(inputs,base,&x,1,NULL,0,&r,NULL));OK(maelys_datalog_result_free(r));forbidden=0;
    OK(maelys_datalog_session_inputs_base(inputs,&base));size_t charge=stats(&f).current_bytes;
    f.ledger.fail=f.ledger.calls+2;x=fact(2);
    forbidden=1;CHECK(maelys_datalog_session_inputs_apply(inputs,base,&x,1,NULL,0,&r,NULL)==SMALL && !r);forbidden=0;
    OK(maelys_datalog_session_inputs_base(inputs,&after));CHECK(!memcmp(&base,&after,sizeof(base)) && stats(&f).current_bytes==charge);
    f.ledger.fail=0;forbidden=1;OK(maelys_datalog_session_inputs_apply(inputs,base,&x,1,NULL,0,&r,NULL));OK(maelys_datalog_result_free(r));forbidden=0;
    allocation_fixture_observation obs;allocation_fixture_observe(f.provider,&obs);OK(maelys_datalog_session_inputs_base(inputs,&after));
    CHECK(obs.retained==2 && !memcmp(&after,&obs.base,sizeof(after)));
    OK(maelys_datalog_session_inputs_free(inputs));free(storage);close_fixture(&f);
}
static void exact_boundaries(void) {
    fixture witness;setup(&witness,0);OK(initialize(&witness,NULL));
    size_t prepared=witness.ledger.bytes;maelys_datalog_allocation_service_t service;allocation_fixture_service(witness.provider,&service);
    maelys_datalog_fact_t x[16];for(size_t i=0;i<16;++i)x[i]=fact(i);
    maelys_datalog_result_t *r;size_t initial=stats(&witness).current_bytes;
    OK(maelys_datalog_session_solve(witness.session,x,16,&r,NULL));
    size_t growth=stats(&witness).current_bytes-initial;OK(maelys_datalog_result_free(r));
    CHECK(growth>prepared);close_fixture(&witness);
    for(size_t shortfall=0;shortfall<=1;++shortfall) {
        fixture f;setup(&f,0);request(&f,f.plan.total_execution_bytes+prepared-shortfall);
        maelys_datalog_diagnostic_t d=MAELYS_DATALOG_DIAGNOSTIC_INIT;
        CHECK(initialize(&f,&d)==(shortfall?FULL:0));
        if(shortfall)CHECK(f.ledger.calls==1 && f.ledger.releases==1 && !f.session);
        else CHECK(stats(&f).remaining_bytes==0);
        close_fixture(&f);
    }
    for(size_t shortfall=0;shortfall<=1;++shortfall) {
        fixture f;setup(&f,0);request(&f,f.plan.total_execution_bytes+prepared+growth-shortfall);OK(initialize(&f,NULL));
        size_t old=stats(&f).current_bytes;
        maelys_datalog_diagnostic_t d=MAELYS_DATALOG_DIAGNOSTIC_INIT;
        forbidden=1;CHECK(maelys_datalog_session_solve(f.session,x,16,&r,&d)==(shortfall?FULL:0));forbidden=0;
        if(shortfall) {CHECK(!r && stats(&f).current_bytes==old);CHECK(f.ledger.calls==3 && f.ledger.releases==1);}
        else {CHECK(stats(&f).remaining_bytes==0);OK(maelys_datalog_result_free(r));}
        close_fixture(&f);
    }
}
static void leases(void) {
    fixture f;setup(&f,0);OK(initialize(&f,NULL));maelys_datalog_fact_t x=fact(1);
    maelys_datalog_result_t *r;OK(maelys_datalog_session_solve(f.session,&x,1,&r,NULL));
    size_t bytes,alignment;OK(maelys_datalog_result_explanation_storage_requirements(r,MAELYS_DATALOG_EXPLAIN_TRUE,&bytes,&alignment));
    void *storage=malloc(bytes);CHECK(storage);maelys_datalog_prepared_explanation_t *e;
    forbidden=1;OK(maelys_datalog_result_prepare_explanation(r,MAELYS_DATALOG_EXPLAIN_TRUE,"seen",x.terms,1,storage,bytes,&e));
    size_t releases=f.ledger.releases;maelys_datalog_session_allocation_stats_t before=stats(&f);
    CHECK(maelys_datalog_result_free(r)==STATE && f.ledger.releases==releases);
    maelys_datalog_session_allocation_stats_t after=stats(&f);CHECK(!memcmp(&before,&after,sizeof(before)));
    char text[128];OK(maelys_datalog_prepared_explanation_write_text(e,text,sizeof(text)));CHECK(strstr(text,"committed"));
    OK(maelys_datalog_prepared_explanation_release(e));OK(maelys_datalog_result_free(r));forbidden=0;
    CHECK(f.ledger.releases==releases+2);free(storage);close_fixture(&f);
}
typedef struct {fixture *self,*other;int mode;} invalid_probe;
static void invalid_hook(void *context,const maelys_datalog_allocation_service_t *s) {
    invalid_probe *p=context;void *out=(void *)(uintptr_t)1;
    if(p->mode==0) {
        maelys_datalog_diagnostic_t d=MAELYS_DATALOG_DIAGNOSTIC_INIT;
        CHECK(s->acquire(s->context,SIZE_MAX-s->tracking_bytes,1,&out,&d)==BAD && !out);
        CHECK(s->acquire(s->context,1,1,&out,&d)==BAD && !out);
    } else if(p->mode==1) {
        maelys_datalog_allocation_service_t other;allocation_fixture_service(p->other->provider,&other);
        CHECK(other.acquire(other.context,1,1,&out,NULL)==STATE && !out);
    } else if(p->mode==2) {
        /* A payload address from another service is not in this owner's list. */
        maelys_datalog_allocation_service_t other;allocation_fixture_service(p->other->provider,&other);
        (void)other;
        s->release(s->context,p->other->ledger.p[0]);
    } else if(p->mode==4) {
        s->release(s->context,(void *)allocation_fixture_live(p->self->provider));
    } else {
        maelys_datalog_diagnostic_t short_diag=MAELYS_DATALOG_DIAGNOSTIC_INIT;
        short_diag.struct_size=0;CHECK(s->acquire(s->context,1,1,&out,&short_diag)==SMALL && !out);
    }
}
static void forbidden_service(void) {
    fixture a,b;setup(&a,0);setup(&b,0);OK(initialize(&a,NULL));OK(initialize(&b,NULL));
    maelys_datalog_result_t *r;maelys_datalog_fact_t x=fact(1);
    invalid_probe p={&a,&b,0};allocation_fixture_hook(a.provider,invalid_hook,&p);
    for(p.mode=0;p.mode<5;++p.mode) {
        size_t current=stats(&a).current_bytes,other=stats(&b).current_bytes;
        maelys_datalog_status_t expected=p.mode==0?BAD:p.mode==1?0:(p.mode==2 || p.mode==4)?STATE:SMALL;
        forbidden=1;CHECK(maelys_datalog_session_solve(a.session,&x,1,&r,NULL)==expected);
        if(!expected)OK(maelys_datalog_result_free(r));else CHECK(!r);
        forbidden=0;if(expected)CHECK(stats(&a).current_bytes==current);
        CHECK(stats(&b).current_bytes==other);
    }
    allocation_fixture_hook(a.provider,NULL,NULL);close_fixture(&a);close_fixture(&b);
}

/* Public records for an independently computed Python SHA-256 oracle, also
 * run after separate consumer/provider compilation against installed SDKs. */
static void identity_records(void) {
    for(int abi=0;abi<2;++abi)for(size_t cap=1000000;cap<=1000001;++cap) {
        fixture f;setup(&f,abi);request(&f,cap);OK(initialize(&f,NULL));
        const maelys_datalog_program_t *p;maelys_datalog_program_info_t info;char program[65],actual[65];
        maelys_datalog_session_resources_t r=MAELYS_DATALOG_RESOURCES_INIT;
        OK(maelys_datalog_session_program(f.session,&p));OK(maelys_datalog_program_info(p,&info));
        OK(maelys_datalog_program_fingerprint(p,program));OK(maelys_datalog_session_execution_fingerprint(f.session,actual));
        OK(maelys_datalog_session_get_resources(f.session,&r));
        printf("IDENTITY %s %s %s %llu 1048576 %zu %zu %zu %zu %u %llu %zu %d %s\n",
            program,allocation_fixture_snapshot()->name,allocation_fixture_snapshot()->semantic_id,
            (unsigned long long)info.required_capabilities,r.input_facts,r.derived_facts,r.symbols,r.text_bytes,
            r.memory_mode,(unsigned long long)r.required_features,cap,abi?7:6,actual);
        close_fixture(&f);
    }
}

int main(int argc,char **argv) {
    const maelys_datalog_predicate_t predicates[]={MAELYS_DATALOG_EDB("seed",1),MAELYS_DATALOG_IDB_QUERY("seen",1)};
    const maelys_datalog_domain_t domain={"allocation",predicates,2,NULL,0};OK(maelys_datalog_domain_register(&domain));
    const char *source="seen(X) :- seed(X).";OK(maelys_datalog_policy_load_inline("allocation","p",source,strlen(source),&policy,NULL));
    const char *which=argc==2?argv[1]:"all";
#define RUN(name) do {if(!strcmp(which,"all") || !strcmp(which,#name)){name();puts(#name " PASS");}} while(0)
    RUN(identity_records);RUN(admission);RUN(preparation_failures);RUN(inspection);RUN(telemetry_and_identity);RUN(failures_and_reuse);RUN(abi7);RUN(exact_boundaries);RUN(leases);RUN(forbidden_service);
    OK(maelys_datalog_policy_free(policy));return 0;
}
