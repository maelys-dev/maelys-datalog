/* SPDX-License-Identifier: MPL-2.0 */
/* Included by runtime.c: no public-layout aliases and no ambient quota state. */
static maelys_datalog_session_resources_t resource_defaults(void) {
    return (maelys_datalog_session_resources_t){sizeof(maelys_datalog_session_resources_t),
        MAELYS_DATALOG_RESOURCE_CONTRACT_VERSION, MAELYS_DATALOG_MEMORY_FIXED, 0,
        MAELYS_DATALOG_MAX_EDB_FACTS, MAELYS_DATALOG_MAX_IDB_FACTS,
        MAELYS_DATALOG_MAX_SYMBOLS, MAELYS_DATALOG_STRING_POOL_BYTES};
}
static maelys_datalog_status_t resource_prefix(uint32_t version, uint32_t mode, uint64_t features) {
    return version != MAELYS_DATALOG_RESOURCE_CONTRACT_VERSION ||
        mode != MAELYS_DATALOG_MEMORY_FIXED || (features & ~MAELYS_DATALOG_RESOURCE_SUPPORTED_014)
        ? MAELYS_DATALOG_STATUS_UNSUPPORTED : MAELYS_DATALOG_STATUS_OK;
}
#define REQUEST_END(member) (offsetof(maelys_datalog_session_resource_request_t, member) + \
    sizeof(((maelys_datalog_session_resource_request_t *)0)->member))
static maelys_datalog_status_t resource_request_copy(
    const maelys_datalog_session_resource_request_t *in,
    maelys_datalog_session_resource_request_t *out) {
    *out = (maelys_datalog_session_resource_request_t)MAELYS_DATALOG_RESOURCE_REQUEST_INIT;
    if (!in) return MAELYS_DATALOG_STATUS_OK;
    size_t n = in->struct_size;
    if (n < MAELYS_DATALOG_RESOURCE_REQUEST_PREFIX_SIZE)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    maelys_datalog_status_t rc = resource_prefix(in->contract_version, in->memory_mode, in->required_features);
    if (rc) return rc;
    if (n < sizeof(*in) && n != MAELYS_DATALOG_RESOURCE_REQUEST_PREFIX_SIZE &&
        n != REQUEST_END(capacity_mask) && n != REQUEST_END(input_facts) &&
        n != REQUEST_END(derived_facts) && n != REQUEST_END(symbols) && n != REQUEST_END(text_bytes))
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    out->required_features = in->required_features;
    if (n == MAELYS_DATALOG_RESOURCE_REQUEST_PREFIX_SIZE) return MAELYS_DATALOG_STATUS_OK;
    if (in->capacity_mask & ~MAELYS_DATALOG_CAPACITY_ALL) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    out->capacity_mask = in->capacity_mask;
#define COPY_CAP(member, bit) do { if (in->capacity_mask & (bit)) { \
    if (n < REQUEST_END(member)) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT; \
    out->member = in->member; } } while (0)
    COPY_CAP(input_facts, MAELYS_DATALOG_CAPACITY_INPUT_FACTS);
    COPY_CAP(derived_facts, MAELYS_DATALOG_CAPACITY_DERIVED_FACTS);
    COPY_CAP(symbols, MAELYS_DATALOG_CAPACITY_SYMBOLS);
    COPY_CAP(text_bytes, MAELYS_DATALOG_CAPACITY_TEXT_BYTES);
#undef COPY_CAP
    return MAELYS_DATALOG_STATUS_OK;
}
#undef REQUEST_END
maelys_datalog_status_t maelys_datalog_session_config_set_resources(
    maelys_datalog_session_config_t *c, const maelys_datalog_session_resource_request_t *in) {
    if (!c) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    maelys_datalog_session_resource_request_t copy;
    maelys_datalog_status_t rc = resource_request_copy(in, &copy);
    if (!rc) c->resources = in ? copy : (maelys_datalog_session_resource_request_t){0};
    return rc;
}
static maelys_datalog_status_t resource_failure(maelys_datalog_diagnostic_t *d,
    maelys_datalog_status_t rc, const char *field, size_t observed, size_t limit) {
    if (d) {
        d->status = rc; d->source = MAELYS_DATALOG_DIAGNOSTIC_SOLVE;
        d->code = MAELYS_DATALOG_DIAG_OPERATION_REJECTED;
        d->present = MAELYS_DATALOG_DIAGNOSTIC_CAPACITY | MAELYS_DATALOG_DIAGNOSTIC_CONTEXT;
        d->observed_count = observed; d->limit = limit; d->limit_kind = 0;
        snprintf(d->field, sizeof(d->field), "session_%s", field);
        snprintf(d->phase, sizeof(d->phase), "session_resources");
        snprintf(d->message, sizeof(d->message), "Session %s: requested/observed %zu, bound %zu.", field, observed, limit);
    }
    return rc;
}
static maelys_datalog_status_t normalize_resources(const maelys_datalog_internal_ruleset_t *rules,
    const maelys_datalog_session_config_t *c, maelys_datalog_session_resources_t *out,
    maelys_datalog_diagnostic_t *diag) {
    *out = resource_defaults();
    if (c && c->resources.struct_size) {
        const maelys_datalog_session_resource_request_t *q = &c->resources;
        out->required_features = q->required_features;
#define NORMALIZE(member, bit) do { if (q->capacity_mask & (bit)) { \
    if (q->member > out->member) return resource_failure(diag, MAELYS_DATALOG_STATUS_UNSUPPORTED, \
        #member, q->member, out->member); \
    if (q->member != out->member) out->required_features |= MAELYS_DATALOG_RESOURCE_SESSION_CAPACITIES; \
    out->member = q->member; } } while (0)
        NORMALIZE(input_facts, MAELYS_DATALOG_CAPACITY_INPUT_FACTS);
        NORMALIZE(derived_facts, MAELYS_DATALOG_CAPACITY_DERIVED_FACTS);
        NORMALIZE(symbols, MAELYS_DATALOG_CAPACITY_SYMBOLS);
        NORMALIZE(text_bytes, MAELYS_DATALOG_CAPACITY_TEXT_BYTES);
#undef NORMALIZE
    }
    if (out->symbols < rules->symbols.count)
        return resource_failure(diag, MAELYS_DATALOG_STATUS_UNSUPPORTED, "symbols", rules->symbols.count, out->symbols);
    if (out->text_bytes < rules->symbols.used)
        return resource_failure(diag, MAELYS_DATALOG_STATUS_UNSUPPORTED, "text_bytes", rules->symbols.used, out->text_bytes);
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t maelys_datalog_session_get_resources(
    const maelys_datalog_session_t *s, maelys_datalog_session_resources_t *out) {
    if (!s || !out) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    size_t n = out->struct_size;
    if (n < MAELYS_DATALOG_RESOURCES_PREFIX_SIZE) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    maelys_datalog_status_t rc = resource_prefix(out->contract_version, out->memory_mode, out->required_features);
    if (rc) return rc;
    if (n < sizeof(*out)) return MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL;
    maelys_datalog_session_resources_t value = s->resources;
    value.struct_size = n; *out = value;
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t maelys_datalog_session_config_set_backend_v6(
    maelys_datalog_session_config_t *c, const maelys_datalog_backend_v6_t *b) {
    if (!c) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (!b) return maelys_datalog_session_config_set_backend(c, NULL);
    maelys_datalog_status_t rc = maelys_datalog_backend_v6_validate(b);
    if (rc) return rc;
    maelys_datalog_context_release(c->context); c->context = NULL;
    c->backend_v6 = *b;
    strcpy(c->backend_name, b->name); strcpy(c->backend_semantic_id, b->semantic_id);
    c->backend_v6.name = c->backend_name; c->backend_v6.semantic_id = c->backend_semantic_id;
    c->custom_backend = 1; c->use_v6 = 1;
    c->canonical_reference = b == maelys_datalog_backend_reference_v6();
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t maelys_datalog_session_config_set_context_backend_v6(
    maelys_datalog_session_config_t *c, maelys_datalog_context_t *ctx, const char *name) {
    if (!c || !ctx || (name && !name[0])) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (!maelys_datalog_context_is_sealed(ctx)) return MAELYS_DATALOG_STATUS_INVALID_STATE;
    if (name && strlen(name) >= sizeof(c->context_backend_name)) return MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE;
    if (!maelys_datalog_context_backend_v6(ctx, name))
        return maelys_datalog_context_backend(ctx, name) ? MAELYS_DATALOG_STATUS_UNSUPPORTED : MAELYS_DATALOG_STATUS_NOT_FOUND;
    maelys_datalog_context_retain(ctx); maelys_datalog_context_release(c->context);
    c->context = ctx; c->use_v6 = 1; c->custom_backend = 0;
    strcpy(c->context_backend_name, name ? name : "");
    return MAELYS_DATALOG_STATUS_OK;
}

typedef struct {
    maelys_datalog_session_storage_plan_t public;
    maelys_datalog_session_resources_t resources;
    const maelys_datalog_backend_v6_t *backend;
    size_t prepared, workspace, snapshot, canonical, emitted, provider, explanation, pool;
    size_t provider_bytes, provider_alignment;
    uint64_t required, work_limit;
    int borrows, reference;
} fixed_plan_t;
static int plan_part(fixed_plan_t *p, size_t count, size_t width, size_t alignment,
    size_t *offset, size_t *component) {
    size_t n = p->public.arena_bytes;
    if (!backend_alignment_valid(alignment) || count > SIZE_MAX / width) return 0;
    size_t bytes = count * width;
    size_t pad = (alignment - (n % alignment)) % alignment;
    if (pad > SIZE_MAX - n || bytes > SIZE_MAX - n - pad || bytes > SIZE_MAX - *component)
        return 0;
    *offset = n + pad; p->public.arena_bytes = n + pad + bytes;
    p->public.padding_bytes += pad; *component += bytes;
    return 1;
}
static maelys_datalog_status_t policy_config_valid(const maelys_datalog_policy_t *policy, size_t index,
    const maelys_datalog_session_config_t *c) {
    if (!policy) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (policy->released) return MAELYS_DATALOG_STATUS_INVALID_STATE;
    if (index >= policy->set.policy_count) return MAELYS_DATALOG_STATUS_NOT_FOUND;
    if (c && c->context && policy->set.policies[index].modules != c->context)
        return MAELYS_DATALOG_STATUS_INVALID_FIELD;
    return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t fixed_plan(const maelys_datalog_policy_t *policy, size_t index,
    const maelys_datalog_session_config_t *c, fixed_plan_t *p, maelys_datalog_diagnostic_t *diag) {
    memset(p, 0, sizeof(*p));
    p->public = (maelys_datalog_session_storage_plan_t)MAELYS_DATALOG_SESSION_PLAN_INIT;
    p->public.arena_alignment = _Alignof(max_align_t);
    maelys_datalog_status_t rc = policy_config_valid(policy, index, c);
    if (rc) return rc;
    const maelys_datalog_internal_ruleset_t *rules = &policy->set.policies[index];
    rc = normalize_resources(rules, c, &p->resources, diag);
    if (rc) return rc;
    /* Explicit legacy selections never become ABI 6 because their callbacks
     * happen to equal the reference callbacks. Program bounds are immutable. */
    if (c && (c->custom_backend || c->context) && !c->use_v6)
        return MAELYS_DATALOG_STATUS_UNSUPPORTED;
    p->backend = c && c->context ? maelys_datalog_context_backend_v6(c->context,
        c->context_backend_name[0] ? c->context_backend_name : NULL) :
        c && c->custom_backend ? &c->backend_v6 : maelys_datalog_backend_reference_v6();
    rc = maelys_datalog_backend_v6_validate(p->backend);
    if (rc) return rc;
    if (p->resources.required_features & ~p->backend->resource_features)
        return MAELYS_DATALOG_STATUS_UNSUPPORTED;
    maelys_datalog_program_t program = {.ruleset=rules};
    maelys_datalog_program_info_t info;
    rc = maelys_datalog_program_info(&program, &info);
    if (rc) return rc;
    p->required = info.required_capabilities | (c ? c->required_capabilities : 0);
    if (c && c->work_limit) p->required |= MAELYS_DATALOG_CAP_WORK_LIMIT;
    p->work_limit = c && c->work_limit ? c->work_limit : UINT64_C(1048576);
    if (p->required & ~p->backend->capabilities) return MAELYS_DATALOG_STATUS_UNSUPPORTED;
    p->borrows = p->backend->solve == maelys_datalog_backend_reference()->solve;
    p->reference = p->backend == maelys_datalog_backend_reference_v6() ||
        (c && c->custom_backend && c->canonical_reference);
    if (c && c->explanation_kinds && !p->reference) return MAELYS_DATALOG_STATUS_UNSUPPORTED;
    rc = maelys_datalog_callback_status(p->backend->storage_requirements(
        &program, &p->resources, &p->provider_bytes, &p->provider_alignment));
    if (rc) return rc;
    if (!backend_alignment_valid(p->provider_alignment)) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    p->public.required_features = p->resources.required_features;
    /* Saturating sum is expressed by subtraction after checked E <= B. F is
     * compiled policy content, never deducted from the dynamic input quota. */
    p->pool = p->resources.input_facts;
    size_t room = MAELYS_DATALOG_MAX_EDB_FACTS - p->pool;
    p->pool += rules->fact_count < room ? rules->fact_count : room;
    size_t ignored;
#define HOST(count, width, align, off) \
    if (!plan_part(p, count, width, align, &p->off, &p->public.host_bytes)) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT
    if (!plan_part(p, 1, sizeof(maelys_datalog_session_t), _Alignof(maelys_datalog_session_t),
        &ignored, &p->public.host_bytes)) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    HOST(1, maelys_datalog_prepared_session_storage_bytes(p->resources.input_facts, p->pool),
        _Alignof(max_align_t), prepared);
    HOST(1, maelys_datalog_solve_workspace_bytes(p->resources.derived_facts, 1),
        _Alignof(max_align_t), workspace);
    if (!policy->owns_storage) { HOST(1, sizeof(*rules), _Alignof(max_align_t), snapshot); }
    if (!p->borrows) {
        HOST(p->resources.input_facts, sizeof(maelys_datalog_fact_t), _Alignof(maelys_datalog_fact_t), canonical);
        HOST(p->resources.derived_facts, sizeof(maelys_datalog_internal_fact_t), _Alignof(maelys_datalog_internal_fact_t), emitted);
    }
#undef HOST
    if (c && c->backend_storage.struct_size) {
        if (c->backend_storage.size < p->provider_bytes) return MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL;
        if (c->backend_storage.alignment < p->provider_alignment) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
        p->public.external_backend_bytes = p->provider_bytes;
    } else if (!plan_part(p, p->provider_bytes, 1,
        p->provider_alignment, &p->provider, &p->public.backend_bytes)) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    size_t explanation_bytes = 0;
    if (c) for (unsigned k = MAELYS_DATALOG_EXPLAIN_TRUE; k <= MAELYS_DATALOG_EXPLAIN_FALSE; k <<= 1) {
        if (!(c->explanation_kinds & k)) continue;
        uint64_t cap = k == MAELYS_DATALOG_EXPLAIN_TRUE ? MAELYS_DATALOG_CAP_EXPLAIN_TRUE : MAELYS_DATALOG_CAP_EXPLAIN_FALSE;
        if (!(p->backend->capabilities & cap)) return MAELYS_DATALOG_STATUS_UNSUPPORTED;
        size_t n = sizeof(maelys_datalog_explanation_t), a = _Alignof(maelys_datalog_explanation_t);
        if (k == MAELYS_DATALOG_EXPLAIN_FALSE) {
            rc = (maelys_datalog_status_t)maelys_datalog_why_false_storage_bound(&n, &a);
            if (rc) return rc;
        }
        if (n > SIZE_MAX - EXPLANATION_HEADER_BYTES) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
        n += EXPLANATION_HEADER_BYTES;
        if (n > explanation_bytes) explanation_bytes = n;
    }
    if (explanation_bytes) {
        if (c->explanation_storage) {
            if (c->explanation_bytes < explanation_bytes) return MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL;
            p->public.external_explanation_bytes = explanation_bytes;
        } else if (!plan_part(p, 1, explanation_bytes, EXPLANATION_ALIGNMENT,
            &p->explanation, &p->public.explanation_bytes)) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    }
    size_t total = p->public.arena_bytes;
    if (p->public.external_backend_bytes > SIZE_MAX - total) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    total += p->public.external_backend_bytes;
    if (p->public.external_explanation_bytes > SIZE_MAX - total) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    p->public.total_execution_bytes = total + p->public.external_explanation_bytes;
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t maelys_datalog_session_storage_requirements_configured(
    const maelys_datalog_policy_t *policy, size_t index, const maelys_datalog_session_config_t *c,
    maelys_datalog_session_storage_plan_t *out, maelys_datalog_diagnostic_t *diag) {
    maelys_datalog_status_t rc = maelys_datalog_diagnostic_clear(diag);
    if (rc) return rc;
    if (!out || out->struct_size < MAELYS_DATALOG_SESSION_PLAN_PREFIX_SIZE)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (out->plan_version != MAELYS_DATALOG_SESSION_PLAN_VERSION ||
        (out->required_features & ~MAELYS_DATALOG_RESOURCE_SUPPORTED_014)) return MAELYS_DATALOG_STATUS_UNSUPPORTED;
    if (out->reserved) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (out->struct_size < sizeof(*out)) return MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL;
    fixed_plan_t p;
    rc = fixed_plan(policy, index, c, &p, diag);
    if (!rc) { p.public.struct_size = out->struct_size; *out = p.public; }
    return rc;
}

static atomic_flag arena_lock = ATOMIC_FLAG_INIT;
static maelys_datalog_session_t *resource_sessions;
static void lock_arenas(void) { while (atomic_flag_test_and_set_explicit(&arena_lock, memory_order_acquire)) {} }
static void unlock_arenas(void) { atomic_flag_clear_explicit(&arena_lock, memory_order_release); }
static void resource_session_track_legacy(maelys_datalog_session_t *s, size_t bytes,
    const maelys_datalog_backend_storage_t *storage) {
    /* Record existing live ranges for the new constructor. This does not
     * change the legacy provider-arena admission or ownership contract. */
    s->arena_bytes=bytes;s->backend_bytes_start=storage->bytes;s->backend_bytes=storage->size;
    lock_arenas();s->arena_next=resource_sessions;resource_sessions=s;unlock_arenas();
}
static void resource_session_unlink(maelys_datalog_session_t *s) {
    lock_arenas();
    maelys_datalog_session_t **p = &resource_sessions;
    while (*p && *p != s) p = &(*p)->arena_next;
    if (*p) *p = s->arena_next;
    unlock_arenas();
}
static int resource_conflict(const void *a, size_t n, const void *b, size_t m) {
    return n && m && a && b && explanation_overlap(a,n,b,m);
}
static maelys_datalog_status_t fixed_init(void *arena, size_t bytes,
    const maelys_datalog_policy_t *policy, size_t index, const maelys_datalog_session_config_t *config,
    const fixed_plan_t *p, maelys_datalog_session_t **out, maelys_datalog_diagnostic_t *diag) {
    if (!arena || (uintptr_t)arena % p->public.arena_alignment || bytes > UINTPTR_MAX - (uintptr_t)arena)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (bytes < p->public.arena_bytes)
        return resource_failure(diag, MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL, "arena_bytes", bytes, p->public.arena_bytes);
    unsigned char *base = arena;
    void *provider = config && config->backend_storage.struct_size ? config->backend_storage.bytes :
        p->provider_bytes ? base + p->provider : NULL;
    void *explanation = config && config->explanation_storage ? config->explanation_storage :
        p->public.explanation_bytes ? base + p->explanation : NULL;
    size_t explanation_bytes = p->public.explanation_bytes + p->public.external_explanation_bytes;
    const void *ranges[] = {arena, provider, explanation};
    size_t sizes[] = {p->public.arena_bytes, p->public.external_backend_bytes, p->public.external_explanation_bytes};
    for (size_t i=0;i<3;++i) {
        if (resource_conflict(ranges[i],sizes[i],policy,sizeof(*policy)) ||
            (config && resource_conflict(ranges[i],sizes[i],config,sizeof(*config))) ||
            resource_conflict(ranges[i],sizes[i],out,sizeof(*out)) ||
            (diag && resource_conflict(ranges[i],sizes[i],diag,sizeof(*diag))))
            return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
        for(size_t j=0;j<i;++j) if(resource_conflict(ranges[i],sizes[i],ranges[j],sizes[j]))
            return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    }
    maelys_datalog_session_t *s = arena;
    lock_arenas();
    /* A planned arena is published before prepare, so another constructor must
     * inspect only its immutable reservations, never its initializing inputs.
     * Legacy explanation ranges are assigned under the workspace lock. */
    lock_workspaces();
    for (maelys_datalog_session_t *live = resource_sessions; live; live = live->arena_next)
        for(size_t i=0;i<3;++i)
            if (resource_conflict(ranges[i],sizes[i],live,live->arena_bytes) ||
                resource_conflict(ranges[i],sizes[i],live->backend_bytes_start,live->backend_bytes) ||
                resource_conflict(ranges[i],sizes[i],
                    live->planned ? live->reserved_explanation : live->explanation_storage,
                    live->planned ? live->reserved_explanation_bytes : live->explanation_bytes) ||
                (!live->planned && live->inputs && (resource_conflict(ranges[i],sizes[i],live->inputs,
                    maelys_datalog_prepared_session_storage_bytes(live->resources.input_facts,live->inputs->pool_capacity)) ||
                    resource_conflict(ranges[i],sizes[i],live->inputs->result_workspace,
                        maelys_datalog_solve_workspace_bytes(live->resources.derived_facts,1)) ||
                    resource_conflict(ranges[i],sizes[i],live->inputs->prepared,sizeof(*live->inputs->prepared))))) {
                unlock_workspaces(); unlock_arenas(); return MAELYS_DATALOG_STATUS_INVALID_STATE;
            }
    memset(s,0,sizeof(*s));
    s->planned = 1; s->arena_bytes = p->public.arena_bytes;
    s->backend_bytes_start = provider; s->backend_bytes = p->provider_bytes;
    s->reserved_explanation = explanation;
    s->reserved_explanation_bytes = explanation_bytes;
    s->arena_next = resource_sessions; resource_sessions = s;
    unlock_workspaces();
    unlock_arenas();
    const maelys_datalog_internal_ruleset_t *rules = &policy->set.policies[index];
    if (!policy->owns_storage) {
        maelys_datalog_internal_ruleset_t *copy = (void *)(base+p->snapshot);
        *copy = *rules; rules = copy;
    }
    maelys_datalog_internal_solve_result_t *workspace = maelys_datalog_solve_workspace_init(
        base+p->workspace, p->resources.derived_facts, 1);
    maelys_datalog_status_t rc = (maelys_datalog_status_t)maelys_datalog_prepared_session_init_sized(
        base+p->prepared,rules,p->resources.input_facts,p->pool,p->resources.symbols,
        p->resources.text_bytes,workspace,&s->inputs);
    if (rc) { resource_session_unlink(s); return rc; }
    s->program.ruleset = rules; s->program.prepared_inputs = s->inputs;
    s->resources = p->resources; s->work_limit = p->work_limit;
    s->borrows_inputs = p->borrows; s->reference_backend = p->reference;
    s->canonical = (void *)(base+p->canonical); s->emitted = (void *)(base+p->emitted);
    const maelys_datalog_backend_v6_t *b = p->backend;
    strcpy(s->name,b->name); strcpy(s->semantic_id,b->semantic_id);
    /* Common post-prepare operations retain their exact ABI 5 signatures.
     * Copy named members, never reinterpret an ABI 6 descriptor as ABI 5. */
    s->backend.name=s->name; s->backend.semantic_id=s->semantic_id; s->backend.capabilities=b->capabilities;
    s->backend.solve=b->solve; s->backend.commit=b->commit; s->backend.destroy=b->destroy;
    s->backend.destroy_result=b->destroy_result;
    s->backend.explanation_storage_requirements=b->explanation_storage_requirements;
    s->backend.explanation_prepare=b->explanation_prepare;
    s->backend.explanation_write_text=b->explanation_write_text;
    char fingerprint[65], identity[512];
    rc = maelys_datalog_program_fingerprint(&s->program,fingerprint);
    int n = rc ? -1 : snprintf(identity,sizeof(identity),"maelys-execution-v1\n%s\n%s\n%s\n%llu\n%llu\n%s\n",
        fingerprint,s->name,s->semantic_id,(unsigned long long)p->required,
        (unsigned long long)s->work_limit,MAELYS_DATALOG_SIZE_PROFILE_NAME);
    if (n<0 || (size_t)n>=sizeof(identity) || maelys_sha256_hex((const unsigned char *)identity,(size_t)n,s->execution_fingerprint))
        rc=MAELYS_DATALOG_STATUS_INTERNAL;
    if (!rc && p->resources.required_features) {
        const maelys_datalog_session_resources_t *v=&p->resources;
        n=snprintf(identity,sizeof(identity),"maelys-execution-v2\n%s\n%zu\n%zu\n%zu\n%zu\n%u\n%llu\n",
            s->execution_fingerprint,v->input_facts,v->derived_facts,v->symbols,v->text_bytes,
            v->memory_mode,(unsigned long long)v->required_features);
        if (n<0 || (size_t)n>=sizeof(identity) || maelys_sha256_hex((const unsigned char *)identity,(size_t)n,s->execution_fingerprint))
            rc=MAELYS_DATALOG_STATUS_INTERNAL;
    }
    if (!rc && explanation_bytes) {
        maelys_datalog_session_config_t c=*config;
        c.explanation_storage=explanation; c.explanation_bytes=explanation_bytes;
        rc=configure_explanation_workspace(s,&c);
    }
    if (!rc) {
        maelys_datalog_backend_storage_t storage={sizeof(storage),provider,p->provider_bytes,p->provider_alignment};
        rc=maelys_datalog_callback_status(b->prepare(&s->program,&s->resources,&storage,&s->state));
        if (rc) b->destroy(s->state);
    }
    if (rc) {
        destroy_explanation_workspace(s); maelys_datalog_prepared_session_destroy(s->inputs);
        resource_session_unlink(s); return rc;
    }
    if (policy->owns_storage) { maelys_datalog_policy_retain_storage(policy); s->policy_owner=policy; }
    *out=s;
    return MAELYS_DATALOG_STATUS_OK;
}
maelys_datalog_status_t maelys_datalog_session_init_configured(void *arena,size_t bytes,
    const maelys_datalog_policy_t *policy,size_t index,const maelys_datalog_session_config_t *c,
    maelys_datalog_session_t **out,maelys_datalog_diagnostic_t *diag) {
    /* Reject overlapping outputs before clearing them: the supplied arena may
     * already contain a live session. Even a failed init must not corrupt it. */
    if (arena && bytes <= UINTPTR_MAX - (uintptr_t)arena &&
        (resource_conflict(arena,bytes,out,out ? sizeof(*out) : 0) ||
         resource_conflict(arena,bytes,diag,diag ? sizeof(*diag) : 0)))
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (c && (resource_conflict(c->backend_storage.bytes,c->backend_storage.size,out,out ? sizeof(*out) : 0) ||
        resource_conflict(c->backend_storage.bytes,c->backend_storage.size,diag,diag ? sizeof(*diag) : 0) ||
        resource_conflict(c->explanation_storage,c->explanation_bytes,out,out ? sizeof(*out) : 0) ||
        resource_conflict(c->explanation_storage,c->explanation_bytes,diag,diag ? sizeof(*diag) : 0)))
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (out) *out=NULL;
    maelys_datalog_status_t rc=maelys_datalog_diagnostic_clear(diag);
    if(rc) return rc;
    if(!out) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    fixed_plan_t p;
    rc=fixed_plan(policy,index,c,&p,diag);
    return rc?rc:fixed_init(arena,bytes,policy,index,c,&p,out,diag);
}
static maelys_datalog_status_t create_resourced(const maelys_datalog_policy_t *policy,size_t index,
    const maelys_datalog_session_config_t *c,maelys_datalog_session_t **out) {
    if(out) *out=NULL;
    if(!out) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    maelys_datalog_status_t rc=policy_config_valid(policy,index,c);
    if(rc) return rc;
    if ((c->custom_backend || c->context) && !c->use_v6) {
        maelys_datalog_session_resources_t v;
        rc=normalize_resources(&policy->set.policies[index],c,&v,NULL);
        if(rc) return rc;
        if(v.required_features) return MAELYS_DATALOG_STATUS_UNSUPPORTED;
        maelys_datalog_session_config_t legacy=*c;
        legacy.resources.struct_size=0;
        return maelys_datalog_session_create_configured(policy,index,&legacy,out);
    }
    fixed_plan_t p;
    rc=fixed_plan(policy,index,c,&p,NULL);
    if(rc) return rc;
    void *arena=malloc(p.public.arena_bytes);
    if(!arena) return MAELYS_DATALOG_STATUS_INTERNAL;
    rc=fixed_init(arena,p.public.arena_bytes,policy,index,c,&p,out,NULL);
    if(rc) free(arena); else (*out)->owns_arena=1;
    return rc;
}
