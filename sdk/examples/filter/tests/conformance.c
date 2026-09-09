/* SPDX-License-Identifier: MPL-2.0 */
#include "extension.h"
#include "maelys_conformance.h"
int main(void) {
    maelys_datalog_extension_t e = example_filter_extension();
    const maelys_conformance_filter_case_t cases[] = {
        {(const unsigned char *)"alice", (const unsigned char *)"alice", 5, 5, 0, 0, 0, 1},
        {(const unsigned char *)"bob", (const unsigned char *)"alice", 3, 5, 0, 0, 0, 0},
        {NULL, NULL, 0, 0, 0, 0, 0, 1},
        {NULL, NULL, 0, 1, MAELYS_DATALOG_STATUS_INVALID_ARGUMENT, 0, 0, 0},
        {NULL, (const unsigned char *)"x", 1, 1, 0, 0, MAELYS_DATALOG_STATUS_INVALID_ARGUMENT, 0},
        {NULL, (const unsigned char *)"", 0, SIZE_MAX, 0, MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE,
         0, 0},
    };
    MC_REQUIRE(!maelys_conformance_filter(e.filters, cases, sizeof(cases) / sizeof(cases[0])));
    maelys_datalog_context_t *context;
    MC_OK(maelys_datalog_context_create(&context));
    MC_OK(maelys_datalog_context_register(context, &e));
    MC_OK(maelys_datalog_context_seal(context, NULL));
    MC_OK(maelys_datalog_context_free(context));
    puts("filter example: patterns, empty bytes, matching, cost overflow, declaration PASS");
    return 0;
}
