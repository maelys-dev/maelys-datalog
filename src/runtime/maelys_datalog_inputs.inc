/* SPDX-License-Identifier: MPL-2.0 */
/* Private implementation included after the session storage planner. */
struct maelys_datalog_session_inputs {
    maelys_datalog_session_t *owner;
    size_t bytes, capacity, addition_capacity, removal_capacity;
    maelys_datalog_input_base_t base;
    char original_fingerprint[65];
    maelys_datalog_symbol_table_t vocabulary; /* Immutable after initialization. */
    maelys_datalog_internal_fact_t *live, *candidate, *added, *removed;
    size_t count, candidate_count;
    int pending;
    maelys_datalog_symbol_id_t map[MAELYS_DATALOG_MAX_SYMBOLS]; /* Transient. */
};
static uint64_t input_incarnation; /* Protected by arena_lock; never wraps. */
static void retained_hash_u64(maelys_sha256_ctx_t *ctx,uint64_t value) {
    unsigned char bytes[8];for(size_t i=0;i<8;++i)bytes[i]=(unsigned char)(value>>(8*i));
    maelys_sha256_update(ctx,bytes,sizeof(bytes));
}
static void retained_identity(maelys_datalog_session_inputs_t *h) {
    const char *texts[MAELYS_DATALOG_MAX_SYMBOLS];
    for(size_t i=0;i<h->vocabulary.count;++i) {
        const char *text=maelys_datalog_symbol_text(&h->vocabulary,(maelys_datalog_symbol_id_t)(i+1));
        size_t j=i;while(j && strcmp(texts[j-1],text)>0){texts[j]=texts[j-1];--j;}texts[j]=text;
    }
    maelys_sha256_ctx_t hash;maelys_sha256_init(&hash);
    const char domain[]="MAELYS-DATALOG-retained-inputs-v1";
    maelys_sha256_update(&hash,(const unsigned char *)domain,sizeof(domain));
    maelys_sha256_update(&hash,(const unsigned char *)h->original_fingerprint,64);
    retained_hash_u64(&hash,h->capacity);retained_hash_u64(&hash,h->addition_capacity);
    retained_hash_u64(&hash,h->removal_capacity);retained_hash_u64(&hash,h->vocabulary.count);
    for(size_t i=0;i<h->vocabulary.count;++i) {
        size_t n=strlen(texts[i]);retained_hash_u64(&hash,n);
        maelys_sha256_update(&hash,(const unsigned char *)texts[i],n);
    }
    unsigned char digest[32];maelys_sha256_final(&hash,digest);
    for(size_t i=0;i<32;++i) {
        h->owner->execution_fingerprint[2*i]="0123456789abcdef"[digest[i]>>4];
        h->owner->execution_fingerprint[2*i+1]="0123456789abcdef"[digest[i]&15];
    }
    h->owner->execution_fingerprint[64]=0;
}
static int retained_conflict(const maelys_datalog_session_inputs_t *h,
    const void *p,size_t n) { return h && resource_conflict(h,h->bytes,p,n); }

maelys_datalog_status_t maelys_datalog_session_inputs_storage_requirements(
    const maelys_datalog_session_t *s,const maelys_datalog_input_options_t *o,
    size_t *bytes,size_t *alignment) {
    if (!s || !o || !bytes || !alignment || o->struct_size < sizeof(*o) ||
        o->reserved || (!o->symbols && o->symbol_count)) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (o->contract_version != MAELYS_DATALOG_INPUT_CONTRACT_VERSION)
        return MAELYS_DATALOG_STATUS_UNSUPPORTED;
    if (o->fact_capacity > s->resources.input_facts ||
        o->addition_capacity > MAELYS_DATALOG_MAX_EDB_FACTS ||
        o->removal_capacity > MAELYS_DATALOG_MAX_EDB_FACTS ||
        o->symbol_count > MAELYS_DATALOG_MAX_SYMBOLS) return MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE;
    /* All terms bounded above by build ceilings; still use checked arithmetic. */
    if (o->fact_capacity > (SIZE_MAX-o->addition_capacity-o->removal_capacity)/2)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    size_t n=2*o->fact_capacity+o->addition_capacity+o->removal_capacity;
    if (n > (SIZE_MAX-sizeof(maelys_datalog_session_inputs_t))/sizeof(maelys_datalog_internal_fact_t))
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    *bytes=sizeof(maelys_datalog_session_inputs_t)+n*sizeof(maelys_datalog_internal_fact_t);
    *alignment=_Alignof(max_align_t);
    return MAELYS_DATALOG_STATUS_OK;
}

maelys_datalog_status_t maelys_datalog_session_inputs_init(
    maelys_datalog_session_t *s,const maelys_datalog_input_options_t *o,
    void *storage,size_t bytes,maelys_datalog_session_inputs_t **out) {
    if (!out) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    size_t needed,alignment;
    maelys_datalog_status_t rc=maelys_datalog_session_inputs_storage_requirements(s,o,&needed,&alignment);
    if (rc) return rc;
    if (!storage || (uintptr_t)storage%alignment || bytes>UINTPTR_MAX-(uintptr_t)storage)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (bytes<needed) return MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL;
    if (s->busy || s->active || s->retained_inputs || s->result_generation)
        return MAELYS_DATALOG_STATUS_INVALID_STATE;
    if (resource_conflict(storage,needed,out,sizeof(*out)) ||
        resource_conflict(storage,needed,o,sizeof(*o)) ||
        resource_conflict(storage,needed,o->symbols,o->symbol_count*sizeof(*o->symbols)))
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    for (size_t i=0;i<o->symbol_count;++i) {
        if (!o->symbols[i]) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
        size_t n=strnlen(o->symbols[i],MAELYS_DATALOG_MAX_STRING_BYTES+1u);
        if (n>MAELYS_DATALOG_MAX_STRING_BYTES || resource_conflict(storage,needed,o->symbols[i],n+1))
            return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    }
    lock_arenas();
    lock_workspaces();
    for (maelys_datalog_session_t *p=resource_sessions;p;p=p->arena_next)
        if (resource_conflict(storage,needed,p,p->arena_bytes) ||
            resource_conflict(storage,needed,p->backend_bytes_start,p->backend_bytes) ||
            resource_conflict(storage,needed,p->explanation_storage,p->explanation_bytes) ||
            resource_conflict(storage,needed,p->inputs,
                maelys_datalog_prepared_session_storage_bytes(p->resources.input_facts,p->inputs->pool_capacity)) ||
            resource_conflict(storage,needed,p->inputs->prepared,sizeof(*p->inputs->prepared)) ||
            resource_conflict(storage,needed,p->inputs->result_workspace,
                maelys_datalog_solve_workspace_bytes(p->resources.derived_facts,1)) ||
            retained_conflict(p->retained_inputs,storage,needed)) {
            unlock_workspaces();unlock_arenas();return MAELYS_DATALOG_STATUS_INVALID_STATE;
        }
    if (input_incarnation==UINT64_MAX) {
        unlock_workspaces();unlock_arenas();return MAELYS_DATALOG_STATUS_INVALID_STATE;
    }
    maelys_datalog_session_inputs_t *h=storage;
    memset(h,0,sizeof(*h));
    h->bytes=needed;h->capacity=o->fact_capacity;
    h->addition_capacity=o->addition_capacity;h->removal_capacity=o->removal_capacity;
    h->base.incarnation=++input_incarnation;
    h->vocabulary=s->inputs->prepared->symbols;
    h->live=(void *)(h+1);h->candidate=h->live+h->capacity;
    h->added=h->candidate+h->capacity;h->removed=h->added+h->addition_capacity;
    for (size_t i=0;i<o->symbol_count && !rc;++i) {
        maelys_datalog_symbol_id_t id;
        rc=(maelys_datalog_status_t)maelys_datalog_symbol_intern(&h->vocabulary,o->symbols[i],strlen(o->symbols[i]),&id);
    }
    if (!rc) {
        h->owner=s;s->retained_inputs=h;
        memcpy(h->original_fingerprint,s->execution_fingerprint,65);
        retained_identity(h);*out=h;
    }
    unlock_workspaces();unlock_arenas();
    return rc;
}
maelys_datalog_status_t maelys_datalog_session_inputs_base(
    const maelys_datalog_session_inputs_t *h,maelys_datalog_input_base_t *out) {
    if (!h || !out || retained_conflict(h,out,sizeof(*out))) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (!h->owner || h->owner->busy) return MAELYS_DATALOG_STATUS_INVALID_STATE;
    *out=h->base;return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t maelys_datalog_session_inputs_free(maelys_datalog_session_inputs_t *h) {
    if (!h) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (!h->owner || h->owner->busy || h->owner->active) return MAELYS_DATALOG_STATUS_INVALID_STATE;
    lock_arenas();
    memcpy(h->owner->execution_fingerprint,h->original_fingerprint,65);
    h->owner->retained_inputs=NULL;h->owner=NULL;unlock_arenas();
    return MAELYS_DATALOG_STATUS_OK;
}
static void retained_commit(maelys_datalog_session_inputs_t *h) {
    if (!h || !h->pending) return;
    maelys_datalog_internal_fact_t *old=h->live;
    h->live=h->candidate;h->candidate=old;h->count=h->candidate_count;
    ++h->base.generation;h->pending=0;
}
static void retained_abort(maelys_datalog_session_inputs_t *h) { if(h) h->pending=0; }

static maelys_datalog_status_t retained_convert(maelys_datalog_session_inputs_t *h,
    const maelys_datalog_fact_t *facts,size_t count,int removal,
    maelys_datalog_internal_fact_t *out,size_t *converted) {
    if ((!facts && count) || retained_conflict(h,facts,count*sizeof(*facts)))
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    const maelys_datalog_predicate_registry_t *registry=&h->owner->inputs->prepared->registry;
    size_t n=0;
    for(size_t i=0;i<count;++i) {
        const maelys_datalog_fact_t *f=&facts[i];
        if(!f->predicate || f->arity>MAELYS_DATALOG_MAX_TERMS) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
        size_t length=strnlen(f->predicate,MAELYS_DATALOG_MAX_STRING_BYTES+1u);
        if(length>MAELYS_DATALOG_MAX_STRING_BYTES || retained_conflict(h,f->predicate,length+1))
            return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
        maelys_datalog_internal_fact_t item={0};
        if(!maelys_datalog_predicate_registry_find(registry,f->predicate,f->arity,&item.predicate_id))
            return MAELYS_DATALOG_STATUS_INVALID_FIELD;
        const maelys_datalog_predicate_entry_t *def=maelys_datalog_predicate_registry_get(registry,item.predicate_id);
        if(def->kind_flags & MAELYS_DATALOG_PRED_KIND_POLICY_FACT) return MAELYS_DATALOG_STATUS_FORBIDDEN;
        if(!(def->kind_flags & MAELYS_DATALOG_PRED_KIND_EDB)) return MAELYS_DATALOG_STATUS_INVALID_FIELD;
        item.arity=(uint8_t)f->arity;
        int absent=0;
        for(size_t t=0;t<f->arity;++t) {
            const maelys_datalog_value_t *v=&f->terms[t];
            maelys_datalog_status_t rc=maelys_datalog_validate_value(v,0);
            if(rc) return rc;
            item.terms[t].kind=(maelys_datalog_internal_term_kind_t)v->kind;
            if(v->kind==MAELYS_DATALOG_VALUE_INTEGER) item.terms[t].as.integer=v->as.integer;
            else if(v->kind==MAELYS_DATALOG_VALUE_BOOLEAN) item.terms[t].as.boolean=!!v->as.boolean;
            else {
                size_t len=strnlen(v->as.symbol,MAELYS_DATALOG_MAX_STRING_BYTES+1u);int found=0;
                if(len>MAELYS_DATALOG_MAX_STRING_BYTES || retained_conflict(h,v->as.symbol,len+1))
                    return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
                rc=(maelys_datalog_status_t)maelys_datalog_symbol_lookup_readonly(
                    &h->vocabulary,v->as.symbol,len,&item.terms[t].as.symbol,&found);
                if(rc) return rc;
                if(!found) { if(!removal) return MAELYS_DATALOG_STATUS_INVALID_FIELD; absent=1; }
            }
        }
        if(!absent) out[n++]=item;
    }
    maelys_datalog_fact_set_t set;
    maelys_datalog_fact_set_init(&set,out,count);set.count=n;set.sorted=0;
    maelys_datalog_status_t rc=(maelys_datalog_status_t)maelys_datalog_fact_set_sort(&set);
    if(!rc) rc=(maelys_datalog_status_t)maelys_datalog_fact_set_dedup(&set);
    if(!rc) *converted=set.count;
    return rc;
}
/* L: sorted unique lots; only provisional storage is written. */
static maelys_datalog_status_t retained_compose(maelys_datalog_session_inputs_t *h,size_t na,size_t nr) {
    size_t write=0,remove=0;
    for(size_t read=0;read<h->count;++read) {
        while(remove<nr && maelys_datalog_fact_cmp(&h->removed[remove],&h->live[read])<0) ++remove;
        if(remove<nr && !maelys_datalog_fact_cmp(&h->removed[remove],&h->live[read])) continue;
        h->candidate[write++]=h->live[read];
    }
    size_t n=write,base=0,fresh=0;
    for(size_t i=0;i<na;++i) {
        while(base<n && maelys_datalog_fact_cmp(&h->candidate[base],&h->added[i])<0) ++base;
        if(base<n && !maelys_datalog_fact_cmp(&h->candidate[base],&h->added[i])) continue;
        h->added[fresh++]=h->added[i];
    }
    if(fresh>h->capacity-n) return MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE;
    size_t left=n,right=fresh;write=n+fresh;
    while(right) {
        if(left && maelys_datalog_fact_cmp(&h->candidate[left-1],&h->added[right-1])>0)
            h->candidate[--write]=h->candidate[--left];
        else h->candidate[--write]=h->added[--right];
    }
    h->candidate_count=n+fresh;return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t retained_run(maelys_datalog_session_inputs_t *h,
    maelys_datalog_input_base_t base,int snapshot,int deferred,
    const maelys_datalog_fact_t *added,size_t na,const maelys_datalog_fact_t *removed,size_t nr,
    maelys_datalog_result_t **out,maelys_datalog_diagnostic_t *diag) {
    if (!out || retained_conflict(h,out,sizeof(*out)) ||
        (diag && retained_conflict(h,diag,sizeof(*diag)))) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    *out=NULL;
    if(!h) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    maelys_datalog_status_t rc=maelys_datalog_diagnostic_clear(diag);
    if(rc) return rc;
    maelys_datalog_session_t *s=h->owner;
    if(!s || s->busy || s->active || h->pending || base.incarnation!=h->base.incarnation ||
        base.generation!=h->base.generation || h->base.generation==UINT64_MAX)
        return MAELYS_DATALOG_STATUS_INVALID_STATE;
    if(na>(snapshot?h->capacity:h->addition_capacity) || nr>h->removal_capacity)
        return solve_input_error(diag,MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE,"Raw input transaction capacity exceeded.");
    size_t a=0,r=0;
    rc=retained_convert(h,added,na,0,snapshot?h->candidate:h->added,&a);
    if(!rc && !snapshot) rc=retained_convert(h,removed,nr,1,h->removed,&r);
    if(!rc) { if(snapshot) h->candidate_count=a; else rc=retained_compose(h,a,r); }
    if(!rc) rc=(maelys_datalog_status_t)maelys_datalog_prepared_session_materialize_retained(
        s->inputs,h->candidate,h->candidate_count,&h->vocabulary,h->map);
    if(rc) return solve_input_error(diag,rc,"Input transaction rejected before publication.");
    h->pending=1;s->pending_commit=deferred;
    rc=session_solve_materialized(s,out,diag);
    if(rc) { retained_abort(h);s->pending_commit=0; }
    return rc;
}
maelys_datalog_status_t maelys_datalog_session_inputs_replace(
    maelys_datalog_session_inputs_t *h,maelys_datalog_input_base_t base,
    const maelys_datalog_fact_t *facts,size_t n,maelys_datalog_result_t **out,maelys_datalog_diagnostic_t *d) {
    return retained_run(h,base,1,0,facts,n,NULL,0,out,d);
}
maelys_datalog_status_t maelys_datalog_session_inputs_apply(
    maelys_datalog_session_inputs_t *h,maelys_datalog_input_base_t base,
    const maelys_datalog_fact_t *a,size_t na,const maelys_datalog_fact_t *r,size_t nr,
    maelys_datalog_result_t **out,maelys_datalog_diagnostic_t *d) {
    return retained_run(h,base,0,0,a,na,r,nr,out,d);
}
static maelys_datalog_status_t retained_candidate(maelys_datalog_session_inputs_t *h,
    const maelys_datalog_fact_t *f,size_t n,maelys_datalog_result_t **out,maelys_datalog_diagnostic_t *d) {
    return retained_run(h,h->base,1,1,f,n,NULL,0,out,d);
}
