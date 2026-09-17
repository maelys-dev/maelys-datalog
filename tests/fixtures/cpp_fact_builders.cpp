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
constexpr maelys_datalog_public_value_t cpp_symbol = MAELYS_DATALOG_SYMBOL("alice");
static_assert(cpp_symbol.kind == MAELYS_DATALOG_VALUE_SYMBOL, "symbol kind");
static_assert(cpp_symbol.as.symbol[0] == 'a', "symbol value");
constexpr maelys_datalog_public_predicate_t cpp_declarations[] = {
    MAELYS_DATALOG_EDB("seed", 1),
    MAELYS_DATALOG_IDB("hidden", 1),
    MAELYS_DATALOG_IDB_QUERY("allow", 1),
};
static_assert(cpp_declarations[0].arity == 1, "preserve arity");
static_assert(cpp_declarations[0].flags == MAELYS_DATALOG_PREDICATE_EDB, "EDB");
static_assert(cpp_declarations[1].flags == MAELYS_DATALOG_PREDICATE_IDB, "IDB");
static_assert(cpp_declarations[2].flags ==
    (MAELYS_DATALOG_PREDICATE_IDB | MAELYS_DATALOG_PREDICATE_QUERY), "IDB query");
maelys_datalog_status_t cpp_consumer(maelys_datalog_input_edb_t *edb) {
    maelys_datalog_public_value_t value = MAELYS_DATALOG_SYMBOL("alice");
    return maelys_datalog_input_edb_add_fact(edb, "user", &value, 1u, nullptr);
}
