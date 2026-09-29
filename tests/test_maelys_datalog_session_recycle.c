/* SPDX-License-Identifier: MPL-2.0 */
#include "tests/fixtures/allocation_guard.h"
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef memset
#include "maelys/datalog.h"
#include "maelys/datalog_backend.h"
#include "maelys/datalog_resources.h"
#include "src/runtime/maelys_datalog_recycle_internal.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static atomic_size_t allocations, releases, live, requested, clears;
static atomic_int forbidden;
void *maelys_test_malloc(size_t n) {
    ++allocations;
    if (forbidden) return NULL;
    void *p = malloc(n);
    if (p) { ++live; requested += n; memset(p, 0xa5, n); }
    return p;
}
void *maelys_test_calloc(size_t n, size_t w) {
    void *p = maelys_test_malloc(n*w);
    if (p) memset(p, 0, n*w);
    return p;
}
void *maelys_test_realloc(void *p, size_t n) {
    (void)p; (void)n; assert(!"session recycling never reallocates"); return NULL;
}
void maelys_test_free(void *p) {
    if (p) { assert(live); --live; ++releases; free(p); }
}
void *maelys_test_memset(void *p, int v, size_t n) {
    clears += n; return memset(p, v, n);
}
#define OK(call) assert((call) == MAELYS_DATALOG_STATUS_OK)
static maelys_datalog_policy_t *policy, *other_policy;
static void solve(maelys_datalog_session_t *s, int value) {
    maelys_datalog_fact_t f = {.predicate="seed", .arity=1};
    f.terms[0].kind = MAELYS_DATALOG_VALUE_INTEGER; f.terms[0].as.integer = value;
    maelys_datalog_result_t *r = NULL;
    OK(maelys_datalog_session_solve(s, &f, 1, &r, NULL));
    int yes = 0; OK(maelys_datalog_result_query(r,"seen",f.terms,1,&yes)); assert(yes);
    assert(maelys_datalog_session_free(s) == MAELYS_DATALOG_STATUS_INVALID_STATE);
    OK(maelys_datalog_result_free(r));
    OK(maelys_datalog_session_solve(s,NULL,0,&r,NULL));
    yes = 1; OK(maelys_datalog_result_query(r,"seen",f.terms,1,&yes)); assert(!yes);
    OK(maelys_datalog_result_free(r));
}
static void warm_and_live(void) {
    maelys_datalog_session_recycle_purge();
    size_t base = live, a = allocations, bytes = requested;
    maelys_datalog_session_t *s, *t;
    OK(maelys_datalog_session_create(policy,0,&s)); assert(allocations == a+1);
    printf("cold borrowed arena=%zu bytes; retained bound=%zu bytes\n",
        (size_t)requested-bytes,maelys_datalog_session_recycle_bound());
    solve(s,7); size_t z=clears, f=releases;
    OK(maelys_datalog_session_free(s));
    assert(clears == z && releases == f && live == base+1);
    /* Reinitialize from a different program with the same allocation size.
     * A cache hit must not depend on the heap, retain state, or retain policy. */
    a=allocations; forbidden=1;
    OK(maelys_datalog_session_create(other_policy,0,&s));
    solve(s,19); OK(maelys_datalog_session_free(s));
    OK(maelys_datalog_session_create(policy,0,&s)); solve(s,23);
    assert(allocations == a);
    t=(void *)1;
    assert(maelys_datalog_session_create(policy,0,&t) == MAELYS_DATALOG_STATUS_INTERNAL && !t);
    forbidden=0;
    /* Two simultaneously live sessions never share storage. One close cannot
     * make a still-leased sibling recyclable. */
    OK(maelys_datalog_session_create(policy,0,&t)); assert(s != t);
    solve(t,29); OK(maelys_datalog_session_free(t)); solve(s,31);
    OK(maelys_datalog_session_free(s)); assert(live == base+1);
    maelys_datalog_session_recycle_purge(); assert(live == base);
}
static void sized_and_caller(void) {
    maelys_datalog_session_config_t *c;
    OK(maelys_datalog_session_config_create(&c));
    maelys_datalog_session_resource_request_t q = MAELYS_DATALOG_RESOURCE_REQUEST_INIT;
    q.capacity_mask = MAELYS_DATALOG_CAPACITY_INPUT_FACTS | MAELYS_DATALOG_CAPACITY_DERIVED_FACTS;
    q.input_facts=2; q.derived_facts=2;
    OK(maelys_datalog_session_config_set_resources(c,&q));
    maelys_datalog_session_storage_plan_t p=MAELYS_DATALOG_SESSION_PLAN_INIT;
    OK(maelys_datalog_session_storage_requirements_configured(policy,0,c,&p,NULL));
    void *arena=malloc(p.arena_bytes); assert(arena);
    size_t base=live,a=allocations;
    maelys_datalog_session_t *s,*t;
    forbidden=1;
    OK(maelys_datalog_session_init_configured(arena,p.arena_bytes,policy,0,c,&s,NULL));
    solve(s,1); OK(maelys_datalog_session_free(s));
    assert(allocations == a && live == base);
    /* A caller arena must not have filled the idle slot. */
    t=(void *)1;
    assert(maelys_datalog_session_create_configured(policy,0,c,&t) == MAELYS_DATALOG_STATUS_INTERNAL && !t);
    forbidden=0; free(arena);
    OK(maelys_datalog_session_create_configured(policy,0,c,&s)); solve(s,2);
    OK(maelys_datalog_session_free(s)); a=allocations; forbidden=1;
    OK(maelys_datalog_session_create_configured(policy,0,c,&s)); solve(s,3);
    assert(allocations == a); OK(maelys_datalog_session_free(s));
    /* The default arena is larger: equal-size reuse cannot accept this block. */
    assert(maelys_datalog_session_create(policy,0,&t) == MAELYS_DATALOG_STATUS_INTERNAL && !t);
    forbidden=0; maelys_datalog_session_recycle_purge(); assert(live == base);
    OK(maelys_datalog_session_config_free(c));
}
static void cross_constructor(void) {
    maelys_datalog_session_config_t *c; OK(maelys_datalog_session_config_create(&c));
    maelys_datalog_session_resource_request_t q=MAELYS_DATALOG_RESOURCE_REQUEST_INIT;
    OK(maelys_datalog_session_config_set_resources(c,&q));
    size_t base=live; maelys_datalog_session_t *s;
    OK(maelys_datalog_session_create(policy,0,&s));
    char before[65],after[65]; OK(maelys_datalog_session_execution_fingerprint(s,before));
    OK(maelys_datalog_session_free(s));
    size_t a=allocations; forbidden=1;
    OK(maelys_datalog_session_create_configured(policy,0,c,&s)); solve(s,11);
    OK(maelys_datalog_session_execution_fingerprint(s,after));assert(!strcmp(before,after));
    OK(maelys_datalog_session_free(s));
    OK(maelys_datalog_session_create(policy,0,&s)); solve(s,13);
    OK(maelys_datalog_session_free(s)); assert(allocations==a);
    forbidden=0; maelys_datalog_session_recycle_purge();assert(live==base);
    OK(maelys_datalog_session_config_free(c));
}
static maelys_datalog_status_t reject_prepare(const maelys_datalog_program_t *p,
    const maelys_datalog_backend_storage_t *storage, void **out) {
    (void)p; (void)storage; *out=NULL; return MAELYS_DATALOG_STATUS_INVALID_FIELD;
}
static void failures(void) {
    size_t base=live;
    maelys_datalog_backend_t b=*maelys_datalog_backend_reference(); b.prepare=reject_prepare;
    maelys_datalog_session_options_t o={.abi_version=MAELYS_DATALOG_BACKEND_ABI_VERSION,
        .struct_size=sizeof(o),.backend=&b};
    maelys_datalog_session_t *s;
    /* Fail both after a cache hit and after a fresh allocation. No failed
     * preparation or borrowed program/module reference may remain cached. */
    for (unsigned cached=0;cached<2;++cached) {
        if (cached) { OK(maelys_datalog_session_create(policy,0,&s)); OK(maelys_datalog_session_free(s)); }
        s=(void *)1;
        assert(maelys_datalog_session_create_ex(policy,0,&o,&s)==MAELYS_DATALOG_STATUS_INVALID_FIELD && !s);
        assert(live==base);
    }
}
static maelys_datalog_status_t large_requirement(const maelys_datalog_program_t *p,
    const maelys_datalog_session_resources_t *r,size_t *n,size_t *a) {
    (void)p; (void)r; *n=maelys_datalog_session_recycle_bound()+1; *a=1; return 0;
}
static void oversized_provider(void) {
    maelys_datalog_session_config_t *c; OK(maelys_datalog_session_config_create(&c));
    maelys_datalog_backend_v6_t b=*maelys_datalog_backend_reference_v6();
    b.storage_requirements=large_requirement;
    OK(maelys_datalog_session_config_set_backend_v6(c,&b));
    size_t base=live; maelys_datalog_session_t *s;
    OK(maelys_datalog_session_create_configured(policy,0,c,&s)); solve(s,43);
    OK(maelys_datalog_session_free(s)); assert(live==base);
    forbidden=1;
    assert(maelys_datalog_session_create_configured(policy,0,c,&s)==MAELYS_DATALOG_STATUS_INTERNAL && !s);
    forbidden=0; OK(maelys_datalog_session_config_free(c));
}
static unsigned destroys;
static void reenter_destroy(void *state) {
    ++destroys;
    maelys_datalog_backend_reference()->destroy(state);
    maelys_datalog_session_t *s;
    OK(maelys_datalog_session_create(other_policy,0,&s)); solve(s,41);
    OK(maelys_datalog_session_free(s));
}
static void reentrancy(void) {
    size_t base=live;
    maelys_datalog_backend_t b=*maelys_datalog_backend_reference(); b.destroy=reenter_destroy;
    maelys_datalog_session_options_t o={.abi_version=MAELYS_DATALOG_BACKEND_ABI_VERSION,
        .struct_size=sizeof(o),.backend=&b};
    maelys_datalog_session_t *s;
    OK(maelys_datalog_session_create_ex(policy,0,&o,&s)); solve(s,37);
    OK(maelys_datalog_session_free(s)); assert(live==base+1 && destroys==1);
    maelys_datalog_session_recycle_purge(); assert(live==base);
}
static void *worker(void *arg) {
    (void)arg;
    for (unsigned i=0;i<50;++i) {
        maelys_datalog_session_t *s;
        OK(maelys_datalog_session_create(i%2?policy:other_policy,0,&s));
        solve(s,(int)i); OK(maelys_datalog_session_free(s));
        if (i%11==0) maelys_datalog_session_recycle_purge();
    }
    return NULL;
}
static void concurrency(void) {
    size_t base=live;
    pthread_t t[4];
    for (unsigned i=0;i<4;++i) assert(!pthread_create(&t[i],NULL,worker,NULL));
    for (unsigned i=0;i<4;++i) assert(!pthread_join(t[i],NULL));
    assert(live<=base+1); maelys_datalog_session_recycle_purge(); assert(live==base);
}
int main(int argc,char **argv) {
    const maelys_datalog_predicate_t predicates[] = {
        {"seed",1,MAELYS_DATALOG_PREDICATE_EDB},
        {"seen",1,MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY}};
    const maelys_datalog_domain_t domain = {"recycle",predicates,2,NULL,0};
    OK(maelys_datalog_domain_register(&domain));
    const char *a="seen(X) :- seed(X).", *b="seen(X) :- seed(X), X >= 0.";
    OK(maelys_datalog_policy_load_inline("recycle","first",a,strlen(a),&policy,NULL));
    OK(maelys_datalog_policy_load_inline("recycle","other",b,strlen(b),&other_policy,NULL));
    const char *which=argc==2?argv[1]:"all";
#define RUN(name) do { if(!strcmp(which,"all") || !strcmp(which,#name)) {name();puts(#name " PASS");} } while(0)
    RUN(warm_and_live); RUN(sized_and_caller); RUN(cross_constructor); RUN(failures); RUN(oversized_provider); RUN(reentrancy); RUN(concurrency);
    OK(maelys_datalog_policy_free(policy)); OK(maelys_datalog_policy_free(other_policy));
    assert(!live);
    puts("session recycling: cold/warm, exact size, caller storage, leases, failures, reentry, concurrency PASS");
    return 0;
}
