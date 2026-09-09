/* SPDX-License-Identifier: MPL-2.0 */
#include "extension.h"
#include "maelys_conformance.h"
int main(void) {
    const maelys_datalog_public_predicate_t predicates[] = {
        {"seed", 1, MAELYS_DATALOG_PREDICATE_EDB},
        {"allow", 1, MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY}};
    const maelys_datalog_public_domain_t domain = {"frontend_example", predicates, 2, NULL, 0};
    MC_OK(maelys_datalog_domain_register(&domain));
    maelys_datalog_extension_t e = example_frontend_extension();
    MC_REQUIRE(
        !maelys_conformance_frontend(e.frontends, domain.name, "# comment\nallow <- seed", 0, 1));
    MC_REQUIRE(!maelys_conformance_frontend(e.frontends, domain.name, "allow -> seed",
                                            MAELYS_DATALOG_STATUS_INVALID_FIELD, 0));
    /* Syntactically accepted by the frontend, rejected by mandatory IR validation. */
    MC_REQUIRE(!maelys_conformance_frontend(e.frontends, domain.name, "seed <- allow",
                                            MAELYS_DATALOG_STATUS_INVALID_FIELD, 0));
    puts("frontend example: lowering, source error, EDB-head rejection PASS");
    return 0;
}
