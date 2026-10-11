/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-11
 *
 * SHA-256 (FIPS 180-4), em C puro — sem HAL. Implementacao de referencia,
 * nao otimizada: a trilha de auditoria grava uma linha por vez (Fase 5,
 * storage_task), nao precisa de velocidade de hardware. O H723 tem o
 * periferico HASH com SHA-256 por hardware (ver stm32h7xx_hal_legacy.h);
 * migrar para ele e' uma otimizacao futura, nao um requisito agora — esta
 * versao compila e testa igual no host e no alvo, sem depender de CubeMX.
 */
#ifndef SHA256_H
#define SHA256_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHA256_DIGEST_SIZE (32u)
#define SHA256_HEX_LEN     (64u) /* digest em hex minusculo, sem terminador */

typedef struct {
    uint32_t state[8];
    uint64_t bit_len;
    uint8_t  buffer[64];
    size_t   buffer_len;
} Sha256_Ctx_t;

void Sha256_Init(Sha256_Ctx_t *ctx);
void Sha256_Update(Sha256_Ctx_t *ctx, const uint8_t *data, size_t len);
void Sha256_Final(Sha256_Ctx_t *ctx, uint8_t out_digest[SHA256_DIGEST_SIZE]);

/* Atalho: hash de um unico buffer. */
void Sha256_Compute(const uint8_t *data, size_t len, uint8_t out_digest[SHA256_DIGEST_SIZE]);

/* Formata o digest em hex minusculo. out precisa de pelo menos
 * SHA256_HEX_LEN+1 bytes (inclui o terminador nulo). */
void Sha256_ToHex(const uint8_t digest[SHA256_DIGEST_SIZE], char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* SHA256_H */
