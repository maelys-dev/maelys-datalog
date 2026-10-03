/* SPDX-License-Identifier: MPL-2.0 */
/* Installed-public-SDK consumer, shared byte for byte by base and candidate.
 * Memory mode holds 64 solved sessions; count mode excludes fixture setup.
 * Driver/status-check instructions are retained as an explicit attribution residual.
 * No latency claim is made by this probe. */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#include <maelys/datalog.h>
#include <maelys/datalog_resources.h>
#include <maelys/datalog_window.h>
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __APPLE__
#include <malloc/malloc.h>
#endif
#ifdef MAELYS_BENCH_COUNT
#include <valgrind/callgrind.h>
/* Isolate repeated regions from caller-loop unrolling and delayed attribution
 * of adjacent driver blocks. Dump while instrumentation is still enabled. */
__attribute__((noinline)) static void compact_collect_start(void) {
    CALLGRIND_ZERO_STATS;
    CALLGRIND_START_INSTRUMENTATION;
}
__attribute__((noinline)) static void compact_collect_stop(void) {
    CALLGRIND_DUMP_STATS_AT("compact");
    CALLGRIND_STOP_INSTRUMENTATION;
}
#define START() compact_collect_start()
#define STOP() compact_collect_stop()
#else
#define START() ((void)0)
#define STOP() ((void)0)
#endif
#define OK(call) do { maelys_datalog_status_t rc=(call); if(rc) {fprintf(stderr,"%s: %d\n",#call,rc); abort();} } while(0)
static maelys_datalog_value_t symbol(const char *s) {
    maelys_datalog_value_t v={.kind=MAELYS_DATALOG_VALUE_SYMBOL};v.as.symbol=s;return v;
}
static maelys_datalog_fact_t fact(const char *p,const char *a,const char *b) {
    maelys_datalog_fact_t f={.predicate=p,.arity=b?2:1};f.terms[0]=symbol(a);
    if(b) f.terms[1]=symbol(b);return f;
}
static const maelys_datalog_predicate_t predicates[]={
    {"user",1,MAELYS_DATALOG_PREDICATE_EDB}, {"owns",2,MAELYS_DATALOG_PREDICATE_EDB},
    {"delegated",2,MAELYS_DATALOG_PREDICATE_EDB}, {"blocked",1,MAELYS_DATALOG_PREDICATE_EDB},
    {"can_read",2,MAELYS_DATALOG_PREDICATE_IDB},
    {"has_any_document",1,MAELYS_DATALOG_PREDICATE_IDB|MAELYS_DATALOG_PREDICATE_QUERY},
    {"allow",2,MAELYS_DATALOG_PREDICATE_IDB|MAELYS_DATALOG_PREDICATE_QUERY},
    {"event",2,MAELYS_DATALOG_PREDICATE_EDB},
    {"seen",1,MAELYS_DATALOG_PREDICATE_IDB|MAELYS_DATALOG_PREDICATE_QUERY}
};
static const maelys_datalog_domain_t domain={"compact_probe",predicates,9,NULL,0};
static const char source[]="can_read(User, Doc) :- owns(User, Doc) or delegated(User, Doc), not(blocked(User)).\n"
    "has_any_document(User) :- owns(User, _).\nallow(User, Doc) :- user(User), can_read(User, Doc).\n"
    "seen(X) :- event(_, X).\n";
static maelys_datalog_policy_t *policy;
static maelys_datalog_input_edb_t *edb;
static char users[30][16],docs[30][16],events[65][16];
static void setup(void) {
    assert(maelys_datalog_domain_register(&domain)==0);
    OK(maelys_datalog_policy_load_inline(domain.name,"compact",source,sizeof(source)-1,&policy,NULL));
    maelys_datalog_fact_t facts[93];size_t n=0;
    for(unsigned i=0;i<30;++i) {snprintf(users[i],16,"user%u",i);snprintf(docs[i],16,"doc%u.pdf",i);}
    for(unsigned i=0;i<30;++i) {
        facts[n++]=fact("user",users[i],NULL);facts[n++]=fact("owns",users[i],docs[i]);
        facts[n++]=fact("delegated",users[i],docs[(i+1)%30]);
        if(!(i%10)) facts[n++]=fact("blocked",users[i],NULL);
    }
    assert(n==93);OK(maelys_datalog_input_edb_create_with_capacity(93,4096,&edb));
    OK(maelys_datalog_input_edb_add_facts(edb,facts,n,NULL));
    for(unsigned i=0;i<65;++i) snprintf(events[i],16,"event%u",i);
}
static void check(maelys_datalog_result_t *r) {
    maelys_datalog_value_t terms[]={symbol(users[1]),symbol(docs[1])};int present=0;
    OK(maelys_datalog_result_query(r,"allow",terms,2,&present));assert(present);
    terms[0]=symbol(users[10]);terms[1]=symbol(docs[10]);
    OK(maelys_datalog_result_query(r,"allow",terms,2,&present));assert(!present);
}
static void plans(void) {
    size_t bytes,align;
    OK(maelys_datalog_policy_storage_requirements(&bytes,&align));
    printf("policy,%zu,%zu\n",bytes,align);
    maelys_datalog_session_storage_plan_t plan=MAELYS_DATALOG_SESSION_PLAN_INIT;
    OK(maelys_datalog_session_storage_requirements_configured(policy,0,NULL,&plan,NULL));
    printf("plan,arena=%zu,host=%zu,backend=%zu,explanation=%zu,padding=%zu,total=%zu\n",
        plan.arena_bytes,plan.host_bytes,plan.backend_bytes,plan.explanation_bytes,plan.padding_bytes,plan.total_execution_bytes);
    /* A caller queries the candidate requirements and initializes exactly that
     * size. A short arena must fail; the queried plan is never an old constant. */
    void *arena=NULL;assert(posix_memalign(&arena,plan.arena_alignment,plan.arena_bytes)==0);
    maelys_datalog_session_t *s=NULL;
    assert(maelys_datalog_session_init_configured(arena,plan.arena_bytes-1,policy,0,NULL,&s,NULL)==MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL && !s);
    OK(maelys_datalog_session_init_configured(arena,plan.arena_bytes,policy,0,NULL,&s,NULL));
    maelys_datalog_result_t *r=NULL;OK(maelys_datalog_session_solve_edb(s,edb,&r,NULL));check(r);
    size_t eb,ea;OK(maelys_datalog_result_explanation_storage_requirements(r,MAELYS_DATALOG_EXPLAIN_TRUE,&eb,&ea));
    printf("explain_true,%zu,%zu\n",eb,ea);
    OK(maelys_datalog_result_explanation_storage_requirements(r,MAELYS_DATALOG_EXPLAIN_FALSE,&eb,&ea));
    printf("explain_false,%zu,%zu\n",eb,ea);
    OK(maelys_datalog_result_free(r));OK(maelys_datalog_session_free(s));free(arena);
    OK(maelys_datalog_window_storage_requirements(64,4096,&bytes,&align));printf("last_n,%zu,%zu\n",bytes,align);
    const maelys_datalog_group_window_capacities_t caps={64,64,64,4096};
    OK(maelys_datalog_group_window_storage_requirements(&caps,&bytes,&align));printf("groups,%zu,%zu\n",bytes,align);
}
static void memory(void) {
    maelys_datalog_session_t *sessions[64]={0};
#ifdef __APPLE__
    assert(getenv("MallocScribble") && !strcmp(getenv("MallocScribble"),"1"));
    malloc_statistics_t before,after;malloc_zone_statistics(NULL,&before);
#endif
    for(size_t i=0;i<64;++i) {
        OK(maelys_datalog_session_create(policy,0,&sessions[i]));
        maelys_datalog_result_t *r=NULL;OK(maelys_datalog_session_solve_edb(sessions[i],edb,&r,NULL));check(r);OK(maelys_datalog_result_free(r));
    }
#ifdef __APPLE__
    malloc_zone_statistics(NULL,&after);
    printf("live64,heap_in_use_delta=%zu,heap_allocated_delta=%zu,per_session_heap=%zu\n",
        after.size_in_use-before.size_in_use,after.size_allocated-before.size_allocated,
        (after.size_in_use-before.size_in_use)/64);
#else
    printf("live64,allocator_telemetry=unavailable\n");
#endif
    for(size_t i=0;i<64;++i) OK(maelys_datalog_session_free(sessions[i]));
}
__attribute__((noinline)) static void ordinary_region(void) {
        START();
        for(unsigned i=0;i<16;++i) {
            maelys_datalog_session_t *s=NULL;maelys_datalog_result_t *r=NULL;
            OK(maelys_datalog_session_create(policy,0,&s));
            OK(maelys_datalog_session_solve_edb(s,edb,&r,NULL));
            /* The query and status checks are part of this complete-request driver. */
            maelys_datalog_value_t q[]={symbol(users[1]),symbol(docs[1])};int present=0;
            OK(maelys_datalog_result_query(r,"allow",q,2,&present));assert(present);
            OK(maelys_datalog_result_free(r));OK(maelys_datalog_session_free(s));
        }
        STOP();
}
static void ordinary(void) {
    maelys_datalog_session_t *warm=NULL;maelys_datalog_result_t *wr=NULL;
    OK(maelys_datalog_session_create(policy,0,&warm));
    OK(maelys_datalog_session_solve_edb(warm,edb,&wr,NULL));check(wr);
    OK(maelys_datalog_result_free(wr));OK(maelys_datalog_session_free(warm));
    for(unsigned repeat=0;repeat<2;++repeat) {
        ordinary_region();
        printf("checked ordinary93 repeat=%u transactions=16\n",repeat);
    }
}

__attribute__((noinline)) static void window_region(maelys_datalog_window_t *w) {
    START();
    for(unsigned i=0;i<130;++i) {maelys_datalog_value_t v=symbol(events[i%65]);OK(maelys_datalog_window_push(w,"event",&v,1,NULL,NULL));}
    STOP();
}
static void window(void) {
    for(unsigned repeat=0;repeat<2;++repeat) {
        maelys_datalog_session_t *a=NULL,*b=NULL;OK(maelys_datalog_session_create(policy,0,&a));OK(maelys_datalog_session_create(policy,0,&b));
        size_t bytes,align;OK(maelys_datalog_window_storage_requirements(64,4096,&bytes,&align));
        void *arena=NULL;assert(posix_memalign(&arena,align,bytes)==0);maelys_datalog_window_t *w=NULL;
        OK(maelys_datalog_window_init(arena,bytes,64,4096,0,a,b,&w,NULL));
        /* Each repetition starts from the same primed banks/cursor. All work
         * of each measured push, including expiry and result release, counts. */
        for(unsigned i=0;i<130;++i) {maelys_datalog_value_t v=symbol(events[i%65]);OK(maelys_datalog_window_push(w,"event",&v,1,NULL,NULL));}
        window_region(w);
        maelys_datalog_result_t *r=NULL;OK(maelys_datalog_window_result(w,&r));size_t n=0;OK(maelys_datalog_result_derived_fact_count(r,&n));assert(n==64);
        printf("checked last_n64 repeat=%u transactions=130 derived=%zu\n",repeat,n);
        OK(maelys_datalog_window_free(w));free(arena);OK(maelys_datalog_session_free(a));OK(maelys_datalog_session_free(b));
    }
}

int main(int argc,char **argv) {
    if(argc!=2) return 2;setup();
    if(!strcmp(argv[1],"memory")) {plans();memory();}
    else if(!strcmp(argv[1],"ordinary")) ordinary();
    else if(!strcmp(argv[1],"window")) window();
    else return 2;
    OK(maelys_datalog_input_edb_free(edb));OK(maelys_datalog_policy_free(policy));return 0;
}
