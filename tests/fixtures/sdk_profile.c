/* SPDX-License-Identifier: MPL-2.0 */
#include <maelys/datalog.h>
#include <stdio.h>

int main(void) {
    size_t actual = 0;
    if (maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_FACTS_PER_PRED, &actual)
            != MAELYS_DATALOG_STATUS_OK || actual != EXPECT_FACTS_PER_PRED) {
        fprintf(stderr, "SDK profile mismatch: expected %u facts/predicate, got %zu\n",
                (unsigned)EXPECT_FACTS_PER_PRED, actual);
        return 1;
    }
    return 0;
}
