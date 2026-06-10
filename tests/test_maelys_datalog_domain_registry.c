#include "examples/domains/maelys_datalog_example_domains.h"
#include "src/core/maelys_datalog_domain_registry.h"
#include "src/core/maelys_datalog_predicate_registry.h"
#include "tests/helpers/test_framework.h"

#include <stdio.h>
#include <string.h>

static const maelys_datalog_predicate_def_t *find_def(const maelys_datalog_predicate_registry_t *registry,
                                                      const char *name,
                                                      size_t arity) {
    maelys_datalog_predicate_id_t pid = 0;
    if (!maelys_datalog_predicate_registry_find(registry, name, arity, &pid)) return NULL;
    return maelys_datalog_predicate_registry_get(registry, pid);
}

static maelys_result_t install_domain(const char *domain_name, maelys_datalog_predicate_registry_t *registry) {
    maelys_datalog_predicate_registry_init_core(registry);
    maelys_result_t rc = maelys_datalog_example_domains_install();
    if (rc != MAELYS_OK) return rc;
    return maelys_datalog_domain_registry_install(domain_name, registry);
}

static int test_example_domains_install_returns_ok(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_example_domains_install(), "%d");
    TEST_END();
}

static int test_graph_domain_registered(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_example_domains_install(), "%d");
    const maelys_datalog_domain_def_t *def = maelys_datalog_domain_registry_find("graph");
    TEST_ASSERT_NOT_NULL(def);
    TEST_ASSERT_EQUAL_STRING("graph", def->domain_name);
    TEST_ASSERT_NOT_NULL(def->install_predicates);
    TEST_END();
}

static int test_decision_domain_registered(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_example_domains_install(), "%d");
    const maelys_datalog_domain_def_t *def = maelys_datalog_domain_registry_find("decision");
    TEST_ASSERT_NOT_NULL(def);
    TEST_ASSERT_EQUAL_STRING("decision", def->domain_name);
    TEST_ASSERT_NOT_NULL(def->install_predicates);
    TEST_END();
}

static int test_example_domains_install_idempotent(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_example_domains_install(), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_example_domains_install(), "%d");
    TEST_ASSERT_NOT_NULL(maelys_datalog_domain_registry_find("graph"));
    TEST_ASSERT_NOT_NULL(maelys_datalog_domain_registry_find("decision"));
    TEST_END();
}

static int test_domain_registry_rejects_unknown_domain(void) {
    TEST_BEGIN();
    maelys_datalog_predicate_registry_t registry;
    maelys_datalog_predicate_registry_init_core(&registry);
    TEST_ASSERT_NULL(maelys_datalog_domain_registry_find("missing"));
    TEST_ASSERT_EQUAL(MAELYS_ERR_UNSUPPORTED, maelys_datalog_domain_registry_install("missing", &registry), "%d");
    TEST_END();
}

static int test_graph_edge_is_edb(void) {
    TEST_BEGIN();
    maelys_datalog_predicate_registry_t registry;
    TEST_ASSERT_EQUAL(MAELYS_OK, install_domain("graph", &registry), "%d");
    const maelys_datalog_predicate_def_t *def = find_def(&registry, "edge", 2);
    TEST_ASSERT_NOT_NULL(def);
    TEST_ASSERT_TRUE(def->kind_flags & MAELYS_DATALOG_PRED_KIND_EDB);
    TEST_ASSERT_FALSE(def->kind_flags & MAELYS_DATALOG_PRED_KIND_POLICY_FACT);
    TEST_END();
}

static int test_graph_source_is_policy_fact(void) {
    TEST_BEGIN();
    maelys_datalog_predicate_registry_t registry;
    TEST_ASSERT_EQUAL(MAELYS_OK, install_domain("graph", &registry), "%d");
    const maelys_datalog_predicate_def_t *def = find_def(&registry, "source", 1);
    TEST_ASSERT_NOT_NULL(def);
    TEST_ASSERT_TRUE(def->kind_flags & MAELYS_DATALOG_PRED_KIND_POLICY_FACT);
    TEST_ASSERT_FALSE(def->kind_flags & MAELYS_DATALOG_PRED_KIND_EDB);
    TEST_END();
}

static int test_graph_path_is_idb(void) {
    TEST_BEGIN();
    maelys_datalog_predicate_registry_t registry;
    TEST_ASSERT_EQUAL(MAELYS_OK, install_domain("graph", &registry), "%d");
    const maelys_datalog_predicate_def_t *def = find_def(&registry, "path", 2);
    TEST_ASSERT_NOT_NULL(def);
    TEST_ASSERT_TRUE(def->kind_flags & MAELYS_DATALOG_PRED_KIND_IDB);
    TEST_ASSERT_FALSE(def->kind_flags & MAELYS_DATALOG_PRED_KIND_QUERY);
    TEST_END();
}

static int test_graph_reachable_is_query_idb(void) {
    TEST_BEGIN();
    maelys_datalog_predicate_registry_t registry;
    TEST_ASSERT_EQUAL(MAELYS_OK, install_domain("graph", &registry), "%d");
    const maelys_datalog_predicate_def_t *def = find_def(&registry, "reachable", 1);
    TEST_ASSERT_NOT_NULL(def);
    TEST_ASSERT_TRUE(def->kind_flags & MAELYS_DATALOG_PRED_KIND_IDB);
    TEST_ASSERT_TRUE(def->kind_flags & MAELYS_DATALOG_PRED_KIND_QUERY);
    TEST_END();
}

static int test_decision_safe_is_policy_fact(void) {
    TEST_BEGIN();
    maelys_datalog_predicate_registry_t registry;
    TEST_ASSERT_EQUAL(MAELYS_OK, install_domain("decision", &registry), "%d");
    const maelys_datalog_predicate_def_t *def = find_def(&registry, "safe", 1);
    TEST_ASSERT_NOT_NULL(def);
    TEST_ASSERT_TRUE(def->kind_flags & MAELYS_DATALOG_PRED_KIND_POLICY_FACT);
    TEST_ASSERT_FALSE(def->kind_flags & MAELYS_DATALOG_PRED_KIND_EDB);
    TEST_END();
}

static int test_decision_blocked_is_edb(void) {
    TEST_BEGIN();
    maelys_datalog_predicate_registry_t registry;
    TEST_ASSERT_EQUAL(MAELYS_OK, install_domain("decision", &registry), "%d");
    const maelys_datalog_predicate_def_t *def = find_def(&registry, "blocked", 1);
    TEST_ASSERT_NOT_NULL(def);
    TEST_ASSERT_TRUE(def->kind_flags & MAELYS_DATALOG_PRED_KIND_EDB);
    TEST_ASSERT_FALSE(def->kind_flags & MAELYS_DATALOG_PRED_KIND_POLICY_FACT);
    TEST_END();
}

static int test_decision_allow_is_query_idb(void) {
    TEST_BEGIN();
    maelys_datalog_predicate_registry_t registry;
    TEST_ASSERT_EQUAL(MAELYS_OK, install_domain("decision", &registry), "%d");
    const maelys_datalog_predicate_def_t *def = find_def(&registry, "allow", 1);
    TEST_ASSERT_NOT_NULL(def);
    TEST_ASSERT_TRUE(def->kind_flags & MAELYS_DATALOG_PRED_KIND_IDB);
    TEST_ASSERT_TRUE(def->kind_flags & MAELYS_DATALOG_PRED_KIND_QUERY);
    TEST_END();
}

static int test_decision_deny_is_query_idb(void) {
    TEST_BEGIN();
    maelys_datalog_predicate_registry_t registry;
    TEST_ASSERT_EQUAL(MAELYS_OK, install_domain("decision", &registry), "%d");
    const maelys_datalog_predicate_def_t *def = find_def(&registry, "deny", 1);
    TEST_ASSERT_NOT_NULL(def);
    TEST_ASSERT_TRUE(def->kind_flags & MAELYS_DATALOG_PRED_KIND_IDB);
    TEST_ASSERT_TRUE(def->kind_flags & MAELYS_DATALOG_PRED_KIND_QUERY);
    TEST_END();
}

static int test_decision_reduce_is_query_idb(void) {
    TEST_BEGIN();
    maelys_datalog_predicate_registry_t registry;
    TEST_ASSERT_EQUAL(MAELYS_OK, install_domain("decision", &registry), "%d");
    const maelys_datalog_predicate_def_t *def = find_def(&registry, "reduce", 1);
    TEST_ASSERT_NOT_NULL(def);
    TEST_ASSERT_TRUE(def->kind_flags & MAELYS_DATALOG_PRED_KIND_IDB);
    TEST_ASSERT_TRUE(def->kind_flags & MAELYS_DATALOG_PRED_KIND_QUERY);
    TEST_END();
}

static int test_graph_and_decision_can_share_registry(void) {
    TEST_BEGIN();
    maelys_datalog_predicate_registry_t registry;
    maelys_datalog_predicate_registry_init_core(&registry);
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_example_domains_install(), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_domain_registry_install("graph", &registry), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_domain_registry_install("decision", &registry), "%d");
    TEST_ASSERT_NOT_NULL(find_def(&registry, "edge", 2));
    TEST_ASSERT_NOT_NULL(find_def(&registry, "allow", 1));
    TEST_END();
}

static int test_domain_registry_rejects_conflicting_predicate(void) {
    TEST_BEGIN();
    maelys_datalog_predicate_registry_t registry;
    maelys_datalog_predicate_registry_init_core(&registry);
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_predicate_registry_add_domain(&registry, "p", 1,
                                                                              MAELYS_DATALOG_PRED_KIND_EDB), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_predicate_registry_add_domain(&registry, "p", 1,
                                                                   MAELYS_DATALOG_PRED_KIND_IDB),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_predicate_registry_add_domain(&registry, "p", 2,
                                                                   MAELYS_DATALOG_PRED_KIND_EDB),
                      "%d");
    TEST_END();
}

static int test_registry_freezes_and_rejects_mutation(void) {
    TEST_BEGIN();
    maelys_datalog_predicate_registry_t registry;
    maelys_datalog_predicate_registry_init_core(&registry);
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_predicate_registry_add_domain(&registry, "p", 1,
                                                                              MAELYS_DATALOG_PRED_KIND_EDB), "%d");
    TEST_ASSERT_FALSE(maelys_datalog_predicate_registry_is_frozen(&registry));
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_predicate_registry_freeze(&registry), "%d");
    TEST_ASSERT_TRUE(maelys_datalog_predicate_registry_is_frozen(&registry));
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_STATE,
                      maelys_datalog_predicate_registry_add_domain(&registry, "q", 1,
                                                                   MAELYS_DATALOG_PRED_KIND_EDB),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_STATE,
                      maelys_datalog_predicate_registry_add_atom(&registry, "atom"),
                      "%d");
    TEST_END();
}

static int test_standalone_domain_registry_has_no_parent_paths(void) {
    TEST_BEGIN();
    FILE *f = fopen("src/core/maelys_datalog_domain_registry.c", "rb");
    TEST_ASSERT_NOT_NULL(f);
    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1u, f);
    fclose(f);
    buf[n] = '\0';
    TEST_ASSERT_TRUE(strstr(buf, "agents/") == NULL);
    TEST_ASSERT_TRUE(strstr(buf, "protocols/") == NULL);
    TEST_ASSERT_TRUE(strstr(buf, "transports/") == NULL);
    TEST_END();
}

int main(int argc, char **argv) {
    test_case_t cases[] = {
        {"maelys_datalog_domain_registry/example_domains_install_returns_ok", TEST_MODE_NON_BLOCKING, test_example_domains_install_returns_ok},
        {"maelys_datalog_domain_registry/graph_domain_registered", TEST_MODE_NON_BLOCKING, test_graph_domain_registered},
        {"maelys_datalog_domain_registry/decision_domain_registered", TEST_MODE_NON_BLOCKING, test_decision_domain_registered},
        {"maelys_datalog_domain_registry/example_domains_install_idempotent", TEST_MODE_NON_BLOCKING, test_example_domains_install_idempotent},
        {"maelys_datalog_domain_registry/rejects_unknown_domain", TEST_MODE_NON_BLOCKING, test_domain_registry_rejects_unknown_domain},
        {"maelys_datalog_domain_registry/graph_edge_is_edb", TEST_MODE_NON_BLOCKING, test_graph_edge_is_edb},
        {"maelys_datalog_domain_registry/graph_source_is_policy_fact", TEST_MODE_NON_BLOCKING, test_graph_source_is_policy_fact},
        {"maelys_datalog_domain_registry/graph_path_is_idb", TEST_MODE_NON_BLOCKING, test_graph_path_is_idb},
        {"maelys_datalog_domain_registry/graph_reachable_is_query_idb", TEST_MODE_NON_BLOCKING, test_graph_reachable_is_query_idb},
        {"maelys_datalog_domain_registry/decision_safe_is_policy_fact", TEST_MODE_NON_BLOCKING, test_decision_safe_is_policy_fact},
        {"maelys_datalog_domain_registry/decision_blocked_is_edb", TEST_MODE_NON_BLOCKING, test_decision_blocked_is_edb},
        {"maelys_datalog_domain_registry/decision_allow_is_query_idb", TEST_MODE_NON_BLOCKING, test_decision_allow_is_query_idb},
        {"maelys_datalog_domain_registry/decision_deny_is_query_idb", TEST_MODE_NON_BLOCKING, test_decision_deny_is_query_idb},
        {"maelys_datalog_domain_registry/decision_reduce_is_query_idb", TEST_MODE_NON_BLOCKING, test_decision_reduce_is_query_idb},
        {"maelys_datalog_domain_registry/graph_and_decision_can_share_registry", TEST_MODE_NON_BLOCKING, test_graph_and_decision_can_share_registry},
        {"maelys_datalog_domain_registry/rejects_conflicting_predicate", TEST_MODE_NON_BLOCKING, test_domain_registry_rejects_conflicting_predicate},
        {"maelys_datalog_domain_registry/freezes_and_rejects_mutation", TEST_MODE_NON_BLOCKING, test_registry_freezes_and_rejects_mutation},
        {"maelys_datalog_domain_registry/standalone_has_no_parent_paths", TEST_MODE_NON_BLOCKING, test_standalone_domain_registry_has_no_parent_paths},
    };
    return test_main("maelys_datalog_domain_registry", cases, (int)(sizeof(cases) / sizeof(cases[0])), argc, argv);
}
