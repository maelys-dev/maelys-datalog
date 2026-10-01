/* SPDX-License-Identifier: MPL-2.0 */
/* Public installed SDK + public bounded provider. Complete window operations;
 * preparation and independent set oracle excluded. Software counts, not time. */
#include <maelys/datalog_window.h>
#include "input_provider.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef WINDOW_BENCH_NATIVE_SMOKE
#include <valgrind/callgrind.h>
#else
#define CALLGRIND_ZERO_STATS ((void)0)
#define CALLGRIND_TOGGLE_COLLECT ((void)0)
#define CALLGRIND_DUMP_STATS_AT(x) ((void)(x))
#endif
#define OK(x) do{int rc_=(x);if(rc_){fprintf(stderr,"%d: %s: %d\n",__LINE__,#x,rc_);abort();}}while(0)
#define STEPS 200u
static const char *input[]={"in0","in1","in2","in3"},*output[]={"out0","out1","out2","out3"};
static char text[512][16];
static maelys_datalog_fact_t make_fact(int group,size_t id,size_t value,int symbols) {
    maelys_datalog_fact_t f={.predicate=input[id%4],.arity=group?1:2};
    f.terms[0]=(maelys_datalog_value_t){.kind=MAELYS_DATALOG_VALUE_INTEGER,.as.integer=(int64_t)id};
    size_t t=group?0:1;f.terms[t].kind=symbols?MAELYS_DATALOG_VALUE_SYMBOL:MAELYS_DATALOG_VALUE_INTEGER;
    if(symbols)f.terms[t].as.symbol=text[value];else f.terms[t].as.integer=(int64_t)value;
    return f;
}
static int equal(const maelys_datalog_fact_t *a,const maelys_datalog_fact_t *b) {
    if(strcmp(a->predicate,b->predicate)||a->arity!=b->arity)return 0;
    for(size_t t=0;t<a->arity;++t) {
        if(a->terms[t].kind!=b->terms[t].kind)return 0;
        if(a->terms[t].kind==MAELYS_DATALOG_VALUE_SYMBOL){if(strcmp(a->terms[t].as.symbol,b->terms[t].as.symbol))return 0;}
        else if(a->terms[t].as.integer!=b->terms[t].as.integer)return 0;
    }
    return 1;
}
static uint64_t check(maelys_datalog_result_t *r,const maelys_datalog_fact_t *raw,size_t n,int group) {
    maelys_datalog_fact_t expected[256];size_t unique=0;
    for(size_t i=0;i<n;++i){size_t j=0;while(j<unique&&!equal(&raw[i],&expected[j]))++j;if(j==unique)expected[unique++]=raw[i];}
    size_t total;OK(maelys_datalog_result_derived_fact_count(r,&total));assert(total==unique);
    uint64_t hash=UINT64_C(14695981039346656037);
    for(size_t p=0;p<4;++p) {
        maelys_datalog_fact_view_t rows[256];size_t count;
        OK(maelys_datalog_result_enumerate(r,output[p],group?1:2,rows,256,&count));
        size_t wanted=0;for(size_t i=0;i<unique;++i)wanted+=!strcmp(expected[i].predicate,input[p]);assert(count==wanted);
        for(size_t i=0;i<count;++i) {
            maelys_datalog_fact_t value={.predicate=input[p],.arity=group?1:2};
            for(size_t t=0;t<value.arity;++t) {
                const maelys_datalog_term_view_t *v=&rows[i].terms[t];value.terms[t].kind=v->kind;
                hash=(hash^v->kind)*UINT64_C(1099511628211);
                if(v->kind==MAELYS_DATALOG_VALUE_SYMBOL) {
                    const char *s;size_t bytes;OK(maelys_datalog_result_symbol_text(r,v->as.symbol_id,&s,&bytes));value.terms[t].as.symbol=s;
                    hash=(hash^v->as.symbol_id)*UINT64_C(1099511628211);
                    for(size_t j=0;j<bytes;++j)hash=(hash^(unsigned char)s[j])*UINT64_C(1099511628211);
                } else {value.terms[t].as.integer=v->as.integer;hash=(hash^(uint64_t)v->as.integer)*UINT64_C(1099511628211);}
            }
            size_t matches=0;for(size_t j=0;j<unique;++j)matches+=equal(&value,&expected[j]);assert(matches==1);
        }
    }
    return hash;
}
static int push(maelys_datalog_window_t *w,maelys_datalog_group_window_t *g,const maelys_datalog_fact_t *f,uint64_t deadline,uint32_t *id) {
    maelys_datalog_fact_t duplicates[2]={*f,*f};
    return g?maelys_datalog_group_window_push_until(g,duplicates,2,deadline,id,NULL):
        maelys_datalog_window_push_until(w,f->predicate,f->terms+1,1,deadline,id,NULL);
}
static int replace(maelys_datalog_window_t *w,maelys_datalog_group_window_t *g,const maelys_datalog_fact_t *f,size_t n) {
    return g?maelys_datalog_group_window_replace_static(g,f,n,NULL):maelys_datalog_window_replace_static(w,f,n,NULL);
}
static maelys_datalog_result_t *result(maelys_datalog_window_t *w,maelys_datalog_group_window_t *g) {
    maelys_datalog_result_t *r=NULL;if(g)OK(maelys_datalog_group_window_result(g,&r));else OK(maelys_datalog_window_result(w,&r));return r;
}
static void fixture(const char *label,int group,int symbols,size_t n,const char *mode) {
    int replacing=!strcmp(mode,"replace"),reject=!strcmp(mode,"reject"),expiring=!strcmp(mode,"expire"),
        shared=!strcmp(mode,"shared"),noop=!strcmp(mode,"noop");
    static maelys_datalog_predicate_t all_predicates[2][8];static int registered[2];
    maelys_datalog_predicate_t *predicates=all_predicates[group];
    for(size_t i=0;i<4;++i){predicates[i]=(maelys_datalog_predicate_t)MAELYS_DATALOG_EDB(input[i],group?1:2);
        predicates[i+4]=(maelys_datalog_predicate_t)MAELYS_DATALOG_IDB_QUERY(output[i],group?1:2);}
    maelys_datalog_domain_t domain={group?"window_groups":"window_occurrences",predicates,8,NULL,0};
    if(!registered[group]){OK(maelys_datalog_domain_register(&domain));registered[group]=1;}
    const char *source=group?"out0(X):-in0(X). out1(X):-in1(X). out2(X):-in2(X). out3(X):-in3(X).":
        "out0(I,X):-in0(I,X). out1(I,X):-in1(I,X). out2(I,X):-in2(I,X). out3(I,X):-in3(I,X).";
    maelys_datalog_policy_t *policy=NULL;OK(maelys_datalog_policy_load_inline(domain.name,"window",source,strlen(source),&policy,NULL));
    maelys_datalog_session_config_t *config=NULL;OK(maelys_datalog_session_config_create(&config));
    OK(maelys_datalog_session_config_set_backend_v7(config,input_fixture_transactions()));
    maelys_datalog_session_t *sessions[2];maelys_datalog_session_inputs_t *handles[2];void *storage[2];
    const char *vocabulary[512];for(size_t i=0;i<2*n;++i)vocabulary[i]=text[i];
    maelys_datalog_input_options_t options=MAELYS_DATALOG_INPUT_OPTIONS_INIT;
    options.fact_capacity=n;options.addition_capacity=0;options.removal_capacity=0;
    if(symbols){options.symbols=vocabulary;options.symbol_count=2*n;}
    for(size_t i=0;i<2;++i){size_t bytes,alignment;OK(maelys_datalog_session_create_configured(policy,0,config,&sessions[i]));
        OK(maelys_datalog_session_inputs_storage_requirements(sessions[i],&options,&bytes,&alignment));
        storage[i]=malloc(bytes);assert(storage[i]);OK(maelys_datalog_session_inputs_init(sessions[i],&options,storage[i],bytes,&handles[i]));}
    OK(maelys_datalog_session_config_free(config));
    size_t m=n/2,bytes,alignment;maelys_datalog_window_options_t wo={sizeof(wo),n,MAELYS_DATALOG_WINDOW_EXPIRATION};
    maelys_datalog_group_window_capacities_t cap={m,2*m,n,16384};
    maelys_datalog_window_t *w=NULL;maelys_datalog_group_window_t *g=NULL;
    if(group)OK(maelys_datalog_group_window_storage_requirements_configured(&cap,&wo,&bytes,&alignment));
    else OK(maelys_datalog_window_storage_requirements_configured(m,16384,&wo,&bytes,&alignment));
    void *area=malloc(bytes);assert(area);
    if(group)OK(maelys_datalog_group_window_init_configured(area,bytes,&cap,&wo,0,sessions[0],sessions[1],&g,NULL));
    else OK(maelys_datalog_window_init_configured(area,bytes,m,16384,&wo,0,sessions[0],sessions[1],&w,NULL));
    maelys_datalog_fact_t statics[256],events[256],raw[256],incoming[256];size_t ns=replacing||reject?n:m,ne=0,next=0;
    for(size_t i=0;i<ns;++i)statics[i]=make_fact(group,i,shared&&!group?m+i:i,symbols);
    OK(replace(w,g,statics,ns));
    if(!replacing&&!reject)for(size_t i=0;i<m;++i) {
        events[ne++]=make_fact(group,i,shared&&group?i:m+i,symbols);uint32_t id;
        OK(push(w,g,&events[i],i+1,&id));assert(id==next++);
    }
    maelys_datalog_result_t *r=result(w,g);memcpy(raw,statics,ns*sizeof(*raw));memcpy(raw+ns,events,ne*sizeof(*raw));(void)check(r,raw,ns+ne,group);
    void *explain_storage=NULL;maelys_datalog_prepared_explanation_t *lease=NULL;
    if(reject) {OK(maelys_datalog_result_explanation_storage_requirements(r,MAELYS_DATALOG_EXPLAIN_TRUE,&bytes,&alignment));
        explain_storage=malloc(bytes);assert(explain_storage);
        OK(maelys_datalog_result_prepare_explanation(r,MAELYS_DATALOG_EXPLAIN_TRUE,output[0],statics[0].terms,statics[0].arity,explain_storage,bytes,&lease));}
    uint64_t digest=0,now=0;CALLGRIND_ZERO_STATS;
    for(size_t tx=0;tx<STEPS;++tx) {
        int expiry=noop || (expiring && !(tx%2));
        if(replacing||reject)for(size_t i=0;i<n;++i)incoming[i]=make_fact(group,i,i+((tx/3)%2?n:0),symbols);
        else incoming[0]=make_fact(group,next,shared&&group?next%m:m+next%m,symbols);
        size_t expired=SIZE_MAX;uint32_t id=UINT32_MAX;int rc;
        CALLGRIND_TOGGLE_COLLECT;
        if(replacing||reject)rc=replace(w,g,incoming,n);
        else if(expiry)rc=g?maelys_datalog_group_window_expire(g,noop?0:now+1,&expired,NULL):maelys_datalog_window_expire(w,noop?0:now+1,&expired,NULL);
        else rc=push(w,g,incoming,now+m,&id);
        CALLGRIND_TOGGLE_COLLECT;
        if(reject){assert(rc==MAELYS_DATALOG_STATUS_INVALID_STATE);}
        else {
            OK(rc);
            if(replacing){memcpy(statics,incoming,n*sizeof(*statics));}
            else if(expiry) {
                if(noop)assert(expired==0);
                else {++now;assert(expired==1);memmove(events,events+1,(--ne)*sizeof(*events));}
            } else {
                assert(id==next++);if(ne==m){memmove(events,events+1,(--ne)*sizeof(*events));}events[ne++]=incoming[0];
            }
        }
        r=result(w,g);memcpy(raw,statics,ns*sizeof(*raw));memcpy(raw+ns,events,ne*sizeof(*raw));
        digest=(digest^check(r,raw,ns+ne,group))*UINT64_C(1099511628211);
    }
    char name[160];snprintf(name,sizeof(name),"%s/%s/%zu/%s/%s",label,group?"groups":"occurrences",n,symbols?"symbol":"integer",mode);
    CALLGRIND_DUMP_STATS_AT(name);CALLGRIND_ZERO_STATS;printf("%s,%u,%016" PRIx64 "\n",name,STEPS,digest);
    if(lease)OK(maelys_datalog_prepared_explanation_release(lease));free(explain_storage);
    if(g)OK(maelys_datalog_group_window_free(g));else OK(maelys_datalog_window_free(w));free(area);
    for(size_t i=0;i<2;++i){OK(maelys_datalog_session_inputs_free(handles[i]));free(storage[i]);OK(maelys_datalog_session_free(sessions[i]));}
    OK(maelys_datalog_policy_free(policy));
}
int main(int argc,char **argv) {
    if(argc!=2)return 2;for(size_t i=0;i<512;++i)snprintf(text[i],sizeof(text[i]),"s%04zu",i);
    const char *modes[]={"roll","shared","expire","replace","reject","noop"};puts("case,transactions,digest");
    for(int group=0;group<2;++group)for(int symbols=0;symbols<2;++symbols)for(size_t n=64;n<=256;n*=4)
        for(size_t mode=0;mode<sizeof(modes)/sizeof(*modes);++mode)fixture(argv[1],group,symbols,n,modes[mode]);
    return 0;
}
