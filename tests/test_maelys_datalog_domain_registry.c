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

static maelys_result_t install_callback_test_predicates(maelys_datalog_predicate_registry_t *registry) {
    return maelys_datalog_predicate_registry_add_domain(registry,
                                                        "callback_pred",
                                                        1,
                                                        MAELYS_DATALOG_PRED_KIND_EDB);
}

static const maelys_datalog_predicate_def_t k_static_table_a[] = {
    {.name = "static_safe", .arity = 1, .kind_flags = MAELYS_DATALOG_PRED_KIND_EDB},
    {.name = "static_allow",
     .arity = 1,
     .kind_flags = MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
};

static const maelys_datalog_predicate_def_t k_static_table_b[] = {
    {.name = "static_blocked", .arity = 1, .kind_flags = MAELYS_DATALOG_PRED_KIND_EDB},
};

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

static int test_domain_registry_static_table_basic(void) {
    TEST_BEGIN();
    const maelys_datalog_domain_def_t def = {
        .domain_name = "static_basic_domain",
        .predicates = k_static_table_a,
        .predicate_count = sizeof(k_static_table_a) / sizeof(k_static_table_a[0]),
        .description = "static table basic",
        .install_predicates = NULL,
    };
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_domain_registry_register(&def), "%d");
    const maelys_datalog_domain_def_t *registered = maelys_datalog_domain_registry_find("static_basic_domain");
    TEST_ASSERT_NOT_NULL(registered);
    TEST_ASSERT_NULL(registered->install_predicates);
    TEST_ASSERT_NOT_NULL(registered->predicates);
    TEST_ASSERT_EQUAL((size_t)2u, registered->predicate_count, "%zu");
    TEST_END();
}

static int test_domain_registry_static_table_idempotent(void) {
    TEST_BEGIN();
    const maelys_datalog_domain_def_t def = {
        .domain_name = "static_idempotent_domain",
        .predicates = k_static_table_a,
        .predicate_count = sizeof(k_static_table_a) / sizeof(k_static_table_a[0]),
        .description = "static table idempotent",
        .install_predicates = NULL,
    };
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_domain_registry_register(&def), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_domain_registry_register(&def), "%d");
    TEST_END();
}

static int test_domain_registry_static_table_idempotent_conflicting_table(void) {
    TEST_BEGIN();
    const maelys_datalog_domain_def_t first = {
        .domain_name = "static_conflict_domain",
        .predicates = k_static_table_a,
        .predicate_count = sizeof(k_static_table_a) / sizeof(k_static_table_a[0]),
        .description = "static table conflict first",
        .install_predicates = NULL,
    };
    const maelys_datalog_domain_def_t second = {
        .domain_name = "static_conflict_domain",
        .predicates = k_static_table_b,
        .predicate_count = sizeof(k_static_table_b) / sizeof(k_static_table_b[0]),
        .description = "static table conflict second",
        .install_predicates = NULL,
    };
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_domain_registry_register(&first), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_domain_registry_register(&second), "%d");
    maelys_datalog_predicate_registry_t registry;
    maelys_datalog_predicate_registry_init_core(&registry);
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_domain_registry_install("static_conflict_domain", &registry), "%d");
    TEST_ASSERT_NOT_NULL(find_def(&registry, "static_safe", 1));
    TEST_ASSERT_NOT_NULL(find_def(&registry, "static_allow", 1));
    TEST_ASSERT_NULL(find_def(&registry, "static_blocked", 1));
    TEST_END();
}

static int test_domain_registry_static_table_install(void) {
    TEST_BEGIN();
    const maelys_datalog_domain_def_t def = {
        .domain_name = "static_install_domain",
        .predicates = k_static_table_a,
        .predicate_count = sizeof(k_static_table_a) / sizeof(k_static_table_a[0]),
        .description = "static table install",
        .install_predicates = NULL,
    };
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_domain_registry_register(&def), "%d");
    maelys_datalog_predicate_registry_t registry;
    maelys_datalog_predicate_registry_init_core(&registry);
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_domain_registry_install("static_install_domain", &registry), "%d");
    const maelys_datalog_predicate_def_t *safe = find_def(&registry, "static_safe", 1);
    const maelys_datalog_predicate_def_t *allow = find_def(&registry, "static_allow", 1);
    TEST_ASSERT_NOT_NULL(safe);
    TEST_ASSERT_TRUE(safe->kind_flags & MAELYS_DATALOG_PRED_KIND_EDB);
    TEST_ASSERT_NOT_NULL(allow);
    TEST_ASSERT_TRUE(allow->kind_flags & MAELYS_DATALOG_PRED_KIND_IDB);
    TEST_ASSERT_TRUE(allow->kind_flags & MAELYS_DATALOG_PRED_KIND_QUERY);
    TEST_END();
}

static int test_domain_registry_static_and_callback_both_non_null(void) {
    TEST_BEGIN();
    const maelys_datalog_domain_def_t def = {
        .domain_name = "static_invalid_both_domain",
        .predicates = k_static_table_a,
        .predicate_count = sizeof(k_static_table_a) / sizeof(k_static_table_a[0]),
        .description = "invalid both",
        .install_predicates = install_callback_test_predicates,
    };
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT, maelys_datalog_domain_registry_register(&def), "%d");
    TEST_END();
}

static int test_domain_registry_static_table_zero_count(void) {
    TEST_BEGIN();
    const maelys_datalog_domain_def_t def = {
        .domain_name = "static_invalid_zero_domain",
        .predicates = k_static_table_a,
        .predicate_count = 0,
        .description = "invalid zero count",
        .install_predicates = NULL,
    };
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT, maelys_datalog_domain_registry_register(&def), "%d");
    TEST_END();
}

static int test_domain_registry_callback_null_table_null(void) {
    TEST_BEGIN();
    const maelys_datalog_domain_def_t def = {
        .domain_name = "static_invalid_empty_domain",
        .predicates = NULL,
        .predicate_count = 0,
        .description = "invalid empty",
        .install_predicates = NULL,
    };
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT, maelys_datalog_domain_registry_register(&def), "%d");
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
        {"maelys_datalog_domain_registry/static_table_basic", TEST_MODE_NON_BLOCKING, test_domain_registry_static_table_basic},
        {"maelys_datalog_domain_registry/static_table_idempotent", TEST_MODE_NON_BLOCKING, test_domain_registry_static_table_idempotent},
        {"maelys_datalog_domain_registry/static_table_idempotent_conflicting_table", TEST_MODE_NON_BLOCKING, test_domain_registry_static_table_idempotent_conflicting_table},
        {"maelys_datalog_domain_registry/static_table_install", TEST_MODE_NON_BLOCKING, test_domain_registry_static_table_install},
        {"maelys_datalog_domain_registry/static_and_callback_both_non_null", TEST_MODE_NON_BLOCKING, test_domain_registry_static_and_callback_both_non_null},
        {"maelys_datalog_domain_registry/static_table_zero_count", TEST_MODE_NON_BLOCKING, test_domain_registry_static_table_zero_count},
        {"maelys_datalog_domain_registry/callback_null_table_null", TEST_MODE_NON_BLOCKING, test_domain_registry_callback_null_table_null},
        {"maelys_datalog_domain_registry/rejects_conflicting_predicate", TEST_MODE_NON_BLOCKING, test_domain_registry_rejects_conflicting_predicate},
        {"maelys_datalog_domain_registry/freezes_and_rejects_mutation", TEST_MODE_NON_BLOCKING, test_registry_freezes_and_rejects_mutation},
        {"maelys_datalog_domain_registry/standalone_has_no_parent_paths", TEST_MODE_NON_BLOCKING, test_standalone_domain_registry_has_no_parent_paths},
    };
    return test_main("maelys_datalog_domain_registry", cases, (int)(sizeof(cases) / sizeof(cases[0])), argc, argv);
}
