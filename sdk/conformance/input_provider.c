/* SPDX-License-Identifier: MIT */
#include <maelys/datalog_backend.h>
#include "input_provider.h"
#include <string.h>
#include <stddef.h>
typedef struct {
    maelys_datalog_fact_t fact;
    char predicate[64], text[MAELYS_DATALOG_PUBLIC_MAX_TERMS][64];
} input_fixture_row;
typedef struct {
    size_t capacity,count,candidate_count,rules;
    const char *source[8],*head[8];
    size_t arity[8];
    input_fixture_row *live,*candidate;
    maelys_datalog_input_base_t base,next;
    int staged,committed;
    unsigned fault;
    input_fixture_observation observation;
} input_fixture_state;
static maelys_datalog_status_t input_fixture_plan(const maelys_datalog_program_t *p,
    const maelys_datalog_session_resources_t *r,size_t *bytes,size_t *alignment) {
    maelys_datalog_program_info_t info;maelys_datalog_status_t rc=maelys_datalog_program_info(p,&info);if(rc)return rc;
    if(info.fact_count || info.rule_count>8 || (info.required_capabilities & ~MAELYS_DATALOG_CAP_POSITIVE))return MAELYS_DATALOG_STATUS_UNSUPPORTED;
    for(size_t i=0;i<info.rule_count;++i) {
        maelys_datalog_ir_rule_t rule;rc=maelys_datalog_program_rule(p,i,&rule);if(rc)return rc;
        if(rule.body_count!=1 || rule.body[0].kind!=MAELYS_DATALOG_IR_ATOM ||
           rule.head.arity!=rule.body[0].atom.arity)return MAELYS_DATALOG_STATUS_UNSUPPORTED;
        for(size_t j=0;j<rule.head.arity;++j) {
            if(rule.head.terms[j].kind!=MAELYS_DATALOG_IR_VARIABLE ||
               rule.body[0].atom.terms[j].kind!=MAELYS_DATALOG_IR_VARIABLE ||
               rule.head.terms[j].as.variable!=rule.body[0].atom.terms[j].as.variable)return MAELYS_DATALOG_STATUS_UNSUPPORTED;
            for(size_t k=0;k<j;++k)if(rule.head.terms[k].as.variable==rule.head.terms[j].as.variable)return MAELYS_DATALOG_STATUS_UNSUPPORTED;
        }
        int edb=0;
        for(size_t j=0;j<info.predicate_count;++j) {
            maelys_datalog_predicate_t pred;rc=maelys_datalog_program_predicate(p,j,&pred);if(rc)return rc;
            if(!strcmp(pred.name,rule.body[0].atom.predicate) && pred.arity==rule.body[0].atom.arity)
                edb=!!(pred.flags&MAELYS_DATALOG_PREDICATE_EDB);
        }
        if(!edb)return MAELYS_DATALOG_STATUS_UNSUPPORTED;
    }
    if(r->input_facts>(SIZE_MAX-sizeof(input_fixture_state))/(2*sizeof(input_fixture_row)))return MAELYS_DATALOG_STATUS_UNSUPPORTED;
    *bytes=sizeof(input_fixture_state)+2*r->input_facts*sizeof(input_fixture_row);*alignment=_Alignof(max_align_t);
    return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t input_fixture_prepare(const maelys_datalog_program_t *p,
    const maelys_datalog_session_resources_t *r,const maelys_datalog_backend_storage_t *storage,void **out) {
    size_t bytes,align;maelys_datalog_status_t rc=input_fixture_plan(p,r,&bytes,&align);if(rc)return rc;
    if(storage->size<bytes)return MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL;
    input_fixture_state *s=storage->bytes;memset(s,0,sizeof(*s));s->capacity=r->input_facts;
    s->live=(void *)(s+1);s->candidate=s->live+s->capacity;
    maelys_datalog_program_info_t info;rc=maelys_datalog_program_info(p,&info);if(rc)return rc;s->rules=info.rule_count;
    for(size_t i=0;i<s->rules;++i) {
        maelys_datalog_ir_rule_t rule;rc=maelys_datalog_program_rule(p,i,&rule);if(rc)return rc;
        s->head[i]=rule.head.predicate;s->source[i]=rule.body[0].atom.predicate;s->arity[i]=rule.head.arity;
    }
    *out=s;return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t input_fixture_copy(input_fixture_row *row,const maelys_datalog_fact_t *f) {
    if(strlen(f->predicate)>=sizeof(row->predicate))return MAELYS_DATALOG_STATUS_UNSUPPORTED;
    for(size_t t=0;t<f->arity;++t)if(f->terms[t].kind==MAELYS_DATALOG_VALUE_SYMBOL &&
        strlen(f->terms[t].as.symbol)>=sizeof(row->text[t]))return MAELYS_DATALOG_STATUS_UNSUPPORTED;
    row->fact=*f;strcpy(row->predicate,f->predicate);row->fact.predicate=row->predicate;
    for(size_t t=0;t<f->arity;++t)if(f->terms[t].kind==MAELYS_DATALOG_VALUE_SYMBOL) {
        strcpy(row->text[t],f->terms[t].as.symbol);row->fact.terms[t].as.symbol=row->text[t];
    }
    return MAELYS_DATALOG_STATUS_OK;
}
static int input_fixture_equal(const maelys_datalog_fact_t *a,const maelys_datalog_fact_t *b) {
    if(a->arity!=b->arity || strcmp(a->predicate,b->predicate))return 0;
    for(size_t i=0;i<a->arity;++i) {
        if(a->terms[i].kind!=b->terms[i].kind)return 0;
        if(a->terms[i].kind==MAELYS_DATALOG_VALUE_SYMBOL) { if(strcmp(a->terms[i].as.symbol,b->terms[i].as.symbol))return 0; }
        else if(a->terms[i].kind==MAELYS_DATALOG_VALUE_INTEGER) { if(a->terms[i].as.integer!=b->terms[i].as.integer)return 0; }
        else if(a->terms[i].as.boolean!=b->terms[i].as.boolean)return 0;
    }
    return 1;
}
static maelys_datalog_status_t input_fixture_derive(input_fixture_state *s,maelys_datalog_backend_output_t *out) {
    if(s->fault==1)return MAELYS_DATALOG_STATUS_INVALID_FIELD;
    if(s->fault==2) {
        maelys_datalog_fact_t bad={.predicate="undeclared",.arity=0};(void)maelys_datalog_backend_emit(out,&bad);
        return MAELYS_DATALOG_STATUS_OK;
    }
    if(s->fault==3) { (void)maelys_datalog_backend_charge(out,UINT64_MAX);return MAELYS_DATALOG_STATUS_OK; }
    for(size_t i=0;i<s->candidate_count;++i)for(size_t j=0;j<s->rules;++j) {
        const maelys_datalog_fact_t *f=&s->candidate[i].fact;
        if(f->arity!=s->arity[j] || strcmp(f->predicate,s->source[j]))continue;
        maelys_datalog_status_t rc=maelys_datalog_backend_charge(out,1);if(rc)return rc;
        maelys_datalog_fact_t value=*f;value.predicate=s->head[j];rc=maelys_datalog_backend_emit(out,&value);if(rc)return rc;
    }
    return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t input_fixture_snapshot_solve(void *state,const maelys_datalog_fact_t *facts,
    size_t n,maelys_datalog_backend_output_t *out,void **result,maelys_datalog_diagnostic_t *diag) {
    (void)diag;input_fixture_state *s=state;*result=s;s->staged=1;s->committed=0;
    if(n>s->capacity)return MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE;
    ++s->observation.snapshots;s->observation.delivered+=n;s->candidate_count=n;
    for(size_t i=0;i<n;++i){maelys_datalog_status_t rc=input_fixture_copy(&s->candidate[i],&facts[i]);if(rc)return rc;}
    return input_fixture_derive(s,out);
}
static maelys_datalog_status_t input_fixture_solve(void *state,const maelys_datalog_backend_input_t *packet,
    maelys_datalog_backend_output_t *out,void **result,maelys_datalog_diagnostic_t *diag) {
    (void)diag;input_fixture_state *s=state;*result=s;s->staged=1;s->committed=0;
    if(packet->struct_size<sizeof(*packet) || packet->contract_version!=1 ||
       packet->base.generation==UINT64_MAX || packet->next.generation!=packet->base.generation+1 ||
       packet->next.incarnation!=packet->base.incarnation || !packet->base.incarnation ||
       (s->base.incarnation?memcmp(&s->base,&packet->base,sizeof(s->base)):packet->base.generation!=0))return MAELYS_DATALOG_STATUS_INVALID_STATE;
    s->next=packet->next;s->candidate_count=0;
    if(packet->kind==MAELYS_DATALOG_BACKEND_INPUT_DELTA) {
        ++s->observation.deltas;s->candidate_count=s->count;
        for(size_t i=0;i<s->count;++i){maelys_datalog_status_t rc=input_fixture_copy(&s->candidate[i],&s->live[i].fact);if(rc)return rc;}
    } else if(packet->kind!=MAELYS_DATALOG_BACKEND_INPUT_REPLACE || packet->removal_count)return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    else ++s->observation.snapshots;
    for(size_t side=0;side<2;++side) {
        size_t n=side?packet->addition_count:packet->removal_count;
        const maelys_datalog_backend_input_view_t *view=side?packet->additions:packet->removals;
        maelys_datalog_fact_t unchanged={.arity=99},check=unchanged;
        if(maelys_datalog_backend_input_at(view,n,&check)!=MAELYS_DATALOG_STATUS_NOT_FOUND || memcmp(&check,&unchanged,sizeof(check)))return MAELYS_DATALOG_STATUS_INTERNAL;
        for(size_t i=0;i<n;++i) {
            maelys_datalog_fact_t f;maelys_datalog_status_t rc=maelys_datalog_backend_input_at(view,i,&f);if(rc)return rc;
            ++s->observation.delivered;rc=maelys_datalog_backend_charge(out,1);if(rc)return rc;
            size_t at=0;while(at<s->candidate_count && !input_fixture_equal(&s->candidate[at].fact,&f))++at;
            if(!side && at<s->candidate_count) {
                --s->candidate_count;if(at<s->candidate_count){rc=input_fixture_copy(&s->candidate[at],&s->candidate[s->candidate_count].fact);if(rc)return rc;}
            } else if(side && at==s->candidate_count) {
                if(at==s->capacity)return MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE;
                rc=input_fixture_copy(&s->candidate[s->candidate_count++],&f);if(rc)return rc;
            }
        }
    }
    return input_fixture_derive(s,out);
}
static void input_fixture_commit(void *state,void *result) {
    input_fixture_state *s=state;(void)result;
    input_fixture_row *swap=s->live;s->live=s->candidate;s->candidate=swap;
    s->count=s->candidate_count;s->base=s->next;s->committed=1;++s->observation.commits;
}
static void input_fixture_release(void *state,void *result) {
    input_fixture_state *s=state;(void)result;
    if(s->staged) { if(s->committed)++s->observation.releases;else ++s->observation.aborts; }
    s->staged=0;s->committed=0;
}
static void input_fixture_destroy(void *state) { (void)state; }
static maelys_datalog_status_t input_fixture_explain_size(void *state,void *result,
    maelys_datalog_explanation_kind_t kind,size_t *bytes,size_t *alignment) {
    (void)state;(void)result;if(kind!=MAELYS_DATALOG_EXPLAIN_TRUE)return MAELYS_DATALOG_STATUS_UNSUPPORTED;
    *bytes=1;*alignment=1;return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t input_fixture_explain_prepare(void *state,void *result,
    maelys_datalog_explanation_kind_t kind,const char *predicate,const maelys_datalog_value_t *terms,
    size_t arity,void *storage,size_t bytes,size_t *text_size) {
    (void)result;input_fixture_state *s=state;
    if(kind!=MAELYS_DATALOG_EXPLAIN_TRUE)return MAELYS_DATALOG_STATUS_UNSUPPORTED;
    if(!bytes)return MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL;
    maelys_datalog_fact_t query={.predicate=predicate,.arity=arity};memcpy(query.terms,terms,arity*sizeof(*terms));
    for(size_t i=0;i<s->count;++i)for(size_t j=0;j<s->rules;++j) {
        maelys_datalog_fact_t fact=s->live[i].fact;
        if(fact.arity!=s->arity[j] || strcmp(fact.predicate,s->source[j]))continue;
        fact.predicate=s->head[j];if(input_fixture_equal(&query,&fact)) {
            *(unsigned char *)storage=1;*text_size=strlen("identity projection from committed EDB");return MAELYS_DATALOG_STATUS_OK;
        }
    }
    return MAELYS_DATALOG_STATUS_NOT_FOUND;
}
static maelys_datalog_status_t input_fixture_explain_write(void *state,void *result,
    maelys_datalog_explanation_kind_t kind,const void *storage,char *text,size_t capacity) {
    (void)state;(void)result;(void)kind;(void)storage;
    const char message[]="identity projection from committed EDB";
    if(capacity<sizeof(message))return MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL;
    memcpy(text,message,sizeof(message));return MAELYS_DATALOG_STATUS_OK;
}
#define INPUT_COMMON \
    .name="input_conformance",.semantic_id="public.identity-projection.v1", \
    .capabilities=MAELYS_DATALOG_CAP_POSITIVE|MAELYS_DATALOG_CAP_WORK_LIMIT|MAELYS_DATALOG_CAP_EXPLAIN_TRUE, \
    .resource_features=MAELYS_DATALOG_RESOURCE_SESSION_CAPACITIES, \
    .storage_requirements=input_fixture_plan,.prepare=input_fixture_prepare, \
    .commit=input_fixture_commit,.destroy_result=input_fixture_release,.destroy=input_fixture_destroy, \
    .explanation_storage_requirements=input_fixture_explain_size,.explanation_prepare=input_fixture_explain_prepare, \
    .explanation_write_text=input_fixture_explain_write
const maelys_datalog_backend_v6_t *input_fixture_snapshot(void) {
    static const maelys_datalog_backend_v6_t b={.abi_version=6,.struct_size=sizeof(b),INPUT_COMMON,.solve=input_fixture_snapshot_solve};return &b;
}
const maelys_datalog_backend_v7_t *input_fixture_transactions(void) {
    static const maelys_datalog_backend_v7_t b={.abi_version=7,.struct_size=sizeof(b),INPUT_COMMON,.solve=input_fixture_solve};return &b;
}
void input_fixture_fault(void *storage,unsigned mode) { ((input_fixture_state *)storage)->fault=mode; }
void input_fixture_observe(const void *storage,input_fixture_observation *out) {
    const input_fixture_state *s=storage;*out=s->observation;out->base=s->base;out->retained=s->count;
    uint64_t digest=0;
    for(size_t i=0;i<s->count;++i) {
        const maelys_datalog_fact_t *f=&s->live[i].fact;uint64_t hash=UINT64_C(14695981039346656037);
        for(const char *p=f->predicate;*p;++p)hash=(hash^(unsigned char)*p)*UINT64_C(1099511628211);
        for(size_t j=0;j<f->arity;++j) {
            hash=(hash^f->terms[j].kind)*UINT64_C(1099511628211);
            if(f->terms[j].kind==MAELYS_DATALOG_VALUE_SYMBOL)for(const char *p=f->terms[j].as.symbol;*p;++p)hash=(hash^(unsigned char)*p)*UINT64_C(1099511628211);
            else hash=(hash^(uint64_t)(f->terms[j].kind==MAELYS_DATALOG_VALUE_INTEGER?f->terms[j].as.integer:f->terms[j].as.boolean))*UINT64_C(1099511628211);
        }
        digest^=hash;
    }
    out->digest=digest;
}

size_t input_fixture_committed(const void *storage,void *out,size_t capacity) {
    const input_fixture_state *s=storage;
    size_t bytes=sizeof(s->base)+s->count*sizeof(*s->live);
    if(out && capacity>=bytes) {
        memcpy(out,&s->base,sizeof(s->base));
        memcpy((unsigned char *)out+sizeof(s->base),s->live,s->count*sizeof(*s->live));
    }
    return bytes;
}
