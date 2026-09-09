/* SPDX-License-Identifier: MIT */
#include "extension.h"
#include <stdio.h>

/* Active in Release builds too: never hide callback checks inside assert(). */
#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "starter check failed at line %d: %s\n", __LINE__, #condition); \
        return 1; \
    } \
} while (0)

int main(void)
{
    maelys_datalog_extension_t extension = starter_filter_declaration();
    size_t units = 99;
    int matched = 1;
    CHECK(extension.filters[0].validate_pattern(NULL, 0)
          == MAELYS_DATALOG_STATUS_UNSUPPORTED);
    CHECK(extension.filters[0].cost(0, 0, &units)
          == MAELYS_DATALOG_STATUS_UNSUPPORTED);
    CHECK(units == 0);
    CHECK(extension.filters[0].cost(0, 0, NULL)
          == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    CHECK(extension.filters[0].evaluate(NULL, 0, NULL, 0, &matched)
          == MAELYS_DATALOG_STATUS_UNSUPPORTED);
    CHECK(matched == 0);
    CHECK(extension.filters[0].evaluate(NULL, 0, NULL, 0, NULL)
          == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);

    maelys_datalog_context_t *context = NULL;
    size_t count = 0;
    CHECK(maelys_datalog_context_create(&context) == MAELYS_DATALOG_STATUS_OK);
    CHECK(maelys_datalog_context_register(context, &extension) == MAELYS_DATALOG_STATUS_OK);
    CHECK(maelys_datalog_context_seal(context, NULL)
          == MAELYS_DATALOG_STATUS_OK);
    CHECK(maelys_datalog_context_component_count(
              context, MAELYS_DATALOG_EXTENSION_FILTER, &count)
          == MAELYS_DATALOG_STATUS_OK);
    CHECK(count > 0);
    CHECK(maelys_datalog_context_free(context) == MAELYS_DATALOG_STATUS_OK);
    puts("filter starter: registration and unimplemented callbacks PASS");
    return 0;
}
