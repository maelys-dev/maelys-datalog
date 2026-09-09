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
    maelys_datalog_extension_t extension = starter_planner_declaration();
    maelys_datalog_join_candidate_t candidate = {0};
    size_t selected = 0;
    CHECK(extension.planners[0].choose(&candidate, 1, &selected)
          == MAELYS_DATALOG_STATUS_UNSUPPORTED);
    CHECK(selected == SIZE_MAX);
    CHECK(extension.planners[0].choose(NULL, 0, NULL)
          == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);

    maelys_datalog_context_t *context = NULL;
    size_t count = 0;
    CHECK(maelys_datalog_context_create(&context) == MAELYS_DATALOG_STATUS_OK);
    CHECK(maelys_datalog_context_register(context, &extension) == MAELYS_DATALOG_STATUS_OK);
    CHECK(maelys_datalog_context_seal(context, "starter_planner")
          == MAELYS_DATALOG_STATUS_OK);
    CHECK(maelys_datalog_context_component_count(
              context, MAELYS_DATALOG_EXTENSION_PLANNER, &count)
          == MAELYS_DATALOG_STATUS_OK);
    CHECK(count > 0);
    CHECK(maelys_datalog_context_free(context) == MAELYS_DATALOG_STATUS_OK);
    puts("planner starter: registration and unimplemented callbacks PASS");
    return 0;
}
