/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-11
 */

#include "audit_chain.h"
#include <string.h>

/* Exatamente 64 '0' (AUDIT_CHAIN_HASH_HEX_LEN) + terminador — o tamanho do
 * array abaixo faz o compilador recusar a build se a contagem estiver
 * errada (string maior nao caberia); o teste de host tambem confere
 * strlen() == 64 (test_audit_chain.c). */
const char AuditChain_GenesisHashHex[AUDIT_CHAIN_HASH_HEX_LEN + 1u] =
    "0000000000000000000000000000000000000000000000000000000000000000";

static void copy_hash(char *dst, size_t dst_size, const char *src)
{
    size_t n = strlen(src);
    if (n > AUDIT_CHAIN_HASH_HEX_LEN) { n = AUDIT_CHAIN_HASH_HEX_LEN; }
    if (dst_size <= n) { n = (dst_size > 0u) ? (dst_size - 1u) : 0u; }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

void AuditChain_Init(AuditChain_t *chain, const char *prev_hash_hex)
{
    const char *src = (prev_hash_hex != NULL) ? prev_hash_hex : AuditChain_GenesisHashHex;
    copy_hash(chain->prev_hash_hex, sizeof(chain->prev_hash_hex), src);
}

static void compute_chain_hash(const char *prev_hash_hex, const char *line_content,
                               char *out_hash_hex, size_t out_hash_hex_size)
{
    Sha256_Ctx_t ctx;
    uint8_t digest[SHA256_DIGEST_SIZE];

    Sha256_Init(&ctx);
    Sha256_Update(&ctx, (const uint8_t *)prev_hash_hex, strlen(prev_hash_hex));
    Sha256_Update(&ctx, (const uint8_t *)line_content, strlen(line_content));
    Sha256_Final(&ctx, digest);
    Sha256_ToHex(digest, out_hash_hex, out_hash_hex_size);
}

void AuditChain_AppendLine(AuditChain_t *chain, const char *line_content,
                           char *out_hash_hex, size_t out_hash_hex_size)
{
    compute_chain_hash(chain->prev_hash_hex, line_content, out_hash_hex, out_hash_hex_size);
    copy_hash(chain->prev_hash_hex, sizeof(chain->prev_hash_hex), out_hash_hex);
}

uint8_t AuditChain_VerifyLine(const char *prev_hash_hex, const char *line_content,
                              const char *hash_hex)
{
    char computed[AUDIT_CHAIN_HASH_HEX_LEN + 1u];

    compute_chain_hash(prev_hash_hex, line_content, computed, sizeof(computed));
    return (strcmp(computed, hash_hex) == 0) ? 1u : 0u;
}
