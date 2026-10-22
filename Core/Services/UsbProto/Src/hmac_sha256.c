/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-14
 */
#include "hmac_sha256.h"
#include "sha256.h"
#include <string.h>

#define HMAC_SHA256_BLOCK_SIZE (64U)

void HmacSha256_Compute(const uint8_t *key, size_t key_len,
                         const uint8_t *data, size_t data_len,
                         uint8_t out_digest[HMAC_SHA256_DIGEST_SIZE])
{
    uint8_t key_block[HMAC_SHA256_BLOCK_SIZE];
    uint8_t ipad[HMAC_SHA256_BLOCK_SIZE];
    uint8_t opad[HMAC_SHA256_BLOCK_SIZE];
    uint8_t inner_digest[HMAC_SHA256_DIGEST_SIZE];
    Sha256_Ctx_t ctx;
    size_t i;

    /* Chave maior que o bloco: usa o hash dela (RFC 2104). */
    if (key_len > HMAC_SHA256_BLOCK_SIZE) {
        uint8_t hashed_key[HMAC_SHA256_DIGEST_SIZE];
        Sha256_Compute(key, key_len, hashed_key);
        memset(key_block, 0, sizeof(key_block));
        memcpy(key_block, hashed_key, sizeof(hashed_key));
    } else {
        memset(key_block, 0, sizeof(key_block));
        memcpy(key_block, key, key_len);
    }

    for (i = 0U; i < HMAC_SHA256_BLOCK_SIZE; i++) {
        ipad[i] = key_block[i] ^ 0x36U;
        opad[i] = key_block[i] ^ 0x5CU;
    }

    Sha256_Init(&ctx);
    Sha256_Update(&ctx, ipad, sizeof(ipad));
    Sha256_Update(&ctx, data, data_len);
    Sha256_Final(&ctx, inner_digest);

    Sha256_Init(&ctx);
    Sha256_Update(&ctx, opad, sizeof(opad));
    Sha256_Update(&ctx, inner_digest, sizeof(inner_digest));
    Sha256_Final(&ctx, out_digest);
}
