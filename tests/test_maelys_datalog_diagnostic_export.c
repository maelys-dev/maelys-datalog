/* SPDX-License-Identifier: MPL-2.0 */
#include "src/compiler/maelys_datalog_program_internal.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
_Static_assert(sizeof(maelys_datalog_internal_solve_diagnostic_t) <= 32u,
               "public details must not inflate the solver diagnostic");
int main(void) {
    maelys_datalog_internal_ruleset_t *r=calloc(1,sizeof(*r));assert(r);
    assert(maelys_datalog_ruleset_init(r,"diagnostic","export","",1)==MAELYS_OK);
    assert(maelys_datalog_predicate_registry_add_domain(&r->registry,"output",2,MAELYS_DATALOG_PREDICATE_IDB)==MAELYS_OK);
    r->rule_count=1;
    maelys_datalog_internal_solve_diagnostic_t compact={0};
    compact.category=MAELYS_DATALOG_SOLVE_DIAG_IDB_OVERFLOW;
    compact.predicate_id=0;compact.rule_id=0;compact.capacity=64;compact.count_observed=65;
    compact.limit_kind=MAELYS_DATALOG_LIMIT_MAX_FACTS_PER_PRED;
    compact.depth=7;compact.depth_limit=10;
    maelys_datalog_internal_solve_diagnostic_t saved=compact;
    maelys_datalog_diagnostic_t detail=MAELYS_DATALOG_DIAGNOSTIC_INIT;
    maelys_datalog_copy_solve_diagnostic(&detail,&compact,r,MAELYS_ERR_PAYLOAD_TOO_LARGE);
    const uint64_t bits=MAELYS_DATALOG_DIAGNOSTIC_CAPACITY|MAELYS_DATALOG_DIAGNOSTIC_PREDICATE|MAELYS_DATALOG_DIAGNOSTIC_DEPTH|MAELYS_DATALOG_DIAGNOSTIC_RULE;
    assert((detail.present & bits)==bits);
    assert(detail.status==MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE && detail.code==MAELYS_DATALOG_DIAG_SOLVE_IDB_OVERFLOW);
    assert(!strcmp(detail.predicate,"output") && detail.arity==2);
    assert(detail.observed_count==65 && detail.limit==64 && detail.depth==7 && detail.depth_limit==10 && detail.rule_id==0);
    assert(detail.limit_kind==MAELYS_DATALOG_LIMIT_MAX_FACTS_PER_PRED);
    assert(!memcmp(&saved,&compact,sizeof(saved)));
    /* The bound identity is explicit even when two numeric capacities coincide. */
    compact.limit_kind=MAELYS_DATALOG_LIMIT_MAX_PREDICATES;
    maelys_datalog_copy_solve_diagnostic(&detail,&compact,r,MAELYS_ERR_PAYLOAD_TOO_LARGE);
    assert(detail.limit_kind==MAELYS_DATALOG_LIMIT_MAX_PREDICATES && detail.limit==64);
    compact.predicate_id=UINT16_MAX;compact.rule_id=UINT16_MAX;compact.capacity=0;compact.count_observed=0;compact.depth_limit=0;compact.category=MAELYS_DATALOG_SOLVE_DIAG_INTERNAL_ERROR;
    maelys_datalog_copy_solve_diagnostic(&detail,&compact,r,MAELYS_ERR_INTERNAL);
    assert(!(detail.present & bits));
    maelys_datalog_internal_diagnostic_t load={0};
    load.code=MAELYS_DATALOG_DIAG_PARSER_ARITY_MISMATCH;
    strcpy(load.file,"source.dl");strcpy(load.predicate,"output");strcpy(load.field,"arity");strcpy(load.domain,"export");strcpy(load.token,"output");
    load.line=4;load.column=8;load.expected_arity=2;load.observed_arity=3;load.arity=3;
    maelys_datalog_copy_load_diagnostic(&detail,&load,MAELYS_ERR_INVALID_FIELD);
    assert(detail.present & MAELYS_DATALOG_DIAGNOSTIC_ARITY);
    assert(detail.present & MAELYS_DATALOG_DIAGNOSTIC_CONTEXT);
    assert(detail.present & MAELYS_DATALOG_DIAGNOSTIC_LOCATION);
    assert(detail.expected_arity==2 && detail.observed_arity==3);
    assert(!strcmp(detail.file,"source.dl") && !strcmp(detail.field,"arity") && !strcmp(detail.domain,"export"));
    free(r);
    return 0;
}
