/* SPDX-License-Identifier: MPL-2.0 */
/* Installed public SDK only; software counts, never a latency benchmark. */
#include <maelys/datalog_transactions.h>
#ifdef INPUT_DELIVERY_BENCH
#include "input_provider.h"
#endif
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <valgrind/callgrind.h>
#define OK(x) do{int rc_=(x);if(rc_){fprintf(stderr,"%d: %s: %d\n",__LINE__,#x,rc_);abort();}}while(0)
#define STEPS 200u
static const char *inputs[]={"in0","in1","in2","in3"};
static const char *outputs[]={"out0","out1","out2","out3"};
static const maelys_datalog_predicate_t predicates[]={
    MAELYS_DATALOG_EDB("in0",1),MAELYS_DATALOG_EDB("in1",1),MAELYS_DATALOG_EDB("in2",1),MAELYS_DATALOG_EDB("in3",1),
    MAELYS_DATALOG_IDB_QUERY("out0",1),MAELYS_DATALOG_IDB_QUERY("out1",1),MAELYS_DATALOG_IDB_QUERY("out2",1),MAELYS_DATALOG_IDB_QUERY("out3",1)};
static char text[512][16];
static uint64_t check(maelys_datalog_result_t *r,const maelys_datalog_fact_t *expected,size_t n,int project) {
    size_t total;OK(maelys_datalog_result_derived_fact_count(r,&total));assert(total==(project?n:0));
    uint64_t hash=UINT64_C(14695981039346656037);
    for(size_t p=0;p<4;++p) {
        maelys_datalog_fact_view_t rows[256];size_t count;
        OK(maelys_datalog_result_enumerate(r,outputs[p],1,rows,256,&count));assert(count==(project?n/4:0));
        for(size_t i=0;i<count;++i) {
            const maelys_datalog_term_view_t *v=&rows[i].terms[0];size_t found=0;
            const char *value=NULL;size_t bytes=0;
            if(v->kind==MAELYS_DATALOG_VALUE_SYMBOL)OK(maelys_datalog_result_symbol_text(r,v->as.symbol_id,&value,&bytes));
            for(size_t j=p;j<n;j+=4) {
                const maelys_datalog_value_t *e=&expected[j].terms[0];
                if(v->kind==e->kind && (value?!strcmp(value,e->as.symbol):v->as.integer==e->as.integer))++found;
            }
            assert(found==1);
            hash=(hash^(uint64_t)v->kind)*UINT64_C(1099511628211);
            if(value) {
                hash=(hash^v->as.symbol_id)*UINT64_C(1099511628211);
                for(size_t j=0;j<bytes;++j)hash=(hash^(unsigned char)value[j])*UINT64_C(1099511628211);
            } else hash=(hash^(uint64_t)v->as.integer)*UINT64_C(1099511628211);
        }
    }
    return hash;
}
static void fixture(const char *path,size_t n,int symbols,int project,size_t changed,const char *change) {
    maelys_datalog_policy_t *policy=NULL;
    const char *source=project?"out0(X) :- in0(X). out1(X) :- in1(X). out2(X) :- in2(X). out3(X) :- in3(X).":"\n";
    OK(maelys_datalog_policy_load_inline("retained_bench","fixture",source,strlen(source),&policy,NULL));
    maelys_datalog_session_t *s=NULL;
#ifdef INPUT_DELIVERY_BENCH
    maelys_datalog_session_config_t *config=NULL;OK(maelys_datalog_session_config_create(&config));
    if(!strcmp(path,"delta7"))OK(maelys_datalog_session_config_set_backend_v7(config,input_fixture_transactions()));
    else OK(maelys_datalog_session_config_set_backend_v6(config,input_fixture_snapshot()));
    OK(maelys_datalog_session_create_configured(policy,0,config,&s));OK(maelys_datalog_session_config_free(config));
    int snapshot=!strcmp(path,"snapshot6"),delta=!snapshot;
#else
    OK(maelys_datalog_session_create(policy,0,&s));
    int snapshot=!strcmp(path,"snapshot"),delta=!strcmp(path,"delta");
#endif
    maelys_datalog_fact_t facts[2][256],add[2][256],remove[2][256];
    const char *vocabulary[512];for(size_t i=0;i<2*n;++i)vocabulary[i]=text[i];
    for(size_t bank=0;bank<2;++bank)for(size_t i=0;i<n;++i) {
        size_t value=i+(bank && i>=n-changed?n:0);
        maelys_datalog_fact_t *f=&facts[bank][i];memset(f,0,sizeof(*f));f->predicate=inputs[i%4];f->arity=1;
        f->terms[0].kind=symbols?MAELYS_DATALOG_VALUE_SYMBOL:MAELYS_DATALOG_VALUE_INTEGER;
        if(symbols)f->terms[0].as.symbol=text[value];else f->terms[0].as.integer=(int64_t)value;
    }
    for(size_t bank=0;bank<2;++bank)for(size_t i=0;i<changed;++i) {
        add[bank][i]=facts[bank][n-changed+i];remove[bank][i]=facts[1-bank][n-changed+i];
    }
    maelys_datalog_session_inputs_t *h=NULL;void *storage=NULL;
    if(!snapshot) {
        maelys_datalog_input_options_t o=MAELYS_DATALOG_INPUT_OPTIONS_INIT;
        o.fact_capacity=n;o.addition_capacity=n;o.removal_capacity=n;
        if(symbols){o.symbols=vocabulary;o.symbol_count=2*n;}
        size_t bytes,alignment;OK(maelys_datalog_session_inputs_storage_requirements(s,&o,&bytes,&alignment));
        storage=malloc(bytes);assert(storage);OK(maelys_datalog_session_inputs_init(s,&o,storage,bytes,&h));
    }
    maelys_datalog_result_t *r=NULL;
    if(h) {
        maelys_datalog_input_base_t base;OK(maelys_datalog_session_inputs_base(h,&base));
        OK(maelys_datalog_session_inputs_replace(h,base,facts[0],n,&r,NULL));
    } else OK(maelys_datalog_session_solve(s,facts[0],n,&r,NULL));
    (void)check(r,facts[0],n,project);OK(maelys_datalog_result_free(r));
    uint64_t digest=0;
    CALLGRIND_ZERO_STATS;
    for(size_t tx=0;tx<STEPS;++tx) {
        size_t bank=1-tx%2;maelys_datalog_input_base_t base;
        CALLGRIND_TOGGLE_COLLECT;
        if(h) {
            OK(maelys_datalog_session_inputs_base(h,&base));
            if(delta)OK(maelys_datalog_session_inputs_apply(h,base,add[bank],changed,remove[bank],changed,&r,NULL));
            else OK(maelys_datalog_session_inputs_replace(h,base,facts[bank],n,&r,NULL));
        } else OK(maelys_datalog_session_solve(s,facts[bank],n,&r,NULL));
        CALLGRIND_TOGGLE_COLLECT;
        digest=(digest^check(r,facts[bank],n,project))*UINT64_C(1099511628211);
        CALLGRIND_TOGGLE_COLLECT;OK(maelys_datalog_result_free(r));CALLGRIND_TOGGLE_COLLECT;
    }
    char label[128];snprintf(label,sizeof(label),"%s/%s/%zu/%s/%s",path,project?"projection":"inert",n,symbols?"symbol":"integer",change);
    CALLGRIND_DUMP_STATS_AT(label);CALLGRIND_ZERO_STATS;
    printf("%s,%u,%016" PRIx64 "\n",label,STEPS,digest);
    if(h)OK(maelys_datalog_session_inputs_free(h));free(storage);
    OK(maelys_datalog_session_free(s));OK(maelys_datalog_policy_free(policy));
}
int main(int argc,char **argv) {
#ifdef INPUT_DELIVERY_BENCH
    if(argc!=2 || (strcmp(argv[1],"snapshot6")&&strcmp(argv[1],"delta6")&&strcmp(argv[1],"delta7")))return 2;
#else
    if(argc!=2 || (strcmp(argv[1],"snapshot")&&strcmp(argv[1],"replace")&&strcmp(argv[1],"delta")))return 2;
#endif
    for(size_t i=0;i<512;++i)snprintf(text[i],sizeof(text[i]),"s%04zu",i);
    maelys_datalog_domain_t d={"retained_bench",predicates,8,NULL,0};OK(maelys_datalog_domain_register(&d));
    puts("case,transactions,digest");
    for(int project=0;project<2;++project)for(int symbols=0;symbols<2;++symbols)for(size_t n=64;n<=256;n*=4) {
        fixture(argv[1],n,symbols,project,0,"empty");fixture(argv[1],n,symbols,project,1,"one");
        fixture(argv[1],n,symbols,project,4,"small");fixture(argv[1],n,symbols,project,n,"replace");
    }
    return 0;
}
