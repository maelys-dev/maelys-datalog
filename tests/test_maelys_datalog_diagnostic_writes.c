/* SPDX-License-Identifier: MPL-2.0 */
/* Observe actual reset traffic, not just the number of CPU instructions. */
#include <maelys/datalog.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
static maelys_datalog_diagnostic_t *watched;
static size_t reset_calls, reset_bytes;
static void *counted_memset(void *p, int c, size_t n) {
    if (watched && (uintptr_t)p >= (uintptr_t)watched &&
        (uintptr_t)p < (uintptr_t)watched + sizeof(*watched)) reset_bytes += n;
    return memset(p,c,n);
}
#undef memset
#define memset counted_memset
#define maelys_datalog_diagnostic_clear measured_clear
#include "src/core/maelys_datalog_diagnostic.c"
#undef maelys_datalog_diagnostic_clear
#undef memset
maelys_datalog_status_t maelys_datalog_diagnostic_clear(maelys_datalog_diagnostic_t *d) {
    if(d==watched)++reset_calls;
    return measured_clear(d);
}
static void poison(maelys_datalog_diagnostic_t *d) {
    memset(d,0xa7,sizeof(*d));
    d->struct_size=sizeof(*d);d->abi_version=MAELYS_DATALOG_DIAGNOSTIC_ABI_VERSION;
    reset_calls=reset_bytes=0;
}
static void check_reset(maelys_datalog_diagnostic_t *d) {
    assert(reset_calls==1 && reset_bytes>0 && reset_bytes+8<200);
    assert(d->source==0 && d->status==0 && d->code==0 && d->present==0);
    assert(d->line==0 && d->column==0 && d->arity==0 && d->limit==0 && d->limit_kind==0);
#define TAIL(field) do { assert(d->field[0]==0); for(size_t i=1;i<sizeof(d->field);++i)assert((unsigned char)d->field[i]==0xa7); } while(0)
    TAIL(phase);TAIL(message);TAIL(hint);TAIL(file);TAIL(predicate);TAIL(token);TAIL(field);TAIL(domain);
#undef TAIL
}
int main(void) {
    const maelys_datalog_predicate_t predicates[]={
        {"seed",1,MAELYS_DATALOG_PREDICATE_EDB},
        {"out",1,MAELYS_DATALOG_PREDICATE_IDB}};
    const maelys_datalog_domain_t domain={"reset_writes",predicates,2,NULL,0};
    assert(!maelys_datalog_domain_register(&domain));
    maelys_datalog_policy_t *p=NULL;maelys_datalog_session_t *s=NULL;
    const char source[]="out(X) :- seed(X).";
    assert(!maelys_datalog_policy_load_inline(domain.name,"p",source,strlen(source),&p,NULL));
    assert(!maelys_datalog_session_create(p,0,&s));
    assert(!maelys_datalog_policy_free(p));
    maelys_datalog_input_edb_t *edb=NULL;
    assert(!maelys_datalog_input_edb_create_with_capacity(1,16,&edb));
    maelys_datalog_fact_t fact={"seed",1,{{MAELYS_DATALOG_VALUE_INTEGER,{.integer=1}}}};
    assert(!maelys_datalog_input_edb_add_facts(edb,&fact,1,NULL));
    maelys_datalog_diagnostic_t d=MAELYS_DATALOG_DIAGNOSTIC_INIT;watched=&d;
    for(int direct=0;direct<2;++direct){
        poison(&d);maelys_datalog_result_t *r=NULL;
        assert(!(direct?maelys_datalog_session_solve(s,&fact,1,&r,&d):maelys_datalog_session_solve_edb(s,edb,&r,&d)));
        check_reset(&d);assert(!maelys_datalog_result_free(r));
    }
    poison(&d);
    assert(maelys_datalog_session_solve_edb(s,NULL,NULL,&d)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    check_reset(&d);
    poison(&d);d.abi_version=99;unsigned char before[sizeof(d)];memcpy(before,&d,sizeof(d));
    assert(maelys_datalog_session_solve_edb(s,edb,NULL,&d)==MAELYS_DATALOG_STATUS_UNSUPPORTED);
    assert(reset_calls==1 && !reset_bytes && !memcmp(before,&d,sizeof(d)));
    watched=NULL;
    assert(!maelys_datalog_input_edb_free(edb));assert(!maelys_datalog_session_free(s));
    puts("diagnostic: one bounded reset per solve, text tails preserved, invalid version untouched");
    return 0;
}
