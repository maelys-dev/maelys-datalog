#include "examples/domains/maelys_datalog_example_domains.h"

#include "src/core/maelys_datalog_domain_registry.h"
#include "src/core/maelys_datalog_predicate_registry.h"

static maelys_result_t install_graph(maelys_datalog_predicate_registry_t *reg) {
    maelys_result_t rc;
    rc = maelys_datalog_predicate_registry_add_domain(reg, "edge", 2,
                                                       MAELYS_DATALOG_PRED_KIND_EDB);
    if (rc != MAELYS_OK) return rc;
    rc = maelys_datalog_predicate_registry_add_domain(reg, "source", 1,
                                                       MAELYS_DATALOG_PRED_KIND_POLICY_FACT);
    if (rc != MAELYS_OK) return rc;
    rc = maelys_datalog_predicate_registry_add_domain(reg, "path", 2,
                                                       MAELYS_DATALOG_PRED_KIND_IDB);
    if (rc != MAELYS_OK) return rc;
    return maelys_datalog_predicate_registry_add_domain(reg, "reachable", 1,
                                                        MAELYS_DATALOG_PRED_KIND_IDB |
                                                            MAELYS_DATALOG_PRED_KIND_QUERY);
}

static maelys_result_t install_decision(maelys_datalog_predicate_registry_t *reg) {
    maelys_result_t rc;
    rc = maelys_datalog_predicate_registry_add_domain(reg, "safe", 1,
                                                       MAELYS_DATALOG_PRED_KIND_POLICY_FACT);
    if (rc != MAELYS_OK) return rc;
    rc = maelys_datalog_predicate_registry_add_domain(reg, "blocked", 1,
                                                       MAELYS_DATALOG_PRED_KIND_EDB);
    if (rc != MAELYS_OK) return rc;
    rc = maelys_datalog_predicate_registry_add_domain(reg, "allow", 1,
                                                       MAELYS_DATALOG_PRED_KIND_IDB |
                                                           MAELYS_DATALOG_PRED_KIND_QUERY);
    if (rc != MAELYS_OK) return rc;
    rc = maelys_datalog_predicate_registry_add_domain(reg, "deny", 1,
                                                       MAELYS_DATALOG_PRED_KIND_IDB |
                                                           MAELYS_DATALOG_PRED_KIND_QUERY);
    if (rc != MAELYS_OK) return rc;
    return maelys_datalog_predicate_registry_add_domain(reg, "reduce", 1,
                                                        MAELYS_DATALOG_PRED_KIND_IDB |
                                                            MAELYS_DATALOG_PRED_KIND_QUERY);
}

static const maelys_datalog_domain_def_t k_graph_domain = {
    .domain_name = "graph",
    .predicates = NULL,
    .predicate_count = 0,
    .description = "Graph traversal: edge/2, source/1, path/2, reachable/1",
    .install_predicates = install_graph,
};

static const maelys_datalog_domain_def_t k_decision_domain = {
    .domain_name = "decision",
    .predicates = NULL,
    .predicate_count = 0,
    .description = "Decision: safe/1, blocked/1, allow/1, deny/1, reduce/1",
    .install_predicates = install_decision,
};

maelys_result_t maelys_datalog_example_domains_install(void) {
    maelys_result_t rc;
    rc = maelys_datalog_domain_registry_register(&k_graph_domain);
    if (rc != MAELYS_OK) return rc;
    return maelys_datalog_domain_registry_register(&k_decision_domain);
}
