#pragma once
#ifndef MAELYS_UTF8_H
#define MAELYS_UTF8_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int maelys_utf8_validate(const unsigned char *data, size_t len);
int maelys_bytes_look_textual(const unsigned char *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif
