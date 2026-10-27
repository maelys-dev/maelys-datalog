/* SPDX-License-Identifier: MPL-2.0 */
/* Golden observations generated with this same observer on signed v0.26.0.
 * Semantic fields and text bytes only: no addresses or native record padding. */
#include <maelys/datalog_explanations.h>
#include "src/public/maelys_datalog_public_internal.h"
#include "common/maelys_sha256.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) {fprintf(stderr,"%d: %s\n",__LINE__,#x);exit(1);} } while(0)
#define OK(x) CHECK((x)==MAELYS_DATALOG_STATUS_OK)
static char observation[65536];static size_t used;
#if MAELYS_DATALOG_HAS_RULE_VARIABLE_NAMES
static const maelys_datalog_program_t *name_program;
static const char *name_source;
static void binding_name(size_t rule_id, unsigned variable) {
    if (!rule_id) return;
    const char *name = NULL;
    maelys_datalog_status_t rc = maelys_datalog_program_rule_variable_name(name_program,rule_id-1,variable,&name);
    if(variable>=26) CHECK(rc==MAELYS_DATALOG_STATUS_NOT_FOUND);
    else {OK(rc);CHECK(name && strstr(name_source,name));}
}
#endif
static void number(uint64_t n) { int k=snprintf(observation+used,sizeof(observation)-used,"%" PRIu64 ";",n);CHECK(k>0 && (size_t)k<sizeof(observation)-used);used+=(size_t)k; }
static void text(const char *s) {if(!s)s="";number(strlen(s));size_t n=strlen(s);CHECK(n<sizeof(observation)-used);memcpy(observation+used,s,n);used+=n;}
static void value(maelys_datalog_value_t v) {number(v.kind);if(v.kind==MAELYS_DATALOG_VALUE_SYMBOL)text(v.as.symbol);else if(v.kind==MAELYS_DATALOG_VALUE_INTEGER)number((uint64_t)v.as.integer);else if(v.kind==MAELYS_DATALOG_VALUE_BOOLEAN)number(v.as.boolean);}
static void fact(maelys_datalog_fact_t f) {text(f.predicate);number(f.arity);for(size_t i=0;i<f.arity;++i)value(f.terms[i]);}
static void atom(maelys_datalog_ir_atom_t a) {text(a.predicate);number(a.arity);for(size_t i=0;i<a.arity;++i){maelys_datalog_ir_term_t t=a.terms[i];number(t.kind);if(t.kind==MAELYS_DATALOG_IR_SYMBOL)text(t.as.symbol);else if(t.kind==MAELYS_DATALOG_IR_INTEGER)number((uint64_t)t.as.integer);else if(t.kind==MAELYS_DATALOG_IR_BOOLEAN)number(t.as.boolean);else if(t.kind==MAELYS_DATALOG_IR_VARIABLE)number(t.as.variable);}}
#define N(o,f) number((o).f)
static void views(maelys_datalog_prepared_explanation_t *p) {
 maelys_datalog_explanation_info_t i;OK(maelys_datalog_prepared_explanation_info(p,&i));
 N(i,kind);N(i,found);N(i,truncated);N(i,step_count);N(i,premise_count);N(i,false_status);N(i,false_summary);N(i,query_origin);N(i,limit_hits);N(i,candidate_rule_count);N(i,substitution_count);N(i,diagnostic_count);N(i,filter_cost_units);fact(i.query);
 for(size_t n=0;n<i.step_count;++n){maelys_datalog_explanation_step_view_t s;OK(maelys_datalog_prepared_explanation_step(p,n,&s));N(s,rule_id);N(s,premise_begin);N(s,premise_count);fact(s.fact);
#if MAELYS_DATALOG_HAS_RULE_VARIABLE_NAMES
 if(s.rule_id){maelys_datalog_ir_rule_t rule;OK(maelys_datalog_program_rule(name_program,s.rule_id-1,&rule));for(size_t t=0;t<rule.head.arity;++t)if(rule.head.terms[t].kind==MAELYS_DATALOG_IR_VARIABLE)binding_name(s.rule_id,rule.head.terms[t].as.variable);}
#endif
}
 for(size_t n=0;n<i.premise_count;++n){maelys_datalog_explanation_premise_view_t s;OK(maelys_datalog_prepared_explanation_premise(p,n,&s));N(s,kind);N(s,origin);N(s,body_index);N(s,parent_step);atom(s.atom);N(s,op);value(s.lhs);value(s.rhs);value(s.filter_value);N(s,filter_program_index);N(s,filter_kind);N(s,projected_variable);N(s,aggregate_value);}
 for(size_t n=0;n<i.diagnostic_count;++n){maelys_datalog_explanation_obstacle_view_t s;OK(maelys_datalog_prepared_explanation_obstacle(p,n,&s));N(s,rule_id);N(s,depth);fact(s.target);N(s,bound_variable_mask);for(unsigned v=0;v<32;++v)if(s.bound_variable_mask & (UINT32_C(1)<<v)){value(s.substitution[v]);
#if MAELYS_DATALOG_HAS_RULE_VARIABLE_NAMES
 binding_name(s.rule_id,v);
#endif
}N(s,support_count);for(size_t k=0;k<s.support_count;++k){N(s.supports[k],body_index);N(s.supports[k],origin);fact(s.supports[k].fact);}N(s,obstacle_kind);N(s,obstacle_origin);N(s,body_index);N(s,unbound_term_mask);atom(s.pattern);N(s,op);value(s.lhs);value(s.rhs);value(s.filter_value);N(s,filter_program_index);N(s,filter_kind);}
}
static const char *sources[]={
 "pair(A,B) :- edge(A,B).",
 "pair(User,Resource) :- edge(User,Resource).",
 "pair(Resource,User) :- edge(User,Resource).",
 "pair(U,Resource) :- edge(U,Resource).",
 "pair(User,1) :- edge(User,_).",
 "pair(User,User) :- seed(User) or edge(User,Other).",
 "visible(Total) :- count(Item,seed(Item),Total).",
 "visible(User) :- seed(User), contains(User,\"x\").",
 "visible(Result) :- edge(Input,Result), Result > Input + 0."
};
#ifndef RULE_NAMES_GENERATE_GOLDEN
#include "fixtures/rule_name_identity_v0260.h"
#endif
#define INT(n) {.kind=MAELYS_DATALOG_VALUE_INTEGER,.as.integer=(n)}
#define SYM(s) {.kind=MAELYS_DATALOG_VALUE_SYMBOL,.as.symbol=(s)}
int main(void){
 const maelys_datalog_predicate_t defs[]={{"seed",1,MAELYS_DATALOG_PREDICATE_EDB},{"edge",2,MAELYS_DATALOG_PREDICATE_EDB},{"pair",2,MAELYS_DATALOG_PREDICATE_IDB|MAELYS_DATALOG_PREDICATE_QUERY},{"visible",1,MAELYS_DATALOG_PREDICATE_IDB|MAELYS_DATALOG_PREDICATE_QUERY}};
 const char *atoms[]={"x"};const maelys_datalog_domain_t d={"name_identity",defs,4,atoms,1};OK(maelys_datalog_domain_register(&d));
 maelys_datalog_fact_t inputs[]={{.predicate="seed",.arity=1,.terms={INT(1)}},{.predicate="seed",.arity=1,.terms={SYM("x")}},{.predicate="edge",.arity=2,.terms={INT(1),INT(2)}}};
 for(size_t f=0;f<sizeof(sources)/sizeof(sources[0]);++f){
 maelys_datalog_policy_t *p;OK(maelys_datalog_policy_load_inline(d.name,"golden",sources[f],strlen(sources[f]),&p,NULL));maelys_datalog_session_t *s;OK(maelys_datalog_session_create(p,0,&s));const maelys_datalog_program_t *program;OK(maelys_datalog_session_program(s,&program));
#if MAELYS_DATALOG_HAS_RULE_VARIABLE_NAMES
 name_program=program;name_source=sources[f];
#endif
 char hashes[6][65];OK(maelys_datalog_policy_fingerprint(p,hashes[0]));OK(maelys_datalog_program_fingerprint(program,hashes[1]));OK(maelys_datalog_session_execution_fingerprint(s,hashes[2]));strcpy(hashes[3],p->set.policies[0].sha256);
 maelys_datalog_result_t *r;OK(maelys_datalog_session_solve(s,f==7?inputs+1:inputs,f==7?1:3,&r,NULL));
 char texts[32768]="";size_t text_used=0;used=0;
 const char *predicate=f<6?"pair":"visible";size_t arity=f<6?2:1;
 maelys_datalog_fact_view_t facts[16];size_t count;OK(maelys_datalog_result_enumerate(r,predicate,arity,facts,16,&count));CHECK(count);
 for(int kind=1;kind<=2;++kind){maelys_datalog_value_t q[2]={INT(99),INT(99)};
 if(kind==MAELYS_DATALOG_EXPLAIN_TRUE){for(size_t t=0;t<arity;++t){q[t].kind=facts[0].terms[t].kind;if(q[t].kind==MAELYS_DATALOG_VALUE_INTEGER)q[t].as.integer=facts[0].terms[t].as.integer;else if(q[t].kind==MAELYS_DATALOG_VALUE_BOOLEAN)q[t].as.boolean=facts[0].terms[t].as.boolean;if(q[t].kind==MAELYS_DATALOG_VALUE_SYMBOL){const char *name;size_t bytes;OK(maelys_datalog_result_symbol_text(r,facts[0].terms[t].as.symbol_id,&name,&bytes));q[t].as.symbol=name;}}}
 size_t bytes,alignment;OK(maelys_datalog_result_explanation_storage_requirements(r,(maelys_datalog_explanation_kind_t)kind,&bytes,&alignment));void *storage=malloc(bytes);CHECK(storage);maelys_datalog_prepared_explanation_t *e;OK(maelys_datalog_result_prepare_explanation(r,(maelys_datalog_explanation_kind_t)kind,predicate,q,arity,storage,bytes,&e));views(e);size_t length;OK(maelys_datalog_prepared_explanation_text_size(e,&length));CHECK(length+1<sizeof(texts)-text_used);OK(maelys_datalog_prepared_explanation_write_text(e,texts+text_used,sizeof(texts)-text_used));text_used+=length;texts[text_used++]='\n';OK(maelys_datalog_prepared_explanation_release(e));free(storage);}
 CHECK(!maelys_sha256_hex((unsigned char*)texts,text_used,hashes[4]));CHECK(!maelys_sha256_hex((unsigned char*)observation,used,hashes[5]));
#ifdef RULE_NAMES_GENERATE_GOLDEN
 printf("{ ");for(unsigned k=0;k<6;++k)printf("\"%s\"%s",hashes[k],k==5?" },\n":", ");
#else
 for(unsigned k=0;k<6;++k){if(strcmp(hashes[k],rule_name_identity_v0260[f][k]))fprintf(stderr,"fixture %zu identity %u: %s != %s\n",f,k,hashes[k],rule_name_identity_v0260[f][k]);CHECK(!strcmp(hashes[k],rule_name_identity_v0260[f][k]));}
#endif
 OK(maelys_datalog_result_free(r));OK(maelys_datalog_session_free(s));OK(maelys_datalog_policy_free(p));}
#ifndef RULE_NAMES_GENERATE_GOLDEN
 puts("v0.26.0 identities, explanation text and structured fields: PASS");
#endif
 return 0;
}
