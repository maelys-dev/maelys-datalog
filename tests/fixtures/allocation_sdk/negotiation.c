/* SPDX-License-Identifier: MPL-2.0 */
#include <maelys/datalog_backend.h>
#include "allocation_provider.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
const maelys_datalog_backend_v6_t *matrix_provider6(void);
unsigned matrix_provider6_prepared(void);
static void *acquire(void *p,size_t n,size_t a) {(void)p;(void)n;(void)a;abort();}
static void release(void *p,void *q,size_t n,size_t a) {(void)p;(void)q;(void)n;(void)a;abort();}
int main(int argc,char **argv) {
    assert(argc==2);int old=!strcmp(argv[1],"old");
    maelys_datalog_session_config_t *c;assert(!maelys_datalog_session_config_create(&c));
    maelys_datalog_caller_allocator_t a=MAELYS_DATALOG_CALLER_ALLOCATOR_INIT;
    a.acquire=acquire;a.release=release;
    maelys_datalog_session_allocation_request_t q=MAELYS_DATALOG_ALLOCATION_REQUEST_INIT;
    q.execution_byte_cap=SIZE_MAX;q.allocator=&a;
    assert(maelys_datalog_session_config_set_resources(c,&q.base)==(old?MAELYS_DATALOG_STATUS_UNSUPPORTED:0));
    assert(maelys_datalog_session_config_set_backend_v6(c,allocation_fixture_snapshot())==(old?MAELYS_DATALOG_STATUS_UNSUPPORTED:0));
    assert(maelys_datalog_session_config_set_backend_v7(c,allocation_fixture_transactions())==(old?MAELYS_DATALOG_STATUS_UNSUPPORTED:0));
    if(!old) {
        const maelys_datalog_predicate_t predicates[]={MAELYS_DATALOG_EDB("seed",1),MAELYS_DATALOG_IDB_QUERY("seen",1)};
        const maelys_datalog_domain_t d={"matrix",predicates,2,NULL,0};assert(!maelys_datalog_domain_register(&d));
        maelys_datalog_policy_t *policy;const char *source="seen(X) :- seed(X).";
        assert(!maelys_datalog_policy_load_inline("matrix","p",source,strlen(source),&policy,NULL));
        assert(!maelys_datalog_session_config_set_backend_v6(c,matrix_provider6()));
        maelys_datalog_session_t *s=NULL;
        assert(maelys_datalog_session_create_configured(policy,0,c,&s)==MAELYS_DATALOG_STATUS_UNSUPPORTED && !s);
        assert(!matrix_provider6_prepared());assert(!maelys_datalog_policy_free(policy));
    }
    assert(!maelys_datalog_session_config_free(c));return 0;
}
