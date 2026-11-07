/* SPDX-License-Identifier: MPL-2.0 */
/* Which term kind each spelling of a truth value becomes, compiled and run
 * once per C standard by tools/check_c11_fact_builders.sh. The builders
 * classify by type: C23 made true/false bool constants, C11 defines them as
 * the int constants 1 and 0, and comparisons yield int in both. */
#include <maelys/datalog.h>
#include <stdbool.h>
#include <stdio.h>

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
#define LITERAL_KIND MAELYS_DATALOG_VALUE_BOOLEAN
#define STANDARD "C23"
#else
#define LITERAL_KIND MAELYS_DATALOG_VALUE_INTEGER
#define STANDARD "C11"
#endif

static int failures;

static void expect(const char *spelling, maelys_datalog_detail_argument_t argument,
                   maelys_datalog_value_kind_t kind, int64_t integer) {
    const maelys_datalog_value_t *value = &argument.value;
    int same = !argument.out_of_range && value->kind == kind
        && (kind == MAELYS_DATALOG_VALUE_BOOLEAN ? value->as.boolean == integer
                                                 : value->as.integer == integer);
    if (!same) {
        fprintf(stderr, "%s: %s did not become the expected term\n", STANDARD, spelling);
        ++failures;
    }
}

#define EXPECT(expression, kind, integer) \
    expect(#expression, MAELYS_DATALOG_DETAIL_ARGUMENT(expression), (kind), (integer))

int main(void) {
    const bool enabled = true;
    int one = 1;
    EXPECT(true, LITERAL_KIND, 1);
    EXPECT(false, LITERAL_KIND, 0);
    EXPECT(1 < 2, MAELYS_DATALOG_VALUE_INTEGER, 1);
    EXPECT(!one, MAELYS_DATALOG_VALUE_INTEGER, 0);
    EXPECT(enabled, MAELYS_DATALOG_VALUE_BOOLEAN, 1);
    EXPECT(MAELYS_DATALOG_BOOL(true), MAELYS_DATALOG_VALUE_BOOLEAN, 1);
    EXPECT(MAELYS_DATALOG_BOOL(1 < 2), MAELYS_DATALOG_VALUE_BOOLEAN, 1);
    EXPECT(MAELYS_DATALOG_BOOL(0), MAELYS_DATALOG_VALUE_BOOLEAN, 0);
    EXPECT(1, MAELYS_DATALOG_VALUE_INTEGER, 1);
    if (failures) return 1;
    printf("%s fact-builder truth values PASS\n", STANDARD);
    return 0;
}
