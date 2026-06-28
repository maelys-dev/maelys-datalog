#include "src/core/maelys_datalog_audit.h"
#include "src/core/maelys_datalog_decision.h"
#include "src/core/maelys_datalog_diagnostic.h"
#include "src/core/maelys_datalog_domain_registry.h"
#include "src/core/maelys_datalog_edb.h"
#include "src/core/maelys_datalog_lexer.h"
#include "src/core/maelys_datalog_parser.h"
#include "src/core/maelys_datalog_predicate_registry.h"
#include "src/core/maelys_datalog_ruleset.h"
#include "src/core/maelys_datalog_solver.h"
#include "src/core/maelys_datalog_symbol_table.h"
#include "src/core/maelys_datalog_types.h"
#include "src/manifest/maelys_datalog_manifest.h"
#include "tests/helpers/test_framework.h"

#include <stddef.h>

static int test_datalog_headers_owner_types_available(void) {
    TEST_BEGIN();
#if defined(MAELYS_DATALOG_PROFILE_LARGE)
    TEST_ASSERT_EQUAL((size_t)2048u, (size_t)MAELYS_DATALOG_MAX_EDB_FACTS, "%zu");
    TEST_ASSERT_EQUAL((size_t)2048u, (size_t)MAELYS_DATALOG_MAX_IDB_FACTS, "%zu");
    TEST_ASSERT_EQUAL((size_t)256u, (size_t)MAELYS_DATALOG_MAX_FACTS_PER_PRED, "%zu");
    TEST_ASSERT_EQUAL_STRING("LARGE", MAELYS_DATALOG_SIZE_PROFILE_NAME);
#else
    TEST_ASSERT_EQUAL((size_t)1024u, (size_t)MAELYS_DATALOG_MAX_EDB_FACTS, "%zu");
    TEST_ASSERT_EQUAL((size_t)1024u, (size_t)MAELYS_DATALOG_MAX_IDB_FACTS, "%zu");
    TEST_ASSERT_EQUAL((size_t)64u, (size_t)MAELYS_DATALOG_MAX_FACTS_PER_PRED, "%zu");
    TEST_ASSERT_EQUAL_STRING("SMALL", MAELYS_DATALOG_SIZE_PROFILE_NAME);
#endif
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_SYMBOL_ID_INVALID,
                      (maelys_datalog_symbol_id_t)0u,
                      "%u");
    TEST_ASSERT_TRUE(sizeof(maelys_datalog_predicate_registry_t) > 0u);
    TEST_ASSERT_TRUE(sizeof(maelys_datalog_symbol_table_t) > 0u);
    TEST_ASSERT_TRUE(sizeof(maelys_datalog_ruleset_t) > 0u);
    TEST_ASSERT_TRUE(sizeof(maelys_datalog_edb_t) > 0u);
    TEST_ASSERT_TRUE(sizeof(maelys_datalog_solve_diagnostic_t) > 0u);
    TEST_ASSERT_TRUE(sizeof(maelys_datalog_policy_set_t) > 0u);
    TEST_ASSERT_EQUAL_STRING(MAELYS_DATALOG_DECISION_NAME_DENY_DEFAULT,
                             maelys_datalog_decision_name(MAELYS_DATALOG_DECISION_DENY_DEFAULT));
    TEST_END();
}

int main(int argc, char **argv) {
    const test_case_t cases[] = {
        {"maelys_datalog_headers/owner_types_available",
         TEST_MODE_NON_BLOCKING,
         test_datalog_headers_owner_types_available},
    };
    return test_main("maelys_datalog_headers",
                     cases,
                     (int)(sizeof(cases) / sizeof(cases[0])),
                     argc,
                     argv);
}
