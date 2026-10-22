/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-14
 *
 * HMAC-SHA256 (RFC 2104), em C puro - sobre Sha256_* (Audit_Trail/sha256.h).
 * Usado pela autenticacao DEV do protocolo USB (Apendice A, Fase 7.2):
 * HMAC-SHA256(chave_dev, nonce || serial).
 */
#ifndef HMAC_SHA256_H
#define HMAC_SHA256_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HMAC_SHA256_DIGEST_SIZE (32U)

void HmacSha256_Compute(const uint8_t *key, size_t key_len,
                         const uint8_t *data, size_t data_len,
                         uint8_t out_digest[HMAC_SHA256_DIGEST_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* HMAC_SHA256_H */
