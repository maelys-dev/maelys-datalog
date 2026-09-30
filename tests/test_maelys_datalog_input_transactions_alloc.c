/* SPDX-License-Identifier: MPL-2.0 */
/* All engine units are allocation-guarded; inspect only the committed state.
 * Candidate/sort scratch is explicitly not part of the rollback promise. */
#include "tests/fixtures/allocation_guard.h"
#include "src/runtime/maelys_datalog_runtime.c"
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef memset
#include <assert.h>
static size_t alloc_calls,free_calls;
static int forbidden;
void *maelys_test_malloc(size_t n){++alloc_calls;return forbidden?NULL:malloc(n);}
void *maelys_test_calloc(size_t n,size_t s){++alloc_calls;return forbidden?NULL:calloc(n,s);}
void *maelys_test_realloc(void *p,size_t n){++alloc_calls;return forbidden?NULL:realloc(p,n);}
void maelys_test_free(void *p){if(p)++free_calls;free(p);}
void *maelys_test_memset(void *p,int c,size_t n){return memset(p,c,n);}
#define OK(x) assert((x)==MAELYS_DATALOG_STATUS_OK)
static maelys_datalog_fact_t fact(int64_t n) {
    maelys_datalog_fact_t f={.predicate="ev",.arity=1};f.terms[0].kind=MAELYS_DATALOG_VALUE_INTEGER;
    f.terms[0].as.integer=n;return f;
}
static void same_committed(maelys_datalog_session_inputs_t *h,maelys_datalog_input_base_t base,
    const maelys_datalog_internal_fact_t *facts,size_t n,const maelys_datalog_symbol_table_t *v) {
    assert(!memcmp(&h->base,&base,sizeof(base)) && h->count==n && !h->pending);
    assert(!memcmp(h->live,facts,n*sizeof(*facts)) && !memcmp(&h->vocabulary,v,sizeof(*v)));
}
int main(void) {
    const maelys_datalog_predicate_t predicates[]={MAELYS_DATALOG_EDB("ev",1),MAELYS_DATALOG_IDB_QUERY("total",1)};
    const maelys_datalog_domain_t domain={"retained_alloc",predicates,2,NULL,0};
    OK(maelys_datalog_domain_register(&domain));
    const char *source="total(N) :- sum(V,ev(V),N).";maelys_datalog_policy_t *p=NULL;
    OK(maelys_datalog_policy_load_inline(domain.name,"sum",source,strlen(source),&p,NULL));
    maelys_datalog_session_t *s=NULL;OK(maelys_datalog_session_create(p,0,&s));
    maelys_datalog_input_options_t o=MAELYS_DATALOG_INPUT_OPTIONS_INIT;
    o.fact_capacity=4;o.addition_capacity=4;o.removal_capacity=4;
    size_t bytes,alignment;OK(maelys_datalog_session_inputs_storage_requirements(s,&o,&bytes,&alignment));
    void *storage=malloc(bytes);assert(storage);
    maelys_datalog_session_inputs_t *h=NULL;
    size_t before=alloc_calls,before_free=free_calls;
    forbidden=1;
    OK(maelys_datalog_session_inputs_init(s,&o,storage,bytes,&h));
    assert(alloc_calls==before && free_calls==before_free);
    maelys_datalog_fact_t a=fact(3),b=fact(INT32_MAX);
    maelys_datalog_result_t *r=NULL;
    OK(maelys_datalog_session_inputs_replace(h,h->base,&a,1,&r,NULL));OK(maelys_datalog_result_free(r));
    maelys_datalog_input_base_t base=h->base;
    maelys_datalog_internal_fact_t saved[4];memcpy(saved,h->live,h->count*sizeof(*saved));
    maelys_datalog_symbol_table_t vocabulary=h->vocabulary;
    /* Solver overflow after successful conversion/composition. */
    assert(maelys_datalog_session_inputs_apply(h,base,&b,1,NULL,0,&r,NULL)==MAELYS_DATALOG_STATUS_INVALID_FIELD && !r);
    same_committed(h,base,saved,1,&vocabulary);
    /* A prepared candidate may still be discarded by its window adapter. */
    OK(maelys_datalog_session_solve_candidate(s,&b,1,&r,NULL));
    assert(h->pending && h->base.generation==base.generation);
    OK(maelys_datalog_result_free(r));same_committed(h,base,saved,1,&vocabulary);
    /* Retry succeeds after withdrawing the overflow contributor. */
    OK(maelys_datalog_session_inputs_apply(h,base,&b,1,&a,1,&r,NULL));OK(maelys_datalog_result_free(r));
    assert(h->base.generation==base.generation+1 && h->live[0].terms[0].as.integer==INT32_MAX);
    /* Rejection on raw validation also preserves the entire committed payload. */
    base=h->base;memcpy(saved,h->live,sizeof(*saved));a.predicate="absent";
    assert(maelys_datalog_session_inputs_apply(h,base,&a,1,&b,1,&r,NULL)==MAELYS_DATALOG_STATUS_INVALID_FIELD);
    same_committed(h,base,saved,1,&vocabulary);
    h->base.generation=UINT64_MAX;base=h->base;
    assert(maelys_datalog_session_inputs_apply(h,base,NULL,0,NULL,0,&r,NULL)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    same_committed(h,base,saved,1,&vocabulary);
    OK(maelys_datalog_session_inputs_free(h));
    assert(alloc_calls==before && free_calls==before_free);forbidden=0;
    OK(maelys_datalog_session_free(s));OK(maelys_datalog_policy_free(p));free(storage);
    puts("retained input allocation/rollback: no engine calls; validation, solver rejection, candidate abort, retry, generation exhaustion PASS");
    return 0;
}
