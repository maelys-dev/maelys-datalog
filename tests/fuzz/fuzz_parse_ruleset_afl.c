/*
 * P3-C35-QUATER - AFL++ persistent-mode harness for the Maelys DL parser.
 *
 * Entry point: maelys_datalog_parse_ruleset_ex()
 *
 * Setup mirrors the deterministic corpus runner and the C35-TER libFuzzer
 * target:
 *   ruleset_init -> add predicates -> register atoms -> freeze -> parse -> clear
 *
 * AFL++ persistent mode uses __AFL_LOOP to reduce fork overhead.
 * FUZZ_ATOMS are registered before parsing so string constants such as
 * "alice", "push", and "main" can pass the parser atom allowlist check and
 * reach deeper validation paths.
 *
 * Invariant: any byte sequence must not crash, hang, trigger UB, or produce an
 * ASAN/UBSAN/LSAN finding. Parse errors are valid outcomes.
 */

#include "include/maelys_datalog.h"
#include "src/core/maelys_datalog_parser.h"
#include "src/core/maelys_datalog_predicate_registry.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __AFL_HAVE_MANUAL_CONTROL
#include <unistd.h>
#endif

__AFL_FUZZ_INIT();

static const char *const FUZZ_ATOMS[] = {
    "a",
    "b",
    "c",
    "alice",
    "bob",
    "mallory",
    "proj-1",
    "main",
    "release",
    "push",
    "commit",
    "ok",
    "msg",
    "blocked-user",
    "notanint",
    "true",
    "false",
    NULL
};

static const maelys_datalog_predicate_def_t FUZZ_PREDS[] = {
    {"user", 1, MAELYS_DATALOG_PRED_KIND_EDB},
    {"owns", 2, MAELYS_DATALOG_PRED_KIND_EDB},
    {"safe", 1, MAELYS_DATALOG_PRED_KIND_EDB},
    {"edge", 2, MAELYS_DATALOG_PRED_KIND_EDB},
    {"fact", 1, MAELYS_DATALOG_PRED_KIND_EDB},
    {"p", 1, MAELYS_DATALOG_PRED_KIND_EDB},
    {"q", 2, MAELYS_DATALOG_PRED_KIND_EDB},
    {"r", 1, MAELYS_DATALOG_PRED_KIND_EDB},
    {"blocked", 1, MAELYS_DATALOG_PRED_KIND_POLICY_FACT},
    {"trusted", 1, MAELYS_DATALOG_PRED_KIND_POLICY_FACT},
    {"allow", 1, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
    {"deny", 2, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
    {"path", 2, MAELYS_DATALOG_PRED_KIND_IDB},
    {"reachable", 1, MAELYS_DATALOG_PRED_KIND_IDB},
};

#define FUZZ_PRED_COUNT (sizeof(FUZZ_PREDS) / sizeof(FUZZ_PREDS[0]))

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

#ifdef __AFL_HAVE_MANUAL_CONTROL
    __AFL_INIT();
#endif

    unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;

    while (__AFL_LOOP(10000)) {
        size_t size = __AFL_FUZZ_TESTCASE_LEN;

        if (size == 0u || size > 4096u) continue;

        maelys_datalog_ruleset_t ruleset;
        maelys_datalog_diagnostic_t diag = {0};

        if (maelys_datalog_ruleset_init(&ruleset,
                                        "afl.main",
                                        "afl_domain",
                                        MAELYS_DATALOG_SHA256_UNSET,
                                        0) != MAELYS_OK) {
            continue;
        }

        int setup_ok = 1;
        for (size_t i = 0; i < FUZZ_PRED_COUNT && setup_ok; i++) {
            if (maelys_datalog_predicate_registry_add_domain(&ruleset.registry,
                                                             FUZZ_PREDS[i].name,
                                                             FUZZ_PREDS[i].arity,
                                                             FUZZ_PREDS[i].kind_flags) != MAELYS_OK) {
                setup_ok = 0;
            }
        }

        for (size_t i = 0; FUZZ_ATOMS[i] != NULL && setup_ok; i++) {
            if (maelys_datalog_predicate_registry_add_atom(&ruleset.registry,
                                                           FUZZ_ATOMS[i]) != MAELYS_OK) {
                setup_ok = 0;
            }
        }

        if (!setup_ok) {
            maelys_datalog_ruleset_clear(&ruleset);
            continue;
        }

        if (maelys_datalog_predicate_registry_freeze(&ruleset.registry) != MAELYS_OK) {
            maelys_datalog_ruleset_clear(&ruleset);
            continue;
        }

        (void)maelys_datalog_parse_ruleset_ex(&ruleset,
                                              (const char *)buf,
                                              size,
                                              "afl",
                                              &diag);

        maelys_datalog_ruleset_clear(&ruleset);
    }

    return 0;
}
