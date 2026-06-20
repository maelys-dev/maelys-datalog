#pragma once
#ifndef MAELYS_DATALOG_RULESET_H
#define MAELYS_DATALOG_RULESET_H

#include "common/maelys_errors.h"
#include "src/core/maelys_datalog_predicate_registry.h"
#include "src/core/maelys_datalog_symbol_table.h"
#include "src/core/maelys_datalog_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char name[64];
    size_t arity;
} maelys_datalog_query_whitelist_entry_t;

typedef struct {
    int loaded;
    char policy_id[128];
    char domain[64];
    char sha256[65];
    maelys_datalog_query_whitelist_entry_t query_whitelist[MAELYS_DATALOG_MAX_QUERY_WHITELIST];
    size_t query_whitelist_count;
    int enforces_query_whitelist;
    int positive_recursion_supported;
    int negation_supported;
    int negation_recursion_supported;
    uint32_t strata[MAELYS_DATALOG_MAX_PREDICATES];
    uint32_t max_stratum;
    int strata_assigned;
    int has_positive_recursion;
    int test_only;
    maelys_datalog_symbol_table_t symbols;
    maelys_datalog_predicate_registry_t registry;
    maelys_datalog_fact_t facts[MAELYS_DATALOG_MAX_RULE_FACTS];
    size_t fact_count;
    maelys_datalog_rule_t rules[MAELYS_DATALOG_MAX_RULES];
    size_t rule_count;
} maelys_datalog_ruleset_t;

maelys_result_t maelys_datalog_ruleset_init(maelys_datalog_ruleset_t *ruleset,
                                            const char *policy_id,
                                            const char *domain,
                                            const char *sha256,
                                            int test_only);
void maelys_datalog_ruleset_clear(maelys_datalog_ruleset_t *ruleset);
int maelys_datalog_ruleset_has_allow_all(const maelys_datalog_ruleset_t *ruleset);
maelys_result_t maelys_datalog_ruleset_finalize_sha256(maelys_datalog_ruleset_t *ruleset);

#ifdef __cplusplus
}
#endif

#endif
