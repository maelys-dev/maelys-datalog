/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_DATALOG_VALUES_INTERNAL_H
#define MAELYS_DATALOG_VALUES_INTERNAL_H
#include "maelys/datalog.h"
#include "src/core/maelys_datalog_prepared_session.h"
maelys_datalog_status_t maelys_datalog_validate_value(const maelys_datalog_value_t *,
                                                       int strict_boolean);
maelys_datalog_status_t maelys_datalog_resolve_public_terms(const maelys_datalog_symbol_table_t *,
                                                            const maelys_datalog_value_t *,
                                                            size_t, maelys_datalog_internal_term_t *,
                                                            int *out_found, int strict_boolean);
#endif
