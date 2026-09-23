/* SPDX-License-Identifier: MPL-2.0 */
/* Also compiled outside the tree against the installed static/shared SDK. */
#include <maelys/datalog_window.h>
#include <maelys/datalog_backend.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OK(call) do { int rc_ = (call); if (rc_) { fprintf(stderr, "%s:%d: %s -> %d\n", __FILE__, __LINE__, #call, rc_); abort(); } } while (0)
static maelys_datalog_public_value_t integer(int64_t n) {
    maelys_datalog_public_value_t v = {.kind = MAELYS_DATALOG_VALUE_INTEGER}; v.as.integer = n; return v;
}
static maelys_datalog_public_value_t symbol(const char *s) {
    maelys_datalog_public_value_t v = {.kind = MAELYS_DATALOG_VALUE_SYMBOL}; v.as.symbol = s; return v;
}
static const maelys_datalog_public_predicate_t predicates[] = {
    {"group",1,MAELYS_DATALOG_PREDICATE_POLICY_FACT},
    {"event",3,MAELYS_DATALOG_PREDICATE_EDB},
    {"block",2,MAELYS_DATALOG_PREDICATE_EDB},
    {"edge",3,MAELYS_DATALOG_PREDICATE_EDB},
    {"tick",1,MAELYS_DATALOG_PREDICATE_EDB},
    {"mixed",4,MAELYS_DATALOG_PREDICATE_EDB},
    {"mixed_copy",4,MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY},
    {"copy",3,MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY},
    {"counted",2,MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY},
    {"distinct",2,MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY},
    {"summed",2,MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY},
    {"lowest",2,MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY},
    {"highest",2,MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY},
    {"blocked",1,MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY},
    {"allowed",1,MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY},
    {"path",2,MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY}
};
static const char *source =
    "group(0). group(1). copy(I,G,V) :- event(I,G,V). "
    "counted(G,N) :- group(G), count(I,event(I,G,_),N). "
    "distinct(G,N) :- group(G), count(V,event(_,G,V),N). "
    "summed(G,N) :- group(G), sum(V,event(_,G,V),N). "
    "lowest(G,N) :- group(G), min(V,event(_,G,V),N). "
    "highest(G,N) :- group(G), max(V,event(_,G,V),N). "
    "blocked(G) :- block(_,G). allowed(G) :- group(G), not(blocked(G)). "
    "path(X,Y) :- edge(_,X,Y). path(X,Z) :- path(X,Y), edge(_,Y,Z).";
typedef struct {
    maelys_datalog_session_t *a, *b, *oracle;
    maelys_datalog_window_t *w;
    unsigned char *storage;
    size_t bytes, alignment;
} fixture;
static void sessions(fixture *f, const char *s) {
    memset(f, 0, sizeof(*f));
    maelys_datalog_policy_t *p = NULL;
    maelys_datalog_public_diagnostic_t d;
    int rc = maelys_datalog_policy_load_inline("window", "window.test", s, strlen(s), &p, &d);
    if (rc) { fprintf(stderr, "load: %d %s\n", rc, d.message); abort(); }
    OK(maelys_datalog_session_create(p, 0, &f->a));
    OK(maelys_datalog_session_create(p, 0, &f->b));
    OK(maelys_datalog_session_create(p, 0, &f->oracle));
    OK(maelys_datalog_policy_free(p));
}
static void init(fixture *f, size_t n, size_t text, uint32_t first) {
    OK(maelys_datalog_window_storage_requirements(n,text,&f->bytes,&f->alignment));
    f->storage = malloc(f->bytes + f->alignment);
    assert(f->storage && (uintptr_t)f->storage % f->alignment == 0);
    memset(f->storage, 0xa5, f->bytes + f->alignment);
    OK(maelys_datalog_window_init(f->storage,f->bytes,n,text,first,f->a,f->b,&f->w,NULL));
}
static void close_fixture(fixture *f) {
    if (f->w) OK(maelys_datalog_window_free(f->w));
    if (f->storage) for (size_t i = 0; i < f->alignment; ++i) assert(f->storage[f->bytes+i] == 0xa5);
    OK(maelys_datalog_session_free(f->a));
    OK(maelys_datalog_session_free(f->b));
    OK(maelys_datalog_session_free(f->oracle));
    free(f->storage);
}
static maelys_datalog_result_t *result(fixture *f) {
    maelys_datalog_result_t *r = NULL; OK(maelys_datalog_window_result(f->w,&r)); return r;
}
static void state(fixture *f, size_t n, uint64_t next) {
    size_t actual; uint64_t cursor;
    OK(maelys_datalog_window_state(f->w,&actual,&cursor)); assert(actual==n && cursor==next);
}
static void query(fixture *f, const char *p, size_t n, int64_t a, int64_t b, int expected) {
    maelys_datalog_public_value_t v[] = {integer(a),integer(b)}; int present = -1;
    OK(maelys_datalog_result_query(result(f),p,v,n,&present)); assert(present==expected);
}
/* Compare all IDB, canonical IDs AND resolved symbols. No input from the window
 * is used to construct the oracle; its FIFO is maintained by each caller. */
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
static void oracle(fixture *f, const maelys_datalog_public_fact_t *facts, size_t count) {
    maelys_datalog_result_t *r=NULL;
    OK(maelys_datalog_session_solve(f->oracle,facts,count,&r,NULL));
    assert(same_result(result(f),r)); OK(maelys_datalog_result_free(r));
}
static void push(fixture *f, const char *p, int64_t a, int64_t b, size_t n, uint32_t id) {
    maelys_datalog_public_value_t values[]={integer(a),integer(b)};
    uint32_t got=UINT32_MAX;
    OK(maelys_datalog_window_push(f->w,p,values,n,&got,NULL)); assert(got==id);
}
static void rejected(fixture *f, const char *p, const maelys_datalog_public_value_t *v,
    size_t n, maelys_datalog_status_t expected) {
    const maelys_datalog_public_fact_t *before, *after; size_t count, after_count;
    uint64_t next, after_next; maelys_datalog_result_t *old=result(f);
    size_t used, capacity, after_used, after_capacity;
    OK(maelys_datalog_window_text_usage(f->w,&used,&capacity));
    OK(maelys_datalog_window_events(f->w,&before,&count));
    maelys_datalog_public_fact_t *copy=malloc((count+1)*sizeof(*copy)); assert(copy);
    memcpy(copy,before,count*sizeof(*copy));
    OK(maelys_datalog_window_state(f->w,&count,&next));
    uint32_t got=UINT32_MAX; maelys_datalog_public_diagnostic_t d;
    assert(maelys_datalog_window_push(f->w,p,v,n,&got,&d)==expected);
    assert(got==UINT32_MAX && d.source==MAELYS_DATALOG_DIAGNOSTIC_SOLVE && d.message[0]);
    OK(maelys_datalog_window_events(f->w,&after,&after_count));
    OK(maelys_datalog_window_state(f->w,&after_count,&after_next));
    assert(old==result(f) && before==after && count==after_count && next==after_next);
    assert(!memcmp(copy,after,count*sizeof(*copy)));
    OK(maelys_datalog_window_text_usage(f->w,&after_used,&after_capacity));
    assert(used==after_used && capacity==after_capacity);
    oracle(f,copy,count); free(copy);
}
static void aggregates_and_rejections(void) {
    fixture f; sessions(&f,source); init(&f,3,128,0);
    state(&f,0,0); query(&f,"summed",2,0,0,1); query(&f,"lowest",2,0,0,0);
    push(&f,"event",0,5,2,0); push(&f,"event",0,5,2,1); push(&f,"event",0,9,2,2);
    state(&f,3,3); query(&f,"summed",2,0,19,1); query(&f,"counted",2,0,3,1);
    query(&f,"distinct",2,0,2,1); query(&f,"highest",2,0,9,1);
    maelys_datalog_public_value_t v[]={integer(0),integer(INT32_MAX)};
    rejected(&f,"event",v,2,MAELYS_DATALOG_STATUS_INVALID_FIELD);
    v[1]=integer(-1); rejected(&f,"event",v,2,MAELYS_DATALOG_STATUS_INVALID_FIELD);
    v[1]=integer(INT64_MAX); rejected(&f,"event",v,2,MAELYS_DATALOG_STATUS_INVALID_FIELD);
    v[1]=integer(7); rejected(&f,"missing",v,2,MAELYS_DATALOG_STATUS_INVALID_FIELD);
    rejected(&f,"event",v,1,MAELYS_DATALOG_STATUS_INVALID_FIELD);
    rejected(&f,"event",v,4,MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    v[1].kind=(maelys_datalog_value_kind_t)999;
    rejected(&f,"event",v,2,MAELYS_DATALOG_STATUS_INVALID_FIELD);
    v[1]=symbol(NULL); rejected(&f,"event",v,2,MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    push(&f,"block",0,0,1,3); query(&f,"summed",2,0,14,1); query(&f,"allowed",1,0,0,0);
    push(&f,"tick",0,0,0,4); query(&f,"lowest",2,0,9,1);
    push(&f,"tick",0,0,0,5); query(&f,"highest",2,0,9,0); query(&f,"summed",2,0,0,1);
    push(&f,"tick",0,0,0,6); query(&f,"allowed",1,0,0,1);
    close_fixture(&f);
}
static void directed_limits(void) {
    fixture f; sessions(&f,source); init(&f,1,6,INT32_MAX);
    /* Exactly enough interned text for event plus its NUL. */
    push(&f,"event",0,7,2,INT32_MAX); state(&f,1,(uint64_t)INT32_MAX+1);
    maelys_datalog_public_value_t v[]={integer(0),integer(8)};
    rejected(&f,"event",v,2,MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE); close_fixture(&f);
    sessions(&f,"copy(I,G,V) :- event(I,G,V)."); init(&f,1,8,10);
    maelys_datalog_public_value_t s[]={integer(0),symbol("x")};
    OK(maelys_datalog_window_push(f.w,"event",s,2,NULL,NULL));
    s[1]=symbol("xxx"); rejected(&f,"event",s,2,MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
    s[1]=symbol("y"); OK(maelys_datalog_window_push(f.w,"event",s,2,NULL,NULL));
    state(&f,1,12); close_fixture(&f);
    size_t max; OK(maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_FACTS_PER_PRED,&max));
    sessions(&f,source); init(&f,max+1,128,0);
    for (size_t i=0;i<max;++i) push(&f,"event",0,1,2,(uint32_t)i);
    rejected(&f,"event",v,2,MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
    push(&f,"tick",0,0,0,(uint32_t)max); state(&f,max+1,max+1);
    push(&f,"event",0,1,2,(uint32_t)max+1); close_fixture(&f);
}
static void explanations(void) {
    fixture f; sessions(&f,source); init(&f,1,64,0); push(&f,"event",0,5,2,0);
    maelys_datalog_public_value_t q[]={integer(0),integer(5)};
    size_t bytes,alignment,required;
    OK(maelys_datalog_result_explanation_storage_requirements(result(&f),MAELYS_DATALOG_EXPLAIN_TRUE,&bytes,&alignment));
    void *workspace=malloc(bytes); assert(workspace && (uintptr_t)workspace%alignment==0);
    maelys_datalog_prepared_explanation_t *e=NULL;
    OK(maelys_datalog_result_prepare_explanation(result(&f),MAELYS_DATALOG_EXPLAIN_TRUE,"summed",q,2,workspace,bytes,&e));
    OK(maelys_datalog_prepared_explanation_text_size(e,&required));
    char *text=malloc(required+1), *again=malloc(required+1); assert(text && again);
    OK(maelys_datalog_prepared_explanation_write_text(e,text,required+1));
    maelys_datalog_public_value_t v[]={integer(0),integer(7)};
    rejected(&f,"event",v,2,MAELYS_DATALOG_STATUS_INVALID_STATE);
    assert(maelys_datalog_window_free(f.w)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    OK(maelys_datalog_prepared_explanation_write_text(e,again,required+1)); assert(!strcmp(text,again));
    OK(maelys_datalog_prepared_explanation_release(e));
    push(&f,"event",0,7,2,1); query(&f,"summed",2,0,7,1);
    free(text); free(again); free(workspace); close_fixture(&f);
}
static void generated_sequences(void) {
    fixture f; sessions(&f,source); init(&f,7,128,0);
    maelys_datalog_public_fact_t fifo[7]={0}; size_t used=0; uint32_t seed=0x19750322u;
    for (uint32_t id=0;id<600;++id) {
        seed=seed*1664525u+1013904223u;
        unsigned kind=seed%8u; const char *p=kind==0?"block":kind==1?"edge":"event";
        size_t arity=kind==0?2:3;
        maelys_datalog_public_value_t v[]={integer((seed>>8)%2),integer((seed>>16)%7)};
        if (kind==1) { v[0]=integer((seed>>8)%3); v[1]=integer(v[0].as.integer+1); }
        uint32_t got=UINT32_MAX;
        OK(maelys_datalog_window_push(f.w,p,v,arity-1,&got,NULL)); assert(got==id);
        if (used==7) { memmove(fifo,fifo+1,6*sizeof(*fifo)); --used; }
        fifo[used]=(maelys_datalog_public_fact_t){.predicate=p,.arity=arity};
        fifo[used].terms[0]=integer(id); fifo[used].terms[1]=v[0]; if(arity==3)fifo[used].terms[2]=v[1]; ++used;
        oracle(&f,fifo,used);
        if (id%37==0) {
            maelys_datalog_public_value_t bad[]={integer(0),integer(INT32_MAX)};
            /* Guaranteed failure: wrong arity, including at a full window. */
            rejected(&f,"event",bad,1,MAELYS_DATALOG_STATUS_INVALID_FIELD);
        }
    }
    /* Negative control: an incorrect source tuple must be detected. */
    maelys_datalog_result_t *wrong=NULL;
    maelys_datalog_public_fact_t bad={.predicate="event",.arity=3,.terms={integer(999),integer(0),integer(123)}};
    OK(maelys_datalog_session_solve(f.oracle,&bad,1,&wrong,NULL));
    assert(!same_result(result(&f),wrong)); OK(maelys_datalog_result_free(wrong));
    close_fixture(&f);
}
static void full_arity_typed_events(void) {
    fixture f; sessions(&f,"mixed_copy(I,A,B,C) :- mixed(I,A,B,C). "
        "distinct(0,N) :- count(V,mixed(_,V,_,_),N).");
    init(&f,3,64,0);
    maelys_datalog_public_value_t values[]={integer(1),integer(2),symbol("1")};
    maelys_datalog_public_fact_t fifo[3]={0};
    for(uint32_t i=0;i<3;++i) {
        if(i==1) { values[0].kind=MAELYS_DATALOG_VALUE_BOOLEAN; values[0].as.boolean=27; }
        if(i==2) values[0]=symbol("1");
        OK(maelys_datalog_window_push(f.w,"mixed",values,3,NULL,NULL));
        fifo[i]=(maelys_datalog_public_fact_t){.predicate="mixed",.arity=4,
            .terms={integer(i),values[0],values[1],values[2]}};
    }
    oracle(&f,fifo,3); query(&f,"distinct",2,0,3,1);
    const maelys_datalog_public_fact_t *events; size_t count;
    OK(maelys_datalog_window_events(f.w,&events,&count));
    assert(count==3 && events[1].terms[1].kind==MAELYS_DATALOG_VALUE_BOOLEAN && events[1].terms[1].as.boolean==1);
    close_fixture(&f);
}
static void text_occupancy(void) {
    fixture f; sessions(&f,"copy(I,G,V) :- event(I,G,V)."); init(&f,2,9,0);
    size_t used=99, capacity=88;
    OK(maelys_datalog_window_text_usage(f.w,&used,&capacity)); assert(used==0 && capacity==9);
    maelys_datalog_public_value_t v[]={integer(0),symbol("xx")};
    OK(maelys_datalog_window_push(f.w,"event",v,2,NULL,NULL));
    /* event + NUL = 6, xx + NUL = 3: text fills before N does. */
    state(&f,1,1);
    OK(maelys_datalog_window_text_usage(f.w,&used,&capacity)); assert(used==9 && capacity==9);
    v[1]=symbol("yy"); rejected(&f,"event",v,2,MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
    v[1]=symbol("xx"); OK(maelys_datalog_window_push(f.w,"event",v,2,NULL,NULL));
    state(&f,2,2);
    OK(maelys_datalog_window_text_usage(f.w,&used,&capacity)); assert(used==9 && capacity==9);
    push(&f,"event",0,7,2,2);  /* One reference to xx remains. */
    OK(maelys_datalog_window_text_usage(f.w,&used,&capacity)); assert(used==9);
    push(&f,"event",0,8,2,3);  /* Last reference expires. */
    OK(maelys_datalog_window_text_usage(f.w,&used,&capacity)); assert(used==6 && capacity==9);
    assert(maelys_datalog_window_text_usage(NULL,&used,&capacity)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    assert(used==6 && capacity==9);
    assert(maelys_datalog_window_text_usage(f.w,NULL,&capacity)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT && capacity==9);
    assert(maelys_datalog_window_text_usage(f.w,&used,NULL)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT && used==6);
    close_fixture(&f);
}
static void renewable_snapshot_vocabulary(void) {
    fixture f; sessions(&f,"copy(I,G,V) :- event(I,G,V)."); init(&f,3,128,0);
    size_t max; OK(maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_SYMBOLS,&max));
    char strings[3][32]; maelys_datalog_public_fact_t fifo[3]={0}; size_t used=0;
    for (uint32_t id=0;id<2*max+3;++id) {
        char incoming[32]; snprintf(incoming,sizeof(incoming),"symbol_%u",id);
        maelys_datalog_public_value_t v[]={integer(0),symbol(incoming)};
        OK(maelys_datalog_window_push(f.w,"event",v,2,NULL,NULL));
        if (used==3) { memmove(strings,strings+1,2*sizeof(*strings)); --used; }
        strcpy(strings[used++],incoming);
        for(size_t i=0;i<used;++i) fifo[i]=(maelys_datalog_public_fact_t){.predicate="event",.arity=3,
            .terms={integer(id-used+1+i),integer(0),symbol(strings[i])}};
        memset(incoming,'!',strlen(incoming)); /* Input strings were copied. */
        oracle(&f,fifo,used);
    }
    close_fixture(&f);
}
static void admission(void) {
    fixture f; sessions(&f,source);
    size_t bytes=123,alignment=456,max,text;
    OK(maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_EDB_FACTS,&max));
    OK(maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_INPUT_EDB_TEXT_BYTES,&text));
    assert(maelys_datalog_window_storage_requirements(0,0,&bytes,&alignment)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    assert(bytes==123 && alignment==456);
    assert(maelys_datalog_window_storage_requirements(max+1,0,&bytes,&alignment)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    assert(maelys_datalog_window_storage_requirements(1,text+1,&bytes,&alignment)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    assert(maelys_datalog_window_storage_requirements(SIZE_MAX,SIZE_MAX,&bytes,&alignment)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    OK(maelys_datalog_window_storage_requirements(2,64,&bytes,&alignment));
    void *storage=malloc(bytes+alignment); assert(storage);
    assert(maelys_datalog_window_init(storage,bytes-1,2,64,0,f.a,f.b,&f.w,NULL)==MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL && !f.w);
    assert(maelys_datalog_window_init((char*)storage+1,bytes,2,64,0,f.a,f.b,&f.w,NULL)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    assert(maelys_datalog_window_init(storage,bytes,2,64,UINT32_MAX,f.a,f.b,&f.w,NULL)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    assert(maelys_datalog_window_init(storage,bytes,2,64,0,f.a,f.a,&f.w,NULL)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    maelys_datalog_result_t *r=NULL; OK(maelys_datalog_session_solve(f.b,NULL,0,&r,NULL));
    assert(maelys_datalog_window_init(storage,bytes,2,64,0,f.a,f.b,&f.w,NULL)==MAELYS_DATALOG_STATUS_INVALID_STATE && !f.w);
    OK(maelys_datalog_result_free(r));
    OK(maelys_datalog_session_solve(f.a,NULL,0,&r,NULL));
    assert(maelys_datalog_window_init(storage,bytes,2,64,0,f.a,f.b,&f.w,NULL)==MAELYS_DATALOG_STATUS_INVALID_STATE && !f.w);
    OK(maelys_datalog_result_free(r));
    fixture other; sessions(&other,"copy(I,G,V) :- event(I,G,V).");
    assert(maelys_datalog_window_init(storage,bytes,2,64,0,f.a,other.b,&f.w,NULL)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    close_fixture(&other); free(storage);
    init(&f,2,64,0); close_fixture(&f);
}
static unsigned fault_phase;
static maelys_datalog_status_t injected_solve(void *state,
    const maelys_datalog_public_fact_t *facts, size_t count,
    maelys_datalog_backend_output_t *output, void **out_result,
    maelys_datalog_public_diagnostic_t *diag) {
    maelys_datalog_status_t rc=MAELYS_DATALOG_STATUS_OK;
    if (fault_phase!=1) rc=maelys_datalog_backend_reference()->solve(state,facts,count,output,out_result,diag);
    if (!rc && fault_phase) {
        if (diag) {
            diag->source=MAELYS_DATALOG_DIAGNOSTIC_SOLVE;
            diag->code=MAELYS_DATALOG_STATUS_INTERNAL;
            strcpy(diag->message,"Injected backend failure");
        }
        return MAELYS_DATALOG_STATUS_INTERNAL;
    }
    return rc;
}
static void initialization_and_backend_failure(void) {
    /* Empty runtime input can still derive an overflowing aggregate from policy
     * facts. Initialization must return both sessions without a leaked result. */
    fixture f; sessions(&f,"group(2147483647). group(1). summed(0,N) :- sum(V,group(V),N).");
    size_t bytes,alignment;
    OK(maelys_datalog_window_storage_requirements(1,64,&bytes,&alignment));
    void *storage=malloc(bytes); assert(storage);
    assert(maelys_datalog_window_init(storage,bytes,1,64,0,f.a,f.b,&f.w,NULL)==MAELYS_DATALOG_STATUS_INVALID_FIELD);
    assert(!f.w); close_fixture(&f); free(storage);

    /* Faults before work and after complete emission/result creation exercise
     * candidate cleanup; only the ordinary ABI 3 callback is replaced. */
    sessions(&f,"copy(I,G,V) :- event(I,G,V).");
    maelys_datalog_policy_t *p=NULL;
    const char *s="copy(I,G,V) :- event(I,G,V).";
    OK(maelys_datalog_policy_load_inline("window","window.work",s,strlen(s),&p,NULL));
    maelys_datalog_backend_t backend=*maelys_datalog_backend_reference();
    backend.name="window_fault_test"; backend.semantic_id="window.fault.test.v1";
    backend.solve=injected_solve;
    maelys_datalog_session_options_t options={MAELYS_DATALOG_BACKEND_ABI_VERSION,sizeof(options),&backend,0,0};
    OK(maelys_datalog_session_free(f.a)); OK(maelys_datalog_session_free(f.b));
    OK(maelys_datalog_session_create_ex(p,0,&options,&f.a));
    OK(maelys_datalog_session_create_ex(p,0,&options,&f.b));
    OK(maelys_datalog_policy_free(p));
    init(&f,2,64,0);
    push(&f,"event",0,7,2,0); push(&f,"event",0,8,2,1);
    maelys_datalog_public_value_t v[]={integer(0),integer(1)};
    for(fault_phase=1;fault_phase<=2;++fault_phase)
        rejected(&f,"event",v,2,MAELYS_DATALOG_STATUS_INTERNAL);
    fault_phase=0;
    push(&f,"event",0,1,2,2); state(&f,2,3);
    close_fixture(&f);
}
static void closed_window(fixture *f) {
    unsigned char *before=malloc(f->bytes); assert(before);
    memcpy(before,f->storage,f->bytes);
    size_t count=123; uint64_t next=456; uint32_t id=789;
    maelys_datalog_result_t *r=NULL;
    const maelys_datalog_public_fact_t *events=NULL;
    maelys_datalog_public_value_t v[]={integer(0),integer(1)};
    maelys_datalog_public_diagnostic_t diag;
    /* Push first: the regression dereferenced freed sessions before returning. */
    assert(maelys_datalog_window_push(f->w,"event",v,2,&id,&diag)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    assert(id==789 && !strcmp(diag.phase,"window") && strstr(diag.message,"closed"));
    assert(maelys_datalog_window_state(f->w,&count,&next)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    assert(count==123 && next==456);
    assert(maelys_datalog_window_result(f->w,&r)==MAELYS_DATALOG_STATUS_INVALID_STATE && !r);
    assert(maelys_datalog_window_events(f->w,&events,&count)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    assert(!events && count==123);
    size_t used=17,capacity=19;
    assert(maelys_datalog_window_text_usage(f->w,&used,&capacity)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    assert(used==17 && capacity==19);
    assert(maelys_datalog_window_free(f->w)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    assert(!memcmp(before,f->storage,f->bytes)); free(before);
}
static void lifecycle(void) {
    fixture f; sessions(&f,source); init(&f,1,64,0);
    push(&f,"event",0,1,2,0);
    OK(maelys_datalog_window_free(f.w));
    closed_window(&f);
    /* Closing does not prevent explicit reinitialization of the caller arena. */
    OK(maelys_datalog_window_init(f.storage,f.bytes,1,64,17,f.a,f.b,&f.w,NULL));
    state(&f,0,17); push(&f,"event",0,1,2,17);
    OK(maelys_datalog_window_free(f.w));
    /* Keep only the caller arena alive. No stale call may touch either session. */
    OK(maelys_datalog_session_free(f.a)); OK(maelys_datalog_session_free(f.b));
    closed_window(&f);
    OK(maelys_datalog_session_free(f.oracle)); free(f.storage);
}
int main(void) {
    maelys_datalog_public_domain_t d={"window",predicates,sizeof(predicates)/sizeof(*predicates),NULL,0};
    OK(maelys_datalog_domain_register(&d));
    lifecycle();
    admission(); initialization_and_backend_failure(); aggregates_and_rejections(); directed_limits(); explanations();
    generated_sequences(); full_arity_typed_events(); text_occupancy(); renewable_snapshot_vocabulary();
    puts("last-N window: admission, transactions, aggregates, leases, limits, FIFO oracle and vocabulary rotation PASS");
    return 0;
}
