/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-14
 *
 * Sessao USER/DEV do protocolo USB (Apendice A, Fase 7.2): AUTH_CHALLENGE
 * pede um nonce de 16B, AUTH_RESPONSE eleva a sessao a DEV se
 * HMAC-SHA256(chave_dev, nonce||serial) bater, AUTH_LOGOUT volta a USER.
 *
 * C puro, sem HAL/RTC - "agora" e os bytes "aleatorios" do nonce vem
 * injetados pelo chamador (Core/Src/main.c), que e' quem conhece
 * HAL_GetTick()/PortDWT_GetCycles() - mesmo padrao de injecao do resto do
 * modulo (UsbProtoDispatchCtx_t). Isso deixa a logica de sessao
 * (expiracao, nonce de uso unico, comparacao HMAC) testavel no host com
 * valores sinteticos.
 */
#ifndef USB_PROTO_AUTH_H
#define USB_PROTO_AUTH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define USB_PROTO_AUTH_NONCE_LEN       (16U)
#define USB_PROTO_AUTH_SERIAL_LEN      (12U) /* UID de 96 bits do STM32H7 */
#define USB_PROTO_AUTH_DEV_KEY_LEN     (32U)
#define USB_PROTO_AUTH_HMAC_LEN        (32U) /* HMAC_SHA256_DIGEST_SIZE */
#define USB_PROTO_AUTH_CHALLENGE_TIMEOUT_MS (10000U)
/* Fase 7.4: sessao DEV cai para USER apos este tempo SEM nenhum comando
 * (Parte B 2.6, "timeout de sessao"); o PC recebe o evento AUTH_EXPIROU. */
#define USB_PROTO_AUTH_SESSION_TIMEOUT_MS (10UL * 60UL * 1000UL)

typedef enum {
    USB_PROTO_ROLE_USER = 0,
    USB_PROTO_ROLE_DEV  = 1
} UsbProtoRole_t;

typedef enum {
    USB_PROTO_AUTH_OK = 0,
    USB_PROTO_AUTH_ERR_NO_KEY,       /* chave DEV nao provisionada - falha sempre, fail-safe */
    USB_PROTO_AUTH_ERR_NO_CHALLENGE, /* nenhum AUTH_CHALLENGE pendente (ou ja consumido) */
    USB_PROTO_AUTH_ERR_EXPIRED,      /* nonce emitido ha mais de USB_PROTO_AUTH_CHALLENGE_TIMEOUT_MS */
    USB_PROTO_AUTH_ERR_HMAC_MISMATCH,
    USB_PROTO_AUTH_ERR_WRONG_ROLE    /* ex.: LOGOUT sem sessao DEV */
} UsbProtoAuth_Status_t;

typedef struct {
    uint8_t  dev_key[USB_PROTO_AUTH_DEV_KEY_LEN];
    uint8_t  dev_key_valid;

    UsbProtoRole_t role;
    uint8_t  nonce[USB_PROTO_AUTH_NONCE_LEN];
    uint8_t  nonce_pending; /* 1 = aguardando AUTH_RESPONSE pra este nonce */
    uint32_t nonce_issued_at_ms;
    uint32_t last_activity_ms; /* ultimo comando em sessao DEV (Fase 7.4) */
} UsbProtoAuthSession_t;

/* dev_key_valid=0 -> AUTH_RESPONSE sempre falha (ERR_NO_KEY), mesmo com a
 * resposta certa - nao ha' chave nenhuma pra comparar. */
void UsbProtoAuth_Init(UsbProtoAuthSession_t *sess,
                        const uint8_t dev_key[USB_PROTO_AUTH_DEV_KEY_LEN],
                        uint8_t dev_key_valid);

UsbProtoRole_t UsbProtoAuth_GetRole(const UsbProtoAuthSession_t *sess);

/* Sempre sucede (gera um nonce novo, substituindo qualquer um pendente -
 * so' o ULTIMO challenge emitido pode ser respondido). */
void UsbProtoAuth_HandleChallenge(UsbProtoAuthSession_t *sess, uint32_t now_ms,
                                   const uint8_t random_bytes[USB_PROTO_AUTH_NONCE_LEN],
                                   uint8_t out_nonce[USB_PROTO_AUTH_NONCE_LEN]);

/* Nonce e' consumido (nonce_pending->0) em qualquer caminho, sucesso ou
 * falha - uma resposta errada nao pode ser tentada de novo sem challenge
 * novo (anti bruteforce simples). */
UsbProtoAuth_Status_t UsbProtoAuth_HandleResponse(
    UsbProtoAuthSession_t *sess, uint32_t now_ms,
    const uint8_t serial[USB_PROTO_AUTH_SERIAL_LEN],
    const uint8_t received_hmac[USB_PROTO_AUTH_HMAC_LEN],
    UsbProtoRole_t *out_role);

UsbProtoAuth_Status_t UsbProtoAuth_HandleLogout(UsbProtoAuthSession_t *sess,
                                                 UsbProtoRole_t *out_role);

/* Fase 7.4 - chamar a cada comando valido recebido: renova a sessao DEV. */
void UsbProtoAuth_Touch(UsbProtoAuthSession_t *sess, uint32_t now_ms);

/* Fase 7.4 - chamar periodicamente: devolve 1 (uma vez) quando a sessao DEV
 * expirou por inatividade e ja' voltou para USER. */
uint8_t UsbProtoAuth_CheckExpiry(UsbProtoAuthSession_t *sess, uint32_t now_ms);

/* Fase 7.4 - cabo desconectado/porta fechada: volta para USER na hora (o
 * proximo PC a conectar nao herda a sessao DEV do anterior). */
void UsbProtoAuth_Reset(UsbProtoAuthSession_t *sess);

#ifdef __cplusplus
}
#endif

#endif /* USB_PROTO_AUTH_H */
