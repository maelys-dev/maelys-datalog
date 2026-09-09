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
    maelys_datalog_extension_t extension = starter_backend_declaration();
    int sentinel = 0;
    void *state = &sentinel;
    void *result = &sentinel;
    CHECK(extension.backends[0].capabilities == 0);
    CHECK(extension.backends[0].prepare(NULL, &state)
          == MAELYS_DATALOG_STATUS_UNSUPPORTED);
    CHECK(state == NULL);
    CHECK(extension.backends[0].prepare(NULL, NULL)
          == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    CHECK(extension.backends[0].solve(NULL, NULL, 0, NULL, &result, NULL)
          == MAELYS_DATALOG_STATUS_UNSUPPORTED);
    CHECK(result == NULL);
    CHECK(extension.backends[0].solve(NULL, NULL, 0, NULL, NULL, NULL)
          == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    extension.backends[0].destroy_result(NULL, NULL);
    extension.backends[0].destroy(NULL);

    maelys_datalog_context_t *context = NULL;
    size_t count = 0;
    CHECK(maelys_datalog_context_create(&context) == MAELYS_DATALOG_STATUS_OK);
    CHECK(maelys_datalog_context_register(context, &extension) == MAELYS_DATALOG_STATUS_OK);
    CHECK(maelys_datalog_context_seal(context, NULL)
          == MAELYS_DATALOG_STATUS_OK);
    CHECK(maelys_datalog_context_component_count(
              context, MAELYS_DATALOG_EXTENSION_BACKEND, &count)
          == MAELYS_DATALOG_STATUS_OK);
    CHECK(count > 0);
    CHECK(maelys_datalog_context_free(context) == MAELYS_DATALOG_STATUS_OK);
    puts("backend starter: registration and unimplemented callbacks PASS");
    return 0;
}
