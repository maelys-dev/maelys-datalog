/* SPDX-License-Identifier: MPL-2.0 */
/* Also compiled outside the tree against the installed static/shared SDK. */
#include <maelys/datalog_group_window.h>
#include <maelys/datalog_backend.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define OK(call) do { int rc_=(call); if(rc_) { fprintf(stderr,"%s:%d: %s -> %d\n",__FILE__,__LINE__,#call,rc_); abort(); } } while(0)
static maelys_datalog_public_value_t integer(int64_t x) {
    maelys_datalog_public_value_t v={.kind=MAELYS_DATALOG_VALUE_INTEGER}; v.as.integer=x; return v;
}
static maelys_datalog_public_value_t symbol(const char *x) {
    maelys_datalog_public_value_t v={.kind=MAELYS_DATALOG_VALUE_SYMBOL}; v.as.symbol=x; return v;
}
static maelys_datalog_public_value_t boolean(int x) {
    maelys_datalog_public_value_t v={.kind=MAELYS_DATALOG_VALUE_BOOLEAN}; v.as.boolean=x; return v;
}
static maelys_datalog_public_fact_t pair(const char *p,int64_t a,int64_t b) {
    maelys_datalog_public_fact_t f={.predicate=p,.arity=2}; f.terms[0]=integer(a); f.terms[1]=integer(b); return f;
}
static const maelys_datalog_public_predicate_t predicates[]={
    {"group",1,MAELYS_DATALOG_PREDICATE_POLICY_FACT},
    {"reading",2,MAELYS_DATALOG_PREDICATE_EDB},
    {"typed",1,MAELYS_DATALOG_PREDICATE_EDB},
    {"mixed",4,MAELYS_DATALOG_PREDICATE_EDB},
    {"edge",2,MAELYS_DATALOG_PREDICATE_EDB},
    {"block",1,MAELYS_DATALOG_PREDICATE_EDB},
    {"copy",2,MAELYS_DATALOG_PREDICATE_IDB|MAELYS_DATALOG_PREDICATE_QUERY},
    {"typed_copy",1,MAELYS_DATALOG_PREDICATE_IDB|MAELYS_DATALOG_PREDICATE_QUERY},
    {"mixed_copy",4,MAELYS_DATALOG_PREDICATE_IDB|MAELYS_DATALOG_PREDICATE_QUERY},
    {"total",1,MAELYS_DATALOG_PREDICATE_IDB|MAELYS_DATALOG_PREDICATE_QUERY},
    {"counted",1,MAELYS_DATALOG_PREDICATE_IDB|MAELYS_DATALOG_PREDICATE_QUERY},
    {"lowest",1,MAELYS_DATALOG_PREDICATE_IDB|MAELYS_DATALOG_PREDICATE_QUERY},
    {"highest",1,MAELYS_DATALOG_PREDICATE_IDB|MAELYS_DATALOG_PREDICATE_QUERY},
    {"allowed",1,MAELYS_DATALOG_PREDICATE_IDB|MAELYS_DATALOG_PREDICATE_QUERY},
    {"path",2,MAELYS_DATALOG_PREDICATE_IDB|MAELYS_DATALOG_PREDICATE_QUERY}
};
static const char *source="group(0). copy(I,V) :- reading(I,V). "
    "typed_copy(V) :- typed(V). mixed_copy(A,B,C,D) :- mixed(A,B,C,D). "
    "total(N) :- sum(V,reading(_,V),N). counted(N) :- count(V,reading(_,V),N). "
    "lowest(N) :- min(V,reading(_,V),N). highest(N) :- max(V,reading(_,V),N). "
    "allowed(G) :- group(G), not(block(G)). "
    "path(X,Y) :- edge(X,Y). path(X,Z) :- path(X,Y), edge(Y,Z).";

typedef struct { uint32_t id; maelys_datalog_public_fact_t *facts; size_t count; } model_group;
typedef struct {
    maelys_datalog_session_t *a,*b,*oracle;
    maelys_datalog_group_window_t *w;
    maelys_datalog_group_window_capacities_t caps;
    void *storage; size_t bytes,alignment;
    model_group *fifo; size_t count; uint64_t next;
} fixture;
static char *copy_text(const char *s) { size_t n=strlen(s)+1; char *p=malloc(n); assert(p); memcpy(p,s,n); return p; }
static void drop_group(model_group *g) {
    for(size_t i=0;i<g->count;++i) {
        free((void *)g->facts[i].predicate);
        for(size_t j=0;j<g->facts[i].arity;++j)
            if(g->facts[i].terms[j].kind==MAELYS_DATALOG_VALUE_SYMBOL) free((void *)g->facts[i].terms[j].as.symbol);
    }
    free(g->facts);
}
static model_group own_group(uint32_t id,const maelys_datalog_public_fact_t *facts,size_t n) {
    model_group g={id,calloc(n+1,sizeof(*facts)),n}; assert(g.facts);
    for(size_t i=0;i<n;++i) {
        g.facts[i]=facts[i]; g.facts[i].predicate=copy_text(facts[i].predicate);
        for(size_t j=0;j<facts[i].arity;++j) {
            maelys_datalog_public_value_t *v=&g.facts[i].terms[j];
            if(v->kind==MAELYS_DATALOG_VALUE_SYMBOL) v->as.symbol=copy_text(v->as.symbol);
            if(v->kind==MAELYS_DATALOG_VALUE_BOOLEAN) v->as.boolean=!!v->as.boolean;
        }
    }
    return g;
}
static void sessions(fixture *f,const char *rules) {
    memset(f,0,sizeof(*f)); maelys_datalog_policy_t *p=NULL;
    maelys_datalog_public_diagnostic_t d;
    int rc=maelys_datalog_policy_load_inline("group_window","group.window",rules,strlen(rules),&p,&d);
    if(rc) { fprintf(stderr,"load %d: %s\n",rc,d.message); abort(); }
    OK(maelys_datalog_session_create(p,0,&f->a)); OK(maelys_datalog_session_create(p,0,&f->b));
    OK(maelys_datalog_session_create(p,0,&f->oracle)); OK(maelys_datalog_policy_free(p));
}
static void init(fixture *f,size_t n,size_t c,size_t u,size_t text,uint32_t first) {
    f->caps=(maelys_datalog_group_window_capacities_t){n,c,u,text}; f->next=first;
    OK(maelys_datalog_group_window_storage_requirements(&f->caps,&f->bytes,&f->alignment));
    f->storage=malloc(f->bytes+f->alignment); assert(f->storage && (uintptr_t)f->storage%f->alignment==0);
    memset(f->storage,0xa5,f->bytes+f->alignment);
    f->fifo=calloc(n,sizeof(*f->fifo)); assert(f->fifo);
    OK(maelys_datalog_group_window_init(f->storage,f->bytes,&f->caps,first,f->a,f->b,&f->w,NULL));
}
static void close_fixture(fixture *f) {
    if(f->w) OK(maelys_datalog_group_window_free(f->w));
    for(size_t i=0;i<f->count;++i) drop_group(&f->fifo[i]);
    if(f->storage) for(size_t i=0;i<f->alignment;++i) assert(((unsigned char *)f->storage)[f->bytes+i]==0xa5);
    free(f->fifo); free(f->storage);
    if(f->a) OK(maelys_datalog_session_free(f->a));
    if(f->b) OK(maelys_datalog_session_free(f->b));
    OK(maelys_datalog_session_free(f->oracle));
}
static maelys_datalog_result_t *result(fixture *f) {
    maelys_datalog_result_t *r=NULL; OK(maelys_datalog_group_window_result(f->w,&r)); return r;
}
/* Deliberately independent O(C^2) set construction, no production comparator,
 * no sorting, no adapter views used to compute the expected snapshot. */
static int equal_fact(const maelys_datalog_public_fact_t *a,const maelys_datalog_public_fact_t *b) {
    if(a->arity!=b->arity || strcmp(a->predicate,b->predicate)) return 0;
    for(size_t j=0;j<a->arity;++j) {
        const maelys_datalog_public_value_t *x=&a->terms[j],*y=&b->terms[j];
        if(x->kind!=y->kind) return 0;
        switch(x->kind) {
            case MAELYS_DATALOG_VALUE_INTEGER: if(x->as.integer!=y->as.integer) return 0; break;
            case MAELYS_DATALOG_VALUE_BOOLEAN: if(!!x->as.boolean!=!!y->as.boolean) return 0; break;
            case MAELYS_DATALOG_VALUE_SYMBOL: if(strcmp(x->as.symbol,y->as.symbol)) return 0; break;
            default: abort();
        }
    }
    return 1;
}
static int same_result(const maelys_datalog_result_t *a, const maelys_datalog_result_t *b) {
    size_t na, nb, capacity;
    OK(maelys_datalog_result_derived_fact_count(a,&na));
    OK(maelys_datalog_result_derived_fact_count(b,&nb));
    if (na != nb) return 0;
    OK(maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_FACTS_PER_PRED,&capacity));
    maelys_datalog_public_fact_view_t *fa = calloc(capacity,sizeof(*fa)), *fb = calloc(capacity,sizeof(*fb));
    assert(fa && fb); int equal = 1;
    for (size_t p=0; p<sizeof(predicates)/sizeof(*predicates) && equal; ++p) {
        if (!(predicates[p].flags & MAELYS_DATALOG_PREDICATE_IDB)) continue;
        OK(maelys_datalog_result_enumerate(a,predicates[p].name,predicates[p].arity,fa,capacity,&na));
        OK(maelys_datalog_result_enumerate(b,predicates[p].name,predicates[p].arity,fb,capacity,&nb));
        if (na != nb) { equal=0; break; }
        for (size_t i=0; i<na && equal; ++i) {
            if (fa[i].arity!=fb[i].arity) { equal=0; break; }
            for (size_t j=0; j<fa[i].arity; ++j) {
                const maelys_datalog_public_term_view_t *x=&fa[i].terms[j], *y=&fb[i].terms[j];
                if (x->kind!=y->kind) { equal=0; break; }
                if (x->kind==MAELYS_DATALOG_VALUE_SYMBOL) {
                    const char *sa, *sb; size_t la,lb;
                    OK(maelys_datalog_result_symbol_text(a,x->as.symbol_id,&sa,&la));
                    OK(maelys_datalog_result_symbol_text(b,y->as.symbol_id,&sb,&lb));
                    if (x->as.symbol_id!=y->as.symbol_id || la!=lb || memcmp(sa,sb,la)) equal=0;
                } else if (x->kind==MAELYS_DATALOG_VALUE_INTEGER) {
                    if (x->as.integer!=y->as.integer) equal=0;
                } else if (x->as.boolean!=y->as.boolean) equal=0;
            }
        }
    }
    free(fa); free(fb); return equal;
}
static int oracle_matches(fixture *f) {
    size_t c=0,u=0,ng,nraw,nfacts;
    maelys_datalog_public_fact_t *set=calloc(f->caps.contributions,sizeof(*set)); assert(set);
    const maelys_datalog_event_group_t *groups;
    const maelys_datalog_public_fact_t *raw,*facts;
    OK(maelys_datalog_group_window_groups(f->w,&groups,&ng));
    OK(maelys_datalog_group_window_contributions(f->w,&raw,&nraw));
    OK(maelys_datalog_group_window_facts(f->w,&facts,&nfacts));
    int same=ng==f->count;
    for(size_t g=0;g<f->count;++g) {
        if(g>=ng || groups[g].id!=f->fifo[g].id || groups[g].fact_offset!=c || groups[g].fact_count!=f->fifo[g].count) same=0;
        for(size_t i=0;i<f->fifo[g].count;++i) {
            maelys_datalog_public_fact_t *v=&f->fifo[g].facts[i];
            if(c>=nraw || !equal_fact(v,&raw[c])) same=0;
            ++c; size_t j=0; for(;j<u;++j) if(equal_fact(v,&set[j])) break;
            if(j==u) set[u++]=*v;
        }
    }
    if(nraw!=c || nfacts!=u) same=0;
    for(size_t i=0;i<nfacts;++i) {
        size_t found=0;
        for(size_t j=0;j<u;++j) found+=equal_fact(&facts[i],&set[j]);
        if(found!=1) same=0;
        for(size_t j=0;j<i;++j) if(equal_fact(&facts[i],&facts[j])) same=0;
    }
    maelys_datalog_group_window_usage_t usage;
    OK(maelys_datalog_group_window_state(f->w,&usage));
    if(usage.groups!=f->count || usage.contributions!=c || usage.unique_facts!=u || usage.next_group!=f->next) same=0;
    maelys_datalog_result_t *expected=NULL;
    OK(maelys_datalog_session_solve(f->oracle,set,u,&expected,NULL));
    if(!same_result(result(f),expected)) same=0;
    OK(maelys_datalog_result_free(expected)); free(set); return same;
}
static void accept(fixture *f,const maelys_datalog_public_fact_t *facts,size_t n) {
    uint32_t id=UINT32_MAX;
    OK(maelys_datalog_group_window_push(f->w,facts,n,&id,NULL)); assert(id==f->next);
    if(f->count==f->caps.groups) { drop_group(f->fifo); --f->count; memmove(f->fifo,f->fifo+1,f->count*sizeof(*f->fifo)); }
    f->fifo[f->count++]=own_group(id,facts,n); ++f->next;
    assert(oracle_matches(f));
}
static void reject(fixture *f,const maelys_datalog_public_fact_t *facts,size_t n,int expected) {
    maelys_datalog_group_window_usage_t before={0},after={0};
    maelys_datalog_result_t *r=result(f); uint32_t id=123;
    OK(maelys_datalog_group_window_state(f->w,&before));
    maelys_datalog_public_diagnostic_t diag;
    int rc=maelys_datalog_group_window_push(f->w,facts,n,&id,&diag);
    if(rc!=expected) { fprintf(stderr,"reject: expected %d got %d: %s\n",expected,rc,diag.message); abort(); }
    OK(maelys_datalog_group_window_state(f->w,&after));
    assert(id==123 && result(f)==r && !memcmp(&before,&after,sizeof(before)));
    if(diag.source==MAELYS_DATALOG_DIAGNOSTIC_NONE || !diag.message[0]) { fprintf(stderr,"diagnostic: status %d, code %d, message %s, predicate %s\n",rc,diag.code,diag.message,n&&facts?facts[0].predicate:"<empty>"); abort(); }
    assert(oracle_matches(f));
}
static void query(fixture *f,const char *predicate,int64_t x,int expected) {
    maelys_datalog_public_value_t v=integer(x); int found=-1;
    OK(maelys_datalog_result_query(result(f),predicate,&v,1,&found)); assert(found==expected);
}
static void shared_and_empty(void) {
    fixture f; sessions(&f,source); init(&f,2,4,3,128,0); assert(oracle_matches(&f));
    maelys_datalog_public_fact_t a[]={pair("reading",1,5),pair("reading",2,5),pair("reading",1,5)};
    accept(&f,a,3); accept(&f,a,1); query(&f,"total",10,1); query(&f,"counted",1,1);
    maelys_datalog_public_fact_t bad=pair("reading",3,INT32_MAX);
    reject(&f,&bad,1,MAELYS_DATALOG_STATUS_INVALID_FIELD);
    accept(&f,NULL,0); query(&f,"total",5,1);
    accept(&f,NULL,0); query(&f,"total",0,1); query(&f,"lowest",0,0); query(&f,"highest",0,0);
    maelys_datalog_group_window_usage_t usage; OK(maelys_datalog_group_window_state(f.w,&usage)); assert(!usage.text_bytes);
    close_fixture(&f);
}
static void typed_and_permuted(void) {
    fixture f; sessions(&f,source); init(&f,2,20,16,512,0);
    char name[]="typed", text[]="1";
    maelys_datalog_public_fact_t a[6]={{.predicate=name,.arity=1},{.predicate="typed",.arity=1},
        {.predicate="typed",.arity=1},{.predicate="typed",.arity=1},
        {.predicate="mixed",.arity=4},{.predicate="block",.arity=1}};
    a[0].terms[0]=symbol(text); a[1].terms[0]=integer(1); a[2].terms[0]=boolean(7); a[3].terms[0]=boolean(-1);
    for(size_t i=0;i<4;++i) a[4].terms[i]=a[i].terms[0];
    a[5].terms[0]=integer(0);
    accept(&f,a,6); query(&f,"allowed",0,0);
    /* Returned success owns predicate and symbol bytes, including model copies. */
    name[0]='X'; text[0]='X'; assert(oracle_matches(&f));
    a[0].predicate="typed"; a[0].terms[0]=symbol("1");
    for(size_t i=0;i<3;++i) { maelys_datalog_public_fact_t t=a[i]; a[i]=a[5-i]; a[5-i]=t; }
    a[1].terms[0]=symbol("1"); /* mixed's first term used the mutated borrowed string. */
    accept(&f,a,6);
    maelys_datalog_public_fact_t wrong={.predicate="reading",.arity=1}; wrong.terms[0]=integer(0);
    reject(&f,&wrong,1,MAELYS_DATALOG_STATUS_INVALID_FIELD);
    wrong=pair("unknown",1,1); reject(&f,&wrong,1,MAELYS_DATALOG_STATUS_INVALID_FIELD);
    close_fixture(&f);
}
static void capacities(void) {
    fixture f; sessions(&f,source); init(&f,2,3,2,128,0);
    maelys_datalog_public_fact_t a[]={pair("reading",1,1),pair("reading",1,1)};
    accept(&f,a,2); accept(&f,a,1); accept(&f,a,2); /* At C=3: remove two, add two. */
    reject(&f,a,3,MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
    reject(&f,a,SIZE_MAX,MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
    reject(&f,NULL,1,MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    close_fixture(&f);
    sessions(&f,source); init(&f,2,4,1,128,0);
    accept(&f,a,1); a[1]=pair("reading",2,1);
    reject(&f,a,2,MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE); accept(&f,a,1); close_fixture(&f);
    size_t max,per; OK(maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_EDB_FACTS,&max));
    OK(maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_FACTS_PER_PRED,&per));
    maelys_datalog_public_fact_t *many=calloc(max+1,sizeof(*many)); assert(many);
    for(size_t i=0;i<=max;++i) many[i]=pair("reading",1,1);
    sessions(&f,source); init(&f,1,max,1,128,0); accept(&f,many,max); accept(&f,many,max);
    reject(&f,many,max+1,MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE); close_fixture(&f);
    sessions(&f,source); init(&f,1,per+1,per+1,128,0);
    for(size_t i=0;i<=per;++i) many[i]=pair("reading",(int64_t)i,1);
    reject(&f,many,per+1,MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE); accept(&f,many,1); close_fixture(&f); free(many);
    sessions(&f,source); init(&f,5,1,1,0,0);
    for(size_t i=0;i<9;++i) accept(&f,NULL,0); /* N is independent of C. */
    reject(&f,a,1,MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE); close_fixture(&f);
}
static void vocabulary_and_text(void) {
    fixture f; sessions(&f,source); init(&f,1,2,1,16,0);
    maelys_datalog_public_fact_t v[2]={{.predicate="typed",.arity=1},{.predicate="typed",.arity=1}};
    for(unsigned i=0;i<600;++i) {
        char text[16]; snprintf(text,sizeof(text),"s%u",i); v[0].terms[0]=v[1].terms[0]=symbol(text);
        accept(&f,v,2); memset(text,'x',strlen(text)); assert(oracle_matches(&f));
    }
    v[0].terms[0]=symbol("too-long-for-the-text-arena");
    reject(&f,v,1,MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
    accept(&f,NULL,0); v[0].terms[0]=symbol("ok"); accept(&f,v,1); close_fixture(&f);
}
static void leases_and_lifecycle(void) {
    fixture f; sessions(&f,source); init(&f,1,2,2,128,INT32_MAX);
    maelys_datalog_public_fact_t a=pair("reading",1,5); accept(&f,&a,1);
    reject(&f,NULL,0,MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
    close_fixture(&f);
    sessions(&f,source); init(&f,2,4,4,128,0); accept(&f,&a,1);
    size_t bytes,alignment;
    OK(maelys_datalog_session_explanation_storage_bound(f.a,MAELYS_DATALOG_EXPLAIN_TRUE,&bytes,&alignment));
    void *workspace=malloc(bytes); assert(workspace); maelys_datalog_prepared_explanation_t *e=NULL;
    maelys_datalog_public_value_t v=integer(5);
    OK(maelys_datalog_result_prepare_explanation(result(&f),MAELYS_DATALOG_EXPLAIN_TRUE,"total",&v,1,workspace,bytes,&e));
    reject(&f,&a,1,MAELYS_DATALOG_STATUS_INVALID_STATE);
    assert(maelys_datalog_group_window_free(f.w)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    assert(oracle_matches(&f)); OK(maelys_datalog_prepared_explanation_release(e)); free(workspace);
    accept(&f,&a,1); OK(maelys_datalog_group_window_free(f.w));
    OK(maelys_datalog_group_window_init(f.storage,f.bytes,&f.caps,7,f.a,f.b,&f.w,NULL));
    OK(maelys_datalog_group_window_free(f.w));
    OK(maelys_datalog_session_free(f.a)); f.a=NULL; OK(maelys_datalog_session_free(f.b)); f.b=NULL;
    maelys_datalog_result_t *r=NULL; const maelys_datalog_public_fact_t *facts=NULL;
    const maelys_datalog_event_group_t *groups=NULL; size_t count=99; uint32_t id=77;
    maelys_datalog_group_window_usage_t usage={0},before=usage;
    assert(maelys_datalog_group_window_push(f.w,&a,1,&id,NULL)==MAELYS_DATALOG_STATUS_INVALID_STATE && id==77);
    assert(maelys_datalog_group_window_result(f.w,&r)==MAELYS_DATALOG_STATUS_INVALID_STATE && !r);
    assert(maelys_datalog_group_window_groups(f.w,&groups,&count)==MAELYS_DATALOG_STATUS_INVALID_STATE && !groups && count==99);
    assert(maelys_datalog_group_window_contributions(f.w,&facts,&count)==MAELYS_DATALOG_STATUS_INVALID_STATE && !facts && count==99);
    assert(maelys_datalog_group_window_facts(f.w,&facts,&count)==MAELYS_DATALOG_STATUS_INVALID_STATE && !facts && count==99);
    assert(maelys_datalog_group_window_state(f.w,&usage)==MAELYS_DATALOG_STATUS_INVALID_STATE && !memcmp(&usage,&before,sizeof(usage)));
    assert(maelys_datalog_group_window_free(f.w)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    f.w=NULL; close_fixture(&f);
}
static void generated(void) {
    const uint32_t seeds[]={1,0x752abc91u,0xdeadbeefu};
    for(size_t seed=0;seed<3;++seed) {
        fixture f; sessions(&f,source); init(&f,4,20,20,512,0); uint32_t random=seeds[seed];
        for(unsigned step=0;step<160;++step) {
            maelys_datalog_public_fact_t batch[5]; random=random*1664525u+1013904223u; size_t n=random%6;
            for(size_t i=0;i<n;++i) {
                random=random*1664525u+1013904223u; unsigned v=random%8;
                if(v<4) batch[i]=pair("reading",v,v+1);
                else if(v<6) batch[i]=pair("edge",v-4,v-3);
                else { batch[i]=(maelys_datalog_public_fact_t){.predicate=v==6?"block":"typed",.arity=1}; batch[i].terms[0]=v==6?integer(0):symbol("shared"); }
            }
            accept(&f,batch,n);
        }
        close_fixture(&f);
    }
    fixture f; sessions(&f,source); init(&f,2,4,4,64,0);
    maelys_datalog_public_fact_t a=pair("reading",1,5); accept(&f,&a,1);
    f.fifo[0].facts[0].terms[1]=integer(6); assert(!oracle_matches(&f)); f.fifo[0].facts[0].terms[1]=integer(5);
    ++f.fifo[0].id; assert(!oracle_matches(&f)); --f.fifo[0].id;
    f.count=0; assert(!oracle_matches(&f)); f.count=1; assert(oracle_matches(&f)); close_fixture(&f);
}
static void admission(void) {
    fixture f; sessions(&f,source);
    maelys_datalog_group_window_capacities_t c={2,4,3,64}; size_t bytes,alignment;
    OK(maelys_datalog_group_window_storage_requirements(&c,&bytes,&alignment));
    void *storage=malloc(bytes+alignment); assert(storage);
    assert(maelys_datalog_group_window_init(storage,bytes-1,&c,0,f.a,f.b,&f.w,NULL)==MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL && !f.w);
    assert(maelys_datalog_group_window_init((char *)storage+1,bytes,&c,0,f.a,f.b,&f.w,NULL)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    assert(maelys_datalog_group_window_init(storage,SIZE_MAX,&c,0,f.a,f.b,&f.w,NULL)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    assert(maelys_datalog_group_window_init(storage,bytes,&c,UINT32_MAX,f.a,f.b,&f.w,NULL)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    assert(maelys_datalog_group_window_init(storage,bytes,&c,0,f.a,f.a,&f.w,NULL)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    maelys_datalog_result_t *lease=NULL;
    for(unsigned i=0;i<2;++i) {
        OK(maelys_datalog_session_solve(i?f.b:f.a,NULL,0,&lease,NULL));
        assert(maelys_datalog_group_window_init(storage,bytes,&c,0,f.a,f.b,&f.w,NULL)==MAELYS_DATALOG_STATUS_INVALID_STATE && !f.w);
        OK(maelys_datalog_result_free(lease));
    }
    fixture other; sessions(&other,"copy(I,V) :- reading(I,V).");
    assert(maelys_datalog_group_window_init(storage,bytes,&c,0,f.a,other.b,&f.w,NULL)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    close_fixture(&other);
    for(unsigned which=0;which<6;++which) {
        maelys_datalog_group_window_capacities_t bad=c;
        if(which==0) bad.groups=0;
        if(which==1) bad.groups=SIZE_MAX;
        if(which==2) bad.contributions=SIZE_MAX;
        if(which==3) bad.unique_facts=0;
        if(which==4) bad.unique_facts=bad.contributions+1;
        if(which==5) bad.text_bytes=SIZE_MAX;
        size_t a=123,b=456;
        assert(maelys_datalog_group_window_storage_requirements(&bad,&a,&b)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
        assert(a==123 && b==456);
    }
    close_fixture(&f); free(storage);
    sessions(&f,"group(2147483647). group(1). total(N) :- sum(V,group(V),N).");
    storage=malloc(bytes); assert(storage);
    assert(maelys_datalog_group_window_init(storage,bytes,&c,0,f.a,f.b,&f.w,NULL)==MAELYS_DATALOG_STATUS_INVALID_FIELD && !f.w);
    free(storage); close_fixture(&f);
}
static unsigned fault_phase,reentries;
static maelys_datalog_group_window_t *callback_window;
static maelys_datalog_status_t injected_solve(void *state,const maelys_datalog_public_fact_t *facts,
    size_t count,maelys_datalog_backend_output_t *out,void **r,maelys_datalog_public_diagnostic_t *diag) {
    if(callback_window) {
        maelys_datalog_group_window_usage_t u;
        assert(maelys_datalog_group_window_state(callback_window,&u)==MAELYS_DATALOG_STATUS_INVALID_STATE);
        assert(maelys_datalog_group_window_push(callback_window,NULL,0,NULL,NULL)==MAELYS_DATALOG_STATUS_INVALID_STATE);
        assert(maelys_datalog_group_window_free(callback_window)==MAELYS_DATALOG_STATUS_INVALID_STATE); ++reentries;
    }
    maelys_datalog_status_t rc=0;
    if(fault_phase!=1) rc=maelys_datalog_backend_reference()->solve(state,facts,count,out,r,diag);
    if(!rc && fault_phase) {
        if(diag) { diag->code=MAELYS_DATALOG_STATUS_INTERNAL; strcpy(diag->message,"Injected backend failure"); }
        return MAELYS_DATALOG_STATUS_INTERNAL;
    }
    return rc;
}
static void backend_failures(void) {
    fixture f; sessions(&f,source); maelys_datalog_policy_t *p=NULL;
    OK(maelys_datalog_policy_load_inline("group_window","group.window",source,strlen(source),&p,NULL));
    maelys_datalog_backend_t backend=*maelys_datalog_backend_reference();
    backend.name="group_window_fault"; backend.semantic_id="group.window.fault.v1"; backend.solve=injected_solve;
    maelys_datalog_session_options_t options={MAELYS_DATALOG_BACKEND_ABI_VERSION,sizeof(options),&backend,0,0};
    OK(maelys_datalog_session_free(f.a)); OK(maelys_datalog_session_free(f.b));
    OK(maelys_datalog_session_create_ex(p,0,&options,&f.a)); OK(maelys_datalog_session_create_ex(p,0,&options,&f.b));
    OK(maelys_datalog_policy_free(p)); init(&f,1,2,2,128,0); callback_window=f.w;
    maelys_datalog_public_fact_t a=pair("reading",1,5); accept(&f,&a,1);
    for(fault_phase=1;fault_phase<=2;++fault_phase) reject(&f,NULL,0,MAELYS_DATALOG_STATUS_INTERNAL);
    fault_phase=0; accept(&f,NULL,0); assert(reentries==4); callback_window=NULL; close_fixture(&f);
}
int main(void) {
    maelys_datalog_public_domain_t domain={"group_window",predicates,sizeof(predicates)/sizeof(*predicates),NULL,0};
    OK(maelys_datalog_domain_register(&domain));
    admission(); shared_and_empty(); typed_and_permuted(); capacities();
    vocabulary_and_text(); leases_and_lifecycle(); generated(); backend_failures();
    puts("multi-fact window: typed FIFO oracle, shared facts, empty groups, atomic failures, budgets and lifecycle PASS");
    return 0;
}
