#pragma once
#ifndef MAELYS_SHA256_H
#define MAELYS_SHA256_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAELYS_SHA256_DIGEST_BYTES 32u
#define MAELYS_SHA256_HEX_BYTES 64u

typedef struct maelys_sha256_ctx {
    uint32_t state[8];
    uint64_t bit_len;
    unsigned char buffer[64];
    size_t buffer_len;
} maelys_sha256_ctx_t;

void maelys_sha256_init(maelys_sha256_ctx_t *ctx);
void maelys_sha256_update(maelys_sha256_ctx_t *ctx, const unsigned char *data, size_t len);
void maelys_sha256_final(maelys_sha256_ctx_t *ctx, unsigned char out[MAELYS_SHA256_DIGEST_BYTES]);
int maelys_sha256_hex(const unsigned char *data, size_t len, char out_hex[MAELYS_SHA256_HEX_BYTES + 1]);
int maelys_sha256_file_hex(const char *path, char out_hex[MAELYS_SHA256_HEX_BYTES + 1]);
int maelys_sha256_hex_is_lowercase(const char *hex);

#ifdef __cplusplus
}
#endif

#endif
