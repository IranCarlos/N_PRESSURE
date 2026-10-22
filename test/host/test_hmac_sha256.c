/* Teste de host de hmac_sha256 (sem HAL). Ver test/host/run.sh.
 * Vetores RFC 4231, verificados tambem contra hmac.new(..., hashlib.sha256)
 * do Python antes de colar aqui. */
#include "hmac_sha256.h"
#include <string.h>
#include <stdio.h>

static int s_fail;
#define CHECK(c) do { if (!(c)) { printf("FALHA %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)

static void hex_to_bytes(const char *hex, uint8_t *out, size_t out_len)
{
    size_t i;
    for (i = 0U; i < out_len; i++) {
        unsigned int byte;
        sscanf(&hex[i * 2U], "%2x", &byte);
        out[i] = (uint8_t)byte;
    }
}

static void check_vector(const uint8_t *key, size_t key_len,
                          const uint8_t *data, size_t data_len,
                          const char *expected_hex)
{
    uint8_t expected[HMAC_SHA256_DIGEST_SIZE];
    uint8_t got[HMAC_SHA256_DIGEST_SIZE];

    hex_to_bytes(expected_hex, expected, sizeof(expected));
    HmacSha256_Compute(key, key_len, data, data_len, got);
    CHECK(memcmp(expected, got, sizeof(expected)) == 0);
}

static void test_rfc4231_case1(void)
{
    uint8_t key[20];
    const char *data = "Hi There";
    memset(key, 0x0b, sizeof(key));
    check_vector(key, sizeof(key), (const uint8_t *)data, strlen(data),
                 "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
}

static void test_rfc4231_case2(void)
{
    const char *key = "Jefe";
    const char *data = "what do ya want for nothing?";
    check_vector((const uint8_t *)key, strlen(key), (const uint8_t *)data, strlen(data),
                 "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

/* Chave de 131 bytes (> bloco de 64B) - forca o ramo que hasheia a chave
 * primeiro (RFC 2104/4231). Tamanhos de string via strlen() em vez de
 * literal hardcoded: um 29U/56U errado aqui (off-by-um na contagem manual)
 * lia 1-2 bytes depois do fim do literal - UB silencioso que so' aparecia
 * como digest errado e nao-deterministico entre builds (achado ao
 * depurar esta mesma falha). */
static void test_rfc4231_case6_long_key(void)
{
    uint8_t key[131];
    const char *data = "Test Using Larger Than Block-Size Key - Hash Key First";
    memset(key, 0xaa, sizeof(key));
    check_vector(key, sizeof(key), (const uint8_t *)data, strlen(data),
                 "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
}

int main(void)
{
    test_rfc4231_case1();
    test_rfc4231_case2();
    test_rfc4231_case6_long_key();
    printf(s_fail ? "hmac_sha256: %d FALHA(S)\n" : "hmac_sha256: OK\n", s_fail);
    return s_fail != 0;
}
