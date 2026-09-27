/* SPDX-License-Identifier: MPL-2.0 */
#include "tests/fixtures/allocation_guard.h"
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef memset
#include <maelys/datalog_window.h>
#include <assert.h>
#define OPTIONS(n) (&(maelys_datalog_window_options_t){sizeof(maelys_datalog_window_options_t),(n),0})
#include <stdio.h>
#include <string.h>

static int forbidden;
static size_t calls, frees, live;
void *maelys_test_malloc(size_t n) {
    if (forbidden) { ++calls; return NULL; }
    void *p=malloc(n); if(p)++live; return p;
}
void *maelys_test_calloc(size_t n,size_t s) {
    if (forbidden) { ++calls; return NULL; }
    void *p=calloc(n,s); if(p)++live; return p;
}
void *maelys_test_realloc(void *p,size_t n) {
    if (forbidden) { ++calls; return NULL; }
    int had=p!=NULL; void *q=realloc(p,n);
    if(q && !had)++live;
    if(!q && had && !n)--live;
    return q;
}
void maelys_test_free(void *p) {
    if (forbidden)++frees;
    if(p) { assert(live); --live; } free(p);
}
void *maelys_test_memset(void *p,int v,size_t n) { return memset(p,v,n); }

/* White-box only for persistent-byte snapshots and a non-lease release fault. */
static maelys_datalog_result_t *release_failure_target;
static maelys_datalog_status_t injected_result_free(maelys_datalog_result_t *r) {
    return r==release_failure_target ? MAELYS_DATALOG_STATUS_INVALID_ARGUMENT : maelys_datalog_result_free(r);
}
#define maelys_datalog_result_free injected_result_free
#define malloc maelys_test_malloc
#define calloc maelys_test_calloc
#define realloc maelys_test_realloc
#define free maelys_test_free
#include "src/runtime/maelys_datalog_group_window.c"
#undef maelys_datalog_result_free
#undef malloc
#undef calloc
#undef realloc
#undef free
static maelys_datalog_value_t integer(int64_t x) {
    maelys_datalog_value_t v={.kind=MAELYS_DATALOG_VALUE_INTEGER};v.as.integer=x;return v;
}
static maelys_datalog_fact_t reading(int64_t id,int64_t x) {
    maelys_datalog_fact_t f={.predicate="reading",.arity=2};f.terms[0]=integer(id);f.terms[1]=integer(x);return f;
}
static void rejected_unchanged(maelys_datalog_group_window_t *w,
    const maelys_datalog_fact_t *facts,size_t n,int expected) {
    group_layout layout; assert(!group_storage_layout(&w->capacities,w->static_capacity,&layout));
    unsigned char *bank=malloc(layout.stride); assert(bank);
    unsigned char header[sizeof(*w)]; memcpy(header,w,sizeof(*w));
    unsigned char *committed=(unsigned char *)w+layout.start+w->active*layout.stride;
    memcpy(bank,committed,layout.stride);
    uint32_t id=UINT32_MAX; maelys_datalog_diagnostic_t d = MAELYS_DATALOG_DIAGNOSTIC_INIT;
    assert(maelys_datalog_group_window_push(w,facts,n,&id,&d)==expected);
    assert(id==UINT32_MAX && !memcmp(header,w,sizeof(*w)) && !memcmp(bank,committed,layout.stride));
    free(bank);
}
static void rejected_static(maelys_datalog_group_window_t *w, const maelys_datalog_fact_t *f,size_t n,int expected) {
    group_layout l; assert(!group_storage_layout(&w->capacities,w->static_capacity,&l));
    size_t bytes=l.stride; unsigned char *committed=(unsigned char *)w+l.start+w->active*l.stride;
    unsigned char *bank=malloc(bytes);assert(bank);memcpy(bank,committed,bytes);
    unsigned char header[sizeof(*w)];memcpy(header,w,sizeof(*w));
    assert(maelys_datalog_group_window_replace_static(w,f,n,NULL)==expected);
    assert(!memcmp(header,w,sizeof(*w))&&!memcmp(bank,committed,bytes));free(bank);
}
int main(void) {
    const maelys_datalog_predicate_t preds[]={
        {"reading",2,MAELYS_DATALOG_PREDICATE_EDB},
        {"total",1,MAELYS_DATALOG_PREDICATE_IDB|MAELYS_DATALOG_PREDICATE_QUERY}};
    const maelys_datalog_domain_t d={"group_alloc",preds,2,NULL,0};
    assert(!maelys_datalog_domain_register(&d));
    maelys_datalog_policy_t *p=NULL;
    const char *source="total(N) :- sum(V,reading(_,V),N).";
    assert(!maelys_datalog_policy_load_inline("group_alloc","group.alloc",source,strlen(source),&p,NULL));
    maelys_datalog_session_config_t *config=NULL;
    assert(!maelys_datalog_session_config_create(&config));
    assert(!maelys_datalog_session_config_set_explanation_workspace(config,MAELYS_DATALOG_EXPLAIN_TRUE|MAELYS_DATALOG_EXPLAIN_FALSE));
    maelys_datalog_session_t *a=NULL,*b=NULL;
    assert(!maelys_datalog_session_create_configured(p,0,config,&a));
    assert(!maelys_datalog_session_create_configured(p,0,config,&b));
    assert(!maelys_datalog_policy_free(p)); assert(!maelys_datalog_session_config_free(config));
    maelys_datalog_group_window_capacities_t caps={2,4,2,16};
    size_t bytes,alignment,explain_bytes,explain_alignment;
    assert(!maelys_datalog_group_window_storage_requirements_configured(&caps, OPTIONS(2),&bytes,&alignment));
    void *storage=malloc(bytes); assert(storage && (uintptr_t)storage%alignment==0); memset(storage,0xa5,bytes);
    assert(!maelys_datalog_session_explanation_storage_bound(a,MAELYS_DATALOG_EXPLAIN_FALSE,&explain_bytes,&explain_alignment));
    void *workspace=malloc(explain_bytes); assert(workspace);
    size_t before=live; forbidden=1; maelys_datalog_group_window_t *w=NULL;
    assert(!maelys_datalog_group_window_init_configured(storage,bytes,&caps, OPTIONS(2),0,a,b,&w,NULL));
    for(uint32_t i=0;i<100;++i) {
        maelys_datalog_fact_t batch[]={reading(i,1),reading(i,1)}; uint32_t id=UINT32_MAX;
        assert(!maelys_datalog_group_window_push(w,batch,2,&id,NULL) && id==i);
        maelys_datalog_group_window_usage_t usage;
        assert(!maelys_datalog_group_window_state(w,&usage) && usage.text_bytes==8);
        maelys_datalog_fact_t bad=reading(i+1,INT32_MAX);
        rejected_unchanged(w,&bad,1,MAELYS_DATALOG_STATUS_INVALID_FIELD);
        bad.predicate="too_long_for_text_storage";
        rejected_unchanged(w,&bad,1,MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
        rejected_unchanged(w,batch,5,MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
        maelys_datalog_fact_t unique[]={reading(i+1,1),reading(i+2,1)};
        rejected_unchanged(w,unique,2,MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
        maelys_datalog_result_t *r=NULL; assert(!maelys_datalog_group_window_result(w,&r));
        maelys_datalog_value_t v=integer(i?2:1); int found=-1;
        assert(!maelys_datalog_result_query(r,"total",&v,1,&found) && found);
        size_t required; char text[8192];
        assert(!maelys_datalog_result_explain_true_text(r,"total",&v,1,text,sizeof(text),&required));
        v=integer(4); maelys_datalog_prepared_explanation_t *e=NULL;
        assert(!maelys_datalog_result_prepare_explanation(r,MAELYS_DATALOG_EXPLAIN_FALSE,"total",&v,1,workspace,explain_bytes,&e));
        rejected_unchanged(w,batch,2,MAELYS_DATALOG_STATUS_INVALID_STATE);
        assert(maelys_datalog_group_window_free(w)==MAELYS_DATALOG_STATUS_INVALID_STATE);
        assert(!maelys_datalog_prepared_explanation_write_text(e,text,sizeof(text)));
        rejected_static(w,NULL,0,MAELYS_DATALOG_STATUS_INVALID_STATE);
        assert(!maelys_datalog_prepared_explanation_release(e));
    }
    maelys_datalog_fact_t static_fact=reading(99,1);
    assert(!maelys_datalog_group_window_replace_static(w,&static_fact,1,NULL));
    rejected_static(w,&static_fact,3,MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
    const maelys_datalog_fact_t *static_view=NULL;size_t static_count;
    assert(!maelys_datalog_group_window_static_facts(w,&static_view,&static_count)&&static_count==1);
    assert(static_view[0].terms[0].as.integer==99);
    release_failure_target=w->result;
    rejected_static(w,NULL,0,MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    release_failure_target=NULL;
    assert(!maelys_datalog_group_window_replace_static(w,NULL,0,NULL));
    release_failure_target=w->result;
    maelys_datalog_fact_t proposed=reading(101,1);
    rejected_unchanged(w,&proposed,1,MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    release_failure_target=NULL;
    assert(!maelys_datalog_group_window_push(w,&proposed,1,NULL,NULL));
    assert(!maelys_datalog_group_window_free(w));
    assert(maelys_datalog_group_window_push(w,NULL,0,NULL,NULL)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    caps=(maelys_datalog_group_window_capacities_t){1,1,1,0};
    assert(!maelys_datalog_group_window_init(storage,bytes,&caps,0,a,b,&w,NULL));
    assert(!maelys_datalog_group_window_push(w,NULL,0,NULL,NULL));
    rejected_unchanged(w,&proposed,1,MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
    assert(!maelys_datalog_group_window_free(w));
    assert(calls==0 && frees==0 && live==before);
    forbidden=0; assert(!maelys_datalog_session_free(a)); assert(!maelys_datalog_session_free(b));
    free(workspace);free(storage);
    puts("multi-fact window allocation contract: no engine allocator/free calls; committed bytes unchanged PASS");
    return 0;
}
