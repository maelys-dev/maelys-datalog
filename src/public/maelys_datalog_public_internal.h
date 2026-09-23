/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_DATALOG_PUBLIC_INTERNAL_H
#define MAELYS_DATALOG_PUBLIC_INTERNAL_H
#include "maelys/datalog_backend.h"
#include "src/manifest/maelys_datalog_manifest.h"
/* Input names are not stored in the solver's symbol pool. Account for their
 * registry budget as well, so valid input vocabulary still fits by default. */
#define MAELYS_DATALOG_INPUT_EDB_TEXT_BYTES \
    (MAELYS_DATALOG_STRING_POOL_BYTES + MAELYS_DATALOG_MAX_PREDICATES * \
     sizeof(((maelys_datalog_predicate_registry_t *)0)->defs[0].name))
struct maelys_datalog_policy {
    maelys_datalog_internal_policy_set_t set;
};
#endif
