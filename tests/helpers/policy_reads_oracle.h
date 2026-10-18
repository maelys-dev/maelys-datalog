/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_TEST_POLICY_READS_ORACLE_H
#define MAELYS_TEST_POLICY_READS_ORACLE_H
#include <maelys/datalog_program.h>
#include <stdio.h>
#include <string.h>
/* Independent walk of public IR: no cache, private literal, or dependency helper. */
static int policy_reads_oracle(const maelys_datalog_policy_t *policy, size_t index,
                               const maelys_datalog_program_t *program) {
    maelys_datalog_program_info_t info;
    if(maelys_datalog_program_info(program,&info)) return 1;
    for(size_t p=0;p<info.predicate_count;p++) {
        maelys_datalog_predicate_t predicate;
        if(maelys_datalog_program_predicate(program,p,&predicate)) return 1;
        int expected=0, actual=-1;
        for(size_t r=0;r<info.rule_count;r++) {
            maelys_datalog_ir_rule_t rule;
            if(maelys_datalog_program_rule(program,r,&rule)) return 1;
            for(size_t j=0;j<rule.body_count;j++) {
                const maelys_datalog_ir_literal_t *l=&rule.body[j];
                if(l->kind==MAELYS_DATALOG_IR_COMPARISON || l->kind==MAELYS_DATALOG_IR_FILTER) continue;
                if(l->kind==MAELYS_DATALOG_IR_ATOM || l->kind==MAELYS_DATALOG_IR_NEGATION ||
                   l->kind==MAELYS_DATALOG_IR_COUNT || l->kind==MAELYS_DATALOG_IR_MIN ||
                   l->kind==MAELYS_DATALOG_IR_MAX || l->kind==MAELYS_DATALOG_IR_SUM) {
                    if(l->atom.arity==predicate.arity && !strcmp(l->atom.predicate,predicate.name)) expected=1;
                } else expected=1;
            }
        }
        if(maelys_datalog_policy_reads_predicate(policy,index,predicate.name,predicate.arity,&actual) || actual!=expected) {
            fprintf(stderr,"IR read oracle: %s/%zu expected %d got %d\n",predicate.name,predicate.arity,expected,actual);
            return 1;
        }
    }
    return 0;
}
#endif
