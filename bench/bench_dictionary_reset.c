/* SPDX-License-Identifier: MPL-2.0 */
/* Internal diagnostic for transaction reset work, not a public API benchmark.
 * Compile with MAELYS_BENCH_COUNT and collect-atstart=no. Count one complete
 * materialization after 50 warmups; setup, checks and output are excluded. */
#include "src/core/maelys_datalog_prepared_session_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef MAELYS_BENCH_COUNT
#include <valgrind/callgrind.h>
#endif
int main(int argc, char **argv) {
    if (argc != 3 || (strcmp(argv[2], "normal") && strcmp(argv[2], "reject"))) return 2;
    size_t entries;
    int saturated = !strcmp(argv[1], "saturated");
    if (!strcmp(argv[1], "empty")) entries = 0;
    else if (!strcmp(argv[1], "sparse")) entries = 32;
    else if (!strcmp(argv[1], "count-limit") || saturated) entries = MAELYS_DATALOG_MAX_SYMBOLS;
    else return 2;
    const int rejected = !strcmp(argv[2], "reject");
    static maelys_datalog_internal_ruleset_t policy;
    const char fingerprint[] = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    assert(maelys_datalog_ruleset_init(&policy, "reset", "reset", fingerprint, 1) == MAELYS_OK);
    assert(maelys_datalog_predicate_registry_add_domain(&policy.registry, "seed", 1,
        MAELYS_DATALOG_PRED_KIND_EDB) == MAELYS_OK);
    assert(maelys_datalog_predicate_registry_freeze(&policy.registry) == MAELYS_OK);
    for (size_t i = 0; i < entries; ++i) {
        char key[MAELYS_DATALOG_MAX_STRING_BYTES + 1u];
        int n = snprintf(key, sizeof(key), "key-%06zu", i); assert(n > 0);
        size_t length = (size_t)n;
        if (saturated) {
            size_t bytes = (sizeof(policy.symbols.storage) - policy.symbols.used) / (entries - i);
            assert(bytes > length && bytes <= sizeof(key));
            memset(key + length, 'x', bytes - 1u - length);
            length = bytes - 1u; key[length] = '\0';
        }
        maelys_datalog_symbol_id_t id;
        assert(maelys_datalog_symbol_intern(&policy.symbols, key, length, &id) == MAELYS_OK);
        assert(id == i + 1u);
    }
    if (saturated) assert(policy.symbols.used == sizeof(policy.symbols.storage));
    maelys_datalog_internal_prepared_session_t *session = NULL;
    assert(maelys_datalog_prepared_session_borrow(&policy, &session) == MAELYS_OK);
    maelys_datalog_fact_t fact = {.predicate = rejected ? "unknown" : "seed", .arity = 1};
    fact.terms[0].kind = MAELYS_DATALOG_VALUE_INTEGER; fact.terms[0].as.integer = 7;
    for (unsigned i = 0; i <= 50; ++i) {
#ifdef MAELYS_BENCH_COUNT
        if (i == 50) { CALLGRIND_TOGGLE_COLLECT; }
#endif
        maelys_result_t rc = maelys_datalog_prepared_session_materialize_inputs(session, &fact, 1);
#ifdef MAELYS_BENCH_COUNT
        if (i == 50) { CALLGRIND_TOGGLE_COLLECT; }
#endif
        assert(rc == (rejected ? MAELYS_ERR_INVALID_FIELD : MAELYS_OK));
        assert(session->edb.fact_count == (rejected ? 0u : 1u));
        assert(session->symbols.count == policy.symbols.count && session->symbols.used == policy.symbols.used);
        if (rejected) assert(!memcmp(&session->symbols, &policy.symbols, sizeof(policy.symbols)));
        for (maelys_datalog_symbol_id_t id = 1; id <= entries; ++id) {
            const char *text = maelys_datalog_symbol_text(&session->symbols, id);
            const char *expected = maelys_datalog_symbol_text(&policy.symbols, id);
            assert(text && !strcmp(text, expected));
        }
    }
    printf("%s,%s,entries=%zu,text=%zu,index=%zu,checked\n", argv[1], argv[2],
        policy.symbols.count, policy.symbols.used, sizeof(policy.symbols.index));
    assert(maelys_datalog_prepared_session_destroy(session) == MAELYS_OK);
    return 0;
}
