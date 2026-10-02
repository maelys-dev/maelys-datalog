/* SPDX-License-Identifier: MPL-2.0 */
/* Native operations corresponding to python_workload.py's 7/93 symbol fixtures.
 * Counts include input reset/append, solve, query and release; preparation and
 * full output validation are excluded. These are not Python or latency results. */
#include <maelys/datalog.h>
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef ALLOCATION_NATIVE_SMOKE
#include <valgrind/callgrind.h>
#else
#define CALLGRIND_ZERO_STATS ((void)0)
#define CALLGRIND_TOGGLE_COLLECT ((void)0)
#define CALLGRIND_DUMP_STATS_AT(x) ((void)(x))
#endif
#define OK(x) do{int rc_=(x);if(rc_){fprintf(stderr,"%d %s: %d\n",__LINE__,#x,rc_);abort();}}while(0)
#define STEPS 200
static maelys_datalog_value_t symbol(const char *x) {return (maelys_datalog_value_t){.kind=MAELYS_DATALOG_VALUE_SYMBOL,.as.symbol=x};}
static maelys_datalog_fact_t fact(const char *p,const char *x,const char *y) {
    maelys_datalog_fact_t f={.predicate=p,.arity=y?2:1};f.terms[0]=symbol(x);if(y)f.terms[1]=symbol(y);return f;
}
static uint64_t mix(uint64_t h,uint64_t v){return (h^v)*UINT64_C(1099511628211);}
static uint64_t verify(maelys_datalog_result_t *r,int large,char users[30][16],char docs[30][16]) {
    const char *names[]={"has_any_document","allow"};size_t total;OK(maelys_datalog_result_derived_fact_count(r,&total));assert(total==(large?138:6));
    uint64_t h=UINT64_C(14695981039346656037);
    for(size_t p=0;p<2;++p) {
        maelys_datalog_fact_view_t v[64];size_t n;OK(maelys_datalog_result_enumerate(r,names[p],p==0?1:2,v,64,&n));
        assert(n==(large?(p==0?30:54):2));h=mix(h,n);
        for(size_t j=0;j<n;++j) {
            const char *u,*d=NULL;size_t bytes;OK(maelys_datalog_result_symbol_text(r,v[j].terms[0].as.symbol_id,&u,&bytes));
            assert(v[j].terms[0].kind==MAELYS_DATALOG_VALUE_SYMBOL);
            size_t at=0;while(at<(large?30u:3u) && strcmp(u,users[at]))++at;assert(at<(large?30u:3u));
            if(p==0)assert(large || at!=1);
            else {assert(v[j].terms[1].kind==MAELYS_DATALOG_VALUE_SYMBOL);OK(maelys_datalog_result_symbol_text(r,v[j].terms[1].as.symbol_id,&d,&bytes));
                assert(large?at%10!=0:at!=2);assert(!strcmp(d,docs[large?at:0]) || (large && !strcmp(d,docs[(at+1)%30])));}
            h=mix(h,p);h=mix(h,v[j].terms[0].as.symbol_id);
            for(const char *q=u;*q;++q)h=mix(h,(unsigned char)*q);
            if(d){h=mix(h,v[j].terms[1].as.symbol_id);for(const char *q=d;*q;++q)h=mix(h,(unsigned char)*q);}
        }
    }
    return h;
}
static void run(const char *label,int large,int convenience) {
    const maelys_datalog_predicate_t p[]={MAELYS_DATALOG_EDB("user",1),MAELYS_DATALOG_EDB("owns",2),MAELYS_DATALOG_EDB("delegated",2),MAELYS_DATALOG_EDB("blocked",1),MAELYS_DATALOG_IDB("can_read",2),MAELYS_DATALOG_IDB_QUERY("has_any_document",1),MAELYS_DATALOG_IDB_QUERY("allow",2)};
    const maelys_datalog_domain_t domain={"allocation_fixed",p,7,NULL,0};static int registered;
    if(!registered){OK(maelys_datalog_domain_register(&domain));registered=1;}
    const char *source="can_read(User, Doc) :- owns(User, Doc) or delegated(User, Doc), not(blocked(User)).\nhas_any_document(User) :- owns(User, _).\nallow(User, Doc) :- user(User), can_read(User, Doc).\n";
    maelys_datalog_policy_t *policy;OK(maelys_datalog_policy_load_inline(domain.name,"perf",source,strlen(source),&policy,NULL));
    char users[30][16],docs[30][16];for(size_t i=0;i<30;++i){snprintf(users[i],16,"user%zu",i);snprintf(docs[i],16,"doc%zu.pdf",i);}
    maelys_datalog_fact_t f[93];size_t n=0;
    if(large)for(size_t i=0;i<30;++i){f[n++]=fact("user",users[i],NULL);f[n++]=fact("owns",users[i],docs[i]);f[n++]=fact("delegated",users[i],docs[(i+1)%30]);if(i%10==0)f[n++]=fact("blocked",users[i],NULL);}
    else {for(size_t i=0;i<3;++i)f[n++]=fact("user",users[i],NULL);f[n++]=fact("owns",users[0],docs[0]);f[n++]=fact("delegated",users[1],docs[0]);f[n++]=fact("owns",users[2],docs[0]);f[n++]=fact("blocked",users[2],NULL);}
    maelys_datalog_input_edb_t *edb=NULL;maelys_datalog_session_t *s=NULL;
    if(!convenience){OK(maelys_datalog_input_edb_create(&edb));OK(maelys_datalog_session_create(policy,0,&s));}
    uint64_t digest=0;int answers[5];
    CALLGRIND_ZERO_STATS;
    for(size_t tx=0;tx<STEPS+50;++tx) {
        if(tx>=50)CALLGRIND_TOGGLE_COLLECT;
        if(convenience){OK(maelys_datalog_input_edb_create(&edb));OK(maelys_datalog_session_create(policy,0,&s));}
        else OK(maelys_datalog_input_edb_clear(edb));
        for(size_t i=0;i<n;++i)OK(maelys_datalog_input_edb_add_facts(edb,&f[i],1,NULL));
        maelys_datalog_result_t *r;OK(maelys_datalog_session_solve_edb(s,edb,&r,NULL));
        if(large) {
            const size_t q[]={0,1,10,29};for(size_t i=0;i<4;++i){maelys_datalog_value_t t[]={symbol(users[q[i]]),symbol(docs[q[i]])};OK(maelys_datalog_result_query(r,"allow",t,2,&answers[i]));}
        } else {
            for(size_t i=0;i<3;++i){maelys_datalog_value_t t[]={symbol(users[i]),symbol(docs[0])};OK(maelys_datalog_result_query(r,"allow",t,2,&answers[i]));}
            for(size_t i=0;i<2;++i){maelys_datalog_value_t t=symbol(users[i]);OK(maelys_datalog_result_query(r,"has_any_document",&t,1,&answers[3+i]));}
        }
        if(tx>=50)CALLGRIND_TOGGLE_COLLECT;
        if(large)assert(!answers[0] && answers[1] && !answers[2] && answers[3]);else assert(answers[0] && answers[1] && !answers[2] && answers[3] && !answers[4]);
        uint64_t h=verify(r,large,users,docs);if(tx>=50)digest=mix(digest,h);
        if(tx>=50)CALLGRIND_TOGGLE_COLLECT;
        OK(maelys_datalog_result_free(r));
        if(convenience){OK(maelys_datalog_session_free(s));OK(maelys_datalog_input_edb_free(edb));}
        if(tx>=50)CALLGRIND_TOGGLE_COLLECT;
    }
    char region[128];snprintf(region,sizeof(region),"%s/fixed/%d-symbol/%s",label,large?93:7,convenience?"convenience":"prepared");CALLGRIND_DUMP_STATS_AT(region);CALLGRIND_ZERO_STATS;
    printf("%s,%u,%" PRIu64 "\n",region,STEPS,digest);
    if(!convenience){OK(maelys_datalog_session_free(s));OK(maelys_datalog_input_edb_free(edb));}OK(maelys_datalog_policy_free(policy));
}
int main(int argc,char **argv){if(argc!=2)return 2;puts("case,transactions,digest");for(int n=0;n<2;++n)for(int c=0;c<2;++c)run(argv[1],n,c);return 0;}
