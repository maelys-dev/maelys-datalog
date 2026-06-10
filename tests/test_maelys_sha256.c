#include "common/maelys_sha256.h"
#include "src/manifest/maelys_datalog_manifest.h"
#include "tests/helpers/test_framework.h"

#include <string.h>

static int test_sha256_vectors(void) {
    TEST_BEGIN();
    char hex[65];
    TEST_ASSERT_EQUAL(0, maelys_sha256_hex((const unsigned char *)"", 0, hex), "%d");
    TEST_ASSERT_EQUAL_STRING("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", hex);
    TEST_ASSERT_EQUAL(0, maelys_sha256_hex((const unsigned char *)"abc", 3, hex), "%d");
    TEST_ASSERT_EQUAL_STRING("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", hex);
    TEST_END();
}

static int test_sha256_raw_bytes_lf_crlf_bom(void) {
    TEST_BEGIN();
    char lf[65], crlf[65], bom[65], nobom[65];
    const unsigned char with_bom[] = {0xef, 0xbb, 0xbf, 'a', 'b', 'c'};
    TEST_ASSERT_EQUAL(0, maelys_sha256_hex((const unsigned char *)"a\n", 2, lf), "%d");
    TEST_ASSERT_EQUAL(0, maelys_sha256_hex((const unsigned char *)"a\r\n", 3, crlf), "%d");
    TEST_ASSERT_FALSE(strcmp(lf, crlf) == 0);
    TEST_ASSERT_EQUAL(0, maelys_sha256_hex((const unsigned char *)"abc", 3, nobom), "%d");
    TEST_ASSERT_EQUAL(0, maelys_sha256_hex(with_bom, sizeof(with_bom), bom), "%d");
    TEST_ASSERT_FALSE(strcmp(nobom, bom) == 0);
    TEST_END();
}

static int test_sha256_lowercase_policy(void) {
    TEST_BEGIN();
    TEST_ASSERT_TRUE(maelys_sha256_hex_is_lowercase("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    TEST_ASSERT_FALSE(maelys_sha256_hex_is_lowercase("BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD"));
    TEST_END();
}

int main(int argc, char **argv) {
    test_case_t cases[] = {
        {"maelys_sha256/vectors", TEST_MODE_NON_BLOCKING, test_sha256_vectors},
        {"maelys_sha256/raw_lf_crlf_bom", TEST_MODE_NON_BLOCKING, test_sha256_raw_bytes_lf_crlf_bom},
        {"maelys_sha256/lowercase_policy", TEST_MODE_NON_BLOCKING, test_sha256_lowercase_policy},
    };
    return test_main("maelys_sha256", cases, (int)(sizeof(cases) / sizeof(cases[0])), argc, argv);
}
