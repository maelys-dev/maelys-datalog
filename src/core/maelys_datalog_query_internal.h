/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_DATALOG_QUERY_INTERNAL_H
#define MAELYS_DATALOG_QUERY_INTERNAL_H
#include "src/core/maelys_datalog_ruleset.h"
int maelys_datalog_query_whitelist_contains(const maelys_datalog_ruleset_t *, const char *, size_t);
/* Caller validates handle/lifetime/arity; the same permission check serves all backends. */
maelys_result_t maelys_datalog_validate_query_predicate(const maelys_datalog_ruleset_t *,
                                                        const char *, size_t,
                                                        maelys_datalog_predicate_id_t *);
#endif
