/* Teste de host de usb_proto_auth (sem HAL). Ver test/host/run.sh.
 * Vetor HMAC sintetico (key/nonce/serial/mac) calculado e confirmado com
 * hmac.new(key, nonce+serial, hashlib.sha256) do Python antes de colar
 * aqui (ver mensagem da sessao). */
#include "usb_proto_auth.h"
#include <string.h>
#include <stdio.h>

static int s_fail;
#define CHECK(c) do { if (!(c)) { printf("FALHA %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)

static const uint8_t KEY[USB_PROTO_AUTH_DEV_KEY_LEN] = {
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
    0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f
};
static const uint8_t NONCE_SRC[USB_PROTO_AUTH_NONCE_LEN] = {
    0x50,0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x5b,0x5c,0x5d,0x5e,0x5f
};
static const uint8_t SERIAL[USB_PROTO_AUTH_SERIAL_LEN] = {
    0xa0,0xa1,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xab
};
static const uint8_t VALID_MAC[USB_PROTO_AUTH_HMAC_LEN] = {
    0xc7,0xc0,0x9b,0x52,0xe6,0xf7,0x91,0x51,0xcf,0x8a,0x4f,0x8e,0xbc,0xc1,0x0d,0x26,
    0x0c,0xc7,0xb6,0x30,0xe5,0x2f,0x6d,0x4b,0x71,0xe0,0x3b,0x5f,0x74,0xbc,0x07,0x18
};

static void test_challenge_then_valid_response_elevates_to_dev(void)
{
    UsbProtoAuthSession_t sess;
    uint8_t nonce[USB_PROTO_AUTH_NONCE_LEN];
    UsbProtoRole_t role;
    UsbProtoAuth_Status_t st;

    UsbProtoAuth_Init(&sess, KEY, 1U);
    CHECK(UsbProtoAuth_GetRole(&sess) == USB_PROTO_ROLE_USER);

    UsbProtoAuth_HandleChallenge(&sess, 1000U, NONCE_SRC, nonce);
    CHECK(memcmp(nonce, NONCE_SRC, sizeof(nonce)) == 0);

    st = UsbProtoAuth_HandleResponse(&sess, 1500U, SERIAL, VALID_MAC, &role);
    CHECK(st == USB_PROTO_AUTH_OK);
    CHECK(role == USB_PROTO_ROLE_DEV);
    CHECK(UsbProtoAuth_GetRole(&sess) == USB_PROTO_ROLE_DEV);
}

static void test_wrong_hmac_rejected(void)
{
    UsbProtoAuthSession_t sess;
    uint8_t nonce[USB_PROTO_AUTH_NONCE_LEN];
    uint8_t bad_mac[USB_PROTO_AUTH_HMAC_LEN];
    UsbProtoRole_t role;
    UsbProtoAuth_Status_t st;

    memcpy(bad_mac, VALID_MAC, sizeof(bad_mac));
    bad_mac[0] ^= 0xFFU;

    UsbProtoAuth_Init(&sess, KEY, 1U);
    UsbProtoAuth_HandleChallenge(&sess, 1000U, NONCE_SRC, nonce);
    st = UsbProtoAuth_HandleResponse(&sess, 1500U, SERIAL, bad_mac, &role);
    CHECK(st == USB_PROTO_AUTH_ERR_HMAC_MISMATCH);
    CHECK(role == USB_PROTO_ROLE_USER);
    CHECK(UsbProtoAuth_GetRole(&sess) == USB_PROTO_ROLE_USER);
}

static void test_expired_nonce_rejected(void)
{
    UsbProtoAuthSession_t sess;
    uint8_t nonce[USB_PROTO_AUTH_NONCE_LEN];
    UsbProtoRole_t role;
    UsbProtoAuth_Status_t st;

    UsbProtoAuth_Init(&sess, KEY, 1U);
    UsbProtoAuth_HandleChallenge(&sess, 1000U, NONCE_SRC, nonce);
    /* 1000 + USB_PROTO_AUTH_CHALLENGE_TIMEOUT_MS + 1 */
    st = UsbProtoAuth_HandleResponse(&sess, 1000U + USB_PROTO_AUTH_CHALLENGE_TIMEOUT_MS + 1U,
                                      SERIAL, VALID_MAC, &role);
    CHECK(st == USB_PROTO_AUTH_ERR_EXPIRED);
    CHECK(UsbProtoAuth_GetRole(&sess) == USB_PROTO_ROLE_USER);
}

static void test_nonce_single_use_cannot_replay(void)
{
    UsbProtoAuthSession_t sess;
    uint8_t nonce[USB_PROTO_AUTH_NONCE_LEN];
    UsbProtoRole_t role;
    UsbProtoAuth_Status_t st;

    UsbProtoAuth_Init(&sess, KEY, 1U);
    UsbProtoAuth_HandleChallenge(&sess, 1000U, NONCE_SRC, nonce);
    st = UsbProtoAuth_HandleResponse(&sess, 1500U, SERIAL, VALID_MAC, &role);
    CHECK(st == USB_PROTO_AUTH_OK);

    /* Logout e tenta reusar o MESMO mac/nonce sem um novo CHALLENGE. */
    UsbProtoAuth_HandleLogout(&sess, &role);
    st = UsbProtoAuth_HandleResponse(&sess, 1600U, SERIAL, VALID_MAC, &role);
    CHECK(st == USB_PROTO_AUTH_ERR_NO_CHALLENGE);
}

static void test_response_without_challenge_rejected(void)
{
    UsbProtoAuthSession_t sess;
    UsbProtoRole_t role;
    UsbProtoAuth_Status_t st;

    UsbProtoAuth_Init(&sess, KEY, 1U);
    st = UsbProtoAuth_HandleResponse(&sess, 1000U, SERIAL, VALID_MAC, &role);
    CHECK(st == USB_PROTO_AUTH_ERR_NO_CHALLENGE);
}

static void test_no_key_provisioned_always_fails(void)
{
    UsbProtoAuthSession_t sess;
    uint8_t nonce[USB_PROTO_AUTH_NONCE_LEN];
    UsbProtoRole_t role;
    UsbProtoAuth_Status_t st;

    UsbProtoAuth_Init(&sess, KEY, 0U); /* dev_key_valid = 0 */
    UsbProtoAuth_HandleChallenge(&sess, 1000U, NONCE_SRC, nonce);
    st = UsbProtoAuth_HandleResponse(&sess, 1500U, SERIAL, VALID_MAC, &role);
    CHECK(st == USB_PROTO_AUTH_ERR_NO_KEY);
    CHECK(UsbProtoAuth_GetRole(&sess) == USB_PROTO_ROLE_USER);
}

static void test_logout_without_dev_session_rejected(void)
{
    UsbProtoAuthSession_t sess;
    UsbProtoRole_t role;
    UsbProtoAuth_Status_t st;

    UsbProtoAuth_Init(&sess, KEY, 1U);
    st = UsbProtoAuth_HandleLogout(&sess, &role);
    CHECK(st == USB_PROTO_AUTH_ERR_WRONG_ROLE);
    CHECK(role == USB_PROTO_ROLE_USER);
}

static void test_logout_from_dev_returns_to_user(void)
{
    UsbProtoAuthSession_t sess;
    uint8_t nonce[USB_PROTO_AUTH_NONCE_LEN];
    UsbProtoRole_t role;
    UsbProtoAuth_Status_t st;

    UsbProtoAuth_Init(&sess, KEY, 1U);
    UsbProtoAuth_HandleChallenge(&sess, 1000U, NONCE_SRC, nonce);
    UsbProtoAuth_HandleResponse(&sess, 1500U, SERIAL, VALID_MAC, &role);
    CHECK(role == USB_PROTO_ROLE_DEV);

    st = UsbProtoAuth_HandleLogout(&sess, &role);
    CHECK(st == USB_PROTO_AUTH_OK);
    CHECK(role == USB_PROTO_ROLE_USER);
    CHECK(UsbProtoAuth_GetRole(&sess) == USB_PROTO_ROLE_USER);
}

static void test_new_challenge_invalidates_previous_pending_nonce(void)
{
    UsbProtoAuthSession_t sess;
    uint8_t nonce1[USB_PROTO_AUTH_NONCE_LEN];
    uint8_t nonce2[USB_PROTO_AUTH_NONCE_LEN];
    uint8_t other_random[USB_PROTO_AUTH_NONCE_LEN];
    UsbProtoRole_t role;
    UsbProtoAuth_Status_t st;

    memset(other_random, 0x7A, sizeof(other_random));

    UsbProtoAuth_Init(&sess, KEY, 1U);
    UsbProtoAuth_HandleChallenge(&sess, 1000U, NONCE_SRC, nonce1);
    UsbProtoAuth_HandleChallenge(&sess, 1100U, other_random, nonce2);
    CHECK(memcmp(nonce1, nonce2, sizeof(nonce1)) != 0);

    /* VALID_MAC foi calculado para NONCE_SRC, nao para o 2o challenge. */
    st = UsbProtoAuth_HandleResponse(&sess, 1200U, SERIAL, VALID_MAC, &role);
    CHECK(st == USB_PROTO_AUTH_ERR_HMAC_MISMATCH);
}

/* Fase 7.4: sessao DEV expira por inatividade; comandos renovam. */
static void elevate(UsbProtoAuthSession_t *sess, uint32_t at_ms)
{
    uint8_t nonce[USB_PROTO_AUTH_NONCE_LEN];
    UsbProtoRole_t role;

    UsbProtoAuth_Init(sess, KEY, 1U);
    UsbProtoAuth_HandleChallenge(sess, at_ms, NONCE_SRC, nonce);
    (void)UsbProtoAuth_HandleResponse(sess, at_ms, SERIAL, VALID_MAC, &role);
    CHECK(role == USB_PROTO_ROLE_DEV);
}

static void test_dev_session_expires_after_inactivity(void)
{
    UsbProtoAuthSession_t sess;

    elevate(&sess, 1000U);
    CHECK(UsbProtoAuth_CheckExpiry(&sess, 1000U + USB_PROTO_AUTH_SESSION_TIMEOUT_MS) == 0U);
    CHECK(UsbProtoAuth_GetRole(&sess) == USB_PROTO_ROLE_DEV);
    CHECK(UsbProtoAuth_CheckExpiry(&sess, 1000U + USB_PROTO_AUTH_SESSION_TIMEOUT_MS + 1U) == 1U);
    CHECK(UsbProtoAuth_GetRole(&sess) == USB_PROTO_ROLE_USER);
    /* avisa uma vez so' */
    CHECK(UsbProtoAuth_CheckExpiry(&sess, 1000U + 2U * USB_PROTO_AUTH_SESSION_TIMEOUT_MS) == 0U);
}

static void test_touch_renews_dev_session(void)
{
    UsbProtoAuthSession_t sess;
    uint32_t t = 1000U + USB_PROTO_AUTH_SESSION_TIMEOUT_MS - 5U;

    elevate(&sess, 1000U);
    UsbProtoAuth_Touch(&sess, t);
    CHECK(UsbProtoAuth_CheckExpiry(&sess, t + USB_PROTO_AUTH_SESSION_TIMEOUT_MS) == 0U);
    CHECK(UsbProtoAuth_GetRole(&sess) == USB_PROTO_ROLE_DEV);
}

static void test_expiry_with_tick_wraparound(void)
{
    UsbProtoAuthSession_t sess;

    elevate(&sess, 0xFFFFFF00U); /* HAL_GetTick() perto de dar a volta */
    CHECK(UsbProtoAuth_CheckExpiry(&sess, 0x00000100U) == 0U); /* so' 512 ms depois */
    CHECK(UsbProtoAuth_GetRole(&sess) == USB_PROTO_ROLE_DEV);
}

static void test_reset_drops_dev_and_pending_nonce(void)
{
    UsbProtoAuthSession_t sess;
    uint8_t nonce[USB_PROTO_AUTH_NONCE_LEN];
    UsbProtoRole_t role;

    elevate(&sess, 1000U);
    UsbProtoAuth_Reset(&sess);
    CHECK(UsbProtoAuth_GetRole(&sess) == USB_PROTO_ROLE_USER);

    UsbProtoAuth_HandleChallenge(&sess, 2000U, NONCE_SRC, nonce);
    UsbProtoAuth_Reset(&sess); /* desconectou com challenge pendente */
    CHECK(UsbProtoAuth_HandleResponse(&sess, 2100U, SERIAL, VALID_MAC, &role) == USB_PROTO_AUTH_ERR_NO_CHALLENGE);
}

int main(void)
{
    test_dev_session_expires_after_inactivity();
    test_touch_renews_dev_session();
    test_expiry_with_tick_wraparound();
    test_reset_drops_dev_and_pending_nonce();
    test_challenge_then_valid_response_elevates_to_dev();
    test_wrong_hmac_rejected();
    test_expired_nonce_rejected();
    test_nonce_single_use_cannot_replay();
    test_response_without_challenge_rejected();
    test_no_key_provisioned_always_fails();
    test_logout_without_dev_session_rejected();
    test_logout_from_dev_returns_to_user();
    test_new_challenge_invalidates_previous_pending_nonce();
    printf(s_fail ? "usb_proto_auth: %d FALHA(S)\n" : "usb_proto_auth: OK\n", s_fail);
    return s_fail != 0;
}
