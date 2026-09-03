#include "common/maelys_utf8.h"

static int is_cont(unsigned char c) {
    return (c & 0xc0u) == 0x80u;
}

int maelys_utf8_validate(const unsigned char *data, size_t len) {
    if (!data) return len == 0;
    size_t i = 0;
    while (i < len) {
        unsigned char c = data[i];
        if (c <= 0x7f) {
            i++;
            continue;
        }
        if (c >= 0xc2 && c <= 0xdf) {
            if (i + 1 >= len || !is_cont(data[i + 1])) return 0;
            i += 2;
            continue;
        }
        if (c == 0xe0) {
            if (i + 2 >= len || data[i + 1] < 0xa0 || data[i + 1] > 0xbf ||
                !is_cont(data[i + 2])) return 0;
            i += 3;
            continue;
        }
        if ((c >= 0xe1 && c <= 0xec) || (c >= 0xee && c <= 0xef)) {
            if (i + 2 >= len || !is_cont(data[i + 1]) || !is_cont(data[i + 2])) return 0;
            i += 3;
            continue;
        }
        if (c == 0xed) {
            if (i + 2 >= len || data[i + 1] < 0x80 || data[i + 1] > 0x9f ||
                !is_cont(data[i + 2])) return 0;
            i += 3;
            continue;
        }
        if (c == 0xf0) {
            if (i + 3 >= len || data[i + 1] < 0x90 || data[i + 1] > 0xbf ||
                !is_cont(data[i + 2]) || !is_cont(data[i + 3])) return 0;
            i += 4;
            continue;
        }
        if (c >= 0xf1 && c <= 0xf3) {
            if (i + 3 >= len || !is_cont(data[i + 1]) || !is_cont(data[i + 2]) ||
                !is_cont(data[i + 3])) return 0;
            i += 4;
            continue;
        }
        if (c == 0xf4) {
            if (i + 3 >= len || data[i + 1] < 0x80 || data[i + 1] > 0x8f ||
                !is_cont(data[i + 2]) || !is_cont(data[i + 3])) return 0;
            i += 4;
            continue;
        }
        return 0;
    }
    return 1;
}

int maelys_bytes_look_textual(const unsigned char *data, size_t len) {
    if (!data) return len == 0;
    if (!maelys_utf8_validate(data, len)) return 0;
    if (len == 0) return 1;

    size_t controls = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = data[i];
        if (c == 0) return 0;
        if ((c < 0x20 && c != '\t' && c != '\n' && c != '\r') || c == 0x7f) {
            controls++;
        }
    }
    return controls * 100 <= len * 5;
}
