/* SPDX-License-Identifier: MPL-2.0 */
/* Installed-SDK consumer: independent snapshots exercise both adapter models. */
#include <maelys/datalog_window.h>
#include <assert.h>
#define OPTIONS(n) (&(maelys_datalog_window_options_t){sizeof(maelys_datalog_window_options_t),(n),0})
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define OK(x) do { int rc_=(x); if(rc_) { fprintf(stderr,"%s:%d: %s -> %d\n",__FILE__,__LINE__,#x,rc_); abort(); } } while(0)
static maelys_datalog_value_t integer(int n) {
    maelys_datalog_value_t v={.kind=MAELYS_DATALOG_VALUE_INTEGER}; v.as.integer=n; return v;
}
static maelys_datalog_fact_t fact(const char *p,int a,int b,size_t arity) {
    maelys_datalog_fact_t f={.predicate=p,.arity=arity}; f.terms[0]=integer(a);f.terms[1]=integer(b);return f;
}
static const maelys_datalog_predicate_t predicates[]={
    MAELYS_DATALOG_EDB("event",2), MAELYS_DATALOG_EDB("permission",1), MAELYS_DATALOG_EDB("record",3),
    MAELYS_DATALOG_IDB_QUERY("copy",2), MAELYS_DATALOG_IDB_QUERY("allow",1),
    MAELYS_DATALOG_IDB_QUERY("deny",1), MAELYS_DATALOG_IDB_QUERY("norm",3), MAELYS_DATALOG_IDB_QUERY("total",1)};
typedef struct {
    maelys_datalog_session_t *a,*b,*oracle;
    maelys_datalog_window_t *w;
    maelys_datalog_group_window_t *g;
    void *storage;
} fixture;
static maelys_datalog_result_t *result(fixture *f) {
    maelys_datalog_result_t *r=NULL;
    if(f->g) OK(maelys_datalog_group_window_result(f->g,&r)); else OK(maelys_datalog_window_result(f->w,&r));
    return r;
}
static int replace(fixture *f,const maelys_datalog_fact_t *facts,size_t n) {
    return f->g ? maelys_datalog_group_window_replace_static(f->g,facts,n,NULL) :
        maelys_datalog_window_replace_static(f->w,facts,n,NULL);
}
static void cursor(fixture *f,size_t events,uint64_t next) {
    size_t count;uint64_t actual;
    if(f->g) { maelys_datalog_group_window_usage_t u;OK(maelys_datalog_group_window_state(f->g,&u)); count=u.groups;actual=u.next_group; }
    else OK(maelys_datalog_window_state(f->w,&count,&actual));
    assert(count==events && next==actual);
}
static void push(fixture *f,int value,uint32_t expected) {
    uint32_t id=UINT32_MAX;maelys_datalog_fact_t e=fact("event",expected,value,2);
    if(f->g) OK(maelys_datalog_group_window_push(f->g,&e,1,&id,NULL));
    else OK(maelys_datalog_window_push(f->w,"event",&e.terms[1],1,&id,NULL));
    assert(id==expected);
}
static void compare(fixture *f,const maelys_datalog_fact_t *facts,size_t n) {
    maelys_datalog_result_t *r=NULL;OK(maelys_datalog_session_solve(f->oracle,facts,n,&r,NULL));
    for(size_t p=3;p<sizeof(predicates)/sizeof(*predicates);++p) {
        maelys_datalog_fact_view_t a[16],b[16];size_t na,nb;
        OK(maelys_datalog_result_enumerate(result(f),predicates[p].name,predicates[p].arity,a,16,&na));
        OK(maelys_datalog_result_enumerate(r,predicates[p].name,predicates[p].arity,b,16,&nb));assert(na==nb);
        for(size_t i=0;i<na;++i) for(size_t j=0;j<a[i].arity;++j) {
            assert(a[i].terms[j].kind==b[i].terms[j].kind);
            if(a[i].terms[j].kind==MAELYS_DATALOG_VALUE_SYMBOL) {
                const char *sa,*sb;size_t la,lb;
                assert(a[i].terms[j].as.symbol_id==b[i].terms[j].as.symbol_id);
                OK(maelys_datalog_result_symbol_text(result(f),a[i].terms[j].as.symbol_id,&sa,&la));
                OK(maelys_datalog_result_symbol_text(r,b[i].terms[j].as.symbol_id,&sb,&lb));assert(la==lb&&!memcmp(sa,sb,la));
            } else if(a[i].terms[j].kind==MAELYS_DATALOG_VALUE_BOOLEAN) assert(a[i].terms[j].as.boolean==b[i].terms[j].as.boolean);
            else assert(a[i].terms[j].as.integer==b[i].terms[j].as.integer);
        }
    }
    OK(maelys_datalog_result_free(r));
}
static void setup(fixture *f,int grouped) {
    memset(f,0,sizeof(*f));maelys_datalog_policy_t *p=NULL;
    const char *source="copy(I,V) :- event(I,V). allow(V) :- event(_,V), permission(V). "
        "deny(V) :- event(_,V), not(permission(V)). norm(A,B,C) :- record(A,B,C). total(N) :- sum(V,event(_,V),N).";
    OK(maelys_datalog_policy_load_inline("updates","updates",source,strlen(source),&p,NULL));
    OK(maelys_datalog_session_create(p,0,&f->a));OK(maelys_datalog_session_create(p,0,&f->b));
    OK(maelys_datalog_session_create(p,0,&f->oracle));OK(maelys_datalog_policy_free(p));
    size_t bytes,alignment;
    maelys_datalog_group_window_capacities_t c={2,2,6,64};
    if(grouped) OK(maelys_datalog_group_window_storage_requirements_configured(&c, OPTIONS(4),&bytes,&alignment));
    else OK(maelys_datalog_window_storage_requirements_configured(2,64, OPTIONS(4),&bytes,&alignment));
    f->storage=malloc(bytes);assert(f->storage&&(uintptr_t)f->storage%alignment==0);memset(f->storage,0xa5,bytes);
    if(grouped) {
        assert(maelys_datalog_group_window_init_configured(f->storage,bytes-1,&c, OPTIONS(4),0,f->a,f->b,&f->g,NULL)==MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL&&!f->g);
        OK(maelys_datalog_group_window_init_configured(f->storage,bytes,&c, OPTIONS(4),0,f->a,f->b,&f->g,NULL));
    } else {
        assert(maelys_datalog_window_init_configured(f->storage,bytes-1,2,64, OPTIONS(4),0,f->a,f->b,&f->w,NULL)==MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL&&!f->w);
        OK(maelys_datalog_window_init_configured(f->storage,bytes,2,64, OPTIONS(4),0,f->a,f->b,&f->w,NULL));
    }
}
static void close_fixture(fixture *f) {
    if(f->g) OK(maelys_datalog_group_window_free(f->g)); else OK(maelys_datalog_window_free(f->w));
    OK(maelys_datalog_session_free(f->a));OK(maelys_datalog_session_free(f->b));OK(maelys_datalog_session_free(f->oracle));
    assert(replace(f,NULL,0)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    const maelys_datalog_fact_t *v=NULL;size_t n=123;
    int rc=f->g?maelys_datalog_group_window_static_facts(f->g,&v,&n):maelys_datalog_window_static_facts(f->w,&v,&n);
    assert(rc==MAELYS_DATALOG_STATUS_INVALID_STATE&&!v&&n==123);free(f->storage);
}
static void static_updates(int grouped) {
    fixture f;setup(&f,grouped);
    maelys_datalog_fact_t expected[6]={fact("event",0,7,2),fact("permission",7,0,1)};
    push(&f,7,0);compare(&f,expected,1);
    OK(replace(&f,expected+1,1));cursor(&f,1,1);compare(&f,expected,2);
    /* An authorization change must take effect without a new event. */
    maelys_datalog_result_t *old=result(&f);maelys_datalog_value_t v=integer(7);
    size_t bytes,alignment;OK(maelys_datalog_result_explanation_storage_requirements(old,MAELYS_DATALOG_EXPLAIN_TRUE,&bytes,&alignment));
    void *arena=malloc(bytes);assert(arena&&(uintptr_t)arena%alignment==0);
    maelys_datalog_prepared_explanation_t *e=NULL;
    OK(maelys_datalog_result_prepare_explanation(old,MAELYS_DATALOG_EXPLAIN_TRUE,"allow",&v,1,arena,bytes,&e));
    assert(replace(&f,NULL,0)==MAELYS_DATALOG_STATUS_INVALID_STATE);assert(result(&f)==old);cursor(&f,1,1);compare(&f,expected,2);
    char text[8192];OK(maelys_datalog_prepared_explanation_write_text(e,text,sizeof(text)));assert(strstr(text,"permission"));
    OK(maelys_datalog_prepared_explanation_release(e));free(arena);
    OK(replace(&f,NULL,0));cursor(&f,1,1);compare(&f,expected,1);
    assert(replace(&f,NULL,1)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    assert(replace(&f,expected,5)==MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
    maelys_datalog_fact_t overflow=fact("event",99,INT32_MAX,2);
    assert(replace(&f,&overflow,1)==MAELYS_DATALOG_STATUS_INVALID_FIELD);compare(&f,expected,1);
    /* Static and event copies of one fact have set semantics; removing either
       supplier alone must retain it, while views retain raw duplicates. */
    maelys_datalog_fact_t duplicates[]={expected[0],expected[0]};OK(replace(&f,duplicates,2));
    push(&f,8,1);push(&f,9,2);expected[1]=fact("event",1,8,2);expected[2]=fact("event",2,9,2);
    compare(&f,expected,3);cursor(&f,2,3);
    const maelys_datalog_fact_t *raw;size_t n;
    if(f.g) OK(maelys_datalog_group_window_static_facts(f.g,&raw,&n));else OK(maelys_datalog_window_static_facts(f.w,&raw,&n));
    assert(n==2&&raw[0].terms[0].as.integer==0&&raw[1].terms[0].as.integer==0);
    OK(replace(&f,NULL,0));compare(&f,expected+1,2);cursor(&f,2,3);
    /* Caller strings are copied, raw booleans normalize, canonical symbol IDs
       match independently constructed snapshots despite input order. */
    char name[]="zebra";maelys_datalog_fact_t record={.predicate="record",.arity=3};
    record.terms[0]=(maelys_datalog_value_t){.kind=MAELYS_DATALOG_VALUE_SYMBOL,.as.symbol=name};
    record.terms[1]=(maelys_datalog_value_t){.kind=MAELYS_DATALOG_VALUE_BOOLEAN,.as.boolean=9};record.terms[2]=integer(1);
    OK(replace(&f,&record,1));name[0]='x';record.terms[0].as.symbol="zebra";expected[3]=record;compare(&f,expected+1,3);
    record.terms[0].as.symbol="this_string_does_not_fit_in_the_small_shared_window_text_capacity_at_all";
    old=result(&f);assert(replace(&f,&record,1)==MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);assert(result(&f)==old);compare(&f,expected+1,3);
    close_fixture(&f);
}
int main(void) {
    const maelys_datalog_domain_t d={"updates",predicates,sizeof(predicates)/sizeof(*predicates),NULL,0};OK(maelys_datalog_domain_register(&d));
    size_t bytes=123,alignment=456;
    assert(maelys_datalog_window_storage_requirements_configured(1,32, OPTIONS(SIZE_MAX),&bytes,&alignment)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT&&bytes==123&&alignment==456);
    maelys_datalog_group_window_capacities_t c={1,1,1,32};
    assert(maelys_datalog_group_window_storage_requirements_configured(&c, OPTIONS(SIZE_MAX),&bytes,&alignment)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT&&bytes==123&&alignment==456);
    maelys_datalog_window_options_t invalid={sizeof(invalid),0,UINT32_MAX};
    assert(maelys_datalog_window_storage_requirements_configured(1,32,&invalid,&bytes,&alignment)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT&&bytes==123&&alignment==456);
    invalid.flags=0;invalid.struct_size=0;
    assert(maelys_datalog_group_window_storage_requirements_configured(&c,&invalid,&bytes,&alignment)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT&&bytes==123&&alignment==456);
    static_updates(0);static_updates(1);puts("transactional static window inputs PASS");return 0;
}
