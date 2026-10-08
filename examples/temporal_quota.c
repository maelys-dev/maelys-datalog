/* SPDX-License-Identifier: MPL-2.0 */
/* Installed-SDK example and storage inventory; no private engine headers. */
#include <maelys/datalog.h>
#include <maelys/datalog_resources.h>
#include <maelys/datalog_window.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OK(x) do { maelys_datalog_status_t rc=(x); if(rc) { \
    fprintf(stderr,"%s: status %d\n",#x,(int)rc); abort(); } } while(0)
static const char source[] =
    "limit(2). used(N) :- counter(N). "
    "used(N) :- history_mode(0), count(I,event(I),N). "
    "deny(0) :- proposed(0), used(N), limit(L), N >= L. "
    "allow(0) :- proposed(0), used(N), not(deny(0)).";
static const maelys_datalog_predicate_t predicates[] = {
    MAELYS_DATALOG_POLICY_FACT("limit",1), MAELYS_DATALOG_EDB("counter",1),
    MAELYS_DATALOG_EDB("event",1), MAELYS_DATALOG_EDB("history_mode",1),
    MAELYS_DATALOG_EDB("proposed",1), MAELYS_DATALOG_IDB_QUERY("used",1),
    MAELYS_DATALOG_IDB_QUERY("allow",1), MAELYS_DATALOG_IDB_QUERY("deny",1)
};
static maelys_datalog_fact_t fact(const char *p,int64_t n) {
    maelys_datalog_fact_t f={0}; f.predicate=p; f.arity=1;
    f.terms[0].kind=MAELYS_DATALOG_VALUE_INTEGER; f.terms[0].as.integer=n; return f;
}
static int query(maelys_datalog_result_t *r,const char *p,int64_t n) {
    maelys_datalog_fact_t f=fact(p,n); int found=0;
    OK(maelys_datalog_result_query(r,p,f.terms,1,&found)); return found;
}
static maelys_datalog_session_config_t *config(size_t E,unsigned explanations) {
    maelys_datalog_session_config_t *c=NULL;
    OK(maelys_datalog_session_config_create(&c));
    maelys_datalog_session_resource_request_t q=MAELYS_DATALOG_RESOURCE_REQUEST_INIT;
    q.capacity_mask=MAELYS_DATALOG_CAPACITY_ALL;
    q.input_facts=E; q.derived_facts=4; q.symbols=32; q.text_bytes=1024;
    OK(maelys_datalog_session_config_set_resources(c,&q));
    OK(maelys_datalog_session_config_set_explanation_workspace(c,explanations));
    return c;
}
/* The application owns this ledger of PERFORMED actions. IDs distinguish
 * identical actions. A real application durably records reservations and
 * completion and serializes expiry/check/reserve/execute/record per quota key.
 * This synchronous fixture only models that serialized section. */
struct entry { uint64_t id, deadline; };
struct ledger { struct entry events[2]; size_t count; uint64_t next; };
static void expire(struct ledger *l,uint64_t now) {
    size_t kept=0;
    for(size_t i=0;i<l->count;i++)
        if(l->events[i].deadline>now) l->events[kept++]=l->events[i];
    l->count=kept;
}
static int attempt(maelys_datalog_session_t *s,struct ledger *l,int succeeds,uint64_t deadline) {
    /* Reserve physical room before any side effect; NEVER discard a live event. */
    if(l->count==2) return 0;
    maelys_datalog_fact_t inputs[]={fact("counter",(int64_t)l->count),fact("proposed",0)};
    maelys_datalog_result_t *r=NULL; OK(maelys_datalog_session_solve(s,inputs,2,&r,NULL));
    int allowed=query(r,"allow",0) && !query(r,"deny",0);
    OK(maelys_datalog_result_free(r));
    if(!allowed || !succeeds) return 0;
    l->events[l->count++]=(struct entry){l->next++,deadline}; return 1;
}
static void safe_example(maelys_datalog_policy_t *p) {
    maelys_datalog_session_t *s=NULL; maelys_datalog_session_config_t *c=config(2,0);
    OK(maelys_datalog_session_create_configured(p,0,c,&s));
    struct ledger l={0};
    assert(!attempt(s,&l,0,100) && l.count==0); /* permitted, action failed */
    assert(attempt(s,&l,1,100) && attempt(s,&l,1,100));
    struct ledger before=l;
    assert(!attempt(s,&l,1,100) && !memcmp(&before,&l,sizeof l));
    expire(&l,99); assert(l.count==2); /* no eviction inside the horizon */
    expire(&l,100); assert(l.count==0 && attempt(s,&l,1,200));
    /* A successful solve can contain deny: OK is not authorization. */
    maelys_datalog_fact_t inputs[]={fact("counter",2),fact("proposed",0)};
    maelys_datalog_result_t *r=NULL; OK(maelys_datalog_session_solve(s,inputs,2,&r,NULL));
    assert(query(r,"deny",0) && !query(r,"allow",0));
    OK(maelys_datalog_result_free(r)); OK(maelys_datalog_session_free(s));
    OK(maelys_datalog_session_config_free(c));
}
static void history_example(maelys_datalog_policy_t *p,size_t N) {
    maelys_datalog_session_config_t *c=config(N+3,0);
    maelys_datalog_session_t *s=NULL;
    OK(maelys_datalog_session_create_configured(p,0,c,&s));
    maelys_datalog_fact_t *inputs=calloc(N+3,sizeof *inputs); assert(inputs);
    inputs[0]=fact("history_mode",0); inputs[1]=fact("proposed",0);
    for(size_t i=0;i<=N;i++) inputs[i+2]=fact("event",(int64_t)i);
    const size_t counts[]={0,1,2,N};
    for(size_t i=0;i<sizeof counts/sizeof *counts;i++) {
        size_t n=counts[i]; maelys_datalog_result_t *r=NULL;
        OK(maelys_datalog_session_solve(s,inputs,n+2,&r,NULL));
        assert(query(r,"used",(int64_t)n));
        assert(query(r,"allow",0)==(n<2) && query(r,"deny",0)==(n>=2));
        OK(maelys_datalog_result_free(r));
        maelys_datalog_fact_t counter[]={fact("counter",(int64_t)n),fact("proposed",0)};
        OK(maelys_datalog_session_solve(s,counter,2,&r,NULL));
        assert(query(r,"used",(int64_t)n));
        assert(query(r,"allow",0)==(n<2) && query(r,"deny",0)==(n>=2));
        OK(maelys_datalog_result_free(r));
    }
    maelys_datalog_result_t *r=NULL;
    assert(maelys_datalog_session_solve(s,inputs,N+3,&r,NULL)!=MAELYS_DATALOG_STATUS_OK && !r);
    OK(maelys_datalog_session_solve(s,inputs,3,&r,NULL)); assert(query(r,"used",1));
    OK(maelys_datalog_result_free(r)); free(inputs);
    OK(maelys_datalog_session_free(s)); OK(maelys_datalog_session_config_free(c));
}
static void fifo_witness(maelys_datalog_policy_t *p) {
    maelys_datalog_session_config_t *c=config(4,0);
    maelys_datalog_session_t *a=NULL,*b=NULL;
    OK(maelys_datalog_session_create_configured(p,0,c,&a));
    OK(maelys_datalog_session_create_configured(p,0,c,&b));
    maelys_datalog_window_options_t o={sizeof o,2,MAELYS_DATALOG_WINDOW_EXPIRATION};
    size_t bytes,alignment; OK(maelys_datalog_window_storage_requirements_configured(2,256,&o,&bytes,&alignment));
    void *storage=malloc(bytes); assert(storage && (uintptr_t)storage%alignment==0);
    maelys_datalog_window_t *w=NULL;
    OK(maelys_datalog_window_init_configured(storage,bytes,2,256,&o,0,a,b,&w,NULL));
    maelys_datalog_fact_t statics[]={fact("history_mode",0),fact("proposed",0)};
    OK(maelys_datalog_window_replace_static(w,statics,2,NULL));
    for(unsigned i=0;i<3;i++) OK(maelys_datalog_window_push_until(w,"event",NULL,0,100,NULL,NULL));
    maelys_datalog_result_t *r=NULL; OK(maelys_datalog_window_result(w,&r));
    assert(query(r,"deny",0) && query(r,"used",2)); /* third push still publishes */
    const maelys_datalog_fact_t *events=NULL; size_t count=0;
    OK(maelys_datalog_window_events(w,&events,&count));
    assert(count==2 && events[0].terms[0].as.integer==1); /* unexpired ID 0 evicted */
    size_t expired=99; OK(maelys_datalog_window_expire(w,99,&expired,NULL)); assert(expired==0);
    OK(maelys_datalog_window_free(w)); free(storage);
    OK(maelys_datalog_session_free(a)); OK(maelys_datalog_session_free(b));
    OK(maelys_datalog_session_config_free(c));
}
static void inventory(maelys_datalog_policy_t *p,size_t N,unsigned ex) {
    const char *names[]={"counter","history","window"};
    for(size_t v=0;v<3;v++) {
        maelys_datalog_session_config_t *c=config(v?N+2:2,ex);
        maelys_datalog_session_storage_plan_t plan=MAELYS_DATALOG_SESSION_PLAN_INIT;
        OK(maelys_datalog_session_storage_requirements_configured(p,0,c,&plan,NULL));
        assert(plan.arena_bytes==plan.host_bytes+plan.backend_bytes+plan.explanation_bytes+plan.padding_bytes);
        assert(plan.total_execution_bytes==plan.arena_bytes+plan.external_backend_bytes+plan.external_explanation_bytes);
        size_t adapter,alignment;
        if(v==2) {
            maelys_datalog_window_options_t o={sizeof o,2,MAELYS_DATALOG_WINDOW_EXPIRATION};
            OK(maelys_datalog_window_storage_requirements_configured(N,256,&o,&adapter,&alignment));
        } else OK(maelys_datalog_input_edb_storage_requirements(v?N+2:2,256,&adapter,&alignment));
        size_t ledger=v==2?0:N*sizeof(struct entry); /* same authoritative event ledger in both injected variants */
        size_t copies=v==2?2:1;
        printf("%s,%zu,%u,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu\n",names[v],N,ex,
            plan.host_bytes,plan.backend_bytes,plan.explanation_bytes,plan.padding_bytes,
            plan.external_backend_bytes,plan.external_explanation_bytes,plan.total_execution_bytes,
            adapter,ledger,copies*plan.total_execution_bytes+adapter+ledger);
        OK(maelys_datalog_session_config_free(c));
    }
}
int main(void) {
    const maelys_datalog_domain_t domain={"temporal_quota",predicates,sizeof predicates/sizeof *predicates,NULL,0};
    OK(maelys_datalog_domain_register(&domain));
    maelys_datalog_policy_t *p=NULL;
    OK(maelys_datalog_policy_load_inline(domain.name,"quota",source,strlen(source),&p,NULL));
    safe_example(p); fifo_witness(p);
    size_t N=0; OK(maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_FACTS_PER_PRED,&N));
    history_example(p,N);
    size_t bytes,alignment; OK(maelys_datalog_policy_storage_requirements(&bytes,&alignment));
    fprintf(stderr,"checks PASS; policy storage bound=%zu alignment=%zu; per-predicate bound=%zu\n",bytes,alignment,N);
    puts("variant,N,explanation_mask,host,backend,explanation,padding,external_backend,external_explanation,session_total,adapter,ledger,total");
    inventory(p,N,0); inventory(p,N,MAELYS_DATALOG_EXPLAIN_TRUE|MAELYS_DATALOG_EXPLAIN_FALSE);
    OK(maelys_datalog_policy_free(p)); return 0;
}
