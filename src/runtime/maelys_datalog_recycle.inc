/* SPDX-License-Identifier: MPL-2.0 */
/* One idle raw allocation per loaded native library, never a prepared session.
 * The default legacy constructor's three regions now share this allocation.
 * All offsets are max_align_t aligned, independent of allocator contents. */
typedef struct {
    size_t bytes, prepared, workspace, snapshot;
} session_layout_t;
static size_t session_align(size_t n) {
    const size_t a = _Alignof(max_align_t);
    return n + (a - n % a) % a;
}
static session_layout_t session_layout(int exports, int copies) {
    session_layout_t p = {0};
    p.prepared = session_align(sizeof(maelys_datalog_session_t) +
        (exports ? sizeof(struct backend_payload) : 0));
    p.workspace = session_align(p.prepared + maelys_datalog_prepared_session_storage_bytes(
        MAELYS_DATALOG_MAX_EDB_FACTS, MAELYS_DATALOG_MAX_EDB_FACTS));
    p.snapshot = session_align(p.workspace +
        maelys_datalog_solve_workspace_bytes(MAELYS_DATALOG_MAX_IDB_FACTS, 1));
    p.bytes = p.snapshot + (copies ? sizeof(maelys_datalog_internal_ruleset_t) : 0);
    return p;
}
size_t maelys_datalog_session_recycle_bound(void) {
    /* Compile-time profile bounds, not an unbounded provider's requirement.
     * Larger explicit arenas are allocated normally and never retained. */
    return session_layout(1, 1).bytes;
}
#if (defined(__GNUC__) || defined(__clang__)) && !defined(__EMSCRIPTEN__)
#define SESSION_RECYCLE_ENABLED 1
static atomic_flag recycle_lock = ATOMIC_FLAG_INIT;
static void *recycle_storage;
static size_t recycle_bytes;
static int recycle_closed;
static void lock_recycle(void) {
    while (atomic_flag_test_and_set_explicit(&recycle_lock, memory_order_acquire)) {}
}
static void unlock_recycle(void) {
    atomic_flag_clear_explicit(&recycle_lock, memory_order_release);
}
static void recycle_drain(int shutdown) {
    lock_recycle();
    void *old = recycle_storage;
    recycle_storage = NULL; recycle_bytes = 0;
    if (shutdown) recycle_closed = 1;
    unlock_recycle();
    /* No allocation, free or provider callback while holding the lock. */
    if (old) free(old);
}
__attribute__((destructor)) static void session_recycle_shutdown(void) {
    /* Native static-library process exit or shared-library unload. No later
     * destructor can leave a new retained block after this function runs. */
    recycle_drain(1);
}
#else
#define SESSION_RECYCLE_ENABLED 0
#endif
void maelys_datalog_session_recycle_purge(void) {
#if SESSION_RECYCLE_ENABLED
    recycle_drain(0);
#endif
}
static void *session_storage_acquire(size_t bytes) {
#if SESSION_RECYCLE_ENABLED
    lock_recycle();
    void *p = NULL;
    if (!recycle_closed && recycle_storage && recycle_bytes == bytes) {
        p = recycle_storage; recycle_storage = NULL; recycle_bytes = 0;
    }
    unlock_recycle();
    if (p) return p;
#endif
    return malloc(bytes);
}
static void session_storage_release(void *p, size_t bytes) {
#if SESSION_RECYCLE_ENABLED
    if (bytes <= maelys_datalog_session_recycle_bound()) {
        lock_recycle();
        if (!recycle_closed) {
            void *old = recycle_storage;
            recycle_storage = p; recycle_bytes = bytes;
            unlock_recycle();
            if (old) free(old);
            return;
        }
        unlock_recycle();
    }
#else
    (void)bytes;
#endif
    free(p);
}
