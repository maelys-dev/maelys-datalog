#pragma once
#ifndef MAELYS_DATALOG_DIAGNOSTIC_H
#define MAELYS_DATALOG_DIAGNOSTIC_H

#include <stddef.h>
#include <stdint.h>

#include "maelys/datalog.h"

typedef struct {
    maelys_datalog_diag_code_t code;
    uint8_t compare_result;
    uint8_t expected_kind;
    uint8_t observed_lhs_kind;
    uint8_t observed_rhs_kind;
    uint8_t failed_op;
    uint8_t term_index;
    uint8_t expected_arity;
    uint8_t observed_arity;
    uint16_t predicate_id;
    char phase[32];
    char file[256];
    size_t line;
    size_t column;
    char predicate[96];
    size_t arity;
    char token[96];
    char field[96];
    char domain[96];
    size_t count;
    size_t limit;
    char message[256];
    char hint[256];
} maelys_datalog_diagnostic_t;

void maelys_datalog_diagnostic_clear(maelys_datalog_diagnostic_t *diag);
void maelys_datalog_diagnostic_set(maelys_datalog_diagnostic_t *diag,
                                   maelys_datalog_diag_code_t code,
                                   const char *phase,
                                   const char *file,
                                   size_t line,
                                   size_t column,
                                   const char *message,
                                   const char *hint);
void maelys_datalog_diagnostic_set_predicate(maelys_datalog_diagnostic_t *diag,
                                             const char *predicate,
                                             size_t arity);
void maelys_datalog_diagnostic_set_limit(maelys_datalog_diagnostic_t *diag,
                                         size_t count,
                                         size_t limit);
void maelys_datalog_diagnostic_set_comparison_error(maelys_datalog_diagnostic_t *diag,
                                                    uint8_t compare_result,
                                                    uint8_t expected_kind,
                                                    uint8_t observed_lhs_kind,
                                                    uint8_t observed_rhs_kind,
                                                    uint8_t failed_op,
                                                    uint8_t term_index);
const char *maelys_datalog_diag_code_name(maelys_datalog_diag_code_t code);

#endif
