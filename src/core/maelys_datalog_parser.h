#pragma once
#ifndef MAELYS_DATALOG_PARSER_H
#define MAELYS_DATALOG_PARSER_H

#include "src/core/maelys_datalog_diagnostic.h"
#include "src/core/maelys_datalog_ruleset.h"

maelys_result_t maelys_datalog_parse_ruleset(maelys_datalog_ruleset_t *ruleset,
                                             const char *src,
                                             size_t len);
maelys_result_t maelys_datalog_parse_ruleset_ex(maelys_datalog_ruleset_t *ruleset,
                                                const char *src,
                                                size_t len,
                                                const char *file_path,
                                                maelys_datalog_diagnostic_t *out_diag);

#endif
