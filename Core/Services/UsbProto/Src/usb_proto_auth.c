/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-14
 */
#include "usb_proto_auth.h"
#include "hmac_sha256.h"
#include <string.h>

/* Comparacao em tempo constante (nao atalha no 1o byte diferente) - evita
 * que o tempo de resposta vaze quantos bytes iniciais do HMAC acertaram. */
static uint8_t constant_time_equal(const uint8_t *a, const uint8_t *b, size_t len)
{
    uint8_t diff = 0U;
    size_t i;
    for (i = 0U; i < len; i++) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return (diff == 0U) ? 1U : 0U;
}

void UsbProtoAuth_Init(UsbProtoAuthSession_t *sess,
                        const uint8_t dev_key[USB_PROTO_AUTH_DEV_KEY_LEN],
                        uint8_t dev_key_valid)
{
    memset(sess, 0, sizeof(*sess));
    if (dev_key_valid != 0U) {
        memcpy(sess->dev_key, dev_key, sizeof(sess->dev_key));
    }
    sess->dev_key_valid = dev_key_valid;
    sess->role = USB_PROTO_ROLE_USER;
    sess->nonce_pending = 0U;
}

UsbProtoRole_t UsbProtoAuth_GetRole(const UsbProtoAuthSession_t *sess)
{
    return sess->role;
}

void UsbProtoAuth_HandleChallenge(UsbProtoAuthSession_t *sess, uint32_t now_ms,
                                   const uint8_t random_bytes[USB_PROTO_AUTH_NONCE_LEN],
                                   uint8_t out_nonce[USB_PROTO_AUTH_NONCE_LEN])
{
    memcpy(sess->nonce, random_bytes, USB_PROTO_AUTH_NONCE_LEN);
    sess->nonce_pending = 1U;
    sess->nonce_issued_at_ms = now_ms;
    memcpy(out_nonce, sess->nonce, USB_PROTO_AUTH_NONCE_LEN);
}

UsbProtoAuth_Status_t UsbProtoAuth_HandleResponse(
    UsbProtoAuthSession_t *sess, uint32_t now_ms,
    const uint8_t serial[USB_PROTO_AUTH_SERIAL_LEN],
    const uint8_t received_hmac[USB_PROTO_AUTH_HMAC_LEN],
    UsbProtoRole_t *out_role)
{
    uint8_t message[USB_PROTO_AUTH_NONCE_LEN + USB_PROTO_AUTH_SERIAL_LEN];
    uint8_t expected_hmac[HMAC_SHA256_DIGEST_SIZE];
    uint8_t nonce_copy[USB_PROTO_AUTH_NONCE_LEN];
    uint32_t issued_at;

    *out_role = sess->role;

    if (sess->dev_key_valid == 0U) {
        sess->nonce_pending = 0U;
        return USB_PROTO_AUTH_ERR_NO_KEY;
    }
    if (sess->nonce_pending == 0U) {
        return USB_PROTO_AUTH_ERR_NO_CHALLENGE;
    }

    /* Copia o nonce/instante ANTES de consumir - o nonce so' pode ser usado
     * uma vez, mas ainda precisamos dele pra calcular o HMAC esperado. */
    memcpy(nonce_copy, sess->nonce, USB_PROTO_AUTH_NONCE_LEN);
    issued_at = sess->nonce_issued_at_ms;
    sess->nonce_pending = 0U; /* consumido - sucesso ou falha, nao da' pra tentar de novo */

    if ((now_ms - issued_at) > USB_PROTO_AUTH_CHALLENGE_TIMEOUT_MS) {
        return USB_PROTO_AUTH_ERR_EXPIRED;
    }

    memcpy(message, nonce_copy, USB_PROTO_AUTH_NONCE_LEN);
    memcpy(&message[USB_PROTO_AUTH_NONCE_LEN], serial, USB_PROTO_AUTH_SERIAL_LEN);
    HmacSha256_Compute(sess->dev_key, sizeof(sess->dev_key), message, sizeof(message), expected_hmac);

    if (constant_time_equal(expected_hmac, received_hmac, HMAC_SHA256_DIGEST_SIZE) == 0U) {
        return USB_PROTO_AUTH_ERR_HMAC_MISMATCH;
    }

    sess->role = USB_PROTO_ROLE_DEV;
    sess->last_activity_ms = now_ms;
    *out_role = sess->role;
    return USB_PROTO_AUTH_OK;
}

UsbProtoAuth_Status_t UsbProtoAuth_HandleLogout(UsbProtoAuthSession_t *sess,
                                                 UsbProtoRole_t *out_role)
{
    if (sess->role != USB_PROTO_ROLE_DEV) {
        *out_role = sess->role;
        return USB_PROTO_AUTH_ERR_WRONG_ROLE;
    }
    sess->role = USB_PROTO_ROLE_USER;
    *out_role = sess->role;
    return USB_PROTO_AUTH_OK;
}

void UsbProtoAuth_Touch(UsbProtoAuthSession_t *sess, uint32_t now_ms)
{
    if (sess->role == USB_PROTO_ROLE_DEV) {
        sess->last_activity_ms = now_ms;
    }
}

uint8_t UsbProtoAuth_CheckExpiry(UsbProtoAuthSession_t *sess, uint32_t now_ms)
{
    /* Diferenca sem sinal: correta mesmo com o HAL_GetTick() dando a volta. */
    if ((sess->role == USB_PROTO_ROLE_DEV) &&
        ((now_ms - sess->last_activity_ms) > USB_PROTO_AUTH_SESSION_TIMEOUT_MS)) {
        sess->role = USB_PROTO_ROLE_USER;
        return 1U;
    }
    return 0U;
}

void UsbProtoAuth_Reset(UsbProtoAuthSession_t *sess)
{
    sess->role = USB_PROTO_ROLE_USER;
    sess->nonce_pending = 0U;
}
