#include "src/core/maelys_datalog_symbol_table.h"
#include "tests/helpers/test_framework.h"

#include <stdio.h>
#include <string.h>

static int test_symbol_table_interns_and_dedupes(void) {
    TEST_BEGIN();
    maelys_datalog_symbol_table_t t;
    maelys_datalog_symbol_table_init(&t);
    maelys_datalog_symbol_id_t a, b, c;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_symbol_intern(&t, "alpha", 5, &a), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_symbol_intern(&t, "alpha", 5, &b), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_symbol_intern(&t, "beta", 4, &c), "%d");
    TEST_ASSERT_EQUAL(a, b, "%u");
    TEST_ASSERT_TRUE(a != c);
    TEST_ASSERT_EQUAL_STRING("alpha", maelys_datalog_symbol_text(&t, a));
    TEST_END();
}

static int test_symbol_table_overflow_fails(void) {
    TEST_BEGIN();
    maelys_datalog_symbol_table_t t;
    maelys_datalog_symbol_table_init(&t);
    maelys_datalog_symbol_id_t id;
    char buf[32];
    maelys_result_t last = MAELYS_OK;
    for (size_t i = 0; i <= MAELYS_DATALOG_MAX_SYMBOLS; i++) {
        snprintf(buf, sizeof(buf), "sym%zu", i);
        last = maelys_datalog_symbol_intern(&t, buf, strlen(buf), &id);
        if (last != MAELYS_OK) break;
    }
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE, last, "%d");
    TEST_END();
}

int main(int argc, char **argv) {
    test_case_t cases[] = {
        {"maelys_datalog_symbol_table/interns_and_dedupes", TEST_MODE_NON_BLOCKING, test_symbol_table_interns_and_dedupes},
        {"maelys_datalog_symbol_table/overflow_fails", TEST_MODE_NON_BLOCKING, test_symbol_table_overflow_fails},
    };
    return test_main("maelys_datalog_symbol_table", cases, (int)(sizeof(cases) / sizeof(cases[0])), argc, argv);
}
