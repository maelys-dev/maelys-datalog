#include "src/core/maelys_datalog_ruleset.h"

#include "src/core/maelys_datalog_predicate_registry.h"
#include "src/core/maelys_datalog_symbol_table.h"

#include <stdio.h>
#include <string.h>

static maelys_result_t copy_ruleset_identity(char *dst, size_t dst_len, const char *src) {
    int n = snprintf(dst, dst_len, "%s", src);
    if (n < 0 || (size_t)n >= dst_len) return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    return MAELYS_OK;
}

maelys_result_t maelys_datalog_ruleset_init(maelys_datalog_ruleset_t *ruleset,
                                            const char *policy_id,
                                            const char *domain,
                                            const char *sha256,
                                            int test_only) {
    if (!ruleset || !policy_id || !domain || !sha256) return MAELYS_ERR_INVALID_ARGUMENT;
    if (ruleset->loaded) return MAELYS_ERR_INVALID_STATE;
    memset(ruleset, 0, sizeof(*ruleset));
    maelys_result_t rc = copy_ruleset_identity(ruleset->policy_id, sizeof(ruleset->policy_id), policy_id);
    if (rc != MAELYS_OK) return rc;
    rc = copy_ruleset_identity(ruleset->domain, sizeof(ruleset->domain), domain);
    if (rc != MAELYS_OK) return rc;
    rc = copy_ruleset_identity(ruleset->sha256, sizeof(ruleset->sha256), sha256);
    if (rc != MAELYS_OK) return rc;
    ruleset->positive_recursion_supported = 1;
    ruleset->negation_supported = 0;
    ruleset->negation_recursion_supported = 0;
    ruleset->test_only = test_only ? 1 : 0;
    maelys_datalog_symbol_table_init(&ruleset->symbols);
    maelys_datalog_predicate_registry_init_core(&ruleset->registry);
    ruleset->loaded = 1;
    return MAELYS_OK;
}

void maelys_datalog_ruleset_clear(maelys_datalog_ruleset_t *ruleset) {
    if (!ruleset) return;
    memset(ruleset, 0, sizeof(*ruleset));
}

/* Always returns 0. Maelys policy is fail-closed by design:
 * allow_projection must be explicitly derived by Datalog rules.
 * A ruleset can never implicitly allow everything.
 * This is intentional, not a missing implementation. */
int maelys_datalog_ruleset_has_allow_all(const maelys_datalog_ruleset_t *r) {
    (void)r;
    return 0;
}
