/* SPDX-License-Identifier: MPL-2.0 */
/* Also compiled as a separate C11/C++17 consumer of the installed SDK. */
#include <maelys/datalog.h>
#include <maelys/datalog_resources.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

int main(void) {
    size_t value = 73;
    CHECK(maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_POLICY_ATOMS, &value) == 0);
    CHECK(value == MAELYS_DATALOG_PUBLIC_MAX_POLICY_ATOMS);
    CHECK(maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_POLICY_ATOM_BYTES, &value) == 0);
    CHECK(value == MAELYS_DATALOG_PUBLIC_MAX_POLICY_ATOM_BYTES);
    const maelys_datalog_predicate_t predicates[] = {
        {"seed", 1, MAELYS_DATALOG_PREDICATE_EDB},
        {"fixed", 1, MAELYS_DATALOG_PREDICATE_POLICY_FACT},
        {"out", 1, MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY},
        {"unused", 1, MAELYS_DATALOG_PREDICATE_EDB}
    };
    const maelys_datalog_domain_t domain = {"consumer_counts", predicates, 4, NULL, 0};
    CHECK(maelys_datalog_domain_register(&domain) == 0);
    const char source[] = "fixed(3). out(X) :- seed(X) or fixed(X).";
    maelys_datalog_policy_t *p = NULL;
    CHECK(maelys_datalog_policy_load_inline(domain.name, "counts", source, strlen(source), &p, NULL) == 0);
    const maelys_datalog_policy_stat_t keys[] = {
        MAELYS_DATALOG_POLICY_PREDICATE_COUNT, MAELYS_DATALOG_POLICY_FACT_COUNT,
        MAELYS_DATALOG_POLICY_RULE_COUNT
    };
    const size_t expected[] = {4, 1, 2};
    char before[MAELYS_DATALOG_PUBLIC_FINGERPRINT_BYTES], after[MAELYS_DATALOG_PUBLIC_FINGERPRINT_BYTES];
    CHECK(maelys_datalog_policy_fingerprint(p, before) == 0);
    const char *policy_id = "unchanged";
    CHECK(maelys_datalog_policy_id(p, 0, &policy_id) == MAELYS_DATALOG_STATUS_OK);
    CHECK(strcmp(policy_id, "counts") == 0);
    policy_id = "unchanged";
    CHECK(maelys_datalog_policy_id(p, 1, &policy_id) == MAELYS_DATALOG_STATUS_NOT_FOUND);
    CHECK(strcmp(policy_id, "unchanged") == 0);
    CHECK(maelys_datalog_policy_id(NULL, 0, &policy_id) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    CHECK(strcmp(policy_id, "unchanged") == 0);
    CHECK(maelys_datalog_policy_id(p, 0, NULL) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    for (size_t i = 0; i < 3; ++i) {
        CHECK(maelys_datalog_policy_stat_get(p, 0, keys[i], &value) == 0);
        CHECK(value == expected[i]);
    }
    value = 73;
    CHECK(maelys_datalog_policy_stat_get(p, 1, keys[0], &value) == MAELYS_DATALOG_STATUS_NOT_FOUND);
    CHECK(value == 73);
    CHECK(maelys_datalog_policy_stat_get(p, 0, (maelys_datalog_policy_stat_t)999, &value) == MAELYS_DATALOG_STATUS_UNSUPPORTED);
    CHECK(value == 73);
    CHECK(maelys_datalog_policy_stat_get(NULL, 0, keys[0], &value) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    CHECK(value == 73);
    CHECK(maelys_datalog_policy_stat_get(p, 0, keys[0], NULL) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    maelys_datalog_session_config_t *config = NULL;
    maelys_datalog_session_t *session = NULL;
    CHECK(maelys_datalog_session_config_create(&config) == 0);
    maelys_datalog_session_resource_request_t request = MAELYS_DATALOG_RESOURCE_REQUEST_INIT;
    request.capacity_mask = MAELYS_DATALOG_CAPACITY_INPUT_FACTS;
    request.input_facts = 0;
    CHECK(maelys_datalog_session_config_set_resources(config, &request) == 0);
    CHECK(maelys_datalog_session_create_configured(p, 0, config, &session) == 0);
    maelys_datalog_session_resources_t resources = MAELYS_DATALOG_RESOURCES_INIT;
    CHECK(maelys_datalog_session_get_resources(session, &resources) == 0 && resources.input_facts == 0);
    for (size_t i = 0; i < 3; ++i) {
        CHECK(maelys_datalog_policy_stat_get(p, 0, keys[i], &value) == 0);
        CHECK(value == expected[i]);
    }
    CHECK(maelys_datalog_policy_fingerprint(p, after) == 0 && strcmp(before, after) == 0);
    CHECK(maelys_datalog_policy_free(p) == 0); /* storage retained by session */
    value = 73;
    CHECK(maelys_datalog_policy_stat_get(p, 0, keys[0], &value) == MAELYS_DATALOG_STATUS_INVALID_STATE);
    CHECK(value == 73);
    policy_id = "unchanged";
    CHECK(maelys_datalog_policy_id(p, 0, &policy_id) == MAELYS_DATALOG_STATUS_INVALID_STATE);
    CHECK(strcmp(policy_id, "unchanged") == 0);
    CHECK(maelys_datalog_session_free(session) == 0);
    CHECK(maelys_datalog_session_config_free(config) == 0);
    puts("consumer introspection: bounds, normalized counts, quotas, failures and lifetimes PASS");
    return 0;
}
