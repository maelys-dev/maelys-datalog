/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_DATALOG_VALUES_INTERNAL_H
#define MAELYS_DATALOG_VALUES_INTERNAL_H
#include "maelys/datalog.h"
#include "src/core/maelys_datalog_prepared_session.h"
maelys_datalog_status_t maelys_datalog_import_public_value(const maelys_datalog_public_value_t *,
                                                           int strict_boolean,
                                                           maelys_datalog_input_term_t *);
maelys_datalog_status_t maelys_datalog_resolve_public_terms(const maelys_datalog_symbol_table_t *,
                                                            const maelys_datalog_public_value_t *,
                                                            size_t, maelys_datalog_term_t *,
                                                            int *out_found, int strict_boolean);
#endif
