/* SPDX-License-Identifier: MPL-2.0 */
/* Public-only consumer: the oracle constructs fresh snapshots independently. */
#include <maelys/datalog_transactions.h>
#include <maelys/datalog_backend.h>
#include <maelys/datalog_advanced.h>
#include <maelys/datalog_resources.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define OK(x) do { int rc_=(x);if(rc_){fprintf(stderr,"%s:%d %s: %d\n",__FILE__,__LINE__,#x,rc_);abort();}}while(0)
static const maelys_datalog_predicate_t predicates[]={
    MAELYS_DATALOG_EDB("seed",1),MAELYS_DATALOG_EDB("blocked",1),
    {"fixed",1,MAELYS_DATALOG_PREDICATE_POLICY_FACT},
    MAELYS_DATALOG_IDB_QUERY("copy",1),MAELYS_DATALOG_IDB_QUERY("allowed",1),
    MAELYS_DATALOG_IDB_QUERY("n",1)};
static const char *vocabulary[]={"zebra","alpha","root","unused","omega"};
static const char *source="fixed(0). copy(X) :- seed(X). allowed(X) :- seed(X), not(blocked(X)). n(N) :- fixed(G), count(X,seed(X),N).";
static maelys_datalog_policy_t *policy;
static int provider_mode;
static size_t provider_calls,provider_facts;
static int equal_value(const maelys_datalog_value_t *a,const maelys_datalog_value_t *b) {
    if(a->kind!=b->kind)return 0;
    if(a->kind==MAELYS_DATALOG_VALUE_SYMBOL)return !strcmp(a->as.symbol,b->as.symbol);
    if(a->kind==MAELYS_DATALOG_VALUE_INTEGER)return a->as.integer==b->as.integer;
    return a->as.boolean==b->as.boolean;
}
static maelys_datalog_status_t snapshot_solve(void *state,const maelys_datalog_fact_t *facts,
    size_t count,maelys_datalog_backend_output_t *output,void **result,maelys_datalog_diagnostic_t *diag) {
    (void)state;(void)diag;*result=NULL;
    ++provider_calls;provider_facts=count;
    for(size_t i=0;i<count;++i)assert(facts[i].predicate && facts[i].arity==1);
    /* Independent public-SDK implementation of this fixture's three rules.
     * It uses ONLY the delivered facts, never the reference solve callback. */
    size_t seeds=0;
    for(size_t i=0;i<count;++i)if(!strcmp(facts[i].predicate,"seed")) {
        ++seeds;maelys_datalog_fact_t f=facts[i];f.predicate="copy";
        maelys_datalog_status_t rc=maelys_datalog_backend_emit(output,&f);if(rc)return rc;
        int blocked=0;
        for(size_t j=0;j<count;++j)if(!strcmp(facts[j].predicate,"blocked") &&
            equal_value(&facts[i].terms[0],&facts[j].terms[0]))blocked=1;
        if(!blocked){f.predicate="allowed";rc=maelys_datalog_backend_emit(output,&f);if(rc)return rc;}
    }
    maelys_datalog_fact_t n={.predicate="n",.arity=1};
    n.terms[0].kind=MAELYS_DATALOG_VALUE_INTEGER;n.terms[0].as.integer=(int64_t)seeds;
    return maelys_datalog_backend_emit(output,&n);
}
static maelys_datalog_fact_t integer(const char *p,int64_t n) {
    maelys_datalog_fact_t f={.predicate=p,.arity=1};f.terms[0].kind=MAELYS_DATALOG_VALUE_INTEGER;
    f.terms[0].as.integer=n;return f;
}
typedef struct { maelys_datalog_session_t *session,*oracle;maelys_datalog_session_inputs_t *inputs;void *storage;size_t bytes; } fixture;
static void setup(fixture *f,size_t capacity) {
    memset(f,0,sizeof(*f));
    if(provider_mode) {
        maelys_datalog_session_config_t *c=NULL;OK(maelys_datalog_session_config_create(&c));
        if(provider_mode==1) {
            maelys_datalog_backend_t b=*maelys_datalog_backend_reference();b.solve=snapshot_solve;
            OK(maelys_datalog_session_config_set_backend(c,&b));
        } else {
            maelys_datalog_backend_v6_t b=*maelys_datalog_backend_reference_v6();b.solve=snapshot_solve;
            OK(maelys_datalog_session_config_set_backend_v6(c,&b));
        }
        OK(maelys_datalog_session_create_configured(policy,0,c,&f->session));
        OK(maelys_datalog_session_config_free(c));
    } else OK(maelys_datalog_session_create(policy,0,&f->session));
    OK(maelys_datalog_session_create(policy,0,&f->oracle));
    maelys_datalog_input_options_t o=MAELYS_DATALOG_INPUT_OPTIONS_INIT;
    o.fact_capacity=capacity;o.addition_capacity=8;o.removal_capacity=8;
    o.symbols=vocabulary;o.symbol_count=sizeof(vocabulary)/sizeof(*vocabulary);
    size_t alignment;
    OK(maelys_datalog_session_inputs_storage_requirements(f->session,&o,&f->bytes,&alignment));
    f->storage=malloc(f->bytes);assert(f->storage && (uintptr_t)f->storage%alignment==0);
    memset(f->storage,0xa5,f->bytes);
    OK(maelys_datalog_session_inputs_init(f->session,&o,f->storage,f->bytes,&f->inputs));
}
static maelys_datalog_input_base_t base(fixture *f) {
    maelys_datalog_input_base_t b;OK(maelys_datalog_session_inputs_base(f->inputs,&b));return b;
}
static void unchanged(fixture *f,maelys_datalog_input_base_t b) {
    maelys_datalog_input_base_t after=base(f);assert(!memcmp(&b,&after,sizeof(b)));
}
static void close_fixture(fixture *f) {
    assert(maelys_datalog_session_free(f->session)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    OK(maelys_datalog_session_inputs_free(f->inputs));
    OK(maelys_datalog_session_free(f->session));OK(maelys_datalog_session_free(f->oracle));free(f->storage);
}
static void compare(fixture *f,maelys_datalog_result_t *a,const maelys_datalog_fact_t *facts,size_t n) {
    maelys_datalog_result_t *b=NULL;OK(maelys_datalog_session_solve(f->oracle,facts,n,&b,NULL));
    size_t ca,cb;OK(maelys_datalog_result_derived_fact_count(a,&ca));OK(maelys_datalog_result_derived_fact_count(b,&cb));assert(ca==cb);
    for(size_t p=3;p<6;++p) {
        maelys_datalog_fact_view_t fa[64],fb[64];size_t na,nb;
        OK(maelys_datalog_result_enumerate(a,predicates[p].name,1,fa,64,&na));
        OK(maelys_datalog_result_enumerate(b,predicates[p].name,1,fb,64,&nb));assert(na==nb);
        for(size_t i=0;i<na;++i) {
            const maelys_datalog_term_view_t *x=&fa[i].terms[0],*y=&fb[i].terms[0];assert(x->kind==y->kind);
            if(x->kind==MAELYS_DATALOG_VALUE_SYMBOL) {
                const char *sx,*sy;size_t nx,ny;assert(x->as.symbol_id==y->as.symbol_id);
                OK(maelys_datalog_result_symbol_text(a,x->as.symbol_id,&sx,&nx));
                OK(maelys_datalog_result_symbol_text(b,y->as.symbol_id,&sy,&ny));assert(nx==ny&&!memcmp(sx,sy,nx));
            } else if(x->kind==MAELYS_DATALOG_VALUE_INTEGER) assert(x->as.integer==y->as.integer);
            else assert(x->as.boolean==y->as.boolean);
        }
    }
    OK(maelys_datalog_result_free(b));
}
static void boundaries(void) {
    fixture f;setup(&f,4);maelys_datalog_result_t *r=NULL;maelys_datalog_input_base_t initial=base(&f);
    maelys_datalog_fact_t facts[9];for(size_t i=0;i<9;++i)facts[i]=integer("seed",(int64_t)i);
    assert(maelys_datalog_session_solve(f.session,facts,1,&r,NULL)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    OK(maelys_datalog_session_inputs_replace(f.inputs,initial,facts,4,&r,NULL));compare(&f,r,facts,4);
    if(provider_mode)assert(provider_facts==4);
    assert(maelys_datalog_session_inputs_free(f.inputs)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    maelys_datalog_result_t *bad=NULL;
    assert(maelys_datalog_session_inputs_apply(f.inputs,base(&f),NULL,0,NULL,0,&bad,NULL)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    OK(maelys_datalog_result_free(r));
    assert(base(&f).generation==initial.generation+1);
    assert(maelys_datalog_session_inputs_replace(f.inputs,initial,NULL,0,&r,NULL)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    maelys_datalog_input_base_t current=base(&f);
    assert(maelys_datalog_session_inputs_apply(f.inputs,current,facts+4,1,NULL,0,&r,NULL)==MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);unchanged(&f,current);
    assert(maelys_datalog_session_inputs_apply(f.inputs,current,facts,9,NULL,0,&r,NULL)==MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);unchanged(&f,current);
    maelys_datalog_fact_t raw_duplicates[9];for(size_t i=0;i<9;++i)raw_duplicates[i]=facts[0];
    assert(maelys_datalog_session_inputs_apply(f.inputs,current,raw_duplicates,9,NULL,0,&r,NULL)==MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);unchanged(&f,current);
    assert(maelys_datalog_session_inputs_replace(f.inputs,current,raw_duplicates,5,&r,NULL)==MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);unchanged(&f,current);
    /* Exact capacity after removal, including merge insertion before element 0. */
    maelys_datalog_fact_t minus=integer("seed",-1);
    OK(maelys_datalog_session_inputs_apply(f.inputs,current,&minus,1,facts+3,1,&r,NULL));
    facts[3]=minus;compare(&f,r,facts,4);OK(maelys_datalog_result_free(r));
    /* Add wins when present in both lots; duplicates do not consume capacity. */
    maelys_datalog_fact_t duplicates[]={minus,minus};
    OK(maelys_datalog_session_inputs_apply(f.inputs,base(&f),duplicates,2,duplicates,2,&r,NULL));
    compare(&f,r,facts,4);OK(maelys_datalog_result_free(r));
    maelys_datalog_fact_t invalid=integer("missing",0);current=base(&f);
    assert(maelys_datalog_session_inputs_apply(f.inputs,current,&invalid,1,&invalid,1,&r,NULL)==MAELYS_DATALOG_STATUS_INVALID_FIELD);unchanged(&f,current);
    invalid=integer("fixed",0);assert(maelys_datalog_session_inputs_apply(f.inputs,current,NULL,0,&invalid,1,&r,NULL)==MAELYS_DATALOG_STATUS_FORBIDDEN);
    invalid=integer("seed",0);invalid.terms[0].kind=MAELYS_DATALOG_VALUE_SYMBOL;invalid.terms[0].as.symbol="unknown";
    assert(maelys_datalog_session_inputs_apply(f.inputs,current,&invalid,1,&invalid,1,&r,NULL)==MAELYS_DATALOG_STATUS_INVALID_FIELD);unchanged(&f,current);
    OK(maelys_datalog_session_inputs_apply(f.inputs,current,NULL,0,&invalid,1,&r,NULL));compare(&f,r,facts,4);OK(maelys_datalog_result_free(r));
    assert(base(&f).generation==current.generation+1); /* Successful absent removal. */
    fixture other;setup(&other,4);
    assert(maelys_datalog_session_inputs_replace(other.inputs,initial,NULL,0,&r,NULL)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    close_fixture(&other);close_fixture(&f);
}
static uint32_t rng=0x53d241a9u;
static uint32_t random32(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static void generated(void) {
    fixture f;setup(&f,48);maelys_datalog_fact_t universe[32];unsigned live[32]={0};
    for(size_t i=0;i<32;++i)universe[i]=integer(i<16?"seed":"blocked",(int64_t)(i%16));
    for(size_t i=0;i<4;++i) {
        universe[i].terms[0].kind=MAELYS_DATALOG_VALUE_SYMBOL;universe[i].terms[0].as.symbol=vocabulary[i];
        universe[i+16].terms[0]=universe[i].terms[0];
    }
    universe[4].terms[0].kind=MAELYS_DATALOG_VALUE_BOOLEAN;universe[4].terms[0].as.boolean=7;
    universe[20].terms[0]=universe[4].terms[0];
    for(size_t tx=0;tx<1200;++tx) {
        maelys_datalog_fact_t adds[8],removes[8],snapshot[32];unsigned ai[8],ri[8];
        size_t na=random32()%9,nr=random32()%9,n=0;
        for(size_t i=0;i<na;++i){ai[i]=random32()%32;adds[i]=universe[ai[i]];}
        for(size_t i=0;i<nr;++i){ri[i]=random32()%32;removes[i]=universe[ri[i]];}
        for(size_t i=0;i<nr;++i)live[ri[i]]=0;
        for(size_t i=0;i<na;++i)live[ai[i]]=1;
        for(size_t i=0;i<32;++i)if(live[i])snapshot[n++]=universe[i];
        maelys_datalog_input_base_t b=base(&f);maelys_datalog_result_t *r=NULL;
        if(tx%17==0)OK(maelys_datalog_session_inputs_replace(f.inputs,b,snapshot,n,&r,NULL));
        else OK(maelys_datalog_session_inputs_apply(f.inputs,b,adds,na,removes,nr,&r,NULL));
        compare(&f,r,snapshot,n);OK(maelys_datalog_result_free(r));assert(base(&f).generation==b.generation+1);
    }
    close_fixture(&f);
}
static void admission(void) {
    maelys_datalog_session_t *s=NULL;OK(maelys_datalog_session_create(policy,0,&s));
    maelys_datalog_input_options_t o=MAELYS_DATALOG_INPUT_OPTIONS_INIT;size_t bytes=7,alignment=9;
    o.contract_version=999;assert(maelys_datalog_session_inputs_storage_requirements(s,&o,&bytes,&alignment)==MAELYS_DATALOG_STATUS_UNSUPPORTED);assert(bytes==7&&alignment==9);
    o.contract_version=1;o.addition_capacity=SIZE_MAX;
    assert(maelys_datalog_session_inputs_storage_requirements(s,&o,&bytes,&alignment)==MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
    o.addition_capacity=0;OK(maelys_datalog_session_inputs_storage_requirements(s,&o,&bytes,&alignment));
    void *storage=malloc(bytes);assert(storage);maelys_datalog_session_inputs_t *h=NULL;
    assert(maelys_datalog_session_inputs_init(s,&o,storage,bytes-1,&h)==MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL);
    OK(maelys_datalog_session_inputs_init(s,&o,storage,bytes,&h));maelys_datalog_input_base_t b;OK(maelys_datalog_session_inputs_base(h,&b));
    maelys_datalog_result_t *r=NULL;OK(maelys_datalog_session_inputs_replace(h,b,NULL,0,&r,NULL));OK(maelys_datalog_result_free(r));
    OK(maelys_datalog_session_inputs_free(h));OK(maelys_datalog_session_free(s));
    OK(maelys_datalog_session_create(policy,0,&s));OK(maelys_datalog_session_inputs_init(s,&o,storage,bytes,&h));
    assert(maelys_datalog_session_inputs_replace(h,b,NULL,0,&r,NULL)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    OK(maelys_datalog_session_inputs_free(h));OK(maelys_datalog_session_free(s));free(storage);
}
static void identity(void) {
    maelys_datalog_session_t *a=NULL,*b=NULL;OK(maelys_datalog_session_create(policy,0,&a));OK(maelys_datalog_session_create(policy,0,&b));
    char plain[65],fa[65],fb[65];OK(maelys_datalog_session_execution_fingerprint(a,plain));
    maelys_datalog_input_options_t o=MAELYS_DATALOG_INPUT_OPTIONS_INIT;o.fact_capacity=4;o.addition_capacity=4;
    const char *left[]={"zebra","alpha","zebra"},*right[]={"alpha","zebra"};
    o.symbols=left;o.symbol_count=3;size_t n,alignment;
    OK(maelys_datalog_session_inputs_storage_requirements(a,&o,&n,&alignment));
    void *sa=malloc(n),*sb=malloc(n);assert(sa&&sb);maelys_datalog_session_inputs_t *ha=NULL,*hb=NULL;
    OK(maelys_datalog_session_inputs_init(a,&o,sa,n,&ha));o.symbols=right;o.symbol_count=2;
    OK(maelys_datalog_session_inputs_init(b,&o,sb,n,&hb));
    OK(maelys_datalog_session_execution_fingerprint(a,fa));OK(maelys_datalog_session_execution_fingerprint(b,fb));
    assert(strcmp(plain,fa) && !strcmp(fa,fb));
    OK(maelys_datalog_session_inputs_free(ha));OK(maelys_datalog_session_inputs_free(hb));
    OK(maelys_datalog_session_execution_fingerprint(a,fa));assert(!strcmp(plain,fa));
    /* Same session before any solve can accept a new contract, with new base. */
    o.addition_capacity=3;OK(maelys_datalog_session_inputs_init(a,&o,sa,n,&ha));
    OK(maelys_datalog_session_execution_fingerprint(a,fa));assert(strcmp(fa,fb));
    OK(maelys_datalog_session_inputs_free(ha));OK(maelys_datalog_session_free(a));OK(maelys_datalog_session_free(b));free(sa);free(sb);
}
int main(void) {
    maelys_datalog_domain_t d={"transactions",predicates,sizeof(predicates)/sizeof(*predicates),NULL,0};
    OK(maelys_datalog_domain_register(&d));
    OK(maelys_datalog_policy_load_inline(d.name,"inputs",source,strlen(source),&policy,NULL));
    for(provider_mode=0;provider_mode<3;++provider_mode) { rng=0x53d241a9u;boundaries();generated(); }
    assert(provider_calls>2400);admission();identity();OK(maelys_datalog_policy_free(policy));
    puts("input transactions: reference/ABI5/ABI6, 3 x 1200 traces (seed 53d241a9), canonical IDs, boundaries and incarnations PASS");return 0;
}
