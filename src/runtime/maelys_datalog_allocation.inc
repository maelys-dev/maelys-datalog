/* SPDX-License-Identifier: MPL-2.0 */
/* Session-local optional service. No bookkeeping allocation outside charged
 * caller blocks; the control object is an elastic-only fixed plan slice. */
typedef struct allocation_block {
    struct allocation_block *next;
    void *payload;
    size_t charge;
    int provisional;
} allocation_block_t;
enum allocation_phase { ALLOCATION_IDLE, ALLOCATION_PREPARE, ALLOCATION_SOLVE,
    ALLOCATION_COMMIT, ALLOCATION_ABORT, ALLOCATION_CLEANUP, ALLOCATION_DESTROY };
typedef struct allocation_state {
    maelys_datalog_session_t *owner;
    maelys_datalog_caller_allocator_t caller;
    maelys_datalog_allocation_service_t service;
    allocation_block_t *blocks;
    size_t cap, current, peak;
    enum allocation_phase phase;
    int calling;
    maelys_datalog_status_t error;
    maelys_datalog_diagnostic_t diagnostic;
} allocation_state_t;
static int allocation_link(allocation_state_t *, allocation_block_t *, size_t, void *);
static void allocation_unlink(allocation_state_t *, allocation_block_t **);
static maelys_datalog_status_t allocation_record(size_t n, size_t prefix,
    size_t full, uint32_t version, uint32_t reserved, uint64_t features) {
    if (n < prefix) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (version != 1 || features) return MAELYS_DATALOG_STATUS_UNSUPPORTED;
    if (reserved) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    return n < full ? MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL : MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t allocation_shape(size_t n, size_t a, size_t *charge) {
    if (!n || !a || (a & (a-1)) || a > _Alignof(max_align_t) ||
        a-1 > SIZE_MAX-sizeof(allocation_block_t) ||
        n > SIZE_MAX-sizeof(allocation_block_t)-(a-1))
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    *charge = sizeof(allocation_block_t) + (a-1) + n;
    return MAELYS_DATALOG_STATUS_OK;
}
static int allocation_inspect_phase(const allocation_state_t *a) {
    return a && !a->calling && (a->phase == ALLOCATION_PREPARE ||
        a->phase == ALLOCATION_SOLVE || a->phase == ALLOCATION_ABORT ||
        a->phase == ALLOCATION_CLEANUP || a->phase == ALLOCATION_DESTROY);
}
static maelys_datalog_status_t allocation_inspect(void *context, size_t n,
    size_t alignment, maelys_datalog_allocation_budget_t *out) {
    allocation_state_t *a = context;
    if (!out || out->struct_size < MAELYS_DATALOG_ALLOCATION_BUDGET_PREFIX_SIZE)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    maelys_datalog_status_t rc = allocation_record(out->struct_size,
        MAELYS_DATALOG_ALLOCATION_BUDGET_PREFIX_SIZE, sizeof(*out),
        out->contract_version, out->reserved, out->required_features);
    if (rc) return rc;
    if (out->reserved_tail) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (!allocation_inspect_phase(a)) return MAELYS_DATALOG_STATUS_INVALID_STATE;
    size_t charge;
    rc = allocation_shape(n, alignment, &charge);
    if (rc) return rc;
    maelys_datalog_allocation_budget_t value = MAELYS_DATALOG_ALLOCATION_BUDGET_INIT;
    value.struct_size = out->struct_size;
    value.cap_bytes = a->cap; value.current_bytes = a->current;
    value.remaining_bytes = a->cap-a->current; value.operation_peak_bytes = a->peak;
    value.required_charge = charge; value.fits = charge <= value.remaining_bytes;
    *out = value;
    return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t allocation_diagnose(maelys_datalog_diagnostic_t *d,
    maelys_datalog_status_t rc, const char *phase, const char *field,
    size_t observed, size_t cap) {
    if (d) {
        d->status = rc; d->source = MAELYS_DATALOG_DIAGNOSTIC_SOLVE;
        d->code = MAELYS_DATALOG_DIAG_NONE;
        d->present = MAELYS_DATALOG_DIAGNOSTIC_CONTEXT;
        snprintf(d->phase, sizeof(d->phase), "%s", phase);
        snprintf(d->field, sizeof(d->field), "%s", field);
        if (rc == MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE) {
            d->present |= MAELYS_DATALOG_DIAGNOSTIC_CAPACITY;
            d->observed_count = observed; d->limit = cap; d->limit_kind = 0;
        } else if (rc == MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL)
            snprintf(d->token, sizeof(d->token), "%zu", observed);
    }
    return rc;
}
static void allocation_copy_diagnostic(maelys_datalog_diagnostic_t *out,
    const maelys_datalog_diagnostic_t *value) {
    if (out) { size_t n=out->struct_size; *out=*value; out->struct_size=n; }
}
static maelys_datalog_status_t allocation_fail(allocation_state_t *a,
    maelys_datalog_status_t rc, const char *field, size_t observed,
    maelys_datalog_diagnostic_t *diag) {
    if (a && !a->error) {
        a->error = rc;
        a->diagnostic = (maelys_datalog_diagnostic_t)MAELYS_DATALOG_DIAGNOSTIC_INIT;
        allocation_diagnose(&a->diagnostic,rc,"backend-acquire",field,observed,a->cap);
    }
    if (a) { allocation_copy_diagnostic(diag,&a->diagnostic); return a->error; }
    return rc;
}
static maelys_datalog_status_t allocation_acquire(void *context, size_t n,
    size_t alignment, void **out, maelys_datalog_diagnostic_t *diag) {
    allocation_state_t *a = context;
    if (out) *out = NULL;
    maelys_datalog_status_t rc = maelys_datalog_diagnostic_clear(diag);
    /* A malformed diagnostic is not writable, but still poisons acquisition. */
    if (rc) return allocation_fail(a,rc,"diagnostic",0,NULL);
    if (!a || !out) return allocation_fail(a,MAELYS_DATALOG_STATUS_INVALID_ARGUMENT,"allocation",0,diag);
    if (a->error) { allocation_copy_diagnostic(diag,&a->diagnostic); return a->error; }
    if (a->calling || (a->phase != ALLOCATION_PREPARE && a->phase != ALLOCATION_SOLVE))
        return allocation_fail(a,MAELYS_DATALOG_STATUS_INVALID_STATE,"allocation_phase",0,diag);
    size_t charge;
    rc = allocation_shape(n,alignment,&charge);
    if (rc || charge > SIZE_MAX-a->current)
        return allocation_fail(a,MAELYS_DATALOG_STATUS_INVALID_ARGUMENT,"allocation",0,diag);
    if (charge > a->cap-a->current)
        return allocation_fail(a,MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE,"execution_byte_cap",a->current+charge,diag);
    a->calling = 1;
    void *raw = a->caller.acquire(a->caller.context,charge,_Alignof(max_align_t));
    a->calling = 0;
    if (!raw) return allocation_fail(a,MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL,"allocator",charge,diag);
    if ((uintptr_t)raw % _Alignof(max_align_t) || charge > UINTPTR_MAX-(uintptr_t)raw) {
        a->calling=1; a->caller.release(a->caller.context,raw,charge,_Alignof(max_align_t)); a->calling=0;
        return allocation_fail(a,MAELYS_DATALOG_STATUS_INVALID_STATE,"allocator_storage",0,diag);
    }
    uintptr_t start = (uintptr_t)raw+sizeof(allocation_block_t);
    void *payload = (void *)((start+alignment-1)&~(uintptr_t)(alignment-1));
    /* Validate exclusivity before writing tracking metadata. An overlapping
     * pointer violates caller ownership and must not free somebody else's block. */
    if (!allocation_link(a,raw,charge,payload))
        return allocation_fail(a,MAELYS_DATALOG_STATUS_INVALID_STATE,"allocator_storage",0,diag);
    a->current += charge;
    if (a->current > a->peak) a->peak = a->current;
    /* Reentrant service use can have poisoned the operation inside acquire. */
    if (a->error) { allocation_copy_diagnostic(diag,&a->diagnostic); return a->error; }
    *out = payload;
    return MAELYS_DATALOG_STATUS_OK;
}
static void allocation_drop(allocation_state_t *a, allocation_block_t **entry) {
    allocation_block_t *block = *entry;
    size_t charge = block->charge;
    allocation_unlink(a,entry);
    a->current -= charge;
    a->calling=1;
    a->caller.release(a->caller.context,block,charge,_Alignof(max_align_t));
    a->calling=0;
}
static void allocation_release(void *context, void *payload) {
    allocation_state_t *a = context;
    if (!a) return;
    allocation_block_t **entry = &a->blocks;
    while (*entry && (*entry)->payload != payload) entry = &(*entry)->next;
    if (!allocation_inspect_phase(a) || !*entry ||
        ((a->phase == ALLOCATION_PREPARE || a->phase == ALLOCATION_SOLVE ||
          a->phase == ALLOCATION_ABORT) && !(*entry)->provisional)) {
        (void)allocation_fail(a,MAELYS_DATALOG_STATUS_INVALID_STATE,"allocation_release",0,NULL);
        return;
    }
    allocation_drop(a,entry);
}
static void allocation_begin(allocation_state_t *a, enum allocation_phase phase) {
    a->phase=phase; a->peak=a->current; a->error=MAELYS_DATALOG_STATUS_OK;
}
static void allocation_adopt(allocation_state_t *a) {
    for (allocation_block_t *b=a->blocks;b;b=b->next) b->provisional=0;
    a->phase=ALLOCATION_IDLE;
}
static void allocation_sweep(allocation_state_t *a, int all) {
    allocation_block_t **entry=&a->blocks;
    while (*entry) {
        if (all || (*entry)->provisional) allocation_drop(a,entry);
        else entry=&(*entry)->next;
    }
    a->phase=ALLOCATION_IDLE;
}
static void allocation_init(allocation_state_t *a, maelys_datalog_session_t *s,
    const maelys_datalog_caller_allocator_t *caller, size_t cap, size_t fixed) {
    memset(a,0,sizeof(*a)); a->owner=s; a->caller=*caller;
    a->cap=cap; a->current=fixed; a->peak=fixed;
    a->service=(maelys_datalog_allocation_service_t){sizeof(a->service),1,0,0,
        sizeof(allocation_block_t),_Alignof(max_align_t),a,
        allocation_inspect,allocation_acquire,allocation_release};
}
maelys_datalog_status_t maelys_datalog_session_get_allocation_stats(
    const maelys_datalog_session_t *s, maelys_datalog_session_allocation_stats_t *out) {
    if (!s || !out || out->struct_size < MAELYS_DATALOG_ALLOCATION_STATS_PREFIX_SIZE)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    maelys_datalog_status_t rc=allocation_record(out->struct_size,
        MAELYS_DATALOG_ALLOCATION_STATS_PREFIX_SIZE,sizeof(*out),
        out->contract_version,out->reserved,out->required_features);
    if (rc) return rc;
    if (s->busy) return MAELYS_DATALOG_STATUS_INVALID_STATE;
    const allocation_state_t *a=s->allocation;
    if (!a) return MAELYS_DATALOG_STATUS_UNSUPPORTED;
    maelys_datalog_session_allocation_stats_t v=MAELYS_DATALOG_ALLOCATION_STATS_INIT;
    v.struct_size=out->struct_size; v.cap_bytes=a->cap; v.current_bytes=a->current;
    v.remaining_bytes=a->cap-a->current; v.operation_peak_bytes=a->peak;
    *out=v;
    return MAELYS_DATALOG_STATUS_OK;
}
