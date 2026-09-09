/* SPDX-License-Identifier: MPL-2.0 */
#include "extension.h"
#include "maelys_conformance.h"
int main(void) {
    maelys_datalog_extension_t e = example_planner_extension();
    maelys_datalog_join_candidate_t candidates[3] = {0};
    for (size_t i = 0; i < 3; ++i) {
        candidates[i].body_index = i;
        candidates[i].kind = MAELYS_DATALOG_JOIN_ATOM;
        candidates[i].arity = 2;
        candidates[i].bound_terms = i;
        MC_REQUIRE(!maelys_conformance_planner(e.planners, candidates, i + 1));
    }
    size_t choice = SIZE_MAX;
    MC_OK(e.planners->choose(candidates, 3, &choice));
    MC_REQUIRE(choice == 2);
    MC_REQUIRE(e.planners->choose(NULL, 0, &choice) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    maelys_datalog_context_t *context;
    MC_OK(maelys_datalog_context_create(&context));
    MC_OK(maelys_datalog_context_register(context, &e));
    MC_OK(maelys_datalog_context_seal(context, "selective"));
    MC_OK(maelys_datalog_context_free(context));
    puts("planner example: safe index, deterministic choice, invalid arguments, selection PASS");
    return 0;
}
