/* Teste de host de usb_proto_dispatch (sem HAL). Ver test/host/run.sh.
 * Vetor HMAC sintetico (key/nonce/serial/mac) e' o mesmo de
 * test_usb_proto_auth.c, confirmado com hmac.new(...) do Python. */
#include "usb_proto_dispatch.h"
#include "fw_version.h"
#include <string.h>
#include <stdio.h>

static int s_fail;
#define CHECK(c) do { if (!(c)) { printf("FALHA %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)

static const uint8_t SERIAL[USB_PROTO_AUTH_SERIAL_LEN] = {
    0xa0,0xa1,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xab
};
static const uint8_t KEY[USB_PROTO_AUTH_DEV_KEY_LEN] = {
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
    0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f
};
static const uint8_t NONCE_SRC[USB_PROTO_AUTH_NONCE_LEN] = {
    0x50,0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x5b,0x5c,0x5d,0x5e,0x5f
};
static const uint8_t VALID_MAC[USB_PROTO_AUTH_HMAC_LEN] = {
    0xc7,0xc0,0x9b,0x52,0xe6,0xf7,0x91,0x51,0xcf,0x8a,0x4f,0x8e,0xbc,0xc1,0x0d,0x26,
    0x0c,0xc7,0xb6,0x30,0xe5,0x2f,0x6d,0x4b,0x71,0xe0,0x3b,0x5f,0x74,0xbc,0x07,0x18
};

static size_t hello_len(void)
{
    return 1U + 1U + USB_PROTO_DEVICE_ID_FIELD_LEN + USB_PROTO_FW_VERSION_FIELD_LEN
           + 1U + USB_PROTO_AUTH_SERIAL_LEN;
}

static void test_hello_ok(void)
{
    UsbProtoDispatchCtx_t ctx = { .device_serial = SERIAL };
    uint8_t rsp[64];
    size_t len = UsbProtoDispatch_Handle(USB_PROTO_CMD_HELLO, NULL, 0U, &ctx, rsp, sizeof(rsp));
    size_t role_idx = 2U + USB_PROTO_DEVICE_ID_FIELD_LEN + USB_PROTO_FW_VERSION_FIELD_LEN;

    CHECK(len == hello_len());
    CHECK(rsp[0] == (uint8_t)USB_PROTO_RSP_OK);
    CHECK(memcmp(&rsp[2], DEVICE_ID, strlen(DEVICE_ID)) == 0);
    CHECK(memcmp(&rsp[2 + USB_PROTO_DEVICE_ID_FIELD_LEN], FIRMWARE_VERSION, strlen(FIRMWARE_VERSION)) == 0);
    CHECK(rsp[role_idx] == (uint8_t)USB_PROTO_ROLE_USER); /* sem ctx.auth -> default USER */
    CHECK(memcmp(&rsp[role_idx + 1U], SERIAL, sizeof(SERIAL)) == 0);
}

static void test_hello_without_serial_is_ocupado(void)
{
    uint8_t rsp[64];
    size_t len = UsbProtoDispatch_Handle(USB_PROTO_CMD_HELLO, NULL, 0U, NULL, rsp, sizeof(rsp));
    CHECK(len == 1U);
    CHECK(rsp[0] == (uint8_t)USB_PROTO_RSP_OCUPADO);
}

static void test_hello_rsp_cap_too_small(void)
{
    UsbProtoDispatchCtx_t ctx = { .device_serial = SERIAL };
    uint8_t rsp[4]; /* nao cabe o HELLO inteiro */
    size_t len = UsbProtoDispatch_Handle(USB_PROTO_CMD_HELLO, NULL, 0U, &ctx, rsp, sizeof(rsp));
    CHECK(len == 1U);
    CHECK(rsp[0] == (uint8_t)USB_PROTO_RSP_OCUPADO);
}

static void test_get_status_without_snapshot_is_ocupado(void)
{
    uint8_t rsp[64];
    size_t len = UsbProtoDispatch_Handle(USB_PROTO_CMD_GET_STATUS, NULL, 0U, NULL, rsp, sizeof(rsp));
    CHECK(len == 1U);
    CHECK(rsp[0] == (uint8_t)USB_PROTO_RSP_OCUPADO);
}

static void test_get_status_with_snapshot(void)
{
    UsbProtoStatusSnapshot_t snap = {
        .system_state = 2U, .pressure_mmhg = 123.5f, .setpoint_mmhg = 150.0f,
        .rtc_valid = 1U, .sd_mounted = 0U, .active_error = 0U, .cycle_number = 42U
    };
    UsbProtoDispatchCtx_t ctx = { .status = &snap };
    uint8_t rsp[64];
    size_t len = UsbProtoDispatch_Handle(USB_PROTO_CMD_GET_STATUS, NULL, 0U, &ctx, rsp, sizeof(rsp));

    CHECK(len == 17U);
    CHECK(rsp[0] == (uint8_t)USB_PROTO_RSP_OK);
    CHECK(rsp[1] == snap.system_state);

    float got_pressure;
    memcpy(&got_pressure, &rsp[2], sizeof(got_pressure));
    CHECK(got_pressure == snap.pressure_mmhg);
}

static void test_unknown_cmd(void)
{
    uint8_t rsp[8];
    size_t len = UsbProtoDispatch_Handle(0x7FFF, NULL, 0U, NULL, rsp, sizeof(rsp));
    CHECK(len == 1U);
    CHECK(rsp[0] == (uint8_t)USB_PROTO_RSP_CMD_DESCONHECIDO);
}

static void test_auth_challenge_without_session_ctx_is_ocupado(void)
{
    uint8_t rsp[32];
    size_t len = UsbProtoDispatch_Handle(USB_PROTO_CMD_AUTH_CHALLENGE, NULL, 0U, NULL, rsp, sizeof(rsp));
    CHECK(len == 1U);
    CHECK(rsp[0] == (uint8_t)USB_PROTO_RSP_OCUPADO);
}

static void test_auth_challenge_while_already_dev_is_sem_permissao(void)
{
    UsbProtoAuthSession_t sess;
    uint8_t nonce[USB_PROTO_AUTH_NONCE_LEN];
    UsbProtoRole_t role;
    UsbProtoDispatchCtx_t ctx;
    uint8_t rsp[32];
    size_t len;

    UsbProtoAuth_Init(&sess, KEY, 1U);
    UsbProtoAuth_HandleChallenge(&sess, 1000U, NONCE_SRC, nonce);
    UsbProtoAuth_HandleResponse(&sess, 1500U, SERIAL, VALID_MAC, &role);
    CHECK(role == USB_PROTO_ROLE_DEV);

    ctx.auth = &sess;
    ctx.challenge_random = NONCE_SRC;
    ctx.now_ms = 2000U;
    ctx.status = NULL;
    ctx.device_serial = SERIAL;
    len = UsbProtoDispatch_Handle(USB_PROTO_CMD_AUTH_CHALLENGE, NULL, 0U, &ctx, rsp, sizeof(rsp));
    CHECK(len == 1U);
    CHECK(rsp[0] == (uint8_t)USB_PROTO_RSP_SEM_PERMISSAO);
}

/* Fluxo completo: CHALLENGE devolve o nonce, RESPONSE com o HMAC certo
 * eleva a DEV (e HELLO passa a reportar role=DEV), LOGOUT volta a USER. */
static void test_full_auth_flow_via_dispatch(void)
{
    UsbProtoAuthSession_t sess;
    UsbProtoDispatchCtx_t ctx;
    uint8_t rsp[64];
    size_t len;

    UsbProtoAuth_Init(&sess, KEY, 1U);
    ctx.auth = &sess;
    ctx.device_serial = SERIAL;
    ctx.status = NULL;

    /* 1) CHALLENGE */
    ctx.challenge_random = NONCE_SRC;
    ctx.now_ms = 1000U;
    len = UsbProtoDispatch_Handle(USB_PROTO_CMD_AUTH_CHALLENGE, NULL, 0U, &ctx, rsp, sizeof(rsp));
    CHECK(len == (1U + USB_PROTO_AUTH_NONCE_LEN));
    CHECK(rsp[0] == (uint8_t)USB_PROTO_RSP_OK);
    CHECK(memcmp(&rsp[1], NONCE_SRC, USB_PROTO_AUTH_NONCE_LEN) == 0);

    /* 2) RESPONSE com o HMAC certo */
    ctx.now_ms = 1500U;
    len = UsbProtoDispatch_Handle(USB_PROTO_CMD_AUTH_RESPONSE, VALID_MAC, (uint16_t)sizeof(VALID_MAC),
                                   &ctx, rsp, sizeof(rsp));
    CHECK(len == 2U);
    CHECK(rsp[0] == (uint8_t)USB_PROTO_RSP_OK);
    CHECK(rsp[1] == (uint8_t)USB_PROTO_ROLE_DEV);
    CHECK(UsbProtoAuth_GetRole(&sess) == USB_PROTO_ROLE_DEV);

    /* 3) HELLO agora reporta role=DEV */
    len = UsbProtoDispatch_Handle(USB_PROTO_CMD_HELLO, NULL, 0U, &ctx, rsp, sizeof(rsp));
    CHECK(len == hello_len());
    CHECK(rsp[2U + USB_PROTO_DEVICE_ID_FIELD_LEN + USB_PROTO_FW_VERSION_FIELD_LEN] == (uint8_t)USB_PROTO_ROLE_DEV);

    /* 4) LOGOUT volta a USER */
    len = UsbProtoDispatch_Handle(USB_PROTO_CMD_AUTH_LOGOUT, NULL, 0U, &ctx, rsp, sizeof(rsp));
    CHECK(len == 2U);
    CHECK(rsp[0] == (uint8_t)USB_PROTO_RSP_OK);
    CHECK(rsp[1] == (uint8_t)USB_PROTO_ROLE_USER);
    CHECK(UsbProtoAuth_GetRole(&sess) == USB_PROTO_ROLE_USER);
}

static void test_auth_response_wrong_hmac_is_nao_autenticado(void)
{
    UsbProtoAuthSession_t sess;
    UsbProtoDispatchCtx_t ctx;
    uint8_t bad_mac[USB_PROTO_AUTH_HMAC_LEN];
    uint8_t rsp[64];
    size_t len;

    memcpy(bad_mac, VALID_MAC, sizeof(bad_mac));
    bad_mac[0] ^= 0xFFU;

    UsbProtoAuth_Init(&sess, KEY, 1U);
    ctx.auth = &sess;
    ctx.device_serial = SERIAL;
    ctx.status = NULL;
    ctx.challenge_random = NONCE_SRC;
    ctx.now_ms = 1000U;
    UsbProtoDispatch_Handle(USB_PROTO_CMD_AUTH_CHALLENGE, NULL, 0U, &ctx, rsp, sizeof(rsp));

    ctx.now_ms = 1500U;
    len = UsbProtoDispatch_Handle(USB_PROTO_CMD_AUTH_RESPONSE, bad_mac, (uint16_t)sizeof(bad_mac),
                                   &ctx, rsp, sizeof(rsp));
    CHECK(len == 2U);
    CHECK(rsp[0] == (uint8_t)USB_PROTO_RSP_NAO_AUTENTICADO);
    CHECK(rsp[1] == (uint8_t)USB_PROTO_ROLE_USER);
}

static void test_auth_response_payload_too_short_is_param_invalido(void)
{
    UsbProtoAuthSession_t sess;
    UsbProtoDispatchCtx_t ctx;
    uint8_t short_payload[4] = {0};
    uint8_t rsp[64];
    size_t len;

    UsbProtoAuth_Init(&sess, KEY, 1U);
    ctx.auth = &sess;
    ctx.device_serial = SERIAL;
    ctx.status = NULL;
    ctx.challenge_random = NONCE_SRC;
    ctx.now_ms = 1000U;
    UsbProtoDispatch_Handle(USB_PROTO_CMD_AUTH_CHALLENGE, NULL, 0U, &ctx, rsp, sizeof(rsp));

    len = UsbProtoDispatch_Handle(USB_PROTO_CMD_AUTH_RESPONSE, short_payload, (uint16_t)sizeof(short_payload),
                                   &ctx, rsp, sizeof(rsp));
    CHECK(len == 2U);
    CHECK(rsp[0] == (uint8_t)USB_PROTO_RSP_PARAM_INVALIDO);
}

static void test_auth_logout_without_dev_session_is_sem_permissao(void)
{
    UsbProtoAuthSession_t sess;
    UsbProtoDispatchCtx_t ctx = { .auth = &sess };
    uint8_t rsp[32];
    size_t len;

    UsbProtoAuth_Init(&sess, KEY, 1U);
    len = UsbProtoDispatch_Handle(USB_PROTO_CMD_AUTH_LOGOUT, NULL, 0U, &ctx, rsp, sizeof(rsp));
    CHECK(len == 2U);
    CHECK(rsp[0] == (uint8_t)USB_PROTO_RSP_SEM_PERMISSAO);
    CHECK(rsp[1] == (uint8_t)USB_PROTO_ROLE_USER);
}

int main(void)
{
    test_hello_ok();
    test_hello_without_serial_is_ocupado();
    test_hello_rsp_cap_too_small();
    test_get_status_without_snapshot_is_ocupado();
    test_get_status_with_snapshot();
    test_unknown_cmd();
    test_auth_challenge_without_session_ctx_is_ocupado();
    test_auth_challenge_while_already_dev_is_sem_permissao();
    test_full_auth_flow_via_dispatch();
    test_auth_response_wrong_hmac_is_nao_autenticado();
    test_auth_response_payload_too_short_is_param_invalido();
    test_auth_logout_without_dev_session_is_sem_permissao();
    printf(s_fail ? "usb_proto_dispatch: %d FALHA(S)\n" : "usb_proto_dispatch: OK\n", s_fail);
    return s_fail != 0;
}
