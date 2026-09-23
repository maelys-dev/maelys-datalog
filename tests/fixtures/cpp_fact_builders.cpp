/* SPDX-License-Identifier: MPL-2.0 */
#include <maelys/datalog.h>
#if defined(MAELYS_DATALOG_FACT) || defined(MAELYS_DATALOG_ADD_FACTS)
#error "C11 batch macros must not leak into C++"
#endif
#ifdef MAELYS_DATALOG_ADD_FACT
#error "C11 generic-selection macros must not leak into C++"
#endif
#ifdef MAELYS_DATALOG_BOOL
#error "The C11 _Bool wrapper must not leak into C++"
#endif
#ifdef MAELYS_DATALOG_QUERY
#error "C11 query macros must not leak into C++"
#endif
constexpr maelys_datalog_value_t cpp_symbol = MAELYS_DATALOG_SYMBOL("alice");
static_assert(cpp_symbol.kind == MAELYS_DATALOG_VALUE_SYMBOL, "symbol kind");
static_assert(cpp_symbol.as.symbol[0] == 'a', "symbol value");
constexpr maelys_datalog_predicate_t cpp_declarations[] = {
    MAELYS_DATALOG_EDB("seed", 1),
    MAELYS_DATALOG_IDB("hidden", 1),
    MAELYS_DATALOG_IDB_QUERY("allow", 1),
    MAELYS_DATALOG_EDB_QUERY("observed", 1),
    MAELYS_DATALOG_POLICY_FACT("fixed", 1),
    MAELYS_DATALOG_POLICY_FACT_QUERY("trusted", 1),
};
static_assert(cpp_declarations[0].arity == 1, "preserve arity");
static_assert(cpp_declarations[0].flags == MAELYS_DATALOG_PREDICATE_EDB, "EDB");
static_assert(cpp_declarations[1].flags == MAELYS_DATALOG_PREDICATE_IDB, "IDB");
static_assert(cpp_declarations[2].flags ==
    (MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY), "IDB query");
static_assert(cpp_declarations[3].flags ==
    (MAELYS_DATALOG_PREDICATE_EDB | MAELYS_DATALOG_PREDICATE_QUERY), "EDB query");
static_assert(cpp_declarations[4].flags == MAELYS_DATALOG_PREDICATE_POLICY_FACT, "policy fact");
static_assert(cpp_declarations[5].flags ==
    (MAELYS_DATALOG_PREDICATE_POLICY_FACT | MAELYS_DATALOG_PREDICATE_QUERY), "policy fact query");
constexpr const char *cpp_atoms[] = {"alice", "mallory"};
constexpr maelys_datalog_domain_t cpp_with_atoms =
    MAELYS_DATALOG_DOMAIN_WITH_ATOMS("cpp_with_atoms", cpp_declarations, cpp_atoms);
constexpr maelys_datalog_domain_t cpp_no_atoms =
    MAELYS_DATALOG_DOMAIN_NO_ATOMS("cpp_no_atoms", cpp_declarations);
static_assert(cpp_with_atoms.predicates == cpp_declarations, "borrow predicates");
static_assert(cpp_no_atoms.predicates == cpp_declarations, "borrow predicates");
static_assert(cpp_with_atoms.predicate_count == 6u, "count predicates");
static_assert(cpp_no_atoms.predicate_count == 6u, "count predicates");
static_assert(cpp_with_atoms.atoms == cpp_atoms, "borrow atoms");
static_assert(cpp_with_atoms.atom_count == 2u, "count atoms separately");
static_assert(cpp_no_atoms.atoms == nullptr && cpp_no_atoms.atom_count == 0u,
    "no atoms is a null pointer and a zero count");
maelys_datalog_status_t cpp_consumer(maelys_datalog_input_edb_t *edb) {
    maelys_datalog_value_t value = MAELYS_DATALOG_SYMBOL("alice");
    return maelys_datalog_input_edb_add_fact(edb, "user", &value, 1u, nullptr);
}
