#include "tests/helpers/test_log.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static test_log_level_t g_log_level = TEST_LOG_LEVEL_INFO;
static FILE *g_log_file = NULL;
static int g_log_color = 1;
static int g_log_color_enabled = -1;

static const char *level_color(test_log_level_t level) {
    switch (level) {
        case TEST_LOG_LEVEL_FATAL: return "\033[1;35m";
        case TEST_LOG_LEVEL_ERROR: return "\033[1;31m";
        case TEST_LOG_LEVEL_WARN:  return "\033[1;33m";
        case TEST_LOG_LEVEL_INFO:  return "\033[1;36m";
        case TEST_LOG_LEVEL_DEBUG: return "\033[0;37m";
        case TEST_LOG_LEVEL_TRACE: return "\033[0;90m";
        default: return "";
    }
}

static const char *level_name(test_log_level_t level) {
    switch (level) {
        case TEST_LOG_LEVEL_FATAL: return "FATAL";
        case TEST_LOG_LEVEL_ERROR: return "ERROR";
        case TEST_LOG_LEVEL_WARN:  return "WARN ";
        case TEST_LOG_LEVEL_INFO:  return "INFO ";
        case TEST_LOG_LEVEL_DEBUG: return "DEBUG";
        case TEST_LOG_LEVEL_TRACE: return "TRACE";
        default: return "UNKWN";
    }
}

static void detect_color(void) {
    FILE *out = g_log_file ? g_log_file : stderr;
    if (g_log_color == 2) {
        g_log_color_enabled = 1;
    } else if (g_log_color == 0) {
        g_log_color_enabled = 0;
    } else {
        g_log_color_enabled = isatty(fileno(out));
    }
}

void test_log_init(test_log_level_t level, FILE *out, int color) {
    g_log_level = level;
    g_log_file = out ? out : stderr;
    g_log_color = color;
    g_log_color_enabled = -1;
}

void test_log_shutdown(void) {
    g_log_file = NULL;
    g_log_color_enabled = -1;
}

void test_log_set_level(test_log_level_t level) {
    g_log_level = level;
}

test_log_level_t test_log_get_level(void) {
    return g_log_level;
}

void test_log_set_file(FILE *out) {
    g_log_file = out ? out : stderr;
}

void test_log_set_color(int color) {
    g_log_color = color;
    g_log_color_enabled = -1;
}

void test_log_write(
    test_log_level_t level,
    const char *component,
    const char *file,
    int line,
    const char *func,
    const char *fmt,
    ...
) {
    if (level > g_log_level) return;
    if (g_log_color_enabled == -1) detect_color();

    char timebuf[20];
    time_t now = time(NULL);
    struct tm ts;
    localtime_r(&now, &ts);
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &ts);

    FILE *out = g_log_file ? g_log_file : stderr;
    if (g_log_color_enabled) {
        fprintf(out, "%s%s [%s]\033[0m ", level_color(level), timebuf, level_name(level));
    } else {
        fprintf(out, "%s [%s] ", timebuf, level_name(level));
    }

    if (component && *component) fputs(component, out);
    if (level <= TEST_LOG_LEVEL_WARN) {
        fprintf(out, "(%s:%d %s) ", file ? file : "?", line, func ? func : "?");
    }

    va_list ap;
    va_start(ap, fmt);
    vfprintf(out, fmt ? fmt : "", ap);
    va_end(ap);

    fputc('\n', out);
    fflush(out);

    if (level == TEST_LOG_LEVEL_FATAL) exit(1);
}
