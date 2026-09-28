/* SPDX-License-Identifier: MPL-2.0 */
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef memset
#include <maelys/datalog_advanced.h>
#include <maelys/datalog_window.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
#define OK(x) CHECK((x)==MAELYS_DATALOG_STATUS_OK)
#define BAD MAELYS_DATALOG_STATUS_INVALID_ARGUMENT
#define NO MAELYS_DATALOG_STATUS_UNSUPPORTED
#define FULL MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE
static size_t allocations, frees;
static int forbidden;
void *maelys_test_malloc(size_t n) { CHECK(!forbidden); ++allocations; return malloc(n); }
void *maelys_test_calloc(size_t n,size_t w) { CHECK(!forbidden); ++allocations; return calloc(n,w); }
void *maelys_test_realloc(void *p,size_t n) { CHECK(!forbidden); ++allocations; return realloc(p,n); }
void maelys_test_free(void *p) { if(p) { CHECK(!forbidden); ++frees; } free(p); }
void *maelys_test_memset(void *p,int c,size_t n) { return memset(p,c,n); }
static maelys_datalog_policy_t *policy;
static maelys_datalog_program_info_t program_bounds;
static maelys_datalog_session_resources_t seen;
static unsigned queries, prepares, destroys, commits, aborts;
static int fail_prepare, overflow_emit;
static void *reentry_arena;
static size_t reentry_bytes;
static maelys_datalog_session_config_t *reentry_config;
typedef struct { size_t E,D,S,T; unsigned live; } state_t;
static size_t provider_bytes=sizeof(state_t), provider_alignment=_Alignof(max_align_t);
static maelys_datalog_fact_t fact(int64_t n) {
    maelys_datalog_fact_t f={.predicate="seed",.arity=1};
    f.terms[0].kind=MAELYS_DATALOG_VALUE_INTEGER;f.terms[0].as.integer=n;return f;
}
static void unchanged_program(const maelys_datalog_program_t *p) {
    maelys_datalog_program_info_t i;OK(maelys_datalog_program_info(p,&i));
    /* Negative control: session quotas may NEVER travel in program_info. This
     * assertion runs in both requirements and prepare, including two sessions. */
    CHECK(i.max_input_facts==program_bounds.max_input_facts);
    CHECK(i.max_derived_facts==program_bounds.max_derived_facts);
    CHECK(i.max_facts_per_predicate==program_bounds.max_facts_per_predicate);
    CHECK(i.required_capabilities==program_bounds.required_capabilities);
}
static maelys_datalog_status_t requirements(const maelys_datalog_program_t *p,
    const maelys_datalog_session_resources_t *v,size_t *n,size_t *a) {
    unchanged_program(p);++queries;seen=*v;
    CHECK(v->struct_size==sizeof(*v) && v->memory_mode==MAELYS_DATALOG_MEMORY_FIXED);
    *n=provider_bytes;*a=provider_alignment;return 0;
}
static maelys_datalog_status_t prepare(const maelys_datalog_program_t *p,
    const maelys_datalog_session_resources_t *v,const maelys_datalog_backend_storage_t *storage,void **out) {
    unchanged_program(p);++prepares;
    CHECK(!memcmp(&seen,v,sizeof(*v)));
    CHECK(storage->size==provider_bytes && storage->alignment==provider_alignment);
    if (reentry_arena) {
        maelys_datalog_session_t *nested;
        CHECK(maelys_datalog_session_init_configured(reentry_arena,reentry_bytes,
            policy,0,reentry_config,&nested,NULL)==MAELYS_DATALOG_STATUS_INVALID_STATE);
        CHECK(!nested);
    }
    *out=storage->bytes;
    if (*out) *(state_t *)*out=(state_t){v->input_facts,v->derived_facts,v->symbols,v->text_bytes,0};
    return fail_prepare?NO:0;
}
static maelys_datalog_status_t solve(void *s,const maelys_datalog_fact_t *facts,size_t n,
    maelys_datalog_backend_output_t *out,void **result,maelys_datalog_diagnostic_t *diag) {
    (void)diag;*result=s;
    size_t count=overflow_emit?3:n;
    for(size_t i=0;i<count;++i) {
        maelys_datalog_fact_t f=overflow_emit?fact((int64_t)i):facts[i];f.predicate="seen";
        maelys_datalog_status_t rc=maelys_datalog_backend_emit(out,&f);
        if(rc) return rc;
    }
    return 0;
}
static void commit(void *s,void *r) {(void)s;(void)r;++commits;}
static void release(void *s,void *r) {(void)s;(void)r;++aborts;}
static void destroy(void *s) {(void)s;++destroys;}
static const maelys_datalog_backend_v6_t provider={
    6,sizeof(provider),"recording6","test.recording6.v1",MAELYS_DATALOG_CAP_POSITIVE,
    MAELYS_DATALOG_RESOURCE_SESSION_CAPACITIES,requirements,prepare,solve,NULL,NULL,NULL,commit,release,destroy};
static maelys_datalog_session_config_t *config(size_t E,size_t D,size_t S,size_t T) {
    maelys_datalog_session_config_t *c;OK(maelys_datalog_session_config_create(&c));
    maelys_datalog_session_resource_request_t q=MAELYS_DATALOG_RESOURCE_REQUEST_INIT;
    q.capacity_mask=MAELYS_DATALOG_CAPACITY_ALL;q.input_facts=E;q.derived_facts=D;q.symbols=S;q.text_bytes=T;
    OK(maelys_datalog_session_config_set_resources(c,&q));return c;
}
static maelys_datalog_session_storage_plan_t plan(maelys_datalog_session_config_t *c) {
    maelys_datalog_session_storage_plan_t p=MAELYS_DATALOG_SESSION_PLAN_INIT;
    OK(maelys_datalog_session_storage_requirements_configured(policy,0,c,&p,NULL));
    CHECK(p.arena_bytes==p.host_bytes+p.backend_bytes+p.explanation_bytes+p.padding_bytes);
    CHECK(p.total_execution_bytes==p.arena_bytes+p.external_backend_bytes+p.external_explanation_bytes);
    return p;
}
static void records(void) {
    maelys_datalog_session_config_t *c=config(4,8,32,2048);
    maelys_datalog_session_resource_request_t q=MAELYS_DATALOG_RESOURCE_REQUEST_INIT;
    q.memory_mode=MAELYS_DATALOG_MEMORY_BACKEND_ELASTIC;CHECK(maelys_datalog_session_config_set_resources(c,&q)==NO);
    q.memory_mode=0;q.required_features=MAELYS_DATALOG_RESOURCE_CALLER_ALLOCATOR;CHECK(maelys_datalog_session_config_set_resources(c,&q)==NO);
    q.required_features=UINT64_C(1)<<63;CHECK(maelys_datalog_session_config_set_resources(c,&q)==NO);
    q.required_features=0;q.capacity_mask=16;CHECK(maelys_datalog_session_config_set_resources(c,&q)==BAD);
    q.capacity_mask=1;q.struct_size=MAELYS_DATALOG_RESOURCE_REQUEST_PREFIX_SIZE+sizeof(uint64_t);
    CHECK(maelys_datalog_session_config_set_resources(c,&q)==BAD);
    q.struct_size=sizeof(q)-1;CHECK(maelys_datalog_session_config_set_resources(c,&q)==BAD);
    maelys_datalog_session_t *s;OK(maelys_datalog_session_create_configured(policy,0,c,&s));
    maelys_datalog_session_resources_t v=MAELYS_DATALOG_RESOURCES_INIT;OK(maelys_datalog_session_get_resources(s,&v));
    CHECK(v.input_facts==4 && v.derived_facts==8 && v.symbols==32 && v.text_bytes==2048);
    CHECK(v.required_features==MAELYS_DATALOG_RESOURCE_SESSION_CAPACITIES);
    v.struct_size=MAELYS_DATALOG_RESOURCES_PREFIX_SIZE;maelys_datalog_session_resources_t old=v;
    CHECK(maelys_datalog_session_get_resources(s,&v)==MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL);CHECK(!memcmp(&old,&v,sizeof(v)));
    struct {maelys_datalog_session_resources_t v;unsigned char tail[32];} extended;
    memset(&extended,0xa7,sizeof(extended));extended.v=(maelys_datalog_session_resources_t)MAELYS_DATALOG_RESOURCES_INIT;
    extended.v.struct_size=sizeof(extended);OK(maelys_datalog_session_get_resources(s,&extended.v));
    CHECK(extended.v.struct_size==sizeof(extended));for(size_t i=0;i<32;++i)CHECK(extended.tail[i]==0xa7);
    OK(maelys_datalog_session_free(s));
    /* A real short object, not a full object advertising a shorter length. */
    struct {size_t size;uint32_t version,mode;uint64_t features;} prefix={sizeof(prefix),1,0,0};
    OK(maelys_datalog_session_config_set_resources(c,(const void *)&prefix));
    OK(maelys_datalog_session_create_configured(policy,0,c,&s));v=(maelys_datalog_session_resources_t)MAELYS_DATALOG_RESOURCES_INIT;
    OK(maelys_datalog_session_get_resources(s,&v));CHECK(v.input_facts==program_bounds.max_input_facts && !v.required_features);
    OK(maelys_datalog_session_free(s));OK(maelys_datalog_session_config_free(c));
}
static void agreement(void) {
    maelys_datalog_session_config_t *c=config(4,2,32,2048);
    OK(maelys_datalog_session_config_set_backend_v6(c,&provider));
    maelys_datalog_session_t *a,*b;unsigned before=queries;
    OK(maelys_datalog_session_create_configured(policy,0,c,&a));CHECK(queries==before+1);
    maelys_datalog_session_resource_request_t q=MAELYS_DATALOG_RESOURCE_REQUEST_INIT;
    q.capacity_mask=3;q.input_facts=7;q.derived_facts=6;OK(maelys_datalog_session_config_set_resources(c,&q));
    OK(maelys_datalog_session_create_configured(policy,0,c,&b));
    maelys_datalog_session_resources_t v=MAELYS_DATALOG_RESOURCES_INIT;OK(maelys_datalog_session_get_resources(a,&v));CHECK(v.input_facts==4 && v.derived_facts==2);
    OK(maelys_datalog_session_get_resources(b,&v));CHECK(v.input_facts==7 && v.derived_facts==6);
    maelys_datalog_fact_t f=fact(12);maelys_datalog_result_t *result;
    OK(maelys_datalog_session_solve(a,&f,1,&result,NULL));size_t n;OK(maelys_datalog_result_derived_fact_count(result,&n));CHECK(n==1);OK(maelys_datalog_result_free(result));
    unsigned prior=commits;overflow_emit=1;
    CHECK(maelys_datalog_session_solve(a,&f,1,&result,NULL)==FULL && !result && commits==prior);overflow_emit=0;
    OK(maelys_datalog_session_solve(a,&f,1,&result,NULL));OK(maelys_datalog_result_free(result));
    OK(maelys_datalog_session_free(a));OK(maelys_datalog_session_free(b));
    fail_prepare=1;unsigned d=destroys;
    CHECK(maelys_datalog_session_create_configured(policy,0,c,&a)==NO && !a && destroys==d+1);fail_prepare=0;
    maelys_datalog_backend_v6_t wrong=provider;wrong.resource_features|=MAELYS_DATALOG_RESOURCE_CALLER_ALLOCATOR;
    before=queries;CHECK(maelys_datalog_session_config_set_backend_v6(c,&wrong)==NO && queries==before);
    wrong=provider;wrong.resource_features=0;CHECK(maelys_datalog_session_config_set_backend_v6(c,&wrong)==NO);
    OK(maelys_datalog_session_config_free(c));
}
static void defaults(void) {
    maelys_datalog_session_t *a,*b;OK(maelys_datalog_session_create(policy,0,&a));
    maelys_datalog_session_config_t *c;OK(maelys_datalog_session_config_create(&c));
    OK(maelys_datalog_session_config_set_backend_v6(c,maelys_datalog_backend_reference_v6()));
    OK(maelys_datalog_session_create_configured(policy,0,c,&b));char x[65],y[65];
    OK(maelys_datalog_session_execution_fingerprint(a,x));OK(maelys_datalog_session_execution_fingerprint(b,y));CHECK(!strcmp(x,y));
    OK(maelys_datalog_session_free(b));
    maelys_datalog_session_resource_request_t q=MAELYS_DATALOG_RESOURCE_REQUEST_INIT;
    q.capacity_mask=1;q.input_facts=program_bounds.max_input_facts;
    OK(maelys_datalog_session_config_set_resources(c,&q));OK(maelys_datalog_session_create_configured(policy,0,c,&b));
    OK(maelys_datalog_session_execution_fingerprint(b,y));CHECK(!strcmp(x,y));OK(maelys_datalog_session_free(b));
    q.input_facts=1;OK(maelys_datalog_session_config_set_resources(c,&q));
    OK(maelys_datalog_session_create_configured(policy,0,c,&b));OK(maelys_datalog_session_execution_fingerprint(b,y));CHECK(strcmp(x,y));
    OK(maelys_datalog_session_free(a));OK(maelys_datalog_session_free(b));
    OK(maelys_datalog_session_config_set_backend(c,maelys_datalog_backend_reference()));
    CHECK(maelys_datalog_session_create_configured(policy,0,c,&b)==NO && !b);
    q.input_facts=program_bounds.max_input_facts;OK(maelys_datalog_session_config_set_resources(c,&q));
    OK(maelys_datalog_session_create_configured(policy,0,c,&b));OK(maelys_datalog_session_free(b));
    q.required_features=MAELYS_DATALOG_RESOURCE_SESSION_CAPACITIES;OK(maelys_datalog_session_config_set_resources(c,&q));
    CHECK(maelys_datalog_session_create_configured(policy,0,c,&b)==NO && !b);
    OK(maelys_datalog_session_config_set_resources(c,NULL));
    maelys_datalog_session_storage_plan_t p=MAELYS_DATALOG_SESSION_PLAN_INIT;
    CHECK(maelys_datalog_session_storage_requirements_configured(policy,0,c,&p,NULL)==NO);
    OK(maelys_datalog_session_config_free(c));
}
static void quotas(void) {
    maelys_datalog_session_config_t *c=config(2,2,2,5);
    maelys_datalog_session_t *s;OK(maelys_datalog_session_create_configured(policy,0,c,&s));
    maelys_datalog_fact_t f[3]={fact(1),fact(2),fact(1)};maelys_datalog_result_t *res;
    /* The helper and query result each consume D. */
    CHECK(maelys_datalog_session_solve(s,f,2,&res,NULL)==FULL && !res);
    OK(maelys_datalog_session_solve(s,f,1,&res,NULL));size_t n;OK(maelys_datalog_result_derived_fact_count(res,&n));CHECK(n==2);OK(maelys_datalog_result_free(res));
    CHECK(maelys_datalog_session_solve(s,f,3,&res,NULL)==FULL && !res);
    f[0].terms[0]=(maelys_datalog_value_t)MAELYS_DATALOG_SYMBOL("aaaa");OK(maelys_datalog_session_solve(s,f,1,&res,NULL));OK(maelys_datalog_result_free(res));
    f[0].terms[0]=(maelys_datalog_value_t)MAELYS_DATALOG_SYMBOL("aaaaa");CHECK(maelys_datalog_session_solve(s,f,1,&res,NULL)==FULL && !res);
    f[0]=fact(3);OK(maelys_datalog_session_solve(s,f,1,&res,NULL));OK(maelys_datalog_result_free(res));
    OK(maelys_datalog_session_free(s));OK(maelys_datalog_session_config_free(c));
    c=config(0,0,0,0);OK(maelys_datalog_session_create_configured(policy,0,c,&s));
    OK(maelys_datalog_session_solve(s,NULL,0,&res,NULL));OK(maelys_datalog_result_free(res));
    CHECK(maelys_datalog_session_solve(s,f,1,&res,NULL)==FULL);OK(maelys_datalog_session_free(s));OK(maelys_datalog_session_config_free(c));
    c=config(SIZE_MAX,2,2,5);CHECK(maelys_datalog_session_create_configured(policy,0,c,&s)==NO && !s);OK(maelys_datalog_session_config_free(c));
}
static void arenas(void) {
    maelys_datalog_session_config_t *c=config(4,8,32,2048);
    OK(maelys_datalog_session_config_set_explanation_workspace(c,MAELYS_DATALOG_EXPLAIN_TRUE));
    maelys_datalog_session_storage_plan_t p=plan(c), large=plan(NULL);CHECK(p.host_bytes<large.host_bytes && p.explanation_bytes);
    printf("resource plan E4/D8/S32/T2048: host=%zu explanation=%zu padding=%zu arena=%zu; default host=%zu\n",
        p.host_bytes,p.explanation_bytes,p.padding_bytes,p.arena_bytes,large.host_bytes);
    void *memory=malloc(p.arena_bytes);CHECK(memory);maelys_datalog_session_t *s,*other;
    CHECK(maelys_datalog_session_init_configured(memory,p.arena_bytes-1,policy,0,c,&s,NULL)==MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL && !s);
    CHECK(maelys_datalog_session_init_configured((char *)memory+1,p.arena_bytes-1,policy,0,c,&s,NULL)==BAD && !s);
    size_t before=allocations;
    forbidden=1;OK(maelys_datalog_session_init_configured(memory,p.arena_bytes,policy,0,c,&s,NULL));
    CHECK(maelys_datalog_session_init_configured(memory,p.arena_bytes,policy,0,c,
        (maelys_datalog_session_t **)memory,NULL)==BAD);
    CHECK(maelys_datalog_session_init_configured(memory,p.arena_bytes,policy,0,c,&other,NULL)==MAELYS_DATALOG_STATUS_INVALID_STATE && !other);
    maelys_datalog_fact_t f=fact(1);maelys_datalog_result_t *res;
    for(unsigned i=0;i<3;++i) { OK(maelys_datalog_session_solve(s,&f,1,&res,NULL));int found=0;
        OK(maelys_datalog_result_query(res,"seen",f.terms,1,&found));CHECK(found);
        OK(maelys_datalog_result_free(res)); }
    OK(maelys_datalog_session_free(s));forbidden=0;CHECK(allocations==before);
    /* Closed caller storage is reusable; session_free must never free it. */
    OK(maelys_datalog_session_init_configured(memory,p.arena_bytes,policy,0,c,&s,NULL));OK(maelys_datalog_session_free(s));free(memory);
#ifdef RESOURCE_ALLOCATION_TEST
    before=allocations;OK(maelys_datalog_session_create_configured(policy,0,c,&s));CHECK(allocations==before+1);
    size_t before_free=frees;OK(maelys_datalog_session_free(s));CHECK(frees==before_free+1);
#endif
    OK(maelys_datalog_session_config_free(c));
}
static void compiled_facts(void) {
    const char *text="root(7). seen(X) :- root(X).";maelys_datalog_policy_t *p;
    OK(maelys_datalog_policy_load_inline("resources","root",text,strlen(text),&p,NULL));
    maelys_datalog_session_config_t *c=config(0,1,0,0);maelys_datalog_session_t *s;
    maelys_datalog_session_storage_plan_t no_facts=plan(c), with_facts=MAELYS_DATALOG_SESSION_PLAN_INIT;
    OK(maelys_datalog_session_storage_requirements_configured(p,0,c,&with_facts,NULL));
    CHECK(with_facts.host_bytes>no_facts.host_bytes); /* min(B, E + F), not E alone */
    OK(maelys_datalog_session_create_configured(p,0,c,&s));maelys_datalog_result_t *res;
    OK(maelys_datalog_session_solve(s,NULL,0,&res,NULL));size_t n;OK(maelys_datalog_result_derived_fact_count(res,&n));CHECK(n==1);
    OK(maelys_datalog_result_free(res));OK(maelys_datalog_session_free(s));OK(maelys_datalog_session_config_free(c));OK(maelys_datalog_policy_free(p));
}
static void diagnostics(void) {
    maelys_datalog_session_config_t *c=config(2,2,1,3);
    maelys_datalog_session_t *s;OK(maelys_datalog_session_create_configured(policy,0,c,&s));
    maelys_datalog_fact_t f[3]={fact(1),fact(2),fact(3)};maelys_datalog_result_t *r;
    maelys_datalog_diagnostic_t d;OK(maelys_datalog_diagnostic_init(&d,sizeof(d)));
    CHECK(maelys_datalog_session_solve(s,f,3,&r,&d)==FULL);
    CHECK(!strcmp(d.field,"session_input_facts") && d.limit==2 && d.observed_count==3 && !d.limit_kind);
    CHECK(maelys_datalog_session_solve(s,f,2,&r,&d)==FULL);
    CHECK(!strcmp(d.field,"session_derived_facts") && d.limit==2 && d.observed_count==3 && !d.limit_kind);
    f[0].terms[0]=(maelys_datalog_value_t)MAELYS_DATALOG_SYMBOL("a");
    f[1].terms[0]=(maelys_datalog_value_t)MAELYS_DATALOG_SYMBOL("b");
    CHECK(maelys_datalog_session_solve(s,f,2,&r,&d)==FULL);
    CHECK(!strcmp(d.field,"session_symbols") && d.limit==1 && d.observed_count==2 && !d.limit_kind);
    f[0].terms[0]=(maelys_datalog_value_t)MAELYS_DATALOG_SYMBOL("aaa");
    CHECK(maelys_datalog_session_solve(s,f,1,&r,&d)==FULL);
    CHECK(!strcmp(d.field,"session_text_bytes") && d.limit==3 && d.observed_count==4 && !d.limit_kind);
    /* A quota failure must not contaminate the next, unrelated diagnostic. */
    f[0].predicate="missing";CHECK(maelys_datalog_session_solve(s,f,1,&r,&d)!=0);
    CHECK(strcmp(d.field,"session_symbols"));
    f[0]=fact(7);OK(maelys_datalog_session_solve(s,f,1,&r,&d));OK(maelys_datalog_result_free(r));
    OK(maelys_datalog_session_free(s));OK(maelys_datalog_session_config_free(c));
    const char *source="root(\"abc\"). seen(X) :- root(X).";maelys_datalog_policy_t *root;
    const maelys_datalog_predicate_t predicates[]={MAELYS_DATALOG_POLICY_FACT("root",1),MAELYS_DATALOG_IDB_QUERY("seen",1)};
    const char *atoms[]={"abc"};
    const maelys_datalog_domain_t domain={"resource_roots",predicates,2,atoms,1};
    OK(maelys_datalog_domain_register(&domain));
    OK(maelys_datalog_policy_load_inline(domain.name,"roots",source,strlen(source),&root,NULL));
    c=config(0,1,0,4);CHECK(maelys_datalog_session_create_configured(root,0,c,&s)==NO && !s);
    OK(maelys_datalog_session_config_free(c));c=config(0,1,1,3);
    CHECK(maelys_datalog_session_create_configured(root,0,c,&s)==NO && !s);
    OK(maelys_datalog_session_config_free(c));c=config(0,1,1,4);
    OK(maelys_datalog_session_create_configured(root,0,c,&s));OK(maelys_datalog_session_solve(s,NULL,0,&r,&d));
    OK(maelys_datalog_result_free(r));OK(maelys_datalog_session_free(s));
    OK(maelys_datalog_session_config_free(c));OK(maelys_datalog_policy_free(root));
}
static void registration(void) {
    maelys_datalog_context_t *ctx;OK(maelys_datalog_context_create(&ctx));
    maelys_datalog_backend_v6_t v[2]={provider,provider};
    v[1].name="another6";v[1].semantic_id="test.another6.v1";v[1].prepare=NULL;
    maelys_datalog_extension_v2_t e={.abi_version=2,.struct_size=sizeof(e),.name="resources",
        .semantic_id="test.resources.v1",.backends_v6=v,.backend_v6_count=2};
    CHECK(maelys_datalog_context_register_v2(ctx,&e)==BAD);size_t n;
    OK(maelys_datalog_context_backend_v6_count(ctx,&n));CHECK(n==0);
    e.backend_v6_count=1;OK(maelys_datalog_context_register_v2(ctx,&e));
    OK(maelys_datalog_context_backend_v6_count(ctx,&n));CHECK(n==1);
    maelys_datalog_component_info_t info;OK(maelys_datalog_context_backend_v6_info(ctx,0,&info));
    CHECK(!strcmp(info.name,provider.name));
    maelys_datalog_backend_t legacy=*maelys_datalog_backend_reference();
    legacy.name=provider.name;legacy.semantic_id="test.legacy.distinct";
    e.name="conflict";e.semantic_id="test.conflict";e.backend_v6_count=0;e.backend_v5_count=1;e.backends_v5=&legacy;
    CHECK(maelys_datalog_context_register_v2(ctx,&e)==MAELYS_DATALOG_STATUS_INVALID_FIELD);
    OK(maelys_datalog_context_backend_v6_count(ctx,&n));CHECK(n==1);
    OK(maelys_datalog_context_seal(ctx,NULL));
    maelys_datalog_session_config_t *c=config(4,4,8,128);
    CHECK(maelys_datalog_session_config_set_context(c,ctx,provider.name)==NO);
    CHECK(maelys_datalog_session_config_set_context_backend_v6(c,ctx,"reference")==NO);
    CHECK(maelys_datalog_session_config_set_context_backend_v6(c,ctx,"missing")==MAELYS_DATALOG_STATUS_NOT_FOUND);
    OK(maelys_datalog_session_config_set_context_backend_v6(c,ctx,provider.name));
    const char *source="seen(X) :- seed(X).";maelys_datalog_policy_t *p;
    OK(maelys_datalog_context_load_inline(ctx,NULL,"resources","ctx",source,strlen(source),&p,NULL));
    maelys_datalog_session_t *s;OK(maelys_datalog_session_create_configured(p,0,c,&s));
    OK(maelys_datalog_session_config_free(c));OK(maelys_datalog_policy_free(p));OK(maelys_datalog_context_free(ctx));
    maelys_datalog_fact_t f=fact(2);maelys_datalog_result_t *r;
    OK(maelys_datalog_session_solve(s,&f,1,&r,NULL));OK(maelys_datalog_result_free(r));OK(maelys_datalog_session_free(s));
}
static void storage_edges(void) {
    maelys_datalog_session_config_t *c=config(4,8,32,2048);
    OK(maelys_datalog_session_config_set_backend_v6(c,&provider));
    maelys_datalog_session_storage_plan_t p=MAELYS_DATALOG_SESSION_PLAN_INIT, saved=p;
    provider_bytes=SIZE_MAX;
    CHECK(maelys_datalog_session_storage_requirements_configured(policy,0,c,&p,NULL)==BAD);
    CHECK(!memcmp(&p,&saved,sizeof(p)));provider_bytes=sizeof(state_t);
    provider_alignment=3;CHECK(maelys_datalog_session_storage_requirements_configured(policy,0,c,&p,NULL)==BAD);
    provider_alignment=_Alignof(max_align_t);
    state_t *state=malloc(sizeof(*state));CHECK(state);
    maelys_datalog_backend_storage_t external={sizeof(external),state,sizeof(*state),_Alignof(max_align_t)};
    OK(maelys_datalog_session_config_set_backend_storage(c,&external));p=plan(c);
    CHECK(!p.backend_bytes && p.external_backend_bytes==sizeof(*state));
    void *memory=malloc(p.arena_bytes);CHECK(memory);maelys_datalog_session_t *s,*t;
    reentry_arena=memory;reentry_bytes=p.arena_bytes;reentry_config=c;
    OK(maelys_datalog_session_init_configured(memory,p.arena_bytes,policy,0,c,&s,NULL));
    reentry_arena=NULL;reentry_config=NULL;
    void *other=malloc(p.arena_bytes);CHECK(other);unsigned prior=prepares;
    state_t saved_state=*state;
    CHECK(maelys_datalog_session_init_configured(other,p.arena_bytes,policy,0,c,
        (maelys_datalog_session_t **)state,NULL)==BAD);
    CHECK(!memcmp(&saved_state,state,sizeof(*state)));
    CHECK(maelys_datalog_session_init_configured(other,p.arena_bytes,policy,0,c,&t,NULL)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    CHECK(!t && prepares==prior);OK(maelys_datalog_session_free(s));
    external.bytes=other;OK(maelys_datalog_session_config_set_backend_storage(c,&external));
    CHECK(maelys_datalog_session_init_configured(other,p.arena_bytes,policy,0,c,&s,NULL)==BAD && !s);
    free(other);free(memory);free(state);OK(maelys_datalog_session_config_free(c));
    /* Caller-owned policy bytes may be overwritten once the session has its copy. */
    size_t bytes,alignment;OK(maelys_datalog_policy_storage_requirements(&bytes,&alignment));
    void *policy_bytes=malloc(bytes);CHECK(policy_bytes);maelys_datalog_policy_t *local;
    const char *source="seen(X) :- seed(X).";
    OK(maelys_datalog_policy_load_frontend_in(policy_bytes,bytes,"resources","copy",source,strlen(source),NULL,&local,NULL));
    c=config(2,2,2,8);OK(maelys_datalog_session_create_configured(local,0,c,&s));
    OK(maelys_datalog_policy_free(local));memset(policy_bytes,0xa5,bytes);free(policy_bytes);
    maelys_datalog_fact_t f=fact(3);maelys_datalog_result_t *r;
    OK(maelys_datalog_session_solve(s,&f,1,&r,NULL));OK(maelys_datalog_result_free(r));
    OK(maelys_datalog_session_free(s));OK(maelys_datalog_session_config_free(c));
}
static void windows(void) {
    maelys_datalog_session_config_t *c=config(4,8,8,64);
    maelys_datalog_session_t *a,*b;OK(maelys_datalog_session_create_configured(policy,0,c,&a));
    OK(maelys_datalog_session_config_set_resources(c,NULL));
    OK(maelys_datalog_session_create_configured(policy,0,c,&b));
    const maelys_datalog_group_window_capacities_t caps={1,4,4,64};size_t n,align;
    OK(maelys_datalog_group_window_storage_requirements(&caps,&n,&align));
    void *bytes=malloc(n);CHECK(bytes);maelys_datalog_group_window_t *w;
    CHECK(maelys_datalog_group_window_init(bytes,n,&caps,0,a,b,&w,NULL)==BAD && !w);
    OK(maelys_datalog_session_free(b));OK(maelys_datalog_session_config_free(c));c=config(4,8,8,64);
    OK(maelys_datalog_session_create_configured(policy,0,c,&b));
    OK(maelys_datalog_group_window_init(bytes,n,&caps,0,a,b,&w,NULL));
    maelys_datalog_fact_t f=fact(1);OK(maelys_datalog_group_window_push(w,&f,1,NULL,NULL));
    maelys_datalog_result_t *r,*same;OK(maelys_datalog_group_window_result(w,&r));
    size_t eb,ea;OK(maelys_datalog_result_explanation_storage_requirements(r,MAELYS_DATALOG_EXPLAIN_TRUE,&eb,&ea));
    void *ex=malloc(eb);CHECK(ex);maelys_datalog_prepared_explanation_t *lease;
    forbidden=1;
    OK(maelys_datalog_result_prepare_explanation(r,MAELYS_DATALOG_EXPLAIN_TRUE,"seen",f.terms,1,ex,eb,&lease));
    f=fact(2);CHECK(maelys_datalog_group_window_push(w,&f,1,NULL,NULL)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    OK(maelys_datalog_group_window_result(w,&same));CHECK(same==r);
    maelys_datalog_value_t one={.kind=MAELYS_DATALOG_VALUE_INTEGER,.as.integer=1};int found;
    OK(maelys_datalog_result_query(r,"seen",&one,1,&found));CHECK(found);
    OK(maelys_datalog_prepared_explanation_release(lease));
    OK(maelys_datalog_group_window_push(w,&f,1,NULL,NULL));
    OK(maelys_datalog_group_window_result(w,&r));OK(maelys_datalog_result_query(r,"seen",&one,1,&found));CHECK(!found);
    OK(maelys_datalog_group_window_free(w));forbidden=0;
    free(ex);free(bytes);OK(maelys_datalog_session_free(a));OK(maelys_datalog_session_free(b));OK(maelys_datalog_session_config_free(c));
}
int main(int argc,char **argv) {
    const maelys_datalog_predicate_t predicates[]={MAELYS_DATALOG_EDB("seed",1),MAELYS_DATALOG_IDB("aux",1),MAELYS_DATALOG_IDB_QUERY("seen",1),MAELYS_DATALOG_POLICY_FACT("root",1)};
    const maelys_datalog_domain_t domain={"resources",predicates,4,NULL,0};OK(maelys_datalog_domain_register(&domain));
    const char *source="aux(X) :- seed(X). seen(X) :- aux(X).";
    OK(maelys_datalog_policy_load_inline("resources","p",source,strlen(source),&policy,NULL));
    maelys_datalog_session_t *s;const maelys_datalog_program_t *program;
    OK(maelys_datalog_session_create(policy,0,&s));OK(maelys_datalog_session_program(s,&program));
    OK(maelys_datalog_program_info(program,&program_bounds));OK(maelys_datalog_session_free(s));
    const char *which=argc==2?argv[1]:"all";
#define RUN(name) do{if(!strcmp(which,"all")||!strcmp(which,#name)){name();puts(#name " PASS");}}while(0)
    RUN(records);RUN(agreement);RUN(defaults);RUN(quotas);RUN(arenas);RUN(compiled_facts);
    RUN(diagnostics);RUN(registration);RUN(storage_edges);
    RUN(windows);
    OK(maelys_datalog_policy_free(policy));return 0;
}
