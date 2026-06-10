#pragma once
#ifndef TEST_LOG_H
#define TEST_LOG_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TEST_LOG_LEVEL_FATAL = 0,
    TEST_LOG_LEVEL_ERROR = 1,
    TEST_LOG_LEVEL_WARN  = 2,
    TEST_LOG_LEVEL_INFO  = 3,
    TEST_LOG_LEVEL_DEBUG = 4,
    TEST_LOG_LEVEL_TRACE = 5
} test_log_level_t;

#ifdef TEST_LOG_TAG
#define TEST_LOG_TAG_PREFIX "[" TEST_LOG_TAG "] "
#else
#define TEST_LOG_TAG_PREFIX ""
#endif

void test_log_init(test_log_level_t level, FILE *out, int color);
void test_log_shutdown(void);
void test_log_set_level(test_log_level_t level);
test_log_level_t test_log_get_level(void);
void test_log_set_file(FILE *out);
void test_log_set_color(int color);
void test_log_write(
    test_log_level_t level,
    const char *component,
    const char *file,
    int line,
    const char *func,
    const char *fmt,
    ...
);

#define test_log_fatal(...) \
    test_log_write(TEST_LOG_LEVEL_FATAL, TEST_LOG_TAG_PREFIX, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define test_log_error(...) \
    test_log_write(TEST_LOG_LEVEL_ERROR, TEST_LOG_TAG_PREFIX, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define test_log_warn(...) \
    test_log_write(TEST_LOG_LEVEL_WARN, TEST_LOG_TAG_PREFIX, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define test_log_info(...) \
    test_log_write(TEST_LOG_LEVEL_INFO, TEST_LOG_TAG_PREFIX, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define test_log_debug(...) \
    test_log_write(TEST_LOG_LEVEL_DEBUG, TEST_LOG_TAG_PREFIX, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define test_log_trace(...) \
    test_log_write(TEST_LOG_LEVEL_TRACE, TEST_LOG_TAG_PREFIX, __FILE__, __LINE__, __func__, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif
