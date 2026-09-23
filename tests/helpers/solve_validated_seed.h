/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_DATALOG_SOLVE_VALIDATED_SEED_H
#define MAELYS_DATALOG_SOLVE_VALIDATED_SEED_H

#include "src/core/maelys_datalog_edb.h"
#include "src/core/maelys_datalog_solver.h"
#include "src/core/maelys_datalog_solver_testing.h"
#include "src/core/maelys_datalog_symbol_table.h"
#include <stdlib.h>
#include <string.h>

/* Exercise accepted corpus/fuzz programs with a small typed EDB, not only the
 * parser. Runtime type/capacity/depth errors are legal outcomes. The fixture is
 * deliberately bounded and does not claim to enumerate every binding. */
static void solve_validated_seed(maelys_datalog_internal_ruleset_t *ruleset) {
    if (!ruleset->program_validated) abort();
    maelys_datalog_internal_fact_t facts[MAELYS_DATALOG_MAX_EDB_FACTS];
    maelys_datalog_internal_edb_t edb;
    if (maelys_datalog_edb_init(&edb, facts, MAELYS_DATALOG_MAX_EDB_FACTS,
                               &ruleset->symbols, &ruleset->registry) != MAELYS_OK) abort();
    maelys_datalog_symbol_id_t alice = 0;
    if (maelys_datalog_symbol_intern(&ruleset->symbols, "alice", 5, &alice) != MAELYS_OK) abort();
    const maelys_datalog_internal_term_t values[] = {
        {.kind = MAELYS_DATALOG_TERM_INT, .as.integer = 0},
        {.kind = MAELYS_DATALOG_TERM_INT, .as.integer = 1},
        {.kind = MAELYS_DATALOG_TERM_BOOL, .as.boolean = 1},
        {.kind = MAELYS_DATALOG_TERM_SYMBOL, .as.symbol = alice},
    };
    for (maelys_datalog_predicate_id_t pid = 0; pid < ruleset->registry.count; ++pid) {
        const maelys_datalog_predicate_entry_t *def =
            maelys_datalog_predicate_registry_get(&ruleset->registry, pid);
        if (!(def->kind_flags & MAELYS_DATALOG_PRED_KIND_EDB)) continue;
        for (size_t v = 0; v < sizeof(values) / sizeof(values[0]); ++v) {
            maelys_datalog_internal_term_t terms[MAELYS_DATALOG_MAX_TERMS] = {0};
            for (size_t t = 0; t < def->arity; ++t) terms[t] = values[v];
            if (maelys_datalog_edb_add_fact(&edb, def->name, terms, def->arity) != MAELYS_OK) abort();
        }
    }
    if (maelys_datalog_edb_finalize(&edb) != MAELYS_OK) abort();
    maelys_datalog_internal_solve_result_t *result = NULL;
#ifdef MAELYS_TESTING
    const maelys_datalog_base_lookup_counts_t before = maelys_datalog_base_lookup_counts;
#endif
    maelys_result_t rc = maelys_datalog_solve_once(ruleset, &edb, &result);
#ifdef MAELYS_TESTING
    if (maelys_datalog_base_lookup_counts.validated_hits != before.validated_hits ||
        maelys_datalog_base_lookup_counts.lookups != before.lookups) abort();
#endif
    if (rc == MAELYS_OK) {
        if (!result) abort();
        maelys_datalog_solve_result_free(result);
    } else if (result) {
        abort(); /* A failed solve must not publish a partial result. */
    }
}
#endif
