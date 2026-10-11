/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-11
 */

#include "sha256.h"
#include <string.h>

static const uint32_t K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static uint32_t rotr(uint32_t x, uint32_t n)
{
    return (x >> n) | (x << (32u - n));
}

/* Processa exatamente um bloco de 64 bytes, atualizando ctx->state. */
static void sha256_process_block(Sha256_Ctx_t *ctx, const uint8_t block[64])
{
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t i;

    for (i = 0; i < 16u; i++) {
        w[i] = ((uint32_t)block[(i * 4u) + 0u] << 24) |
               ((uint32_t)block[(i * 4u) + 1u] << 16) |
               ((uint32_t)block[(i * 4u) + 2u] << 8) |
               ((uint32_t)block[(i * 4u) + 3u]);
    }
    for (i = 16u; i < 64u; i++) {
        uint32_t s0 = rotr(w[i - 15u], 7u) ^ rotr(w[i - 15u], 18u) ^ (w[i - 15u] >> 3u);
        uint32_t s1 = rotr(w[i - 2u], 17u) ^ rotr(w[i - 2u], 19u) ^ (w[i - 2u] >> 10u);
        w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
    }

    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    for (i = 0; i < 64u; i++) {
        uint32_t s1 = rotr(e, 6u) ^ rotr(e, 11u) ^ rotr(e, 25u);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + s1 + ch + K[i] + w[i];
        uint32_t s0 = rotr(a, 2u) ^ rotr(a, 13u) ^ rotr(a, 22u);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;

        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

void Sha256_Init(Sha256_Ctx_t *ctx)
{
    static const uint32_t iv[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };
    memcpy(ctx->state, iv, sizeof(iv));
    ctx->bit_len = 0u;
    ctx->buffer_len = 0u;
}

void Sha256_Update(Sha256_Ctx_t *ctx, const uint8_t *data, size_t len)
{
    size_t i;

    for (i = 0; i < len; i++) {
        ctx->buffer[ctx->buffer_len] = data[i];
        ctx->buffer_len++;
        if (ctx->buffer_len == 64u) {
            sha256_process_block(ctx, ctx->buffer);
            ctx->bit_len += 512u;
            ctx->buffer_len = 0u;
        }
    }
}

void Sha256_Final(Sha256_Ctx_t *ctx, uint8_t out_digest[SHA256_DIGEST_SIZE])
{
    size_t i;
    uint64_t total_bits = ctx->bit_len + ((uint64_t)ctx->buffer_len * 8u);

    /* Padding: 0x80, zeros ate' sobrar espaco para os 8 bytes de tamanho,
     * usando um 2o bloco se nao couber no atual. */
    ctx->buffer[ctx->buffer_len] = 0x80u;
    ctx->buffer_len++;

    if (ctx->buffer_len > 56u) {
        while (ctx->buffer_len < 64u) {
            ctx->buffer[ctx->buffer_len] = 0u;
            ctx->buffer_len++;
        }
        sha256_process_block(ctx, ctx->buffer);
        ctx->buffer_len = 0u;
    }
    while (ctx->buffer_len < 56u) {
        ctx->buffer[ctx->buffer_len] = 0u;
        ctx->buffer_len++;
    }

    for (i = 0; i < 8u; i++) {
        ctx->buffer[56u + i] = (uint8_t)(total_bits >> (56u - (8u * i)));
    }
    sha256_process_block(ctx, ctx->buffer);

    for (i = 0; i < 8u; i++) {
        out_digest[(i * 4u) + 0u] = (uint8_t)(ctx->state[i] >> 24);
        out_digest[(i * 4u) + 1u] = (uint8_t)(ctx->state[i] >> 16);
        out_digest[(i * 4u) + 2u] = (uint8_t)(ctx->state[i] >> 8);
        out_digest[(i * 4u) + 3u] = (uint8_t)(ctx->state[i]);
    }
}

void Sha256_Compute(const uint8_t *data, size_t len, uint8_t out_digest[SHA256_DIGEST_SIZE])
{
    Sha256_Ctx_t ctx;
    Sha256_Init(&ctx);
    Sha256_Update(&ctx, data, len);
    Sha256_Final(&ctx, out_digest);
}

void Sha256_ToHex(const uint8_t digest[SHA256_DIGEST_SIZE], char *out, size_t out_size)
{
    static const char hex[] = "0123456789abcdef";
    size_t i;

    if (out_size < (SHA256_HEX_LEN + 1u)) {
        if (out_size > 0u) { out[0] = '\0'; }
        return;
    }
    for (i = 0; i < SHA256_DIGEST_SIZE; i++) {
        out[(i * 2u) + 0u] = hex[(digest[i] >> 4) & 0x0Fu];
        out[(i * 2u) + 1u] = hex[digest[i] & 0x0Fu];
    }
    out[SHA256_HEX_LEN] = '\0';
}
