#pragma once
#ifndef MAELYS_DATALOG_EXPLANATION_FORMAT_H
#define MAELYS_DATALOG_EXPLANATION_FORMAT_H

#include <stddef.h>

#include "common/maelys_errors.h"
#include "src/core/maelys_datalog_ruleset.h"
#include "src/core/maelys_datalog_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* P4-C65 — Bounded, deterministic Why-true text formatter.
 *
 * Presentation-only. Renders an already-extracted P4-C64
 * maelys_datalog_explanation_t as the versioned text
 * MAELYS-DATALOG-WHY-TRUE-TEXT-v1, using the ruleset exclusively as a
 * read-only vocabulary (predicate names, interned symbols). The formatter
 * never re-solves, never reads an opaque solve result, never selects another
 * witness and never reorders steps or premises. It performs no heap
 * allocation, keeps no global mutable state, stores no pointer to its inputs
 * beyond the call, and is independent of the current locale.
 *
 * The text can contain the exact symbol values of the policy and EDB; it is
 * therefore potentially sensitive. The formatter itself never logs anything.
 *
 * Contract (out_required always counts the text bytes EXCLUDING the
 * terminating NUL; a successful write therefore needs out_capacity >=
 * *out_required + 1):
 *
 *   - ruleset, explanation or out_required NULL
 *       -> MAELYS_ERR_INVALID_ARGUMENT, out_text and out_required untouched;
 *   - out_capacity > 0 with out_text == NULL
 *       -> MAELYS_ERR_INVALID_ARGUMENT, out_required untouched;
 *   - out_text == NULL and out_capacity == 0
 *       -> valid count-only mode: MAELYS_OK and *out_required set to the
 *          exact size, no byte written anywhere;
 *   - ruleset not loaded or predicate registry not frozen
 *       -> MAELYS_ERR_INVALID_STATE, outputs untouched;
 *   - structurally incoherent explanation or vocabulary
 *       -> MAELYS_ERR_INVALID_FIELD, outputs untouched;
 *   - size computation reaching SIZE_MAX
 *       -> MAELYS_ERR_PAYLOAD_TOO_LARGE, outputs untouched;
 *   - insufficient buffer
 *       -> MAELYS_ERR_PAYLOAD_TOO_LARGE, *out_required exact, and, when
 *          out_capacity > 0, out_text[0] = '\0' as the only write (never a
 *          partial prefix);
 *   - sufficient buffer
 *       -> MAELYS_OK, complete NUL-terminated text, *out_required exact.
 *
 * Count-only mode and write mode share the same emission primitives; the
 * counted size and the written size cannot diverge. */
maelys_result_t maelys_datalog_format_explanation_text(
    const maelys_datalog_ruleset_t *ruleset,
    const maelys_datalog_explanation_t *explanation,
    char *out_text,
    size_t out_capacity,
    size_t *out_required);

#ifdef __cplusplus
}
#endif

#endif
