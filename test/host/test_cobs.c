/* Teste de host de cobs (sem HAL). Ver test/host/run.sh. */
#include "cobs.h"
#include <string.h>
#include <stdio.h>

static int s_fail;
#define CHECK(c) do { if (!(c)) { printf("FALHA %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)

static void roundtrip(const uint8_t *in, size_t in_len)
{
    uint8_t encoded[COBS_MAX_ENCODED_LEN(300)];
    uint8_t decoded[300];
    size_t enc_len = 0U;
    size_t dec_len = 0U;

    CHECK(Cobs_Encode(in, in_len, encoded, sizeof(encoded), &enc_len) == COBS_OK);
    /* encode nao deve produzir nenhum 0x00 (e' o que permite usa-lo como delimitador). */
    for (size_t i = 0U; i < enc_len; i++) {
        CHECK(encoded[i] != 0x00U);
    }
    CHECK(Cobs_Decode(encoded, enc_len, decoded, sizeof(decoded), &dec_len) == COBS_OK);
    CHECK(dec_len == in_len);
    CHECK(memcmp(decoded, in, in_len) == 0);
}

static void test_empty(void)
{
    uint8_t empty[1] = { 0x00 }; /* conteudo nao usado (in_len=0), so' evita passar NULL */
    roundtrip(empty, 0U);
}

static void test_no_zeros(void)
{
    const uint8_t in[] = { 0x01, 0x02, 0x03, 0xFF, 0x7E };
    roundtrip(in, sizeof(in));
}

static void test_leading_trailing_zeros(void)
{
    const uint8_t in[] = { 0x00, 0x01, 0x02, 0x00 };
    roundtrip(in, sizeof(in));
}

static void test_all_zeros(void)
{
    uint8_t in[10];
    memset(in, 0, sizeof(in));
    roundtrip(in, sizeof(in));
}

static void test_run_longer_than_254(void)
{
    uint8_t in[300];
    for (size_t i = 0U; i < sizeof(in); i++) {
        in[i] = (uint8_t)(i + 1U); /* nenhum zero, forca o wraparound de codigo 0xFF */
    }
    roundtrip(in, sizeof(in));
}

static void test_mixed_with_long_runs(void)
{
    uint8_t in[300];
    for (size_t i = 0U; i < sizeof(in); i++) {
        in[i] = (uint8_t)((i % 7U == 0U) ? 0x00U : (i + 1U));
    }
    roundtrip(in, sizeof(in));
}

static void test_encode_output_overflow(void)
{
    const uint8_t in[] = { 0x01, 0x02, 0x03 };
    uint8_t out[2]; /* pequeno demais mesmo pro pior caso */
    size_t out_len = 0U;
    CHECK(Cobs_Encode(in, sizeof(in), out, sizeof(out), &out_len) == COBS_ERR_OUTPUT_OVERFLOW);
}

static void test_decode_malformed_zero_in_payload(void)
{
    const uint8_t bad[] = { 0x02, 0x01, 0x00 }; /* 0x00 cru dentro do payload codificado */
    uint8_t out[8];
    size_t out_len = 0U;
    CHECK(Cobs_Decode(bad, sizeof(bad), out, sizeof(out), &out_len) == COBS_ERR_MALFORMED);
}

static void test_decode_malformed_code_overruns_input(void)
{
    const uint8_t bad[] = { 0x05, 0x01, 0x02 }; /* codigo diz 4 bytes de dados, so' tem 2 */
    uint8_t out[8];
    size_t out_len = 0U;
    CHECK(Cobs_Decode(bad, sizeof(bad), out, sizeof(out), &out_len) == COBS_ERR_MALFORMED);
}

int main(void)
{
    test_empty();
    test_no_zeros();
    test_leading_trailing_zeros();
    test_all_zeros();
    test_run_longer_than_254();
    test_mixed_with_long_runs();
    test_encode_output_overflow();
    test_decode_malformed_zero_in_payload();
    test_decode_malformed_code_overruns_input();
    printf(s_fail ? "cobs: %d FALHA(S)\n" : "cobs: OK\n", s_fail);
    return s_fail != 0;
}
