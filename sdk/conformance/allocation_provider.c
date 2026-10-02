/* SPDX-License-Identifier: MIT */
#include <maelys/datalog_backend.h>
#include "allocation_provider.h"
#include <string.h>
#include <stddef.h>
typedef struct {
    maelys_datalog_fact_t fact;
    char predicate[64], text[MAELYS_DATALOG_PUBLIC_MAX_TERMS][64];
} allocation_fixture_row;
typedef struct {
    size_t capacity,count,candidate_count,rules;
    const char *source[8],*head[8],*blocked[8];
    int negation;
    allocation_fixture_row *live_output,*candidate_output;
    size_t output_count,candidate_output_count;
    size_t arity[8];
    allocation_fixture_row *live,*candidate;
    maelys_datalog_input_base_t base,next;
    int staged,committed;
    unsigned fault;
    allocation_fixture_observation observation;
    maelys_datalog_allocation_service_t allocation;
    void *workspace, *temporary;
    void (*hook)(void *,const maelys_datalog_allocation_service_t *);
    void *hook_context;
} allocation_fixture_state;
static maelys_datalog_status_t allocation_fixture_plan(const maelys_datalog_program_t *p,
    const maelys_datalog_session_resources_t *r,size_t *bytes,size_t *alignment) {
    maelys_datalog_program_info_t info;maelys_datalog_status_t rc=maelys_datalog_program_info(p,&info);if(rc)return rc;
    if(info.fact_count || info.rule_count>8 || (info.required_capabilities & ~(MAELYS_DATALOG_CAP_POSITIVE|MAELYS_DATALOG_CAP_NEGATION)))return MAELYS_DATALOG_STATUS_UNSUPPORTED;
    for(size_t i=0;i<info.rule_count;++i) {
        maelys_datalog_ir_rule_t rule;rc=maelys_datalog_program_rule(p,i,&rule);if(rc)return rc;
        if((rule.body_count!=1 && rule.body_count!=2) || rule.body[0].kind!=MAELYS_DATALOG_IR_ATOM ||
           (rule.body_count==2 && (rule.body[1].kind!=MAELYS_DATALOG_IR_NEGATION || rule.body[1].atom.arity!=rule.head.arity)) ||
           rule.head.arity!=rule.body[0].atom.arity)return MAELYS_DATALOG_STATUS_UNSUPPORTED;
        for(size_t j=0;j<rule.head.arity;++j) {
            if(rule.head.terms[j].kind!=MAELYS_DATALOG_IR_VARIABLE ||
               rule.body[0].atom.terms[j].kind!=MAELYS_DATALOG_IR_VARIABLE ||
               rule.head.terms[j].as.variable!=rule.body[0].atom.terms[j].as.variable)return MAELYS_DATALOG_STATUS_UNSUPPORTED;
            if(rule.body_count==2 && (rule.body[1].atom.terms[j].kind!=MAELYS_DATALOG_IR_VARIABLE ||
               rule.body[1].atom.terms[j].as.variable!=rule.head.terms[j].as.variable))return MAELYS_DATALOG_STATUS_UNSUPPORTED;
            for(size_t k=0;k<j;++k)if(rule.head.terms[k].as.variable==rule.head.terms[j].as.variable)return MAELYS_DATALOG_STATUS_UNSUPPORTED;
        }
        int edb=0,negative_edb=rule.body_count==1;
        for(size_t j=0;j<info.predicate_count;++j) {
            maelys_datalog_predicate_t pred;rc=maelys_datalog_program_predicate(p,j,&pred);if(rc)return rc;
            if(!strcmp(pred.name,rule.body[0].atom.predicate) && pred.arity==rule.body[0].atom.arity)
                edb=!!(pred.flags&MAELYS_DATALOG_PREDICATE_EDB);
            if(rule.body_count==2 && !strcmp(pred.name,rule.body[1].atom.predicate) && pred.arity==rule.body[1].atom.arity)
                negative_edb=!!(pred.flags&MAELYS_DATALOG_PREDICATE_EDB);
        }
        if(!edb || !negative_edb)return MAELYS_DATALOG_STATUS_UNSUPPORTED;
    }
    if(r->struct_size<sizeof(maelys_datalog_session_allocation_resources_t) ||
       r->memory_mode!=MAELYS_DATALOG_MEMORY_BACKEND_ELASTIC ||
       !(r->required_features&MAELYS_DATALOG_RESOURCE_CALLER_ALLOCATOR))return MAELYS_DATALOG_STATUS_UNSUPPORTED;
    *bytes=sizeof(allocation_fixture_state);*alignment=_Alignof(max_align_t);
    return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t allocation_fixture_prepare(const maelys_datalog_program_t *p,
    const maelys_datalog_session_resources_t *r,const maelys_datalog_backend_storage_t *storage,void **out) {
    size_t bytes,align;maelys_datalog_status_t rc=allocation_fixture_plan(p,r,&bytes,&align);if(rc)return rc;
    if(storage->size<bytes)return MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL;
    allocation_fixture_state *s=storage->bytes;memset(s,0,sizeof(*s));s->capacity=r->input_facts;
    const maelys_datalog_session_allocation_resources_t *extended=(const void *)r;
    if(!extended->allocation || extended->allocation->struct_size<sizeof(s->allocation))return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    s->allocation=*extended->allocation;
    /* Two bounded prepare requests make partial preparation observable. The
     * host also owns them if no provider state is returned on failure. */
    rc=s->allocation.acquire(s->allocation.context,sizeof(allocation_fixture_row),_Alignof(allocation_fixture_row),(void **)&s->live,NULL);
    if(rc)return rc;
    rc=s->allocation.acquire(s->allocation.context,128,1,&s->workspace,NULL);
    if(rc)return rc;
    memset(s->workspace,0,128);
    maelys_datalog_program_info_t info;rc=maelys_datalog_program_info(p,&info);if(rc)return rc;s->rules=info.rule_count;
    for(size_t i=0;i<s->rules;++i) {
        maelys_datalog_ir_rule_t rule;rc=maelys_datalog_program_rule(p,i,&rule);if(rc)return rc;
        s->head[i]=rule.head.predicate;s->source[i]=rule.body[0].atom.predicate;s->arity[i]=rule.head.arity;
        if(rule.body_count==2){s->blocked[i]=rule.body[1].atom.predicate;s->negation=1;}
    }
    *out=s;return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t allocation_fixture_copy(allocation_fixture_row *row,const maelys_datalog_fact_t *f) {
    if(strlen(f->predicate)>=sizeof(row->predicate))return MAELYS_DATALOG_STATUS_UNSUPPORTED;
    for(size_t t=0;t<f->arity;++t)if(f->terms[t].kind==MAELYS_DATALOG_VALUE_SYMBOL &&
        strlen(f->terms[t].as.symbol)>=sizeof(row->text[t]))return MAELYS_DATALOG_STATUS_UNSUPPORTED;
    row->fact=*f;strcpy(row->predicate,f->predicate);row->fact.predicate=row->predicate;
    for(size_t t=0;t<f->arity;++t)if(f->terms[t].kind==MAELYS_DATALOG_VALUE_SYMBOL) {
        strcpy(row->text[t],f->terms[t].as.symbol);row->fact.terms[t].as.symbol=row->text[t];
    }
    return MAELYS_DATALOG_STATUS_OK;
}
static int allocation_fixture_equal(const maelys_datalog_fact_t *a,const maelys_datalog_fact_t *b) {
    if(a->arity!=b->arity || strcmp(a->predicate,b->predicate))return 0;
    for(size_t i=0;i<a->arity;++i) {
        if(a->terms[i].kind!=b->terms[i].kind)return 0;
        if(a->terms[i].kind==MAELYS_DATALOG_VALUE_SYMBOL) { if(strcmp(a->terms[i].as.symbol,b->terms[i].as.symbol))return 0; }
        else if(a->terms[i].kind==MAELYS_DATALOG_VALUE_INTEGER) { if(a->terms[i].as.integer!=b->terms[i].as.integer)return 0; }
        else if(a->terms[i].as.boolean!=b->terms[i].as.boolean)return 0;
    }
    return 1;
}
/* Deliberately small anti-join: every bound source row is tested against EDB.
 * It is an allocation/lifecycle oracle, not an incremental implementation. */
static maelys_datalog_status_t allocation_fixture_allowed(allocation_fixture_state *s,
    const maelys_datalog_fact_t *f,size_t rule,maelys_datalog_backend_output_t *out,int *yes) {
    *yes=1;
    if(!s->blocked[rule])return MAELYS_DATALOG_STATUS_OK;
    maelys_datalog_fact_t blocker=*f;blocker.predicate=s->blocked[rule];
    for(size_t i=0;i<s->candidate_count;++i) {
        maelys_datalog_status_t rc=maelys_datalog_backend_charge(out,1);if(rc)return rc;
        if(allocation_fixture_equal(&blocker,&s->candidate[i].fact)){*yes=0;break;}
    }
    return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t allocation_fixture_derive(allocation_fixture_state *s,maelys_datalog_backend_output_t *out) {
    if(s->fault==1 || s->fault==5)return MAELYS_DATALOG_STATUS_INVALID_FIELD;
    if(s->fault==2) {
        maelys_datalog_fact_t bad={.predicate="undeclared",.arity=0};(void)maelys_datalog_backend_emit(out,&bad);
        return MAELYS_DATALOG_STATUS_OK;
    }
    if(s->fault==3) { (void)maelys_datalog_backend_charge(out,UINT64_MAX);return MAELYS_DATALOG_STATUS_OK; }
    size_t needed=0;
    /* An actual output block grows when a negated blocker disappears, even
     * while the input shrinks. The former block survives until result cleanup. */
    if(s->negation) {
        for(size_t i=0;i<s->candidate_count;++i)for(size_t j=0;j<s->rules;++j) {
            const maelys_datalog_fact_t *f=&s->candidate[i].fact;int yes;
            if(f->arity!=s->arity[j] || strcmp(f->predicate,s->source[j]))continue;
            maelys_datalog_status_t rc=allocation_fixture_allowed(s,f,j,out,&yes);if(rc)return rc;
            needed+=(size_t)yes;
        }
        maelys_datalog_status_t rc=s->allocation.acquire(s->allocation.context,
            (needed?needed:1)*sizeof(*s->candidate_output),_Alignof(allocation_fixture_row),
            (void **)&s->candidate_output,NULL);if(rc)return rc;
    }
    for(size_t i=0;i<s->candidate_count;++i)for(size_t j=0;j<s->rules;++j) {
        const maelys_datalog_fact_t *f=&s->candidate[i].fact;int yes;
        if(f->arity!=s->arity[j] || strcmp(f->predicate,s->source[j]))continue;
        maelys_datalog_status_t rc=maelys_datalog_backend_charge(out,1);if(rc)return rc;
        rc=allocation_fixture_allowed(s,f,j,out,&yes);if(rc)return rc;if(!yes)continue;
        maelys_datalog_fact_t value=*f;value.predicate=s->head[j];
        if(s->negation) {
            rc=allocation_fixture_copy(&s->candidate_output[s->candidate_output_count++],&value);if(rc)return rc;
        }
        rc=maelys_datalog_backend_emit(out,&value);if(rc)return rc;
    }
    return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t allocation_fixture_stage(allocation_fixture_state *s,
    size_t n, maelys_datalog_diagnostic_t *diag) {
    if(s->hook)s->hook(s->hook_context,&s->allocation);
    size_t rows=n?n:1;
    maelys_datalog_status_t rc=s->allocation.acquire(s->allocation.context,
        rows*sizeof(*s->candidate),_Alignof(allocation_fixture_row),(void **)&s->candidate,diag);
    if(rc)return rc;
    rc=s->allocation.acquire(s->allocation.context,64,1,&s->temporary,diag);
    if(rc)return rc;
    memset(s->temporary,0,64);
    /* Previously retained scratch is restored on either path. */
    memset(s->workspace,1,128);
    return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t allocation_fixture_snapshot_solve(void *state,const maelys_datalog_fact_t *facts,
    size_t n,maelys_datalog_backend_output_t *out,void **result,maelys_datalog_diagnostic_t *diag) {
    (void)diag;allocation_fixture_state *s=state;*result=s;s->staged=1;s->committed=0;
    if(n>s->capacity)return MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE;
    maelys_datalog_status_t stage=allocation_fixture_stage(s,n,diag);
    if(stage)return s->fault==4?MAELYS_DATALOG_STATUS_OK:stage;
    ++s->observation.snapshots;s->observation.delivered+=n;s->candidate_count=n;
    for(size_t i=0;i<n;++i){maelys_datalog_status_t rc=allocation_fixture_copy(&s->candidate[i],&facts[i]);if(rc)return rc;}
    return allocation_fixture_derive(s,out);
}
static maelys_datalog_status_t allocation_fixture_solve(void *state,const maelys_datalog_backend_input_t *packet,
    maelys_datalog_backend_output_t *out,void **result,maelys_datalog_diagnostic_t *diag) {
    (void)diag;allocation_fixture_state *s=state;*result=s;s->staged=1;s->committed=0;
    if(packet->struct_size<sizeof(*packet) || packet->contract_version!=1 ||
       packet->base.generation==UINT64_MAX || packet->next.generation!=packet->base.generation+1 ||
       packet->next.incarnation!=packet->base.incarnation || !packet->base.incarnation ||
       (s->base.incarnation?memcmp(&s->base,&packet->base,sizeof(s->base)):packet->base.generation!=0))return MAELYS_DATALOG_STATUS_INVALID_STATE;
    s->next=packet->next;s->candidate_count=0;
    maelys_datalog_status_t stage=allocation_fixture_stage(s,s->capacity,diag);
    if(stage)return s->fault==4?MAELYS_DATALOG_STATUS_OK:stage;
    if(packet->kind==MAELYS_DATALOG_BACKEND_INPUT_DELTA) {
        ++s->observation.deltas;s->candidate_count=s->count;
        for(size_t i=0;i<s->count;++i){maelys_datalog_status_t rc=allocation_fixture_copy(&s->candidate[i],&s->live[i].fact);if(rc)return rc;}
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
            size_t at=0;while(at<s->candidate_count && !allocation_fixture_equal(&s->candidate[at].fact,&f))++at;
            if(!side && at<s->candidate_count) {
                --s->candidate_count;if(at<s->candidate_count){rc=allocation_fixture_copy(&s->candidate[at],&s->candidate[s->candidate_count].fact);if(rc)return rc;}
            } else if(side && at==s->candidate_count) {
                if(at==s->capacity)return MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE;
                rc=allocation_fixture_copy(&s->candidate[s->candidate_count++],&f);if(rc)return rc;
            }
        }
    }
    return allocation_fixture_derive(s,out);
}
static void allocation_fixture_commit(void *state,void *result) {
    allocation_fixture_state *s=state;(void)result;
    allocation_fixture_row *swap=s->live;s->live=s->candidate;s->candidate=swap;
    if(s->negation) {
        swap=s->live_output;s->live_output=s->candidate_output;s->candidate_output=swap;
        s->output_count=s->candidate_output_count;
    }
    s->count=s->candidate_count;s->base=s->next;s->committed=1;++s->observation.commits;
}
static void allocation_fixture_release(void *state,void *result) {
    allocation_fixture_state *s=state;(void)result;
    if(s->staged) { if(s->committed)++s->observation.releases;else ++s->observation.aborts; }
    if(s->fault!=5 && s->candidate)s->allocation.release(s->allocation.context,s->candidate);
    if(s->candidate_output)s->allocation.release(s->allocation.context,s->candidate_output);
    s->candidate_output=NULL;s->candidate_output_count=0;
    if(s->temporary)s->allocation.release(s->allocation.context,s->temporary);
    s->candidate=NULL;s->temporary=NULL;s->candidate_count=0;
    s->next=(maelys_datalog_input_base_t){0};
    memset(s->workspace,0,128);
    s->staged=0;s->committed=0;
}
static void allocation_fixture_destroy(void *state) {
    allocation_fixture_state *s=state;if(!s)return;
    if(s->live_output)s->allocation.release(s->allocation.context,s->live_output);
    if(s->live)s->allocation.release(s->allocation.context,s->live);
    if(s->workspace)s->allocation.release(s->allocation.context,s->workspace);
}
static maelys_datalog_status_t allocation_fixture_explain_size(void *state,void *result,
    maelys_datalog_explanation_kind_t kind,size_t *bytes,size_t *alignment) {
    (void)state;(void)result;if(kind!=MAELYS_DATALOG_EXPLAIN_TRUE)return MAELYS_DATALOG_STATUS_UNSUPPORTED;
    *bytes=1;*alignment=1;return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t allocation_fixture_explain_prepare(void *state,void *result,
    maelys_datalog_explanation_kind_t kind,const char *predicate,const maelys_datalog_value_t *terms,
    size_t arity,void *storage,size_t bytes,size_t *text_size) {
    (void)result;allocation_fixture_state *s=state;
    if(kind!=MAELYS_DATALOG_EXPLAIN_TRUE)return MAELYS_DATALOG_STATUS_UNSUPPORTED;
    if(!bytes)return MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL;
    maelys_datalog_fact_t query={.predicate=predicate,.arity=arity};memcpy(query.terms,terms,arity*sizeof(*terms));
    if(s->negation) {
        for(size_t i=0;i<s->output_count;++i)if(allocation_fixture_equal(&query,&s->live_output[i].fact)) {
            *(unsigned char *)storage=1;*text_size=strlen("identity projection from committed EDB");return MAELYS_DATALOG_STATUS_OK;
        }
        return MAELYS_DATALOG_STATUS_NOT_FOUND;
    }
    for(size_t i=0;i<s->count;++i)for(size_t j=0;j<s->rules;++j) {
        maelys_datalog_fact_t fact=s->live[i].fact;
        if(fact.arity!=s->arity[j] || strcmp(fact.predicate,s->source[j]))continue;
        fact.predicate=s->head[j];if(allocation_fixture_equal(&query,&fact)) {
            *(unsigned char *)storage=1;*text_size=strlen("identity projection from committed EDB");return MAELYS_DATALOG_STATUS_OK;
        }
    }
    return MAELYS_DATALOG_STATUS_NOT_FOUND;
}
static maelys_datalog_status_t allocation_fixture_explain_write(void *state,void *result,
    maelys_datalog_explanation_kind_t kind,const void *storage,char *text,size_t capacity) {
    (void)state;(void)result;(void)kind;(void)storage;
    const char message[]="identity projection from committed EDB";
    if(capacity<sizeof(message))return MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL;
    memcpy(text,message,sizeof(message));return MAELYS_DATALOG_STATUS_OK;
}
#define ALLOCATION_COMMON \
    .name="allocation_conformance",.semantic_id="public.elastic-projection-antijoin.v2", \
    .capabilities=MAELYS_DATALOG_CAP_NEGATION|MAELYS_DATALOG_CAP_POSITIVE|MAELYS_DATALOG_CAP_WORK_LIMIT|MAELYS_DATALOG_CAP_EXPLAIN_TRUE, \
    .resource_features=MAELYS_DATALOG_RESOURCE_SUPPORTED, \
    .storage_requirements=allocation_fixture_plan,.prepare=allocation_fixture_prepare, \
    .commit=allocation_fixture_commit,.destroy_result=allocation_fixture_release,.destroy=allocation_fixture_destroy, \
    .explanation_storage_requirements=allocation_fixture_explain_size,.explanation_prepare=allocation_fixture_explain_prepare, \
    .explanation_write_text=allocation_fixture_explain_write
const maelys_datalog_backend_v6_t *allocation_fixture_snapshot(void) {
    static const maelys_datalog_backend_v6_t b={.abi_version=6,.struct_size=sizeof(b),ALLOCATION_COMMON,.solve=allocation_fixture_snapshot_solve};return &b;
}
const maelys_datalog_backend_v7_t *allocation_fixture_transactions(void) {
    static const maelys_datalog_backend_v7_t b={.abi_version=7,.struct_size=sizeof(b),ALLOCATION_COMMON,.solve=allocation_fixture_solve};return &b;
}
void allocation_fixture_fault(void *storage,unsigned mode) { ((allocation_fixture_state *)storage)->fault=mode; }
void allocation_fixture_observe(const void *storage,allocation_fixture_observation *out) {
    const allocation_fixture_state *s=storage;*out=s->observation;out->base=s->base;out->retained=s->count;out->output_bytes=s->output_count*sizeof(*s->live_output);
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

size_t allocation_fixture_committed(const void *storage,void *out,size_t capacity) {
    const allocation_fixture_state *s=storage;
    size_t bytes=sizeof(s->base)+s->count*sizeof(*s->live);
    if(out && capacity>=bytes) {
        memcpy(out,&s->base,sizeof(s->base));
        memcpy((unsigned char *)out+sizeof(s->base),s->live,s->count*sizeof(*s->live));
    }
    return bytes;
}

void allocation_fixture_hook(void *storage,
    void (*hook)(void *,const maelys_datalog_allocation_service_t *),void *context) {
    allocation_fixture_state *s=storage;s->hook=hook;s->hook_context=context;
}
void allocation_fixture_service(const void *storage,maelys_datalog_allocation_service_t *out) {
    *out=((const allocation_fixture_state *)storage)->allocation;
}

const void *allocation_fixture_live(const void *storage) { return ((const allocation_fixture_state *)storage)->live; }

/* Raw provider arena image except explicitly out-of-band test observations.
 * Caller-owned block bytes are checked independently by the caller ledger. */
size_t allocation_fixture_arena_image(const void *storage,void *out,size_t capacity) {
    allocation_fixture_state copy=*(const allocation_fixture_state *)storage;
    memset(&copy.observation,0,sizeof(copy.observation));copy.fault=0;
    copy.hook=NULL;copy.hook_context=NULL;
    if(out && capacity>=sizeof(copy))memcpy(out,&copy,sizeof(copy));
    return sizeof(copy);
}

int allocation_fixture_scratch_valid(const void *storage) {
    const allocation_fixture_state *s=storage;
    if(s->staged)return 1;
    for(size_t i=0;i<128;++i)if(((const unsigned char *)s->workspace)[i])return 0;
    return 1;
}
