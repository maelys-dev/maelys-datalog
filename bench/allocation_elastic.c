/* SPDX-License-Identifier: MPL-2.0 */
/* Public-only complete transactions. Independent output checks and telemetry
 * reads remain outside software-count regions. Caller malloc/free included. */
#include <maelys/datalog_window.h>
#include <maelys/datalog_inputs.h>
#include "allocation_provider.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "allocation_writes.h"
#ifndef ALLOCATION_NATIVE_SMOKE
#include <valgrind/callgrind.h>
#else
#define CALLGRIND_ZERO_STATS ((void)0)
#define CALLGRIND_TOGGLE_COLLECT ((void)0)
#define CALLGRIND_DUMP_STATS_AT(x) ((void)(x))
#endif
#define OK(x) do{int rc_=(x);if(rc_){fprintf(stderr,"%d %s: %d\n",__LINE__,#x,rc_);abort();}}while(0)
#define STEPS 200
static size_t copy_start,move_start,set_start,copied,moved,filled;
static void begin(void){copy_start=allocation_copy_requests;move_start=allocation_move_requests;set_start=allocation_set_requests;CALLGRIND_TOGGLE_COLLECT;}
static void end(void){CALLGRIND_TOGGLE_COLLECT;copied+=allocation_copy_requests-copy_start;moved+=allocation_move_requests-move_start;filled+=allocation_set_requests-set_start;}
static uint64_t mix(uint64_t h,uint64_t x){return (h^x)*UINT64_C(1099511628211);}
typedef struct {size_t calls,releases,live,bytes,fail;} ledger;
static void *get(void *ctx,size_t n,size_t a){ledger *l=ctx;assert(a==_Alignof(max_align_t));if(++l->calls==l->fail)return NULL;void *p=malloc(n);assert(p);++l->live;l->bytes+=n;return p;}
static void put(void *ctx,void *p,size_t n,size_t a){ledger *l=ctx;assert(a==_Alignof(max_align_t) && l->bytes>=n && l->live);++l->releases;--l->live;l->bytes-=n;free(p);}
typedef struct {ledger l;maelys_datalog_session_t *session;maelys_datalog_session_inputs_t *inputs;void *arena,*backend,*input_area;size_t fixed;} session;
static char text[128][16];
static maelys_datalog_fact_t fact(const char *name,size_t i){maelys_datalog_fact_t f={.predicate=name,.arity=2};f.terms[0]=(maelys_datalog_value_t){.kind=MAELYS_DATALOG_VALUE_INTEGER,.as.integer=(int64_t)i};f.terms[1]=(maelys_datalog_value_t){.kind=MAELYS_DATALOG_VALUE_SYMBOL,.as.symbol=text[i]};return f;}
static maelys_datalog_policy_t *policy;
static void open_session(session *s,int abi,size_t cap) {
    memset(s,0,sizeof(*s));maelys_datalog_session_config_t *c;OK(maelys_datalog_session_config_create(&c));
    if(abi==7)OK(maelys_datalog_session_config_set_backend_v7(c,allocation_fixture_transactions()));else OK(maelys_datalog_session_config_set_backend_v6(c,allocation_fixture_snapshot()));
    maelys_datalog_caller_allocator_t caller=MAELYS_DATALOG_CALLER_ALLOCATOR_INIT;caller.context=&s->l;caller.acquire=get;caller.release=put;
    maelys_datalog_session_allocation_request_t q=MAELYS_DATALOG_ALLOCATION_REQUEST_INIT;
    q.base.capacity_mask=MAELYS_DATALOG_CAPACITY_ALL;q.base.input_facts=256;q.base.derived_facts=256;q.base.symbols=256;q.base.text_bytes=8192;q.execution_byte_cap=cap;q.allocator=&caller;
    OK(maelys_datalog_session_config_set_resources(c,&q.base));maelys_datalog_session_storage_plan_t plan=MAELYS_DATALOG_SESSION_PLAN_INIT;
    OK(maelys_datalog_session_storage_requirements_configured(policy,0,c,&plan,NULL));s->backend=malloc(plan.backend_bytes);assert(s->backend);
    maelys_datalog_backend_storage_t b={sizeof(b),s->backend,plan.backend_bytes,_Alignof(max_align_t)};OK(maelys_datalog_session_config_set_backend_storage(c,&b));
    OK(maelys_datalog_session_storage_requirements_configured(policy,0,c,&plan,NULL));s->fixed=plan.total_execution_bytes;s->arena=malloc(plan.arena_bytes);assert(s->arena);
    OK(maelys_datalog_session_init_configured(s->arena,plan.arena_bytes,policy,0,c,&s->session,NULL));OK(maelys_datalog_session_config_free(c));
    if(abi==7){const char *words[128];for(size_t i=0;i<128;++i)words[i]=text[i];maelys_datalog_input_options_t o=MAELYS_DATALOG_INPUT_OPTIONS_INIT;o.fact_capacity=256;o.addition_capacity=0;o.removal_capacity=0;o.symbols=words;o.symbol_count=128;size_t n,a;
        OK(maelys_datalog_session_inputs_storage_requirements(s->session,&o,&n,&a));s->input_area=malloc(n);assert(s->input_area);OK(maelys_datalog_session_inputs_init(s->session,&o,s->input_area,n,&s->inputs));}
}
static maelys_datalog_session_allocation_stats_t stats(session *s){maelys_datalog_session_allocation_stats_t v=MAELYS_DATALOG_ALLOCATION_STATS_INIT;OK(maelys_datalog_session_get_allocation_stats(s->session,&v));assert(v.current_bytes==s->fixed+s->l.bytes);return v;}
static void close_session(session *s){if(s->inputs)OK(maelys_datalog_session_inputs_free(s->inputs));OK(maelys_datalog_session_free(s->session));assert(!s->l.live && !s->l.bytes);free(s->arena);free(s->backend);free(s->input_area);}
static int solve(session *s,const maelys_datalog_fact_t *f,size_t n,maelys_datalog_result_t **r){if(s->inputs){maelys_datalog_input_base_t b;OK(maelys_datalog_session_inputs_base(s->inputs,&b));return maelys_datalog_session_inputs_replace(s->inputs,b,f,n,r,NULL);}return maelys_datalog_session_solve(s->session,f,n,r,NULL);}
static uint64_t verify(maelys_datalog_result_t *r,size_t n,int blocked) {
    size_t total;OK(maelys_datalog_result_derived_fact_count(r,&total));assert(total==(blocked?n:2*n));uint64_t digest=0;
    const char *p[]={"seen","allow"};
    for(size_t j=0;j<2;++j){maelys_datalog_fact_view_t rows[128];size_t count;OK(maelys_datalog_result_enumerate(r,p[j],2,rows,128,&count));assert(count==(j && blocked?0:n));
        for(size_t i=0;i<count;++i){assert(rows[i].terms[0].kind==MAELYS_DATALOG_VALUE_INTEGER && rows[i].terms[0].as.integer==(int64_t)i);assert(rows[i].terms[1].kind==MAELYS_DATALOG_VALUE_SYMBOL);const char *word;size_t bytes;OK(maelys_datalog_result_symbol_text(r,rows[i].terms[1].as.symbol_id,&word,&bytes));assert(bytes==strlen(text[i]) && !memcmp(word,text[i],bytes));digest=mix(digest,i);digest=mix(digest,j);}}
    return digest;
}
static void receipt(const char *region,session *s,size_t ns,size_t window,size_t fixed,size_t before,size_t peak,size_t calls,size_t releases,uint64_t digest) {
    size_t current=window;for(size_t i=0;i<ns;++i)current+=stats(&s[i]).current_bytes;
    CALLGRIND_DUMP_STATS_AT(region);CALLGRIND_ZERO_STATS;
    printf("%s,%u,%" PRIu64 ",%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu\n",region,STEPS,digest,fixed+window,before,current,peak,calls,releases,copied,moved,filled,ns,stats(s).cap_bytes,window+ns*stats(s).cap_bytes);
    copied=moved=filled=0;
}
static void simple(const char *label,int abi,size_t n,int mode) {
    int reject=mode==1;
    session s;open_session(&s,abi,4*1024*1024);maelys_datalog_fact_t f[128];for(size_t i=0;i<128;++i)f[i]=fact("user",i);
    maelys_datalog_result_t *r;size_t initial=mode==2?n/2:n;OK(solve(&s,f,initial,&r));(void)verify(r,initial,0);OK(maelys_datalog_result_free(r));
    size_t before=stats(&s).current_bytes,peak=before,calls=s.l.calls,releases=s.l.releases;uint64_t digest=0;CALLGRIND_ZERO_STATS;
    for(size_t tx=0;tx<STEPS;++tx){size_t used=mode==2 && tx%2?n/2:n;if(reject)s.l.fail=s.l.calls+3;begin();int rc=solve(&s,f,used,&r);end();
        if(reject){assert(rc==MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL && !r);assert(stats(&s).current_bytes==before);digest=mix(digest,(uint64_t)(int64_t)rc);}
        else {OK(rc);digest=mix(digest,verify(r,used,0));begin();OK(maelys_datalog_result_free(r));end();}
        size_t p=stats(&s).operation_peak_bytes;if(p>peak)peak=p;
    }
    char region[160];snprintf(region,sizeof(region),"%s/elastic/session/abi%d/%zu/%s",label,abi,n,reject?"caller-reject":mode==2?"growth":"accepted");
    receipt(region,&s,1,0,s.fixed,before,peak,s.l.calls-calls,s.l.releases-releases,digest);s.l.fail=0;close_session(&s);
}
static void window(const char *label,int abi,size_t n,int grouped) {
    session s[2];for(size_t i=0;i<2;++i)open_session(&s[i],abi,4*1024*1024);
    maelys_datalog_window_options_t o={sizeof(o),n,MAELYS_DATALOG_WINDOW_EXPIRATION};maelys_datalog_group_window_capacities_t c={n,n,2*n,8192};size_t bytes,a;
    if(grouped)OK(maelys_datalog_group_window_storage_requirements_configured(&c,&o,&bytes,&a));else OK(maelys_datalog_window_storage_requirements_configured(n,8192,&o,&bytes,&a));
    void *area=malloc(bytes);assert(area);maelys_datalog_window_t *w=NULL;maelys_datalog_group_window_t *g=NULL;
    if(grouped)OK(maelys_datalog_group_window_init_configured(area,bytes,&c,&o,0,s[0].session,s[1].session,&g,NULL));else OK(maelys_datalog_window_init_configured(area,bytes,n,8192,&o,0,s[0].session,s[1].session,&w,NULL));
    maelys_datalog_fact_t users[128];for(size_t i=0;i<n;++i)users[i]=fact("user",i);
    if(grouped)OK(maelys_datalog_group_window_replace_static(g,users,n,NULL));else OK(maelys_datalog_window_replace_static(w,users,n,NULL));
    for(size_t i=0;i<n;++i){maelys_datalog_fact_t f=fact("event",i);uint32_t id;if(grouped)OK(maelys_datalog_group_window_push_until(g,&f,1,10,&id,NULL));else OK(maelys_datalog_window_push_until(w,"event",f.terms+1,1,10,&id,NULL));assert(id==i);}
    /* Equal committed states in both banks, including the provider's output. */
    for(size_t i=0;i<2;++i){if(grouped)OK(maelys_datalog_group_window_replace_static(g,users,n,NULL));else OK(maelys_datalog_window_replace_static(w,users,n,NULL));}
    maelys_datalog_result_t *r;if(grouped)OK(maelys_datalog_group_window_result(g,&r));else OK(maelys_datalog_window_result(w,&r));(void)verify(r,n,1);
    size_t en;OK(maelys_datalog_result_explanation_storage_requirements(r,MAELYS_DATALOG_EXPLAIN_TRUE,&en,&a));void *ex=malloc(en);assert(ex);maelys_datalog_prepared_explanation_t *lease;
    OK(maelys_datalog_result_prepare_explanation(r,MAELYS_DATALOG_EXPLAIN_TRUE,"seen",users[0].terms,2,ex,en,&lease));
    size_t before=bytes+stats(&s[0]).current_bytes+stats(&s[1]).current_bytes,peak=before,calls=s[0].l.calls+s[1].l.calls,releases=s[0].l.releases+s[1].l.releases;uint64_t digest=0;CALLGRIND_ZERO_STATS;
    for(size_t tx=0;tx<STEPS;++tx){size_t first_calls=s[0].l.calls,second_calls=s[1].l.calls;size_t expired=SIZE_MAX;begin();int rc=grouped?maelys_datalog_group_window_expire(g,10,&expired,NULL):maelys_datalog_window_expire(w,10,&expired,NULL);end();assert(rc==MAELYS_DATALOG_STATUS_INVALID_STATE && expired==SIZE_MAX);digest=mix(digest,verify(r,n,1));
        maelys_datalog_session_allocation_stats_t x=stats(&s[0]),y=stats(&s[1]);assert(bytes+x.current_bytes+y.current_bytes==before);
        /* Only the candidate operates; include the other's current reservation. */
        assert((s[0].l.calls>first_calls) != (s[1].l.calls>second_calls));
        size_t p=bytes+(s[0].l.calls>first_calls?x.operation_peak_bytes+y.current_bytes:x.current_bytes+y.operation_peak_bytes);if(p>peak)peak=p;
    }
    char region[160];snprintf(region,sizeof(region),"%s/elastic/%s/abi%d/%zu/late-reject",label,grouped?"groups":"occurrences",abi,n);
    receipt(region,s,2,bytes,s[0].fixed+s[1].fixed,before,peak,s[0].l.calls+s[1].l.calls-calls,s[0].l.releases+s[1].l.releases-releases,digest);
    OK(maelys_datalog_prepared_explanation_release(lease));free(ex);size_t expired;if(grouped)OK(maelys_datalog_group_window_expire(g,10,&expired,NULL));else OK(maelys_datalog_window_expire(w,10,&expired,NULL));assert(expired==n);
    if(grouped)OK(maelys_datalog_group_window_result(g,&r));else OK(maelys_datalog_window_result(w,&r));(void)verify(r,n,0);
    if(grouped)OK(maelys_datalog_group_window_free(g));else OK(maelys_datalog_window_free(w));free(area);for(size_t i=0;i<2;++i)close_session(&s[i]);
}
int main(int argc,char **argv){if(argc!=2)return 2;for(size_t i=0;i<128;++i)snprintf(text[i],16,"word%03zu",i);
    const maelys_datalog_predicate_t p[]={MAELYS_DATALOG_EDB("user",2),MAELYS_DATALOG_EDB("event",2),MAELYS_DATALOG_IDB_QUERY("seen",2),MAELYS_DATALOG_IDB_QUERY("allow",2)};
    const maelys_datalog_domain_t d={"allocation_elastic",p,4,NULL,0};OK(maelys_datalog_domain_register(&d));const char *source="seen(I,X) :- user(I,X). allow(I,X) :- user(I,X), not(event(I,X)).";OK(maelys_datalog_policy_load_inline(d.name,"probe",source,strlen(source),&policy,NULL));
    puts("case,transactions,digest,fixed_bytes,current_before,current_after,operation_peak,acquire_calls,release_calls,copy_requests,move_requests,set_requests,banks,cap_per_bank,configured_bound");
    for(int abi=6;abi<=7;++abi)for(size_t n=8;n<=64;n*=8){simple(argv[1],abi,n,0);simple(argv[1],abi,n,1);simple(argv[1],abi,n,2);window(argv[1],abi,n,0);window(argv[1],abi,n,1);}
    OK(maelys_datalog_policy_free(policy));return 0;
}
