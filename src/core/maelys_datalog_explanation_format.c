/* P4-C65 — Bounded, deterministic Why-true text formatter.
 *
 * Presentation layer only: renders a P4-C64 explanation as
 * MAELYS-DATALOG-WHY-TRUE-TEXT-v1. Structural validation is integral and
 * precedes any visible write; count-only mode and write mode share the same
 * emission primitives (one writer, two modes). No heap allocation, no
 * recursion, no global mutable state, no locale dependency. */

#include "src/core/maelys_datalog_explanation_format.h"

#include <stdint.h>
#include <string.h>

#include "common/maelys_utf8.h"
#include "src/core/maelys_datalog_edb.h"
#include "src/core/maelys_datalog_predicate_registry.h"
#include "src/core/maelys_datalog_symbol_table.h"

/* ====================================================================
 * Counted writer — one writer, two modes.
 *
 * dst == NULL  -> count-only pass: bytes are counted, nothing is written.
 * dst != NULL  -> write pass: the caller has already proven capacity; the
 *                 bound check is kept as a defensive barrier so no byte can
 *                 ever land outside the caller's buffer.
 * ==================================================================== */

typedef struct {
    char *dst;
    size_t capacity;
    size_t emitted;   /* bytes emitted so far, excluding the NUL */
    int overflowed;   /* size arithmetic reached SIZE_MAX */
} fmt_writer_t;

static void wr_byte(fmt_writer_t *w, unsigned char byte) {
    if (w->overflowed) return;
    if (w->emitted == SIZE_MAX) { /* SIZE_MAX guard on every addition */
        w->overflowed = 1;
        return;
    }
    if (w->dst != NULL && w->emitted < w->capacity) {
        w->dst[w->emitted] = (char)byte;
    }
    w->emitted++;
}

static void wr_bytes(fmt_writer_t *w, const unsigned char *bytes, size_t len) {
    for (size_t i = 0; i < len; i++) wr_byte(w, bytes[i]);
}

/* Literal emission without any length scan of the data. */
#define WR_LIT(w, lit) wr_bytes((w), (const unsigned char *)(lit), sizeof(lit) - 1u)

/* Unsigned decimal, canonical: no sign, no padding, no grouping. */
static void wr_u64(fmt_writer_t *w, unsigned long long value) {
    unsigned char digits[20];
    size_t n = 0;
    do {
        digits[n++] = (unsigned char)('0' + (unsigned)(value % 10ull));
        value /= 10ull;
    } while (value != 0ull && n < sizeof(digits));
    while (n > 0u) wr_byte(w, digits[--n]);
}

/* Signed decimal, canonical, INT64_MIN-safe: the magnitude is computed in
 * unsigned arithmetic so -value is never evaluated on the most negative
 * value. */
static void wr_i64(fmt_writer_t *w, long long value) {
    unsigned long long magnitude;
    if (value < 0) {
        wr_byte(w, '-');
        magnitude = (unsigned long long)(-(value + 1)) + 1ull;
    } else {
        magnitude = (unsigned long long)value;
    }
    wr_u64(w, magnitude);
}

/* §3.4 — byte-preserving, locale-independent quoted string.
 *
 * Whole-sequence UTF-8 validity selects the mode:
 *   valid   -> non-ASCII UTF-8 bytes verbatim; \" \\ \n \r \t named escapes;
 *              NUL and the other ASCII controls as \xHH (uppercase);
 *   invalid -> printable ASCII verbatim (except " and \ which keep their
 *              escapes); every non-ASCII or control byte as \xHH — in this
 *              mode LF/CR/TAB are \x0A/\x0D/\x09, never the named escapes.
 * The only escapes are \" \\ \n \r \t and \xHH; no \u escape exists. */
static void wr_quoted(fmt_writer_t *w, const unsigned char *bytes, size_t len) {
    static const char hex[16] = {'0', '1', '2', '3', '4', '5', '6', '7',
                                 '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
    const int whole_sequence_is_utf8 = maelys_utf8_validate(bytes, len);
    wr_byte(w, '"');
    for (size_t i = 0; i < len; i++) {
        const unsigned char b = bytes[i];
        if (b == (unsigned char)'"') {
            wr_byte(w, '\\');
            wr_byte(w, '"');
            continue;
        }
        if (b == (unsigned char)'\\') {
            wr_byte(w, '\\');
            wr_byte(w, '\\');
            continue;
        }
        if (whole_sequence_is_utf8) {
            if (b == (unsigned char)'\n') {
                wr_byte(w, '\\');
                wr_byte(w, 'n');
            } else if (b == (unsigned char)'\r') {
                wr_byte(w, '\\');
                wr_byte(w, 'r');
            } else if (b == (unsigned char)'\t') {
                wr_byte(w, '\\');
                wr_byte(w, 't');
            } else if (b < 0x20u || b == 0x7Fu) {
                wr_byte(w, '\\');
                wr_byte(w, 'x');
                wr_byte(w, (unsigned char)hex[(b >> 4) & 0x0Fu]);
                wr_byte(w, (unsigned char)hex[b & 0x0Fu]);
            } else {
                wr_byte(w, b); /* printable ASCII and UTF-8 bytes verbatim */
            }
        } else {
            if (b >= 0x20u && b <= 0x7Eu) {
                wr_byte(w, b); /* printable ASCII verbatim */
            } else {
                wr_byte(w, '\\');
                wr_byte(w, 'x');
                wr_byte(w, (unsigned char)hex[(b >> 4) & 0x0Fu]);
                wr_byte(w, (unsigned char)hex[b & 0x0Fu]);
            }
        }
    }
    wr_byte(w, '"');
}

/* ====================================================================
 * Read-only vocabulary access.
 *
 * Symbol lengths always come from the symbol table entry, never from a byte
 * scan of the storage: an interned symbol may embed NUL bytes.
 * ==================================================================== */

/* Bounded predicate-name length: the name field is NUL-terminated inside its
 * declared capacity; returns 0 on a missing NUL (invalid vocabulary). */
static int predicate_name_span(const maelys_datalog_predicate_def_t *def,
                               size_t *out_len) {
    const void *nul = memchr(def->name, '\0', sizeof(def->name));
    if (nul == NULL) return 0;
    *out_len = (size_t)((const char *)nul - def->name);
    return 1;
}

static void wr_symbol(fmt_writer_t *w,
                      const maelys_datalog_ruleset_t *ruleset,
                      maelys_datalog_symbol_id_t id) {
    /* Validation already proved the id; symbol_text re-validates before any
     * id - 1 indexing. The exact byte length comes from entries[id - 1].len. */
    const char *text = maelys_datalog_symbol_text(&ruleset->symbols, id);
    const size_t len = (size_t)ruleset->symbols.entries[id - 1u].len;
    wr_quoted(w, (const unsigned char *)text, len);
}

static void wr_term(fmt_writer_t *w,
                    const maelys_datalog_ruleset_t *ruleset,
                    const maelys_datalog_term_t *term) {
    switch (term->kind) {
    case MAELYS_DATALOG_TERM_SYMBOL:
        wr_symbol(w, ruleset, term->as.symbol);
        break;
    case MAELYS_DATALOG_TERM_INT:
        wr_i64(w, term->as.integer);
        break;
    case MAELYS_DATALOG_TERM_BOOL:
        if (term->as.boolean) {
            WR_LIT(w, "true");
        } else {
            WR_LIT(w, "false");
        }
        break;
    default:
        /* Unreachable after validation; emit nothing. */
        break;
    }
}

static void wr_fact(fmt_writer_t *w,
                    const maelys_datalog_ruleset_t *ruleset,
                    const maelys_datalog_fact_t *fact) {
    const maelys_datalog_predicate_def_t *def =
        maelys_datalog_predicate_registry_get(&ruleset->registry, fact->predicate_id);
    size_t name_len = 0;
    if (def == NULL || !predicate_name_span(def, &name_len)) {
        /* Unreachable after validation. */
        return;
    }
    wr_quoted(w, (const unsigned char *)def->name, name_len);
    wr_byte(w, '(');
    for (uint8_t i = 0; i < fact->arity; i++) {
        if (i != 0u) wr_byte(w, ',');
        wr_term(w, ruleset, &fact->terms[i]);
    }
    wr_byte(w, ')');
}

static void wr_premise_kind(fmt_writer_t *w, uint8_t kind) {
    switch (kind) {
    case (uint8_t)MAELYS_DATALOG_EXPLANATION_PREMISE_POSITIVE_FACT:
        WR_LIT(w, "positive");
        break;
    case (uint8_t)MAELYS_DATALOG_EXPLANATION_PREMISE_NEGATED_ABSENCE:
        WR_LIT(w, "negative-absence");
        break;
    case (uint8_t)MAELYS_DATALOG_EXPLANATION_PREMISE_COMPARISON_TRUE:
        WR_LIT(w, "comparison-true");
        break;
    default:
        break; /* unreachable after validation */
    }
}

static void wr_premise_origin(fmt_writer_t *w, uint8_t origin) {
    switch (origin) {
    case (uint8_t)MAELYS_DATALOG_EXPLANATION_ORIGIN_POLICY_FACT:
        WR_LIT(w, "policy-fact");
        break;
    case (uint8_t)MAELYS_DATALOG_EXPLANATION_ORIGIN_EDB:
        WR_LIT(w, "edb");
        break;
    case (uint8_t)MAELYS_DATALOG_EXPLANATION_ORIGIN_IDB:
        WR_LIT(w, "idb");
        break;
    case (uint8_t)MAELYS_DATALOG_EXPLANATION_ORIGIN_NOT_APPLICABLE:
        WR_LIT(w, "none");
        break;
    default:
        break; /* unreachable after validation */
    }
}

static void wr_cmp_op(fmt_writer_t *w, uint8_t op) {
    switch (op) {
    case (uint8_t)MAELYS_DATALOG_CMP_EQ:
        WR_LIT(w, "=");
        break;
    case (uint8_t)MAELYS_DATALOG_CMP_NEQ:
        WR_LIT(w, "!=");
        break;
    case (uint8_t)MAELYS_DATALOG_CMP_LT:
        WR_LIT(w, "<");
        break;
    case (uint8_t)MAELYS_DATALOG_CMP_LTE:
        WR_LIT(w, "<=");
        break;
    case (uint8_t)MAELYS_DATALOG_CMP_GT:
        WR_LIT(w, ">");
        break;
    case (uint8_t)MAELYS_DATALOG_CMP_GTE:
        WR_LIT(w, ">=");
        break;
    default:
        break; /* unreachable after validation */
    }
}

/* ====================================================================
 * §3.3 — grammar emission. Used identically by the counting pass and the
 * writing pass; the produced byte sequence is fully determined by the
 * (ruleset, explanation) pair.
 * ==================================================================== */

static void emit_explanation_text(const maelys_datalog_ruleset_t *ruleset,
                                  const maelys_datalog_explanation_t *explanation,
                                  fmt_writer_t *w) {
    WR_LIT(w, "MAELYS-DATALOG-WHY-TRUE-TEXT-v1\n");
    if (explanation->found == 0u) {
        WR_LIT(w, "status=not-derived\nsteps=0 premises=0\n");
        return;
    }
    if (explanation->truncated != 0u) {
        WR_LIT(w, "status=truncated\nsteps=0 premises=0\n");
        return;
    }
    WR_LIT(w, "status=complete\n");
    WR_LIT(w, "steps=");
    wr_u64(w, (unsigned long long)explanation->step_count);
    WR_LIT(w, " premises=");
    wr_u64(w, (unsigned long long)explanation->premise_count);
    wr_byte(w, '\n');

    for (uint16_t i = 0; i < explanation->step_count; i++) {
        const maelys_datalog_explanation_step_t *step = &explanation->steps[i];
        WR_LIT(w, "step=");
        wr_u64(w, (unsigned long long)i);
        WR_LIT(w, " rule=");
        wr_u64(w, (unsigned long long)step->rule_id);
        WR_LIT(w, " fact=");
        wr_fact(w, ruleset, &step->derived_fact);
        wr_byte(w, '\n');

        for (uint16_t j = 0; j < step->premise_count; j++) {
            const uint16_t global = (uint16_t)(step->premise_begin + j);
            const maelys_datalog_explanation_premise_t *premise =
                &explanation->premises[global];
            WR_LIT(w, "premise=");
            wr_u64(w, (unsigned long long)global);
            WR_LIT(w, " body=");
            wr_u64(w, (unsigned long long)premise->body_index);
            WR_LIT(w, " kind=");
            wr_premise_kind(w, premise->kind);
            WR_LIT(w, " origin=");
            wr_premise_origin(w, premise->origin);
            if (premise->kind ==
                (uint8_t)MAELYS_DATALOG_EXPLANATION_PREMISE_COMPARISON_TRUE) {
                WR_LIT(w, " lhs=");
                wr_term(w, ruleset, &premise->as.comparison.lhs);
                WR_LIT(w, " op=");
                wr_cmp_op(w, premise->op);
                WR_LIT(w, " rhs=");
                wr_term(w, ruleset, &premise->as.comparison.rhs);
            } else {
                WR_LIT(w, " fact=");
                wr_fact(w, ruleset, &premise->as.fact);
            }
            WR_LIT(w, " parent=");
            if (premise->kind ==
                    (uint8_t)MAELYS_DATALOG_EXPLANATION_PREMISE_POSITIVE_FACT &&
                premise->origin == (uint8_t)MAELYS_DATALOG_EXPLANATION_ORIGIN_IDB) {
                wr_u64(w, (unsigned long long)premise->parent_step);
            } else {
                wr_byte(w, '-');
            }
            wr_byte(w, '\n');
        }
    }
    WR_LIT(w, "result-step=");
    wr_u64(w, (unsigned long long)(explanation->step_count - 1u));
    wr_byte(w, '\n');
}

/* ====================================================================
 * §3.6 — integral structural validation, before any visible write.
 * Strictly read-only on the ruleset and the explanation.
 * ==================================================================== */

static maelys_result_t validate_term(const maelys_datalog_ruleset_t *ruleset,
                                     const maelys_datalog_term_t *term) {
    switch (term->kind) {
    case MAELYS_DATALOG_TERM_SYMBOL:
        /* Mandatory order: validity first; on failure return without ever
         * evaluating id - 1. */
        if (!maelys_datalog_symbol_id_is_valid(&ruleset->symbols, term->as.symbol)) {
            return MAELYS_ERR_INVALID_FIELD;
        }
        return MAELYS_OK;
    case MAELYS_DATALOG_TERM_INT:
    case MAELYS_DATALOG_TERM_BOOL:
        return MAELYS_OK;
    case MAELYS_DATALOG_TERM_VAR:
    default:
        /* A ground explanation has no variable and no unknown term kind. */
        return MAELYS_ERR_INVALID_FIELD;
    }
}

static maelys_result_t validate_fact(const maelys_datalog_ruleset_t *ruleset,
                                     const maelys_datalog_fact_t *fact) {
    const maelys_datalog_predicate_def_t *def =
        maelys_datalog_predicate_registry_get(&ruleset->registry, fact->predicate_id);
    size_t name_len = 0;
    if (def == NULL) return MAELYS_ERR_INVALID_FIELD;
    if (!predicate_name_span(def, &name_len)) return MAELYS_ERR_INVALID_FIELD;
    if ((size_t)fact->arity > (size_t)MAELYS_DATALOG_MAX_ARITY) {
        return MAELYS_ERR_INVALID_FIELD;
    }
    if ((size_t)fact->arity != def->arity) return MAELYS_ERR_INVALID_FIELD;
    for (uint8_t i = 0; i < fact->arity; i++) {
        const maelys_result_t rc = validate_term(ruleset, &fact->terms[i]);
        if (rc != MAELYS_OK) return rc;
    }
    return MAELYS_OK;
}

static int origin_is_store(uint8_t origin) {
    return origin == (uint8_t)MAELYS_DATALOG_EXPLANATION_ORIGIN_POLICY_FACT ||
           origin == (uint8_t)MAELYS_DATALOG_EXPLANATION_ORIGIN_EDB ||
           origin == (uint8_t)MAELYS_DATALOG_EXPLANATION_ORIGIN_IDB;
}

static maelys_result_t validate_premise(const maelys_datalog_ruleset_t *ruleset,
                                        const maelys_datalog_explanation_t *explanation,
                                        const maelys_datalog_explanation_premise_t *premise,
                                        uint16_t step_index,
                                        uint16_t lexical_index) {
    if ((size_t)premise->body_index != (size_t)lexical_index) {
        return MAELYS_ERR_INVALID_FIELD;
    }
    switch (premise->kind) {
    case (uint8_t)MAELYS_DATALOG_EXPLANATION_PREMISE_POSITIVE_FACT:
        if (premise->op != 0u) return MAELYS_ERR_INVALID_FIELD;
        switch (premise->origin) {
        case (uint8_t)MAELYS_DATALOG_EXPLANATION_ORIGIN_POLICY_FACT:
        case (uint8_t)MAELYS_DATALOG_EXPLANATION_ORIGIN_EDB:
            if (premise->parent_step != (uint16_t)MAELYS_DATALOG_EXPLANATION_NO_STEP) {
                return MAELYS_ERR_INVALID_FIELD;
            }
            break;
        case (uint8_t)MAELYS_DATALOG_EXPLANATION_ORIGIN_IDB:
            /* Backward-only local link, strictly earlier than this step. */
            if (premise->parent_step == (uint16_t)MAELYS_DATALOG_EXPLANATION_NO_STEP) {
                return MAELYS_ERR_INVALID_FIELD;
            }
            if (premise->parent_step >= step_index) return MAELYS_ERR_INVALID_FIELD;
            if (!maelys_datalog_fact_equals(
                    &explanation->steps[premise->parent_step].derived_fact,
                    &premise->as.fact)) {
                return MAELYS_ERR_INVALID_FIELD;
            }
            break;
        default:
            return MAELYS_ERR_INVALID_FIELD;
        }
        return validate_fact(ruleset, &premise->as.fact);
    case (uint8_t)MAELYS_DATALOG_EXPLANATION_PREMISE_NEGATED_ABSENCE:
        if (premise->op != 0u) return MAELYS_ERR_INVALID_FIELD;
        if (!origin_is_store(premise->origin)) return MAELYS_ERR_INVALID_FIELD;
        if (premise->parent_step != (uint16_t)MAELYS_DATALOG_EXPLANATION_NO_STEP) {
            return MAELYS_ERR_INVALID_FIELD;
        }
        return validate_fact(ruleset, &premise->as.fact);
    case (uint8_t)MAELYS_DATALOG_EXPLANATION_PREMISE_COMPARISON_TRUE: {
        if (premise->origin != (uint8_t)MAELYS_DATALOG_EXPLANATION_ORIGIN_NOT_APPLICABLE) {
            return MAELYS_ERR_INVALID_FIELD;
        }
        if (premise->parent_step != (uint16_t)MAELYS_DATALOG_EXPLANATION_NO_STEP) {
            return MAELYS_ERR_INVALID_FIELD;
        }
        if (premise->op < (uint8_t)MAELYS_DATALOG_CMP_EQ ||
            premise->op > (uint8_t)MAELYS_DATALOG_CMP_GTE) {
            return MAELYS_ERR_INVALID_FIELD;
        }
        const maelys_result_t lhs_rc =
            validate_term(ruleset, &premise->as.comparison.lhs);
        if (lhs_rc != MAELYS_OK) return lhs_rc;
        return validate_term(ruleset, &premise->as.comparison.rhs);
    }
    default:
        return MAELYS_ERR_INVALID_FIELD;
    }
}

static maelys_result_t validate_explanation(const maelys_datalog_ruleset_t *ruleset,
                                            const maelys_datalog_explanation_t *explanation) {
    const uint8_t found = explanation->found;
    const uint8_t truncated = explanation->truncated;
    const uint16_t step_count = explanation->step_count;
    const uint16_t premise_count = explanation->premise_count;

    /* Exactly three valid states (§3.6). */
    if (found == 0u) {
        if (truncated != 0u || step_count != 0u || premise_count != 0u) {
            return MAELYS_ERR_INVALID_FIELD;
        }
        return MAELYS_OK;
    }
    if (found != 1u) return MAELYS_ERR_INVALID_FIELD;
    if (truncated == 1u) {
        if (step_count != 0u || premise_count != 0u) return MAELYS_ERR_INVALID_FIELD;
        return MAELYS_OK;
    }
    if (truncated != 0u) return MAELYS_ERR_INVALID_FIELD;
    if (step_count < 1u) return MAELYS_ERR_INVALID_FIELD;
    if ((size_t)step_count > (size_t)MAELYS_DATALOG_MAX_EXPLANATION_STEPS) {
        return MAELYS_ERR_INVALID_FIELD;
    }
    if ((size_t)premise_count > (size_t)MAELYS_DATALOG_MAX_EXPLANATION_PREMISES) {
        return MAELYS_ERR_INVALID_FIELD;
    }

    /* Premise ranges: contiguous, no hole, no overlap, covering exactly
     * 0..premise_count-1 in step order. */
    size_t next_begin = 0;
    for (uint16_t i = 0; i < step_count; i++) {
        const maelys_datalog_explanation_step_t *step = &explanation->steps[i];
        if ((size_t)step->premise_begin != next_begin) return MAELYS_ERR_INVALID_FIELD;
        next_begin += (size_t)step->premise_count;
        if (next_begin > (size_t)premise_count) return MAELYS_ERR_INVALID_FIELD;

        /* Rule id: non-zero, bounded read-only against the ruleset before
         * any rules[] indexing; 1-based authority preserved. */
        if (step->rule_id == 0u) return MAELYS_ERR_INVALID_FIELD;
        if (step->rule_id > ruleset->rule_count) return MAELYS_ERR_INVALID_FIELD;
        if (ruleset->rules[step->rule_id - 1u].rule_id != step->rule_id) {
            return MAELYS_ERR_INVALID_FIELD;
        }

        const maelys_result_t fact_rc = validate_fact(ruleset, &step->derived_fact);
        if (fact_rc != MAELYS_OK) return fact_rc;

        for (uint16_t j = 0; j < step->premise_count; j++) {
            const maelys_result_t premise_rc = validate_premise(
                ruleset,
                explanation,
                &explanation->premises[step->premise_begin + j],
                i,
                j);
            if (premise_rc != MAELYS_OK) return premise_rc;
        }
    }
    if (next_begin != (size_t)premise_count) return MAELYS_ERR_INVALID_FIELD;

    /* Reachability: every step before the last is referenced directly or
     * transitively from the result step; parents are strictly backward, so a
     * single descending sweep marks the whole result DAG without recursion.
     * The last step can be nobody's parent (parents are always earlier). */
    uint8_t referenced[MAELYS_DATALOG_MAX_EXPLANATION_STEPS];
    memset(referenced, 0, sizeof(referenced));
    referenced[step_count - 1u] = 1u;
    for (uint16_t i = step_count; i-- > 0u;) {
        if (!referenced[i]) continue;
        const maelys_datalog_explanation_step_t *step = &explanation->steps[i];
        for (uint16_t j = 0; j < step->premise_count; j++) {
            const maelys_datalog_explanation_premise_t *premise =
                &explanation->premises[step->premise_begin + j];
            if (premise->kind ==
                    (uint8_t)MAELYS_DATALOG_EXPLANATION_PREMISE_POSITIVE_FACT &&
                premise->origin == (uint8_t)MAELYS_DATALOG_EXPLANATION_ORIGIN_IDB) {
                referenced[premise->parent_step] = 1u;
            }
        }
    }
    for (uint16_t i = 0; i < step_count; i++) {
        if (!referenced[i]) return MAELYS_ERR_INVALID_FIELD;
    }
    return MAELYS_OK;
}

/* ==================================================================== */

maelys_result_t maelys_datalog_format_explanation_text(
    const maelys_datalog_ruleset_t *ruleset,
    const maelys_datalog_explanation_t *explanation,
    char *out_text,
    size_t out_capacity,
    size_t *out_required) {
    if (ruleset == NULL || explanation == NULL || out_required == NULL) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    if (out_text == NULL && out_capacity > 0u) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    if (!ruleset->loaded ||
        !maelys_datalog_predicate_registry_is_frozen(&ruleset->registry)) {
        return MAELYS_ERR_INVALID_STATE;
    }

    const maelys_result_t validation_rc = validate_explanation(ruleset, explanation);
    if (validation_rc != MAELYS_OK) return validation_rc;

    /* Pass 1 — exact size, via the same emission primitives as the write. */
    fmt_writer_t counter;
    counter.dst = NULL;
    counter.capacity = 0u;
    counter.emitted = 0u;
    counter.overflowed = 0;
    emit_explanation_text(ruleset, explanation, &counter);
    if (counter.overflowed) return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    const size_t required = counter.emitted;

    if (out_text == NULL) {
        /* Count-only mode: out_capacity == 0 established above. */
        *out_required = required;
        return MAELYS_OK;
    }

    if (required == SIZE_MAX || out_capacity < required + 1u) {
        /* Insufficient buffer: never a partial prefix. */
        *out_required = required;
        if (out_capacity > 0u) out_text[0] = '\0';
        return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    }

    /* Pass 2 — atomic write; capacity was proven sufficient. */
    fmt_writer_t writer;
    writer.dst = out_text;
    writer.capacity = out_capacity;
    writer.emitted = 0u;
    writer.overflowed = 0;
    emit_explanation_text(ruleset, explanation, &writer);
    out_text[writer.emitted] = '\0';
    *out_required = writer.emitted;
    return MAELYS_OK;
}
