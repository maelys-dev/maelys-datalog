/* SPDX-License-Identifier: MPL-2.0 */
/* Native installed-SDK example; allocation occurs during setup, not push. */
#include <maelys/datalog_group_window.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(call) do { int rc_=(call); if(rc_) { fprintf(stderr,"%s: %d\n",#call,rc_); return 1; } } while(0)
static maelys_datalog_value_t number(int64_t n) {
    maelys_datalog_value_t v={.kind=MAELYS_DATALOG_VALUE_INTEGER}; v.as.integer=n; return v;
}
int main(void) {
    const maelys_datalog_predicate_t predicates[]={
        {"reading",2,MAELYS_DATALOG_PREDICATE_EDB},
        {"total",1,MAELYS_DATALOG_PREDICATE_IDB|MAELYS_DATALOG_PREDICATE_QUERY}};
    const maelys_datalog_domain_t domain={"multi_event",predicates,2,NULL,0};
    CHECK(maelys_datalog_domain_register(&domain));
    const char *rules="total(N) :- sum(V,reading(_,V),N).";
    maelys_datalog_policy_t *policy=NULL;
    CHECK(maelys_datalog_policy_load_inline("multi_event","multi.event",rules,strlen(rules),&policy,NULL));
    maelys_datalog_session_t *a=NULL,*b=NULL;
    CHECK(maelys_datalog_session_create(policy,0,&a));
    CHECK(maelys_datalog_session_create(policy,0,&b));
    CHECK(maelys_datalog_policy_free(policy));
    const maelys_datalog_group_window_capacities_t limits={2,3,2,64};
    size_t bytes,alignment;
    CHECK(maelys_datalog_group_window_storage_requirements(&limits,&bytes,&alignment));
    if(bytes>SIZE_MAX-(alignment-1)) return 1;
    void *allocation=malloc(bytes+alignment-1); if(!allocation) return 1;
    void *arena=(void *)(((uintptr_t)allocation+alignment-1)/alignment*alignment);
    maelys_datalog_group_window_t *w=NULL;
    CHECK(maelys_datalog_group_window_init(arena,bytes,&limits,0,a,b,&w,NULL));
    maelys_datalog_fact_t group_a[2]={{.predicate="reading",.arity=2},{.predicate="reading",.arity=2}};
    group_a[0].terms[0]=number(1); group_a[1].terms[0]=number(2);
    group_a[0].terms[1]=group_a[1].terms[1]=number(5);
    CHECK(maelys_datalog_group_window_push(w,group_a,2,NULL,NULL));
    CHECK(maelys_datalog_group_window_push(w,group_a,1,NULL,NULL)); /* B shares reading(1,5). */
    for(size_t step=0;step<3;++step) {
        if(step) CHECK(maelys_datalog_group_window_push(w,NULL,0,NULL,NULL)); /* C then D. */
        maelys_datalog_result_t *result=NULL; int present=0;
        maelys_datalog_value_t expected=number(step==0?10:step==1?5:0);
        CHECK(maelys_datalog_group_window_result(w,&result));
        CHECK(maelys_datalog_result_query(result,"total",&expected,1,&present));
        if(!present) return 1;
        maelys_datalog_group_window_usage_t usage;
        CHECK(maelys_datalog_group_window_state(w,&usage));
        printf("groups=%zu contributions=%zu unique=%zu sum=%lld\n",usage.groups,
            usage.contributions,usage.unique_facts,(long long)expected.as.integer);
    }
    CHECK(maelys_datalog_group_window_free(w));
    CHECK(maelys_datalog_session_free(a)); CHECK(maelys_datalog_session_free(b)); free(allocation);
    return 0;
}
