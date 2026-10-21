// SPDX-License-Identifier: GPL-3.0-or-later
//
// libcore_scrypt() — библиотечная обёртка над примитивами enceeper/scrypt.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int  scrypt_pbkdf2_sha512(const uint32_t *pass, size_t passlen,
                          const uint32_t *salt, size_t saltlen,
                          uint32_t *out, size_t outlen);
int  scrypt_romix(uint32_t *block, size_t len, int N);
void scrypt_reverse(uint32_t *words, size_t len);
int  isLittleEndian(void);

// Копирует байты в новый uint32_t-буфер (выровненный, zero-padded).
static uint32_t* copy_to_words(const uint8_t *src, size_t len, size_t *out_words) {
    size_t words = (len + 3) / 4;
    uint32_t *buf = (uint32_t *)calloc(words ? words : 1, sizeof(uint32_t));
    if (!buf) return NULL;
    memcpy(buf, src, len);
    *out_words = words;
    return buf;
}

static void reverse_if_le(uint32_t *words, size_t count) {
    if (isLittleEndian() == 1) {
        scrypt_reverse(words, count);
    }
}

int libcore_scrypt(const uint8_t *pass, size_t passlen,
                   const uint8_t *salt, size_t saltlen,
                   uint64_t N, uint32_t r, uint32_t p,
                   uint8_t *out, size_t outlen)
{
    if (!pass || !salt || !out) return -1;
    if (N < 2 || (N & (N - 1)) != 0) return -1;
    if (r == 0 || p == 0 || outlen == 0) return -1;

    size_t B_words = (size_t)p * 32 * r;
    uint32_t *B = (uint32_t *)malloc(B_words * 4);
    if (!B) return -1;

    size_t pass_wlen = 0, salt_wlen = 0;
    uint32_t *pass_w = copy_to_words(pass, passlen, &pass_wlen);
    uint32_t *salt_w = copy_to_words(salt, saltlen, &salt_wlen);
    if (!pass_w || !salt_w) {
        free(pass_w); free(salt_w); free(B);
        return -1;
    }

    reverse_if_le(pass_w, pass_wlen);
    reverse_if_le(salt_w, salt_wlen);

    // Step 1
    int rc = scrypt_pbkdf2_sha512(pass_w, pass_wlen,
                                  salt_w, salt_wlen,
                                  B, B_words);
    if (rc != 1) {
        free(pass_w); free(salt_w); free(B);
        return -1;
    }

    // Step 2
    rc = scrypt_romix(B, B_words, (int)N);
    if (rc != 0) {
        free(pass_w); free(salt_w); free(B);
        return -1;
    }

    // Step 3
    size_t out_words = (outlen + 3) / 4;
    uint32_t *out_w = (uint32_t *)malloc(out_words * 4);
    if (!out_w) {
        free(pass_w); free(salt_w); free(B);
        return -1;
    }

    rc = scrypt_pbkdf2_sha512(pass_w, pass_wlen,
                              B, B_words,
                              out_w, out_words);

    reverse_if_le(out_w, out_words);

    if (rc != 1) {
        free(pass_w); free(salt_w); free(B); free(out_w);
        return -1;
    }

    memcpy(out, out_w, outlen);
    free(pass_w); free(salt_w); free(B); free(out_w);
    return 0;
}
