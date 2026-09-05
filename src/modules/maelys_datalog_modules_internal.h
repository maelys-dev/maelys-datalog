/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_DATALOG_MODULES_INTERNAL_H
#define MAELYS_DATALOG_MODULES_INTERNAL_H
#include "maelys/datalog_module.h"
void maelys_datalog_modules_seal(void);
const maelys_datalog_planner_module_t *maelys_datalog_active_planner(void);
#endif
