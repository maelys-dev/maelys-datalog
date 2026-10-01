/* SPDX-License-Identifier: MPL-2.0 */
/* Installed-SDK consumer: independent snapshots exercise both adapter models. */
#include <maelys/datalog_window.h>
#include <maelys/datalog_inputs.h>
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
    MAELYS_DATALOG_IDB_QUERY("deny",1), MAELYS_DATALOG_IDB_QUERY("norm",3), MAELYS_DATALOG_IDB_QUERY("total",1), MAELYS_DATALOG_EDB("reserve",2),
    MAELYS_DATALOG_IDB_QUERY("eligible",2), MAELYS_DATALOG_IDB_QUERY("risk",1)};
typedef struct {
    maelys_datalog_session_t *a,*b,*oracle;
    maelys_datalog_window_t *w;
    maelys_datalog_group_window_t *g;
    void *storage;
    maelys_datalog_session_inputs_t *inputs[2];
    void *input_storage[2];
} fixture;
static int retained;
static void bases(fixture *f,maelys_datalog_input_base_t b[2]) {
    if(retained) for(size_t i=0;i<2;++i) OK(maelys_datalog_session_inputs_base(f->inputs[i],&b[i]));
}
static void same_bases(fixture *f,const maelys_datalog_input_base_t b[2]) {
    if(retained) { maelys_datalog_input_base_t after[2];bases(f,after);assert(!memcmp(after,b,sizeof(after))); }
}
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
        if (!(predicates[p].flags & MAELYS_DATALOG_PREDICATE_IDB)) continue;
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
static void setup(fixture *f,int grouped,uint32_t flags,uint32_t first) {
    memset(f,0,sizeof(*f));maelys_datalog_policy_t *p=NULL;
    const char *source="copy(I,V) :- event(I,V). allow(V) :- event(_,V), permission(V). "
        "deny(V) :- event(_,V), not(permission(V)). norm(A,B,C) :- record(A,B,C). total(N) :- sum(V,event(_,V),N). "
        "eligible(I,V) :- reserve(I,V), not(event(0,0)). risk(N) :- sum(V,eligible(_,V),N).";
    maelys_datalog_diagnostic_t d=MAELYS_DATALOG_DIAGNOSTIC_INIT;
    int loaded=maelys_datalog_policy_load_inline("updates","updates",source,strlen(source),&p,&d);
    if(loaded) fprintf(stderr,"policy: %s %s\n",d.message,d.hint);
    OK(loaded);
    OK(maelys_datalog_session_create(p,0,&f->a));OK(maelys_datalog_session_create(p,0,&f->b));
    OK(maelys_datalog_session_create(p,0,&f->oracle));OK(maelys_datalog_policy_free(p));
    size_t bytes,alignment;
    if(retained) {
        maelys_datalog_session_t *sessions[]={f->a,f->b};
        const char *symbols[]={"zebra"};
        maelys_datalog_input_options_t o=MAELYS_DATALOG_INPUT_OPTIONS_INIT;
        o.fact_capacity=16;o.symbols=symbols;o.symbol_count=1;
        for(size_t i=0;i<2;++i) {
            OK(maelys_datalog_session_inputs_storage_requirements(sessions[i],&o,&bytes,&alignment));
            f->input_storage[i]=malloc(bytes);assert(f->input_storage[i]);
            OK(maelys_datalog_session_inputs_init(sessions[i],&o,f->input_storage[i],bytes,&f->inputs[i]));
        }
    }
    maelys_datalog_group_window_capacities_t c={2,4,8,64};
    maelys_datalog_window_options_t options={sizeof(options),4,flags};
    if(grouped) OK(maelys_datalog_group_window_storage_requirements_configured(&c, &options,&bytes,&alignment));
    else OK(maelys_datalog_window_storage_requirements_configured(2,64, &options,&bytes,&alignment));
    f->storage=malloc(bytes);assert(f->storage&&(uintptr_t)f->storage%alignment==0);memset(f->storage,0xa5,bytes);
    if(grouped) {
        assert(maelys_datalog_group_window_init_configured(f->storage,bytes-1,&c, &options,first,f->a,f->b,&f->g,NULL)==MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL&&!f->g);
        OK(maelys_datalog_group_window_init_configured(f->storage,bytes,&c, &options,first,f->a,f->b,&f->g,NULL));
    } else {
        assert(maelys_datalog_window_init_configured(f->storage,bytes-1,2,64, &options,first,f->a,f->b,&f->w,NULL)==MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL&&!f->w);
        OK(maelys_datalog_window_init_configured(f->storage,bytes,2,64, &options,first,f->a,f->b,&f->w,NULL));
    }
}
static void close_fixture(fixture *f) {
    if(f->g) OK(maelys_datalog_group_window_free(f->g)); else OK(maelys_datalog_window_free(f->w));
    if(retained) for(size_t i=0;i<2;++i) { OK(maelys_datalog_session_inputs_free(f->inputs[i]));free(f->input_storage[i]); }
    OK(maelys_datalog_session_free(f->a));OK(maelys_datalog_session_free(f->b));OK(maelys_datalog_session_free(f->oracle));
    assert(replace(f,NULL,0)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    const maelys_datalog_fact_t *v=NULL;size_t n=123;
    int rc=f->g?maelys_datalog_group_window_static_facts(f->g,&v,&n):maelys_datalog_window_static_facts(f->w,&v,&n);
    assert(rc==MAELYS_DATALOG_STATUS_INVALID_STATE&&!v&&n==123);
    uint64_t now=789;int has=-1;uint32_t id=456;
    if(f->g) {
        assert(maelys_datalog_group_window_expire(f->g,1,&n,NULL)==MAELYS_DATALOG_STATUS_INVALID_STATE);
        assert(maelys_datalog_group_window_push_until(f->g,NULL,0,1,&id,NULL)==MAELYS_DATALOG_STATUS_INVALID_STATE);
        assert(maelys_datalog_group_window_expiry_watermark(f->g,&now,&has)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    } else {
        assert(maelys_datalog_window_expire(f->w,1,&n,NULL)==MAELYS_DATALOG_STATUS_INVALID_STATE);
        assert(maelys_datalog_window_push_until(f->w,"event",NULL,0,1,&id,NULL)==MAELYS_DATALOG_STATUS_INVALID_STATE);
        assert(maelys_datalog_window_expiry_watermark(f->w,&now,&has)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    }
    assert(n==123&&now==789&&has==-1&&id==456);free(f->storage);
}
static void static_updates(int grouped) {
    fixture f;setup(&f,grouped,0,0);
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
static int expire(fixture *f,uint64_t now,size_t *expired) {
    return f->g ? maelys_datalog_group_window_expire(f->g,now,expired,NULL) : maelys_datalog_window_expire(f->w,now,expired,NULL);
}
static int until(fixture *f,int value,uint32_t sequence,uint64_t deadline,uint32_t *out) {
    maelys_datalog_fact_t e=fact("event",sequence,value,2);
    return f->g ? maelys_datalog_group_window_push_until(f->g,&e,1,deadline,out,NULL) :
        maelys_datalog_window_push_until(f->w,"event",&e.terms[1],1,deadline,out,NULL);
}
static void watermark(fixture *f,uint64_t now,int set) {
    uint64_t actual=123;int has=-1;
    if(f->g) OK(maelys_datalog_group_window_expiry_watermark(f->g,&actual,&has));
    else OK(maelys_datalog_window_expiry_watermark(f->w,&actual,&has));
    assert(actual==now&&has==set);
}
static void timed_updates(int grouped) {
    fixture f;setup(&f,grouped,0,0);size_t expired=123;uint32_t id=123;
    assert(expire(&f,0,&expired)==MAELYS_DATALOG_STATUS_UNSUPPORTED&&expired==123);
    assert(until(&f,7,0,1,&id)==MAELYS_DATALOG_STATUS_UNSUPPORTED&&id==123);close_fixture(&f);
    setup(&f,grouped,MAELYS_DATALOG_WINDOW_EXPIRATION,0);watermark(&f,0,0);
    OK(until(&f,7,0,20,&id));assert(id==0);OK(until(&f,8,1,10,&id));assert(id==1);
    maelys_datalog_fact_t expected[]={fact("event",0,7,2),fact("permission",7,0,1),fact("event",1,8,2)};
    OK(replace(&f,expected,2));cursor(&f,2,2);compare(&f,expected,3);
    maelys_datalog_result_t *old=result(&f);OK(expire(&f,9,&expired));assert(expired==0&&result(&f)==old);watermark(&f,9,1);
    /* Due event is in the middle/end of arrival order, not the oldest. */
    size_t bytes,alignment;OK(maelys_datalog_result_explanation_storage_requirements(old,MAELYS_DATALOG_EXPLAIN_TRUE,&bytes,&alignment));
    void *arena=malloc(bytes);assert(arena);maelys_datalog_prepared_explanation_t *e=NULL;maelys_datalog_value_t v=integer(7);
    OK(maelys_datalog_result_prepare_explanation(old,MAELYS_DATALOG_EXPLAIN_TRUE,"allow",&v,1,arena,bytes,&e));
    OK(expire(&f,9,&expired));assert(expired==0&&result(&f)==old);
    maelys_datalog_input_base_t before[2];bases(&f,before);
    expired=123;assert(expire(&f,10,&expired)==MAELYS_DATALOG_STATUS_INVALID_STATE&&expired==123);watermark(&f,9,1);
    same_bases(&f,before);
    assert(result(&f)==old);cursor(&f,2,2);compare(&f,expected,3);
    char text[8192];OK(maelys_datalog_prepared_explanation_write_text(e,text,sizeof(text)));assert(strstr(text,"permission"));
    OK(maelys_datalog_prepared_explanation_release(e));free(arena);
    OK(expire(&f,10,&expired));assert(expired==1);watermark(&f,10,1);cursor(&f,1,2);compare(&f,expected,2);
    const maelys_datalog_fact_t *events;size_t n;
    if(f.g) {
        const maelys_datalog_event_group_t *groups;OK(maelys_datalog_group_window_groups(f.g,&groups,&n));
        assert(n==1&&groups[0].id==0&&groups[0].fact_offset==0&&groups[0].fact_count==1);
        OK(maelys_datalog_group_window_contributions(f.g,&events,&n));
    } else OK(maelys_datalog_window_events(f.w,&events,&n));
    assert(n==1&&events[0].terms[0].as.integer==0);
    push(&f,9,2);expected[2]=fact("event",2,9,2);
    OK(expire(&f,20,&expired));assert(expired==1);cursor(&f,1,3);compare(&f,expected,3);
    OK(replace(&f,NULL,0));compare(&f,expected+2,1);
    OK(until(&f,10,3,UINT64_MAX,&id));assert(id==3);
    OK(expire(&f,UINT64_MAX,&expired));assert(expired==1);watermark(&f,UINT64_MAX,1);cursor(&f,1,4);compare(&f,expected+2,1);
    old=result(&f);OK(expire(&f,UINT64_MAX,&expired));assert(expired==0&&result(&f)==old);
    expired=123;assert(expire(&f,0,&expired)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT&&expired==123);
    id=123;assert(until(&f,11,4,UINT64_MAX,&id)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT&&id==123);cursor(&f,1,4);
    push(&f,11,4);close_fixture(&f);
    /* Exhausted IDs do not disable expiry or static context replacement. */
    setup(&f,grouped,MAELYS_DATALOG_WINDOW_EXPIRATION,INT32_MAX);
    OK(until(&f,1,INT32_MAX,0,&id));assert(id==INT32_MAX);OK(expire(&f,0,&expired));assert(expired==1);
    cursor(&f,0,(uint64_t)INT32_MAX+1);OK(replace(&f,expected+1,1));close_fixture(&f);
    /* Removal can enable facts through negation and overflow their aggregate:
       reject the expiry and its watermark until context changes. */
    setup(&f,grouped,MAELYS_DATALOG_WINDOW_EXPIRATION,0);
    maelys_datalog_fact_t limits[]={fact("reserve",1,INT32_MAX,2),fact("reserve",2,1,2),fact("event",0,0,2)};
    OK(until(&f,0,0,10,&id));OK(replace(&f,limits,2));compare(&f,limits,3);
    old=result(&f);expired=123;assert(expire(&f,10,&expired)==MAELYS_DATALOG_STATUS_INVALID_FIELD&&expired==123);
    assert(result(&f)==old);cursor(&f,1,1);watermark(&f,0,0);compare(&f,limits,3);
    OK(replace(&f,NULL,0));OK(expire(&f,10,&expired));assert(expired==1);compare(&f,NULL,0);close_fixture(&f);
}
static void empty_timed_group(void) {
    fixture f;setup(&f,1,MAELYS_DATALOG_WINDOW_EXPIRATION,0);uint32_t id;size_t n;
    OK(maelys_datalog_group_window_push_until(f.g,NULL,0,5,&id,NULL));assert(id==0);
    push(&f,7,1);OK(expire(&f,5,&n));assert(n==1);cursor(&f,1,2);
    const maelys_datalog_event_group_t *groups;OK(maelys_datalog_group_window_groups(f.g,&groups,&n));
    assert(n==1&&groups[0].id==1&&groups[0].fact_offset==0&&groups[0].fact_count==1);close_fixture(&f);
}

static void grouped_expiry_slices(void) {
    fixture f;setup(&f,1,MAELYS_DATALOG_WINDOW_EXPIRATION,0);uint32_t id;size_t n;
    maelys_datalog_fact_t a[]={fact("event",10,1,2),fact("event",11,2,2)};
    maelys_datalog_fact_t b[]={fact("event",11,2,2),fact("event",12,3,2)};
    OK(replace(&f,a+1,1));
    OK(maelys_datalog_group_window_push_until(f.g,a,2,20,&id,NULL));assert(id==0);
    OK(maelys_datalog_group_window_push_until(f.g,b,2,10,&id,NULL));assert(id==1);
    OK(expire(&f,10,&n));assert(n==1);compare(&f,a,2);
    maelys_datalog_fact_t c[]={fact("event",13,4,2),fact("event",14,5,2)};
    OK(maelys_datalog_group_window_push(f.g,c,2,&id,NULL));assert(id==2);
    OK(expire(&f,20,&n));assert(n==1);cursor(&f,1,3);
    maelys_datalog_fact_t expected[]={a[1],c[0],c[1]};compare(&f,expected,3);
    const maelys_datalog_event_group_t *groups;OK(maelys_datalog_group_window_groups(f.g,&groups,&n));
    assert(n==1&&groups[0].id==2&&groups[0].fact_offset==0&&groups[0].fact_count==2);
    const maelys_datalog_fact_t *contributions;OK(maelys_datalog_group_window_contributions(f.g,&contributions,&n));
    assert(n==2&&contributions[0].terms[0].as.integer==13&&contributions[1].terms[0].as.integer==14);close_fixture(&f);
}
static void expiry_sequence(int grouped) {
    fixture f;setup(&f,grouped,MAELYS_DATALOG_WINDOW_EXPIRATION,0);
    struct { uint32_t id; int value; uint64_t deadline; int timed; } model[2];
    size_t count=0;uint32_t rng=12345;
    maelys_datalog_fact_t permission=fact("permission",5,0,1);OK(replace(&f,&permission,1));
    for(uint32_t i=0;i<120;++i) {
        rng=rng*1664525u+1013904223u;
        int timed=(rng&3u)!=0;int value=(int)(rng%10u);uint64_t deadline=i+(rng%4u);uint32_t id;
        if(timed) { OK(until(&f,value,i,deadline,&id));assert(id==i); } else push(&f,value,i);
        if(count==2) { model[0]=model[1];count=1; }
        model[count].id=i;model[count].value=value;model[count].deadline=deadline;model[count++].timed=timed;
        size_t kept=0,removed=0;
        for(size_t j=0;j<count;++j) {
            if(model[j].timed&&model[j].deadline<=i) ++removed;else model[kept++]=model[j];
        }
        count=kept;size_t actual=123;OK(expire(&f,i,&actual));assert(actual==removed);cursor(&f,count,i+1);watermark(&f,i,1);
        maelys_datalog_fact_t expected[3]={permission};
        for(size_t j=0;j<count;++j) expected[j+1]=fact("event",model[j].id,model[j].value,2);
        compare(&f,expected,count+1);
    }
    close_fixture(&f);
}

int main(void) {
    const maelys_datalog_domain_t d={"updates",predicates,sizeof(predicates)/sizeof(*predicates),NULL,0};OK(maelys_datalog_domain_register(&d));
    size_t bytes=123,alignment=456;
    assert(maelys_datalog_window_storage_requirements_configured(1,32, OPTIONS(SIZE_MAX),&bytes,&alignment)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT&&bytes==123&&alignment==456);
    maelys_datalog_group_window_capacities_t c={1,1,1,32};
    assert(maelys_datalog_group_window_storage_requirements_configured(&c, OPTIONS(SIZE_MAX),&bytes,&alignment)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT&&bytes==123&&alignment==456);
    size_t plain,timed,a;
    maelys_datalog_window_options_t timing={sizeof(timing),0,MAELYS_DATALOG_WINDOW_EXPIRATION};
    OK(maelys_datalog_window_storage_requirements(8,32,&plain,&a));
    OK(maelys_datalog_window_storage_requirements_configured(8,32,&timing,&timed,&a));assert(timed>plain);
    c.groups=8;OK(maelys_datalog_group_window_storage_requirements(&c,&plain,&a));
    OK(maelys_datalog_group_window_storage_requirements_configured(&c,&timing,&timed,&a));assert(timed>plain);
    maelys_datalog_window_options_t invalid={sizeof(invalid),0,UINT32_MAX};
    assert(maelys_datalog_window_storage_requirements_configured(1,32,&invalid,&bytes,&alignment)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT&&bytes==123&&alignment==456);
    invalid.flags=0;invalid.struct_size=0;
    assert(maelys_datalog_group_window_storage_requirements_configured(&c,&invalid,&bytes,&alignment)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT&&bytes==123&&alignment==456);
    for(retained=0;retained<2;++retained) {
        static_updates(0);static_updates(1);timed_updates(0);timed_updates(1);empty_timed_group();grouped_expiry_slices();expiry_sequence(0);expiry_sequence(1);
    }
    puts("transactional static inputs and explicit expiry: snapshots and retained inputs PASS");return 0;
}
