#include "common/maelys_utf8.h"
#include "tests/helpers/test_framework.h"

#include <stddef.h>

static int test_utf8_valid_sequences(void) {
    TEST_BEGIN();
    const unsigned char ascii[] = "hello";
    const unsigned char accented[] = { 'c', 'a', 'f', 0xc3, 0xa9 };
    const unsigned char emoji[] = { 0xf0, 0x9f, 0x98, 0x80 };
    TEST_ASSERT_TRUE(maelys_utf8_validate(ascii, sizeof(ascii) - 1));
    TEST_ASSERT_TRUE(maelys_utf8_validate(accented, sizeof(accented)));
    TEST_ASSERT_TRUE(maelys_utf8_validate(emoji, sizeof(emoji)));
    TEST_ASSERT_TRUE(maelys_utf8_validate(NULL, 0));
    TEST_ASSERT_FALSE(maelys_utf8_validate(NULL, 1));
    TEST_END();
}

static int test_utf8_invalid_sequences(void) {
    TEST_BEGIN();
    const unsigned char continuation[] = { 0x80 };
    const unsigned char overlong_slash[] = { 0xc0, 0xaf };
    const unsigned char surrogate[] = { 0xed, 0xa0, 0x80 };
    const unsigned char too_large[] = { 0xf4, 0x90, 0x80, 0x80 };
    const unsigned char truncated[] = { 0xe2, 0x82 };
    const unsigned char missing_cont[] = { 0xe2, 0x28, 0xa1 };
    TEST_ASSERT_FALSE(maelys_utf8_validate(continuation, sizeof(continuation)));
    TEST_ASSERT_FALSE(maelys_utf8_validate(overlong_slash, sizeof(overlong_slash)));
    TEST_ASSERT_FALSE(maelys_utf8_validate(surrogate, sizeof(surrogate)));
    TEST_ASSERT_FALSE(maelys_utf8_validate(too_large, sizeof(too_large)));
    TEST_ASSERT_FALSE(maelys_utf8_validate(truncated, sizeof(truncated)));
    TEST_ASSERT_FALSE(maelys_utf8_validate(missing_cont, sizeof(missing_cont)));
    TEST_END();
}

static int test_bytes_look_textual(void) {
    TEST_BEGIN();
    const unsigned char text[] = "line\tone\nline two\r\n";
    const unsigned char with_nul[] = { 'a', 0x00, 'b' };
    const unsigned char controls[] = { 'a', 0x01, 0x02, 0x03, 0x04, 0x05, 'b' };
    const unsigned char invalid[] = { 'x', 0xff };
    TEST_ASSERT_TRUE(maelys_bytes_look_textual(text, sizeof(text) - 1));
    TEST_ASSERT_TRUE(maelys_bytes_look_textual(NULL, 0));
    TEST_ASSERT_FALSE(maelys_bytes_look_textual(NULL, 1));
    TEST_ASSERT_FALSE(maelys_bytes_look_textual(with_nul, sizeof(with_nul)));
    TEST_ASSERT_FALSE(maelys_bytes_look_textual(controls, sizeof(controls)));
    TEST_ASSERT_FALSE(maelys_bytes_look_textual(invalid, sizeof(invalid)));
    TEST_END();
}

int main(int argc, char **argv) {
    test_case_t cases[] = {
        { "maelys_utf8/valid_sequences", TEST_MODE_NON_BLOCKING, test_utf8_valid_sequences },
        { "maelys_utf8/invalid_sequences", TEST_MODE_NON_BLOCKING, test_utf8_invalid_sequences },
        { "maelys_utf8/textual", TEST_MODE_NON_BLOCKING, test_bytes_look_textual },
    };
    return test_main("maelys_utf8", cases, (int)(sizeof(cases) / sizeof(cases[0])), argc, argv);
}
