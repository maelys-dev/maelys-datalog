/* SPDX-License-Identifier: MPL-2.0 */
/* Experimental translation unit only. The normal runtime source is included
 * unchanged; only its input-materialization call is redirected in this binary.
 * There is no installed delta entry point and no native delta backend here. */
#include "src/core/maelys_datalog_prepared_session_internal.h"
static maelys_result_t delta_materialize(maelys_datalog_internal_prepared_session_t *,
    const maelys_datalog_fact_t *, size_t, char *, size_t);
#define maelys_datalog_prepared_session_materialize_inputs_diagnosed delta_materialize
#include "src/runtime/maelys_datalog_runtime.c"
#undef maelys_datalog_prepared_session_materialize_inputs_diagnosed
#include "host_delta_backend.h"
#include <inttypes.h>
#include <limits.h>
#ifdef MAELYS_BENCH_COUNT
#include <valgrind/callgrind.h>
#define COLLECT() CALLGRIND_TOGGLE_COLLECT
#define DUMP(s) do { CALLGRIND_DUMP_STATS_AT(s); CALLGRIND_ZERO_STATS; } while (0)
#else
#define COLLECT() ((void)0)
#define DUMP(s) ((void)(s))
#endif
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); abort(); } } while (0)
#define OK(x) CHECK((x) == 0)
#define NI __attribute__((noinline))
#define CAP 512u
#define STEPS 8u
typedef maelys_datalog_internal_fact_t native_fact;
typedef struct { maelys_datalog_fact_t add[CAP], remove[CAP]; size_t na, nr; } change;
typedef struct {
    native_fact facts[CAP]; size_t count; uint64_t generation, logical;
} retained;
typedef struct {
    maelys_datalog_session_t *session;
    delta_backend_state backend;
    retained committed;
    native_fact added[2][CAP], removed[2][CAP]; size_t na[2], nr[2];
} bank;
typedef struct { bank *owner; const change *steps[2]; size_t count; int delta; } operation;
static operation *current;
static const char *atoms[] = {"alpha", "beta", "gamma", "delta", "epsilon"};

static NI maelys_result_t delta_convert(bank *b, const maelys_datalog_fact_t *source,
    size_t count, native_fact *dest, size_t *used, int adding) {
    if (count > CAP || (!source && count)) return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    *used = 0;
    for (size_t i = 0; i < count; ++i) {
        const maelys_datalog_fact_t *f = &source[i];
        if (!f->predicate || f->arity > MAELYS_DATALOG_MAX_TERMS) return MAELYS_ERR_INVALID_ARGUMENT;
        maelys_datalog_predicate_id_t id;
        if (!maelys_datalog_predicate_registry_find(&b->session->inputs->prepared->registry,
                                                   f->predicate, f->arity, &id)) return MAELYS_ERR_INVALID_FIELD;
        const maelys_datalog_predicate_entry_t *def = maelys_datalog_predicate_registry_get(
            &b->session->inputs->prepared->registry, id);
        if (def->kind_flags & MAELYS_DATALOG_PRED_KIND_POLICY_FACT) return MAELYS_ERR_FORBIDDEN;
        if (!(def->kind_flags & MAELYS_DATALOG_PRED_KIND_EDB)) return MAELYS_ERR_INVALID_FIELD;
        for (size_t j = 0; j < f->arity; ++j) {
            maelys_datalog_status_t rc = maelys_datalog_validate_value(&f->terms[j], 0);
            if (rc) return (maelys_result_t)rc;
            if (f->terms[j].kind == MAELYS_DATALOG_VALUE_SYMBOL &&
                strnlen(f->terms[j].as.symbol, MAELYS_DATALOG_MAX_STRING_BYTES + 1u) > MAELYS_DATALOG_MAX_STRING_BYTES)
                return MAELYS_ERR_INVALID_ARGUMENT;
        }
        native_fact value = {.predicate_id = id, .arity = f->arity}; int found = 0;
        maelys_result_t rc = (maelys_result_t)maelys_datalog_resolve_public_terms(&b->session->inputs->symbols,
            f->terms, f->arity, value.terms, &found, 0);
        if (rc) return rc;
        if (!found) { if (adding) return MAELYS_ERR_INVALID_FIELD; else continue; }
        dest[(*used)++] = value;
    }
    maelys_datalog_fact_set_t set;
    maelys_datalog_fact_set_init(&set, dest, CAP); set.count = *used; set.sorted = 0;
    maelys_result_t rc = maelys_datalog_fact_set_sort(&set);
    if (!rc) rc = maelys_datalog_fact_set_dedup(&set);
    *used = set.count; return rc;
}
static NI size_t delta_position(const native_fact *facts, size_t n, const native_fact *f) {
    size_t lo = 0, hi = n;
    while (lo < hi) { size_t mid = lo + (hi-lo)/2;
        if (maelys_datalog_fact_cmp(&facts[mid], f) < 0) lo = mid+1; else hi = mid;
    } return lo;
}
/* All three arrays are sorted and unique. facts and added are provisional;
 * retained bytes never alias them. Each pass advances its cursors monotonically.
 * Failed capacity checks may change scratch, but never publish *count. */
static NI maelys_result_t delta_compose_linear(native_fact *facts, size_t *count,
    size_t capacity, native_fact *added, size_t na, const native_fact *removed, size_t nr) {
    size_t n = *count;
    if (n > capacity) return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    if (nr) {
        size_t write = 0, removal = 0;
        for (size_t read = 0; read < n; ++read) {
            while (removal < nr && maelys_datalog_fact_cmp(&removed[removal], &facts[read]) < 0)
                ++removal;
            if (removal < nr && !maelys_datalog_fact_cmp(&removed[removal], &facts[read]))
                continue;
            if (write != read) facts[write] = facts[read];
            ++write;
        }
        n = write;
    }
    /* Discard additions already among the survivors. A removed-and-added fact
     * is absent from these survivors, so the addition wins. Compact only scratch. */
    size_t base = 0, fresh = 0;
    for (size_t i = 0; i < na; ++i) {
        while (base < n && maelys_datalog_fact_cmp(&facts[base], &added[i]) < 0) ++base;
        if (base < n && !maelys_datalog_fact_cmp(&facts[base], &added[i])) continue;
        if (fresh != i) added[fresh] = added[i];
        ++fresh;
    }
    if (fresh > capacity - n) return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    /* Merge disjoint sorted lists backward; write-base == unconsumed additions,
     * so a write cannot overwrite an unread survivor. No repeated range moves. */
    size_t left = n, right = fresh, write = n + fresh;
    while (right) {
        if (left && maelys_datalog_fact_cmp(&facts[left-1], &added[right-1]) > 0)
            facts[--write] = facts[--left];
        else
            facts[--write] = added[--right];
    }
    *count = n + fresh;
    return MAELYS_OK;
}

/* A mark lives in an otherwise invalid arity bit in provisional facts only.
 * Binary search compares the original key, never the marked arity. */
#define DELTA_TOMBSTONE 128u
_Static_assert(MAELYS_DATALOG_MAX_TERMS < DELTA_TOMBSTONE, "tombstone arity bit");
static int delta_t_compare(native_fact *fact, const native_fact *key) {
    uint8_t arity = fact->arity;
    if (!(arity & DELTA_TOMBSTONE)) return maelys_datalog_fact_cmp(fact, key);
    fact->arity = arity & ~DELTA_TOMBSTONE;
    int cmp = maelys_datalog_fact_cmp(fact, key);
    fact->arity = arity;
    return cmp;
}
static size_t delta_t_position(native_fact *facts, size_t n, const native_fact *key) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi-lo)/2;
        if (delta_t_compare(&facts[mid], key) < 0) lo = mid+1; else hi = mid;
    }
    return lo;
}
/* Sorted unique inputs, add wins. No new array or heap storage. lo/hi bound
 * the ORIGINAL affected interval [lo,hi). Compaction and merge stay within
 * that interval when cardinality is unchanged. Otherwise a contiguous pool
 * necessarily moves the suffix once. Searches can read outside the interval.
 * Capacity is checked before payload writes; marks are provisional metadata. */
static NI maelys_result_t delta_compose_tombstones(native_fact *facts, size_t *count,
    size_t capacity, native_fact *added, size_t na, const native_fact *removed, size_t nr) {
    size_t n = *count, lo = n, hi = 0, deleted = 0, fresh = 0;
    if (n > capacity) return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    for (size_t i = 0; i < nr; ++i) {
        size_t pos = delta_t_position(facts, n, &removed[i]);
        if (pos == n || delta_t_compare(&facts[pos], &removed[i])) continue;
        facts[pos].arity |= DELTA_TOMBSTONE;
        ++deleted;
        if (pos < lo) lo = pos;
        if (pos+1 > hi) hi = pos+1;
    }
    for (size_t i = 0; i < na; ++i) {
        size_t pos = delta_t_position(facts, n, &added[i]);
        if (pos < n && !delta_t_compare(&facts[pos], &added[i])) {
            if (facts[pos].arity & DELTA_TOMBSTONE) {
                facts[pos].arity &= ~DELTA_TOMBSTONE;
                --deleted;
            }
            continue;
        }
        if (fresh != i) added[fresh] = added[i];
        ++fresh;
        if (pos < lo) lo = pos;
        if (pos > hi) hi = pos;
    }
    if (fresh > capacity - (n-deleted)) return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    if (!deleted && !fresh) return MAELYS_OK;
    size_t write = lo;
    for (size_t read = lo; read < hi; ++read) {
        if (facts[read].arity & DELTA_TOMBSTONE) continue;
        if (write != read) facts[write] = facts[read];
        ++write;
    }
    size_t end = write + fresh;
    if (end != hi && hi < n)
        memmove(facts+end, facts+hi, (n-hi)*sizeof(*facts));
    size_t left = write, right = fresh;
    write = end;
    while (right) {
        if (left > lo && maelys_datalog_fact_cmp(&facts[left-1], &added[right-1]) > 0)
            facts[--write] = facts[--left];
        else
            facts[--write] = added[--right];
    }
    *count = n-deleted+fresh;
    return MAELYS_OK;
}

static NI maelys_result_t delta_stage(operation *op) {
    bank *b = op->owner;
    maelys_datalog_internal_prepared_session_t *p = b->session->inputs;
    /* Validate all submitted entries before provisional input writes. */
    for (size_t step = 0; step < op->count; ++step) {
        maelys_result_t rc = delta_convert(b, op->steps[step]->remove, op->steps[step]->nr,
                                          b->removed[step], &b->nr[step], 0);
        if (!rc) rc = delta_convert(b, op->steps[step]->add, op->steps[step]->na,
                                    b->added[step], &b->na[step], 1);
        if (rc) return rc;
    }
    size_t n = b->committed.count;
    memcpy(p->fact_pool, b->committed.facts, n*sizeof(native_fact));
    for (size_t step = 0; step < op->count; ++step) {
        if (op->delta == 3) {
            size_t capacity = p->edb.fact_capacity < CAP ? p->edb.fact_capacity : CAP;
            maelys_result_t rc = delta_compose_tombstones(p->fact_pool, &n, capacity,
                b->added[step], b->na[step], b->removed[step], b->nr[step]);
            if (rc) return rc;
            continue;
        }
        if (op->delta == 2) {
            size_t capacity = p->edb.fact_capacity < CAP ? p->edb.fact_capacity : CAP;
            maelys_result_t rc = delta_compose_linear(p->fact_pool, &n, capacity,
                b->added[step], b->na[step], b->removed[step], b->nr[step]);
            if (rc) return rc;
            continue;
        }
        for (size_t i = 0; i < b->nr[step]; ++i) {
            native_fact *f = &b->removed[step][i]; size_t pos = delta_position(p->fact_pool,n,f);
            if (pos < n && !maelys_datalog_fact_cmp(&p->fact_pool[pos],f)) {
                memmove(p->fact_pool+pos,p->fact_pool+pos+1,(n-pos-1)*sizeof(*f)); --n;
            }
        }
        for (size_t i = 0; i < b->na[step]; ++i) {
            native_fact *f = &b->added[step][i]; size_t pos = delta_position(p->fact_pool,n,f);
            if (pos < n && !maelys_datalog_fact_cmp(&p->fact_pool[pos],f)) continue;
            if (n == CAP || n == p->edb.fact_capacity) return MAELYS_ERR_PAYLOAD_TOO_LARGE;
            memmove(p->fact_pool+pos+1,p->fact_pool+pos,(n-pos)*sizeof(*f));
            p->fact_pool[pos] = *f; ++n;
        }
    }
    p->edb.fact_count = n; p->edb.fact_set.count = n; p->edb.fact_set.sorted = 1; p->edb.immutable = 0;
    return maelys_datalog_edb_finalize(&p->edb);
}
static NI maelys_result_t delta_materialize(maelys_datalog_internal_prepared_session_t *p,
    const maelys_datalog_fact_t *facts, size_t count, char *message, size_t bytes) {
    if (current && current->delta && current->owner->session->inputs == p) return delta_stage(current);
    return maelys_datalog_prepared_session_materialize_inputs_diagnosed(p,facts,count,message,bytes);
}
static NI void delta_accept(bank *b, uint64_t logical, int delta) {
    /* This O(N) retained copy is deliberately included in the candidate cost. */
    if (delta) {
        size_t n = b->session->inputs->edb.fact_count;
        memcpy(b->committed.facts,b->session->inputs->fact_pool,n*sizeof(native_fact));
        b->committed.count = n;
    }
    ++b->committed.generation; b->committed.logical = logical;
}
static NI maelys_datalog_status_t delta_execute(operation *op, bank *expected_owner,
    uint64_t expected_generation, uint64_t logical, const maelys_datalog_fact_t *snapshot,
    size_t count, int reject_late, maelys_datalog_result_t **out) {
    bank *b = op->owner; *out = NULL;
    if (b != expected_owner || !b->committed.generation || expected_generation != b->committed.generation ||
        expected_generation == UINT64_MAX || b->session->active || b->session->busy || op->count > 2)
        return MAELYS_DATALOG_STATUS_INVALID_STATE;
    if (b->committed.logical + op->count != logical) return MAELYS_DATALOG_STATUS_INVALID_STATE;
    current = op;
    maelys_datalog_status_t rc = maelys_datalog_session_solve_candidate(b->session,
        op->delta ? NULL : snapshot, op->delta ? 0 : count,out,NULL);
    current = NULL;
    if (rc) return rc;
    if (reject_late) {
        OK(maelys_datalog_result_free(*out)); *out = NULL; return MAELYS_DATALOG_STATUS_INVALID_STATE;
    }
    delta_accept(b,logical,op->delta);
    maelys_datalog_result_commit(*out);
    return 0;
}
static maelys_datalog_fact_t fact(size_t key, int symbol) {
    maelys_datalog_fact_t f = {.predicate="event",.arity=3};
    f.terms[0] = (maelys_datalog_value_t){.kind=MAELYS_DATALOG_VALUE_INTEGER,.as.integer=(int64_t)key};
    f.terms[1] = symbol ? (maelys_datalog_value_t){.kind=MAELYS_DATALOG_VALUE_SYMBOL,.as.symbol=atoms[key%5]} :
                         (maelys_datalog_value_t){.kind=MAELYS_DATALOG_VALUE_INTEGER,.as.integer=(int64_t)(key%7)};
    f.terms[2] = (maelys_datalog_value_t){.kind=MAELYS_DATALOG_VALUE_BOOLEAN,.as.boolean=(key%2) ? 9 : 0};
    return f;
}
static bank *make_bank(const maelys_datalog_policy_t *p) {
    bank *b = calloc(1,sizeof(*b)); CHECK(b);
    maelys_datalog_session_config_t *config;
    OK(maelys_datalog_session_config_create(&config));
    maelys_datalog_backend_storage_t storage = {sizeof(storage),&b->backend,sizeof(b->backend),_Alignof(delta_backend_state)};
    OK(maelys_datalog_session_config_set_backend(config,delta_snapshot_backend()));
    OK(maelys_datalog_session_config_set_backend_storage(config,&storage));
    OK(maelys_datalog_session_create_configured(p,0,config,&b->session));
    OK(maelys_datalog_session_config_free(config)); return b;
}
static void seed_bank(bank *b, const maelys_datalog_fact_t *facts, size_t n) {
    maelys_datalog_result_t *r;
    OK(maelys_datalog_session_solve(b->session,facts,n,&r,NULL));
    delta_accept(b,0,1); OK(maelys_datalog_result_free(r));
}
static uint64_t model_hash(const maelys_datalog_session_t *oracle) {
    const maelys_datalog_internal_prepared_session_t *p = oracle->inputs;
    uint64_t h = UINT64_C(14695981039346656037) ^ p->edb.fact_count;
    for (size_t i=0;i<p->edb.fact_count;++i) {
        maelys_datalog_fact_t f; OK(maelys_datalog_export_fact(p->prepared,&p->symbols,&p->fact_pool[i],&f));
        const char *texts[MAELYS_DATALOG_MAX_TERMS+1] = {f.predicate};
        size_t t = 0;
        do { h = (h ^ (unsigned char)texts[0][t])*UINT64_C(1099511628211); } while(texts[0][t++]);
        h = (h ^ f.arity)*UINT64_C(1099511628211);
        for (size_t j=0;j<f.arity;++j) {
            h = (h ^ f.terms[j].kind)*UINT64_C(1099511628211);
            if (f.terms[j].kind == MAELYS_DATALOG_VALUE_SYMBOL) {
                const unsigned char *s=(const unsigned char*)f.terms[j].as.symbol;
                do { h=(h ^ *s)*UINT64_C(1099511628211); } while(*s++);
            } else h=(h ^ (f.terms[j].kind==MAELYS_DATALOG_VALUE_INTEGER ?
                         (uint64_t)f.terms[j].as.integer : (uint64_t)f.terms[j].as.boolean))*UINT64_C(1099511628211);
        }
    } return h;
}
static void oracle_check(maelys_datalog_session_t *oracle, maelys_datalog_result_t *candidate,
    bank *b, const maelys_datalog_fact_t *facts, size_t n) {
    maelys_datalog_result_t *reference; OK(maelys_datalog_session_solve(oracle,facts,n,&reference,NULL));
    CHECK(b->backend.committed_hash == model_hash(oracle));
    CHECK(b->backend.recorded_count == oracle->inputs->edb.fact_count);
    CHECK(b->session->inputs->edb.fact_count == oracle->inputs->edb.fact_count);
    for (size_t i=0;i<oracle->inputs->edb.fact_count;++i) {
        CHECK(!maelys_datalog_fact_cmp(&b->session->inputs->edb.fact_set.facts[i],&oracle->inputs->edb.fact_set.facts[i]));
        maelys_datalog_fact_t f; OK(maelys_datalog_export_fact(oracle->inputs->prepared,&oracle->inputs->symbols,&oracle->inputs->fact_pool[i],&f));
        const maelys_datalog_fact_t *actual=&b->backend.recorded[i];
        CHECK(!strcmp(f.predicate,actual->predicate) && f.arity==actual->arity);
        for(size_t j=0;j<f.arity;++j) {
            CHECK(f.terms[j].kind==actual->terms[j].kind);
            if(f.terms[j].kind==MAELYS_DATALOG_VALUE_SYMBOL) CHECK(!strcmp(f.terms[j].as.symbol,actual->terms[j].as.symbol));
            else if(f.terms[j].kind==MAELYS_DATALOG_VALUE_INTEGER) CHECK(f.terms[j].as.integer==actual->terms[j].as.integer);
            else CHECK(f.terms[j].as.boolean==actual->terms[j].as.boolean);
        }
    }
    maelys_datalog_fact_view_t a[CAP],c[CAP]; size_t na,nc;
    OK(maelys_datalog_result_enumerate(reference,"seen",3,a,CAP,&na));
    OK(maelys_datalog_result_enumerate(candidate,"seen",3,c,CAP,&nc)); CHECK(na==nc);
    for(size_t i=0;i<na;++i) for(size_t j=0;j<3;++j) {
        CHECK(a[i].terms[j].kind==c[i].terms[j].kind);
        if(a[i].terms[j].kind==MAELYS_DATALOG_VALUE_SYMBOL) {
            CHECK(a[i].terms[j].as.symbol_id==c[i].terms[j].as.symbol_id);
            const char *at,*ct; size_t al,cl;
            OK(maelys_datalog_result_symbol_text(reference,a[i].terms[j].as.symbol_id,&at,&al));
            OK(maelys_datalog_result_symbol_text(candidate,c[i].terms[j].as.symbol_id,&ct,&cl));
            CHECK(al==cl && !memcmp(at,ct,al));
        } else if(a[i].terms[j].kind==MAELYS_DATALOG_VALUE_INTEGER) CHECK(a[i].terms[j].as.integer==c[i].terms[j].as.integer);
        else CHECK(a[i].terms[j].as.boolean==c[i].terms[j].as.boolean);
    }
    if(n) { int present=0; OK(maelys_datalog_result_query(candidate,"event",facts[0].terms,3,&present)); CHECK(present); }
    OK(maelys_datalog_result_free(reference));
}
static size_t model_snapshot(const unsigned *supports, size_t n, int symbol, maelys_datalog_fact_t *out, int reverse) {
    size_t count=0;
    for(size_t i=0;i<2*n;++i) { size_t k=reverse ? 2*n-1-i : i; if(supports[k]) out[count++]=fact(k,symbol); }
    return count;
}
/* Caller-side occurrence ledger, independent of the engine native-fact store.
 * The pre-generated positions are the logical trace, not computed native work. */
static NI void caller_update(change *c, unsigned *supports, size_t old, size_t added, int symbol, int delta) {
    CHECK(supports[old]);
    if (!--supports[old] && delta) c->remove[c->nr++]=fact(old,symbol);
    if (!supports[added]++ && delta) c->add[c->na++]=fact(added,symbol);
}
static void run_case(const maelys_datalog_policy_t *policy, int delta, int derive, size_t n,
    int symbol, const char *scenario, int lifecycle) {
    unsigned supports[2*CAP]={0}; size_t positions[CAP], trace[STEPS+1][CAP];
    int window=!strcmp(scenario,"window");
    size_t batch=!strcmp(scenario,"empty") ? 0 : !strcmp(scenario,"small") ? 4 :
                 !strcmp(scenario,"replace") ? n : 1;
    for(size_t i=0;i<n;++i) { positions[i]=window ? i/2 : i; ++supports[positions[i]]; }
    memcpy(trace[0],positions,n*sizeof(size_t));
    for(size_t t=0;t<STEPS;++t) {
        memcpy(trace[t+1],trace[t],n*sizeof(size_t));
        for(size_t i=0;i<batch;++i) {
            size_t pos=(t*batch+i)%n, old=trace[t+1][pos];
            trace[t+1][pos]=old<n ? old+n : old-n;
        }
    }
    char name[160]; snprintf(name,sizeof(name),"%s/%s/%s/%zu/%s/%s",delta == 3 ? "T" : delta == 2 ? "L" : delta ? "B" : "A",
        lifecycle ? "caller" : "engine", derive ? "projection" : "inert",n,symbol ? "symbol" : "integer",scenario);
    maelys_datalog_fact_t full[CAP], oracle_full[CAP]; size_t size=model_snapshot(supports,n,symbol,full,0);
    size_t alloc_before=delta_alloc_calls, bytes_before=delta_alloc_bytes;
    COLLECT();
    bank *banks[2]={make_bank(policy),make_bank(policy)};
    change *journal=calloc(2,sizeof(*journal)); CHECK(journal);
    seed_bank(banks[0],full,size); seed_bank(banks[1],full,size);
    COLLECT();
    size_t setup_calls=delta_alloc_calls-alloc_before, setup_bytes=delta_alloc_bytes-bytes_before;
    char part[192]; snprintf(part,sizeof(part),"%s/init",name); DUMP(part);
    maelys_datalog_session_t *oracle; OK(maelys_datalog_session_create(policy,0,&oracle));
    uint64_t digest=0;
    delta_alloc_disabled=1;
    for(size_t t=0;t<STEPS;++t) {
        /* Include caller work only in the second, separately reported scope.
         * No timing, model oracle, or output checking occurs in either scope. */
        if(lifecycle) COLLECT();
        change *c=&journal[t%2]; c->na=c->nr=0;
        for(size_t i=0;i<batch;++i) {
            size_t pos=(t*batch+i)%n, old=positions[pos], added=trace[t+1][pos];
            caller_update(c,supports,old,added,symbol,delta); positions[pos]=added;
        }
        if(!delta) size=model_snapshot(supports,n,symbol,full,(int)(t%2));
        bank *b=banks[window ? (t%2) : 0];
        operation op={.owner=b,.delta=delta,.count=(size_t)(t+1-b->committed.logical)};
        CHECK(op.count && op.count<=2);
        for(size_t j=0;j<op.count;++j) op.steps[j]=&journal[(b->committed.logical+j)%2];
        maelys_datalog_result_t *result;
        if(!lifecycle) COLLECT();
        maelys_datalog_status_t rc=delta_execute(&op,b,b->committed.generation,t+1,full,size,0,&result);
        COLLECT(); OK(rc);
        /* Independent oracle: raw occurrences from immutable trace, deliberately
         * retaining duplicate suppliers and reversing order on alternate steps. */
        for(size_t i=0;i<n;++i) oracle_full[i]=fact(trace[t+1][t%2 ? n-i-1 : i],symbol);
        oracle_check(oracle,result,b,oracle_full,n);
        digest=(digest ^ b->backend.committed_hash)*UINT64_C(1099511628211);
        COLLECT(); rc=maelys_datalog_result_free(result); COLLECT(); OK(rc);
        if(t==0) { snprintf(part,sizeof(part),"%s/first",name); DUMP(part); }
    }
    delta_alloc_disabled=0;
    snprintf(part,sizeof(part),"%s/steady",name); DUMP(part);
    printf("%s,%u,%" PRIu64 ",%zu,%zu,%zu,%zu,%zu\n",name,STEPS,digest,
        sizeof(retained),sizeof(bank),banks[window ? (STEPS-1)%2 : 0]->session->inputs->edb.fact_count,setup_calls,setup_bytes);
    for(size_t i=0;i<2;++i) { OK(maelys_datalog_session_free(banks[i]->session)); free(banks[i]); }
    OK(maelys_datalog_session_free(oracle)); free(journal);
}
static void run_failure(const maelys_datalog_policy_t *policy, int delta, int derive, int failure) {
    maelys_datalog_fact_t full[64], proposed[64];
    for(size_t i=0;i<64;++i) full[i]=proposed[i]=fact(i,1);
    proposed[0]=fact(64,1);
    bank *b=make_bank(policy); seed_bank(b,full,64);
    retained *before=malloc(sizeof(*before)); CHECK(before); *before=b->committed;
    change *c=calloc(1,sizeof(*c)); CHECK(c); c->add[0]=proposed[0]; c->remove[0]=full[0]; c->na=c->nr=1;
    operation op={.owner=b,.delta=delta,.count=1,.steps={c}};
    uint64_t hash=b->backend.committed_hash; unsigned commits=b->backend.commits;
    delta_alloc_disabled=1;
    for(size_t i=0;i<STEPS;++i) {
        b->backend.fail=failure<3 ? failure : 0;
        maelys_datalog_result_t *result=NULL;
        COLLECT();
        maelys_datalog_status_t rc=delta_execute(&op,b,b->committed.generation,1,proposed,64,failure==3,&result);
        COLLECT();
        CHECK(rc && !result && b->backend.committed_hash==hash && b->backend.commits==commits);
        CHECK(!memcmp(before,&b->committed,sizeof(*before)));
    }
    char name[160]; snprintf(name,sizeof(name),"%s/engine/%s/64/symbol/abort%d/failure",delta==3?"T":delta==2?"L":delta?"B":"A",derive?"projection":"inert",failure);
    DUMP(name);
    printf("%.*s,%u,%" PRIu64 ",%zu,%zu,64,0,0\n",(int)(strlen(name)-8),name,STEPS,hash,sizeof(retained),sizeof(bank));
    b->backend.fail=0; c->na=c->nr=0;
    maelys_datalog_result_t *r=NULL;
    OK(delta_execute(&op,b,b->committed.generation,1,full,64,0,&r)); OK(maelys_datalog_result_free(r));
    CHECK(b->backend.committed_hash==hash);
    delta_alloc_disabled=0;
    OK(maelys_datalog_session_free(b->session)); free(before); free(c); free(b);
}
static void conformance(const maelys_datalog_policy_t *policy, int mode) {
    bank *b=make_bank(policy),*other=make_bank(policy);
    maelys_datalog_fact_t initial=fact(0,1); seed_bank(b,&initial,1); seed_bank(other,&initial,1);
    change *c=calloc(1,sizeof(*c)); retained *before=malloc(sizeof(*before)); CHECK(c && before);
    *before=b->committed;
    operation op={.owner=b,.delta=mode,.count=1,.steps={c}};
    maelys_datalog_result_t *r=NULL;
    delta_alloc_disabled=1;
    CHECK(delta_execute(&op,other,1,1,NULL,0,0,&r)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    CHECK(delta_execute(&op,b,0,1,NULL,0,0,&r)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    c->add[0]=fact(1,1); c->add[0].terms[1].as.symbol="undeclared"; c->na=1;
    CHECK(delta_execute(&op,b,1,1,NULL,0,0,&r)!=0); CHECK(!memcmp(before,&b->committed,sizeof(*before)));
    c->na=0; c->nr=1; c->remove[0]=fact(1,1); c->remove[0].terms[1].as.symbol="absent";
    OK(delta_execute(&op,b,1,1,NULL,0,0,&r)); OK(maelys_datalog_result_free(r)); CHECK(b->committed.count==1);
    *before=b->committed;
    c->nr=0; c->na=1; c->add[0]=fact(1,1);
    for(int failure=1;failure<=3;++failure) {
        uint64_t hash=b->backend.committed_hash; unsigned commits=b->backend.commits;
        b->backend.fail=failure<3 ? failure : 0;
        CHECK(delta_execute(&op,b,b->committed.generation,2,NULL,0,failure==3,&r)!=0);
        CHECK(!r && b->backend.committed_hash==hash && b->backend.commits==commits);
        CHECK(!memcmp(before,&b->committed,sizeof(*before)));
    }
    b->backend.fail=0; c->na=2; c->add[0]=initial; c->add[1]=initial; c->nr=1; c->remove[0]=initial;
    OK(delta_execute(&op,b,b->committed.generation,2,NULL,0,0,&r)); CHECK(b->committed.count==1);
    maelys_datalog_result_t *rejected=NULL;
    CHECK(delta_execute(&op,b,b->committed.generation,3,NULL,0,0,&rejected)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    OK(maelys_datalog_result_free(r));
    *before=b->committed; c->nr=0; c->na=MAELYS_DATALOG_MAX_FACTS_PER_PRED+1;
    CHECK(c->na<=CAP); for(size_t i=0;i<c->na;++i)c->add[i]=fact(i,1);
    maelys_datalog_status_t cap_rc = delta_execute(&op,b,b->committed.generation,3,NULL,0,0,&r);
    CHECK(cap_rc==MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
    CHECK(!memcmp(before,&b->committed,sizeof(*before)));
    c->na=CAP+1;
    CHECK(delta_execute(&op,b,b->committed.generation,3,NULL,0,0,&r)==MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE);
    CHECK(!memcmp(before,&b->committed,sizeof(*before)));
    c->na=1; c->nr=1; c->add[0]=initial; c->remove[0]=initial;
    c->add[0].terms[2].kind=(maelys_datalog_value_kind_t)99;
    CHECK(delta_execute(&op,b,b->committed.generation,3,NULL,0,0,&r)==MAELYS_DATALOG_STATUS_INVALID_FIELD);
    CHECK(!memcmp(before,&b->committed,sizeof(*before)));
    /* Reuse after poisoned provisional storage must start from retained EDB. */
    memset(b->session->inputs->fact_pool,0xa5,sizeof(b->session->inputs->fact_pool));
    memset(b->added,0xa5,sizeof(b->added)); memset(b->removed,0xa5,sizeof(b->removed));
    c->nr=0;
    c->na=0; OK(delta_execute(&op,b,b->committed.generation,3,NULL,0,0,&r)); OK(maelys_datalog_result_free(r));
    b->committed.generation=UINT64_MAX;
    CHECK(delta_execute(&op,b,UINT64_MAX,4,NULL,0,0,&r)==MAELYS_DATALOG_STATUS_INVALID_STATE);
    delta_alloc_disabled=0;
    OK(maelys_datalog_session_free(b->session)); OK(maelys_datalog_session_free(other->session));
    free(b); free(other); free(c); free(before);
}
/* Exhaustive typed-set oracle: 32 bases x 32 removal sets x 32 addition
 * sets, each at a roomy and a tight capacity. This also checks guard bytes and
 * empty/exact/overflow boundaries without running a solver or allocating. */
static void linear_set_checks(int mode) {
    native_fact universe[5] = {{0}};
    for (size_t i=0; i<5; ++i) {
        universe[i].arity = 1;
        universe[i].terms[0].kind = i<2 ? MAELYS_DATALOG_TERM_SYMBOL :
                                  i<4 ? MAELYS_DATALOG_TERM_INT : MAELYS_DATALOG_TERM_BOOL;
        if (i<2) universe[i].terms[0].as.symbol = (maelys_datalog_symbol_id_t)i;
        else if (i<4) universe[i].terms[0].as.integer = i==2 ? -1 : 1;
        else universe[i].terms[0].as.boolean = 1;
    }
    maelys_datalog_fact_set_t ordered;
    maelys_datalog_fact_set_init(&ordered,universe,5); ordered.count=5; ordered.sorted=0;
    OK(maelys_datalog_fact_set_sort(&ordered));
    delta_alloc_disabled=1;
    for(unsigned bm=0;bm<32;++bm) for(unsigned rm=0;rm<32;++rm) for(unsigned am=0;am<32;++am) {
        for(size_t tight=0;tight<2;++tight) {
            native_fact facts[8], additions[5], removals[5], before[8];
            memset(facts,0xa5,sizeof(facts));
            size_t n=0,na=0,nr=0,expected=0;
            unsigned final=(bm & ~rm) | am;
            for(size_t i=0;i<5;++i) {
                if(bm & (1u<<i)) facts[n++]=universe[i];
                if(rm & (1u<<i)) removals[nr++]=universe[i];
                if(am & (1u<<i)) additions[na++]=universe[i];
                if(final & (1u<<i)) ++expected;
            }
            memcpy(before,facts,sizeof(before));
            size_t original=n,capacity=tight ? n : 5;
            maelys_result_t rc=mode==3 ?
                delta_compose_tombstones(facts,&n,capacity,additions,na,removals,nr) :
                delta_compose_linear(facts,&n,capacity,additions,na,removals,nr);
            CHECK(!memcmp(facts+capacity,before+capacity,(8-capacity)*sizeof(*facts)));
            if(expected>capacity) { CHECK(rc==MAELYS_ERR_PAYLOAD_TOO_LARGE && n==original); continue; }
            CHECK(rc==MAELYS_OK && n==expected);
            size_t k=0;
            for(size_t i=0;i<5;++i) if(final & (1u<<i)) CHECK(!maelys_datalog_fact_cmp(&facts[k++],&universe[i]));
        }
    }
    delta_alloc_disabled=0;
}

int main(int argc,char **argv) {
    CHECK((argc==2 || argc==3) && (!strcmp(argv[1],"A") || !strcmp(argv[1],"B") || !strcmp(argv[1],"L") || !strcmp(argv[1],"T") || !strcmp(argv[1],"check")));
    int mode=!strcmp(argv[1],"A") ? 0 : !strcmp(argv[1],"B") ? 1 : !strcmp(argv[1],"L") ? 2 : 3;
    if(!strcmp(argv[1],"check")) { linear_set_checks(2); linear_set_checks(3); }
    int lifecycle=argc==3 && !strcmp(argv[2],"caller");
    CHECK(argc==2 || lifecycle || !strcmp(argv[2],"engine"));
    static const maelys_datalog_predicate_t predicates[]={MAELYS_DATALOG_EDB_QUERY("event",3),MAELYS_DATALOG_EDB("unused",3),MAELYS_DATALOG_IDB_QUERY("seen",3),{ "vocab",1,MAELYS_DATALOG_PREDICATE_POLICY_FACT }};
    const maelys_datalog_domain_t domain={"host_delta",predicates,4,atoms,5}; OK(maelys_datalog_domain_register(&domain));
    const char *scenarios[]={"empty","one","small","replace","window"};
    size_t sizes[]={8,64,256};
    puts("case,transactions,digest,retained_bytes,bank_bytes,final_unique_facts,setup_calls,setup_bytes");
    for(int derive=0;derive<2;++derive) {
        const char *source=derive ?
            "vocab(\"alpha\"). vocab(\"beta\"). vocab(\"gamma\"). vocab(\"delta\"). vocab(\"epsilon\"). seen(K,V,B) :- event(K,V,B)." :
            "vocab(\"alpha\"). vocab(\"beta\"). vocab(\"gamma\"). vocab(\"delta\"). vocab(\"epsilon\"). seen(K,V,B) :- unused(K,V,B).";
        maelys_datalog_policy_t *p; maelys_datalog_diagnostic_t diag=MAELYS_DATALOG_DIAGNOSTIC_INIT;
        int load=maelys_datalog_policy_load_inline(domain.name,"p",source,strlen(source),&p,&diag);
        if(load) fprintf(stderr,"load %d %s %s\n",load,diag.message,diag.hint); CHECK(!load);
        if(!strcmp(argv[1],"check")) { conformance(p,1); conformance(p,2); conformance(p,3); }
        for(size_t n=0;n<3;++n) {
            if(sizes[n]>MAELYS_DATALOG_MAX_FACTS_PER_PRED) continue; /* Explicit inventory exclusion in the report. */
            for(int symbol=0;symbol<2;++symbol) for(size_t s=0;s<5;++s)
                run_case(p,mode,derive,sizes[n],symbol,scenarios[s],lifecycle);
        }
        if(!lifecycle) for(int failure=1;failure<=3;++failure)
            run_failure(p,mode,derive,failure);
        OK(maelys_datalog_policy_free(p));
    }
    return 0;
}
