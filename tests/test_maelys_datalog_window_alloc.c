/* SPDX-License-Identifier: MPL-2.0 */
#include "tests/fixtures/allocation_guard.h"
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef memset
#include <maelys/datalog_window.h>
#include <assert.h>
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

/* White-box ONLY to snapshot precisely the committed region, excluding the
 * candidate bank. Normal behavior is also tested against the installed SDK. */
static maelys_datalog_result_t *release_failure_target;
static maelys_datalog_status_t injected_result_free(maelys_datalog_result_t *r) {
    if (r == release_failure_target) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    return maelys_datalog_result_free(r);
}
#define maelys_datalog_result_free injected_result_free
#define malloc maelys_test_malloc
#define calloc maelys_test_calloc
#define realloc maelys_test_realloc
#define free maelys_test_free
#include "src/runtime/maelys_datalog_window.c"
#undef maelys_datalog_result_free
#undef malloc
#undef calloc
#undef realloc
#undef free
static maelys_datalog_public_value_t integer(int64_t n) {
    maelys_datalog_public_value_t v={.kind=MAELYS_DATALOG_VALUE_INTEGER};v.as.integer=n;return v;
}
static void rejected_unchanged(maelys_datalog_window_t *w, const char *predicate,
    maelys_datalog_public_value_t *values, size_t count, int expected, size_t input_bytes) {
    unsigned char *bank=malloc(input_bytes); assert(bank);
    unsigned char header[sizeof(*w)];
    memcpy(header,w,sizeof(*w)); memcpy(bank,w->inputs[w->active],input_bytes);
    uint32_t id=UINT32_MAX;
    assert(maelys_datalog_window_push(w,predicate,values,count,&id,NULL)==expected);
    assert(id==UINT32_MAX && !memcmp(header,w,sizeof(*w)));
    assert(!memcmp(bank,w->inputs[w->active],input_bytes)); free(bank);
}
int main(void) {
    const maelys_datalog_public_predicate_t preds[]={
        {"event",2,MAELYS_DATALOG_PREDICATE_EDB},
        {"out",1,MAELYS_DATALOG_PREDICATE_IDB|MAELYS_DATALOG_PREDICATE_QUERY}};
    const maelys_datalog_public_domain_t d={"window_alloc",preds,2,NULL,0};
    assert(!maelys_datalog_domain_register(&d));
    maelys_datalog_policy_t *p=NULL;
    const char *source="out(N) :- sum(V,event(_,V),N).";
    assert(!maelys_datalog_policy_load_inline("window_alloc","window.alloc",source,strlen(source),&p,NULL));
    maelys_datalog_session_config_t *config=NULL;
    assert(!maelys_datalog_session_config_create(&config));
    assert(!maelys_datalog_session_config_set_explanation_workspace(config,MAELYS_DATALOG_EXPLAIN_TRUE|MAELYS_DATALOG_EXPLAIN_FALSE));
    maelys_datalog_session_t *a=NULL,*b=NULL;
    assert(!maelys_datalog_session_create_configured(p,0,config,&a));
    assert(!maelys_datalog_session_create_configured(p,0,config,&b));
    assert(!maelys_datalog_policy_free(p)); assert(!maelys_datalog_session_config_free(config));
    size_t bytes,alignment,input_bytes,input_alignment;
    assert(!maelys_datalog_window_storage_requirements(2,16,&bytes,&alignment));
    assert(!maelys_datalog_input_edb_storage_requirements(2,16,&input_bytes,&input_alignment));
    void *storage=malloc(bytes); assert(storage && (uintptr_t)storage%alignment==0);
    size_t explain_bytes,explain_alignment;
    assert(!maelys_datalog_session_explanation_storage_bound(a,MAELYS_DATALOG_EXPLAIN_FALSE,&explain_bytes,&explain_alignment));
    void *workspace=malloc(explain_bytes); assert(workspace);
    size_t before=live; forbidden=1;
    maelys_datalog_window_t *w=NULL;
    assert(!maelys_datalog_window_init(storage,bytes,2,16,0,a,b,&w,NULL));
    for(uint32_t i=0;i<600;++i) {
        maelys_datalog_public_value_t v=integer(1); uint32_t id=UINT32_MAX;
        assert(!maelys_datalog_window_push(w,"event",&v,1,&id,NULL) && id==i);
        size_t used,capacity;
        assert(!maelys_datalog_window_text_usage(w,&used,&capacity) && used==6 && capacity==16);
        v=integer(INT32_MAX);
        rejected_unchanged(w,"event",&v,1,MAELYS_DATALOG_STATUS_INVALID_FIELD,input_bytes);
        v=integer(3);
        rejected_unchanged(w,"a_predicate_that_exceeds_text_storage",&v,1,MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE,input_bytes);
        maelys_datalog_result_t *r=NULL; int present=-1;
        assert(!maelys_datalog_window_result(w,&r)); v=integer(i?2:1);
        assert(!maelys_datalog_result_query(r,"out",&v,1,&present) && present);
        size_t required; char text[8192];
        assert(!maelys_datalog_result_explain_true_text(r,"out",&v,1,text,sizeof(text),&required));
        v=integer(4);
        maelys_datalog_prepared_explanation_t *e=NULL;
        assert(!maelys_datalog_result_prepare_explanation(r,MAELYS_DATALOG_EXPLAIN_FALSE,"out",&v,1,workspace,explain_bytes,&e));
        rejected_unchanged(w,"event",&v,1,MAELYS_DATALOG_STATUS_INVALID_STATE,input_bytes);
        assert(!maelys_datalog_prepared_explanation_write_text(e,text,sizeof(text)));
        assert(!maelys_datalog_prepared_explanation_release(e));
    }
    /* A release failure need not be caused by an explanation lease. */
    release_failure_target=w->result;
    maelys_datalog_public_value_t proposed=integer(1);
    rejected_unchanged(w,"event",&proposed,1,MAELYS_DATALOG_STATUS_INVALID_ARGUMENT,input_bytes);
    maelys_datalog_public_diagnostic_t diag;
    assert(maelys_datalog_window_push(w,"event",&proposed,1,NULL,&diag)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    assert(strstr(diag.message,"could not be released") && !strstr(diag.message,"explanations"));
    release_failure_target=NULL;
    assert(!maelys_datalog_window_push(w,"event",&proposed,1,NULL,NULL));
    assert(!maelys_datalog_window_free(w));
    assert(!w->result && !w->sessions[0] && !w->sessions[1] && !w->inputs[0] && !w->inputs[1]);
    size_t closed_count=123,closed_capacity=456; uint64_t closed_next=789;
    maelys_datalog_result_t *closed_result=NULL;
    const maelys_datalog_public_fact_t *closed_events=NULL;
    assert(maelys_datalog_window_push(w,"event",&proposed,1,NULL,&diag)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    assert(maelys_datalog_window_state(w,&closed_count,&closed_next)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    assert(maelys_datalog_window_text_usage(w,&closed_count,&closed_capacity)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    assert(maelys_datalog_window_result(w,&closed_result)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    assert(maelys_datalog_window_events(w,&closed_events,&closed_count)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    assert(maelys_datalog_window_free(w)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    assert(closed_count==123 && closed_capacity==456 && closed_next==789 && !closed_result && !closed_events);
    /* Same caller arena/sessions are reusable, including zero text capacity. */
    assert(!maelys_datalog_window_init(storage,bytes,1,0,0,a,b,&w,NULL));
    assert(!maelys_datalog_window_text_usage(w,&closed_count,&closed_capacity) && !closed_count && !closed_capacity);
    maelys_datalog_public_value_t v=integer(1);
    assert(maelys_datalog_window_push(w,"event",&v,1,NULL,NULL)==MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
    assert(!maelys_datalog_window_free(w));
    assert(calls==0 && frees==0 && live==before);
    forbidden=0; assert(!maelys_datalog_session_free(a)); assert(!maelys_datalog_session_free(b));
    free(workspace); free(storage);
    puts("last-N allocation contract: no engine allocator/free calls; committed banks byte-exact on rejection PASS");
    return 0;
}
