#pragma once
#ifndef MAELYS_DATALOG_SYMBOL_TABLE_H
#define MAELYS_DATALOG_SYMBOL_TABLE_H

#include "src/core/maelys_datalog_policy.h"

void maelys_datalog_symbol_table_init(maelys_datalog_symbol_table_t *table);
maelys_result_t maelys_datalog_symbol_intern(maelys_datalog_symbol_table_t *table,
                                             const char *text,
                                             size_t len,
                                             maelys_datalog_symbol_id_t *out_id);
int maelys_datalog_symbol_id_is_valid(const maelys_datalog_symbol_table_t *table,
                                      maelys_datalog_symbol_id_t id);
const char *maelys_datalog_symbol_text(const maelys_datalog_symbol_table_t *table,
                                       maelys_datalog_symbol_id_t id);

#endif
