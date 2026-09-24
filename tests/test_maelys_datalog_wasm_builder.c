/* SPDX-License-Identifier: MPL-2.0 */
/* Independent public-SDK oracle and adversarial binding-transport tests.
 * No private engine type, state reset, or symbol interning is used here. */
#include <maelys/datalog.h>
#include "bindings/wasm/maelys_datalog_wasm.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifdef NDEBUG
#error "This conformance probe requires assertions"
#endif

#ifdef MAELYS_WASM_ALLOCATION_TEST
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef memset
static size_t calls, live_allocations, fail_call;
static int forbidden;
void *maelys_test_malloc(size_t n) { ++calls; if (forbidden || calls == fail_call) return NULL; void *p=malloc(n); if(p) ++live_allocations; return p; }
void *maelys_test_calloc(size_t n,size_t s) { ++calls; if(forbidden || calls == fail_call) return NULL; void *p=calloc(n,s); if(p) ++live_allocations; return p; }
void *maelys_test_realloc(void *p,size_t n) { ++calls; if(forbidden || calls == fail_call) return NULL; void *r=realloc(p,n); if(r && !p) ++live_allocations; return r; }
void maelys_test_free(void *p) { ++calls; if(p) --live_allocations; free(p); }
void *maelys_test_memset(void *p,int c,size_t n) { return memset(p,c,n); }
#endif

static uint32_t unary[MAELYS_WASM_FACT_WORDS] = {0,1,1,1,2,1};
static const char text[]="e\0x\0q\0missing\0";
static int query(const char *names, uint32_t offset, uint32_t *out) {
    uint32_t w[MAELYS_WASM_FACT_WORDS]; memcpy(w,unary,sizeof(w)); w[0]=offset;
    return maelys_datalog_wasm_query(w,MAELYS_WASM_FACT_WORDS,names,sizeof(text),out);
}
int main(void) {
    uint32_t out=999, usage[3], cap;
    assert(maelys_datalog_wasm_solve()==MAELYS_DATALOG_STATUS_INVALID_STATE);
#ifdef MAELYS_WASM_ALLOCATION_TEST
    forbidden=1;
    assert(maelys_datalog_wasm_open()==MAELYS_DATALOG_STATUS_INTERNAL);
    assert(live_allocations==0);
    forbidden=0; calls=0; fail_call=2;
    /* Input allocation fails after conversion scratch was already acquired. */
    assert(maelys_datalog_wasm_open()==MAELYS_DATALOG_STATUS_INTERNAL);
    assert(live_allocations==0);
    fail_call=0; calls=0;
#endif
    assert(maelys_datalog_wasm_open()==0);
#ifdef MAELYS_WASM_ALLOCATION_TEST
    assert(calls==2); /* One conversion scratch + one public input buffer. */
#endif
    assert(maelys_datalog_wasm_open()==MAELYS_DATALOG_STATUS_INVALID_STATE);
    assert(maelys_datalog_wasm_limit(MAELYS_DATALOG_LIMIT_MAX_EDB_FACTS,&cap)==0);
    assert(maelys_datalog_wasm_limit(UINT32_MAX,&out)==MAELYS_DATALOG_STATUS_UNSUPPORTED);
    assert(out==999);
    const uint32_t declarations[]={0,1,1,MAELYS_DATALOG_PREDICATE_EDB|MAELYS_DATALOG_PREDICATE_QUERY,
                                   4,1,1,MAELYS_DATALOG_PREDICATE_IDB|MAELYS_DATALOG_PREDICATE_QUERY};
    assert(maelys_datalog_wasm_register_domain("native_transport",declarations,8,2,0,text,sizeof(text))==0);
    assert(maelys_datalog_wasm_register_domain("native_transport",declarations,7,2,0,text,sizeof(text))!=0);
    const char source[]="q(X) :- e(X).";
    assert(maelys_datalog_wasm_load_policy("native_transport","main",source,strlen(source))==0);
    maelys_datalog_policy_t *policy=NULL;
    maelys_datalog_session_t *session=NULL;
    maelys_datalog_result_t *result=NULL;
    maelys_datalog_diagnostic_t diag=MAELYS_DATALOG_DIAGNOSTIC_INIT;
    assert(maelys_datalog_policy_load_inline("native_transport","main",source,strlen(source),&policy,&diag)==0);
    assert(maelys_datalog_session_create(policy,0,&session)==0);
    maelys_datalog_fact_t expected={.predicate="e",.arity=1,
        .terms={{.kind=MAELYS_DATALOG_VALUE_SYMBOL,.as.symbol="x"}}};
    assert(maelys_datalog_session_solve(session,&expected,1,&result,&diag)==0);

#ifdef MAELYS_WASM_ALLOCATION_TEST
    forbidden=1; calls=0;
#endif
    assert(maelys_datalog_wasm_add_facts(unary,15,1,text,sizeof(text))==0);
    assert(maelys_datalog_wasm_input_usage(usage)==0 && usage[0]==1);
    const uint32_t previous_text=usage[1];
    uint32_t pair[30]; memcpy(pair,unary,sizeof(unary)); memcpy(pair+15,unary,sizeof(unary));
    for (int which=0; which<6; ++which) {
        memcpy(pair+15,unary,sizeof(unary));
        switch(which) {
        case 0: pair[15]=UINT32_MAX; break;
        case 1: pair[16]=UINT32_MAX; break;
        case 2: pair[17]=5; break;
        case 3: pair[18]=255; break;
        case 4: pair[18]=3; pair[19]=2; break;
        case 5: pair[18]=3; pair[19]=1; pair[20]=1; break;
        }
        assert(maelys_datalog_wasm_add_facts(pair,30,2,text,sizeof(text))==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
        assert(maelys_datalog_wasm_input_usage(usage)==0 && usage[0]==1 && usage[1]==previous_text);
    }
    assert(maelys_datalog_wasm_add_facts(NULL,0,UINT32_MAX,text,sizeof(text))!=0);
    assert(maelys_datalog_wasm_add_facts(NULL,15,1,text,sizeof(text))!=0);
    assert(maelys_datalog_wasm_add_facts(unary,14,1,text,sizeof(text))!=0);
    assert(maelys_datalog_wasm_add_facts(NULL,0,0,NULL,0)==0);
    assert(maelys_datalog_wasm_solve()==0);
    assert(query(text,4,&out)==0 && out==1);
    assert(maelys_datalog_wasm_derived_count(&out)==0 && out==1);
    assert(maelys_datalog_wasm_solve()==MAELYS_DATALOG_STATUS_INVALID_STATE);
    assert(maelys_datalog_wasm_clear_facts()==MAELYS_DATALOG_STATUS_INVALID_STATE);
    assert(maelys_datalog_wasm_add_facts(NULL,0,0,NULL,0)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    uint32_t terms[3], count=0;
    assert(maelys_datalog_wasm_enumerate("q",1,terms,1,&count)==0 && count==1 && terms[0]==1);
    assert(strcmp(maelys_datalog_wasm_symbol_text(terms[1]),"x")==0);
    maelys_datalog_fact_view_t view;
    size_t count_native=0;
    assert(maelys_datalog_result_enumerate(result,"q",1,&view,1,&count_native)==0 && count_native==count);
    const char *native_text; size_t len;
    assert(maelys_datalog_result_symbol_text(result,view.terms[0].as.symbol_id,&native_text,&len)==0);
    assert(strcmp(native_text,"x")==0);
#ifdef MAELYS_WASM_ALLOCATION_TEST
    assert(calls==0); forbidden=0;
#endif
    uint32_t q[15]; memcpy(q,unary,sizeof(q)); q[0]=4;
    /* Public oracle independently prepares the exact same explanation. */
    for(uint32_t kind=1;kind<=2;++kind) {
        size_t expected_size=0; uint32_t required=0;
        maelys_datalog_value_t value={.kind=MAELYS_DATALOG_VALUE_SYMBOL,.as.symbol="x"};
        int rc=kind==1 ? maelys_datalog_result_explain_true_text(result,"q",&value,1,NULL,0,&expected_size)
                       : maelys_datalog_result_explain_false_text(result,"q",&value,1,NULL,0,&expected_size);
        assert(rc==0);
        assert(maelys_datalog_wasm_explain(kind,q,15,text,sizeof(text),NULL,0,&required)==0);
        assert(required==expected_size);
        char *actual=malloc(required+1u),*oracle=malloc(required+1u);
        assert(actual && oracle); memset(actual,0xa5,required+1u);
        assert(maelys_datalog_wasm_explain(kind,q,15,text,sizeof(text),actual,required,&out)==MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
        assert(out==required && actual[0]==0 && (unsigned char)actual[1]==0xa5);
        assert(maelys_datalog_wasm_explain(kind,q,15,text,sizeof(text),actual,required+1u,&out)==0);
        rc=kind==1 ? maelys_datalog_result_explain_true_text(result,"q",&value,1,oracle,required+1u,&expected_size)
                  : maelys_datalog_result_explain_false_text(result,"q",&value,1,oracle,required+1u,&expected_size);
        assert(rc==0 && strcmp(actual,oracle)==0);
        free(actual); free(oracle);
    }
    assert(maelys_datalog_wasm_explain(1,q,15,text,sizeof(text),NULL,10,&out)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    char fp[65],expected_fp[65];
    assert(maelys_datalog_wasm_fingerprint(1,fp,sizeof(fp))==0);
    assert(maelys_datalog_policy_fingerprint(policy,expected_fp)==0 && strcmp(fp,expected_fp)==0);
    assert(maelys_datalog_result_free(result)==0); result=NULL;
#ifdef MAELYS_WASM_ALLOCATION_TEST
    forbidden=1; calls=0;
#endif
    assert(maelys_datalog_wasm_free_result()==0);
    assert(maelys_datalog_wasm_clear_facts()==0);
    assert(maelys_datalog_wasm_input_usage(usage)==0 && usage[0]==0 && usage[1]==0);
#ifdef MAELYS_WASM_ALLOCATION_TEST
    assert(calls==0); forbidden=0;
#endif
    /* Round-trip all int64 bits against a separately built C input/oracle. */
    const int64_t ints[]={INT64_MIN,INT64_MAX,INT64_C(9007199254740993),0,-1};
    for(size_t i=0;i<sizeof(ints)/sizeof(ints[0]);++i) {
        uint64_t bits; memcpy(&bits,&ints[i],sizeof(bits));
        unary[3]=2; unary[4]=(uint32_t)bits; unary[5]=(uint32_t)(bits>>32);
        assert(maelys_datalog_wasm_add_facts(unary,15,1,text,sizeof(text))==0);
        expected.terms[0].kind=MAELYS_DATALOG_VALUE_INTEGER;
        expected.terms[0].as.integer=ints[i];
        const int oracle_status=maelys_datalog_session_solve(session,&expected,1,&result,&diag);
        const int adapter_status=maelys_datalog_wasm_solve();
        assert(oracle_status==0 && adapter_status==oracle_status);
        assert(maelys_datalog_wasm_enumerate("q",1,terms,1,&count)==0 && count==1);
        assert(maelys_datalog_result_enumerate(result,"q",1,&view,1,&count_native)==0 && count_native==count);
        assert(view.terms[0].kind==MAELYS_DATALOG_VALUE_INTEGER && view.terms[0].as.integer==ints[i]);
        assert(terms[0]==2 && terms[1]==(uint32_t)bits && terms[2]==(uint32_t)(bits>>32));
        assert(maelys_datalog_result_free(result)==0); result=NULL;
        assert(maelys_datalog_wasm_free_result()==0); assert(maelys_datalog_wasm_clear_facts()==0);
    }
    assert(maelys_datalog_session_free(session)==0); assert(maelys_datalog_policy_free(policy)==0);
    assert(maelys_datalog_wasm_close()==0);
    assert(maelys_datalog_wasm_close()==MAELYS_DATALOG_STATUS_INVALID_STATE);
    assert(maelys_datalog_wasm_solve()==MAELYS_DATALOG_STATUS_INVALID_STATE);
    assert(maelys_datalog_wasm_input_usage(usage)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    assert(query(text,4,&out)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    assert(maelys_datalog_wasm_open()==MAELYS_DATALOG_STATUS_INVALID_STATE);
#ifdef MAELYS_WASM_ALLOCATION_TEST
    /* The domain registry is process-lifetime bounded state; no owned handles remain. */
    assert(live_allocations==0);
#endif
    puts("PASS: public SDK oracle, atomic transport, int64, lifetime and allocation contract");
    return 0;
}
