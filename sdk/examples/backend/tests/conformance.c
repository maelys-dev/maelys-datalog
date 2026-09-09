/* SPDX-License-Identifier: MPL-2.0 */
#include "extension.h"
#include "maelys_conformance.h"
static maelys_datalog_public_fact_t pair(const char *predicate, const char *a, const char *b) {
    maelys_datalog_public_fact_t f = {0};
    f.predicate = predicate;
    f.arity = 2;
    f.terms[0].kind = f.terms[1].kind = MAELYS_DATALOG_VALUE_SYMBOL;
    f.terms[0].as.symbol = a;
    f.terms[1].as.symbol = b;
    return f;
}
int main(void) {
    const maelys_datalog_public_predicate_t predicates[] = {
        {"edge", 2, MAELYS_DATALOG_PREDICATE_EDB},
        {"reach", 2, MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY}};
    const maelys_datalog_public_domain_t domain = {"backend_example", predicates, 2, NULL, 0};
    MC_OK(maelys_datalog_domain_register(&domain));
    maelys_datalog_extension_t e = example_backend_extension();
    const char *nodes[] = {"a", "b", "c", "d", "e"};
    maelys_datalog_public_fact_t probes[25], inputs[25];
    for (size_t a = 0; a < 5; ++a)
        for (size_t b = 0; b < 5; ++b)
            probes[a * 5 + b] = pair("reach", nodes[a], nodes[b]);
    uint32_t random = 123;
    for (size_t graph = 0; graph < 20; ++graph) {
        size_t count = 0;
        for (size_t a = 0; a < 5; ++a)
            for (size_t b = 0; b < 5; ++b) {
                random = random * UINT32_C(1664525) + UINT32_C(1013904223);
                if (graph && (random >> 28) < 5)
                    inputs[count++] = pair("edge", nodes[a], nodes[b]);
            }
        MC_REQUIRE(!maelys_conformance_backend(
            e.backends, domain.name,
            "reach(X,Y) :- edge(X,Y).\nreach(X,Z) :- reach(X,Y), edge(Y,Z).\n", inputs, count,
            probes, 25));
    }
    puts("backend example: reference differential, 20 recursive graphs, 500 ground probes PASS");
    return 0;
}
