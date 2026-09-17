/* SPDX-License-Identifier: MPL-2.0 */
#include <maelys/datalog.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const maelys_datalog_public_predicate_t declarations[] = {
    MAELYS_DATALOG_EDB("seed", 1),
    MAELYS_DATALOG_IDB("hidden", 1),
    MAELYS_DATALOG_IDB_QUERY("allow", 1),
};
static unsigned name_calls, arity_calls;
static const char *name_once(void) { ++name_calls; return "dynamic"; }
static size_t arity_once(void) { ++arity_calls; return 2; }

int main(void) {
    assert(strcmp(declarations[0].name, "seed") == 0);
    assert(declarations[0].arity == 1);
    assert(declarations[0].flags == MAELYS_DATALOG_PREDICATE_EDB);
    assert(declarations[1].flags == MAELYS_DATALOG_PREDICATE_IDB);
    assert(declarations[2].flags ==
           (MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY));
    const maelys_datalog_public_predicate_t dynamic[] = {
        MAELYS_DATALOG_EDB(name_once(), arity_once()),
        MAELYS_DATALOG_IDB(name_once(), arity_once()),
        MAELYS_DATALOG_IDB_QUERY(name_once(), arity_once()),
    };
    assert(name_calls == 3 && arity_calls == 3);
    for (size_t i = 0; i < 3; ++i) {
        assert(strcmp(dynamic[i].name, "dynamic") == 0 && dynamic[i].arity == 2);
    }
    const maelys_datalog_public_domain_t domain = {
        "predicate_builders", declarations, 3, NULL, 0,
    };
    assert(maelys_datalog_domain_register(&domain) == MAELYS_DATALOG_STATUS_OK);
    /* Initializers do not silently repair or validate declarations. */
    const maelys_datalog_public_predicate_t invalid[] = {
        MAELYS_DATALOG_EDB("bad", MAELYS_DATALOG_PUBLIC_MAX_TERMS + 1),
    };
    const maelys_datalog_public_domain_t bad_domain = {
        "predicate_builders_invalid", invalid, 1, NULL, 0,
    };
    assert(maelys_datalog_domain_register(&bad_domain) == MAELYS_DATALOG_STATUS_INVALID_FIELD);
    puts("predicate builders: exact flags, static/runtime initializers, single evaluation, registration validation PASS");
    return 0;
}
