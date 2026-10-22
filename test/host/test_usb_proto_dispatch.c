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

/* ---- Fase 7.3: comandos de controle com acoes falsas ------------------ */

static int s_calls;
static float s_last_sp;
static UsbProtoDateTime_t s_last_dt;
static uint16_t s_last_op;
static uint16_t s_last_part;
static UsbProtoRspStatus_t s_fake_result;

static UsbProtoRspStatus_t fake_set_setpoint(float sp) { s_calls++; s_last_sp = sp; return s_fake_result; }
static UsbProtoRspStatus_t fake_set_datetime(const UsbProtoDateTime_t *dt) { s_calls++; s_last_dt = *dt; return s_fake_result; }
static UsbProtoRspStatus_t fake_start(void) { s_calls++; return s_fake_result; }
static UsbProtoRspStatus_t fake_abort(void) { s_calls++; return s_fake_result; }
static UsbProtoRspStatus_t fake_set_operator(uint16_t op, uint16_t part)
{
    s_calls++;
    s_last_op = op;
    s_last_part = part;
    return s_fake_result;
}

static const UsbProtoActions_t FAKE_ACTIONS = {
    fake_set_setpoint, fake_set_datetime, fake_start, fake_abort, fake_set_operator
};

static void reset_fakes(UsbProtoRspStatus_t result)
{
    s_calls = 0;
    s_fake_result = result;
}

static UsbProtoRspStatus_t call(uint16_t cmd, const uint8_t *payload, uint16_t len, const UsbProtoActions_t *actions)
{
    UsbProtoDispatchCtx_t ctx = { .actions = actions };
    uint8_t rsp[8];
    size_t n = UsbProtoDispatch_Handle(cmd, payload, len, &ctx, rsp, sizeof(rsp));
    CHECK(n == 1U); /* todos os comandos da 7.3 respondem so' o status */
    return (UsbProtoRspStatus_t)rsp[0];
}

static void test_control_cmds_without_actions_are_ocupado(void)
{
    static const UsbProtoActions_t EMPTY = { 0 };
    const uint8_t sp[4] = { 0 };

    CHECK(call(USB_PROTO_CMD_START_TEST, NULL, 0U, NULL) == USB_PROTO_RSP_OCUPADO);
    CHECK(call(USB_PROTO_CMD_ABORT_TEST, NULL, 0U, NULL) == USB_PROTO_RSP_OCUPADO);
    CHECK(call(USB_PROTO_CMD_SET_SETPOINT, sp, 4U, &EMPTY) == USB_PROTO_RSP_OCUPADO);
}

static void test_set_setpoint_decodes_float_le(void)
{
    const uint8_t sp_120_5[4] = { 0x00, 0x00, 0xF1, 0x42 }; /* 120.5f = 0x42F10000 */

    reset_fakes(USB_PROTO_RSP_OK);
    CHECK(call(USB_PROTO_CMD_SET_SETPOINT, sp_120_5, 4U, &FAKE_ACTIONS) == USB_PROTO_RSP_OK);
    CHECK(s_calls == 1);
    CHECK(s_last_sp == 120.5f);

    /* status da acao (ex.: politica recusou) passa direto para o PC */
    reset_fakes(USB_PROTO_RSP_FORA_DE_FAIXA);
    CHECK(call(USB_PROTO_CMD_SET_SETPOINT, sp_120_5, 4U, &FAKE_ACTIONS) == USB_PROTO_RSP_FORA_DE_FAIXA);

    /* tamanho errado nao chega na acao */
    reset_fakes(USB_PROTO_RSP_OK);
    CHECK(call(USB_PROTO_CMD_SET_SETPOINT, sp_120_5, 3U, &FAKE_ACTIONS) == USB_PROTO_RSP_PARAM_INVALIDO);
    CHECK(call(USB_PROTO_CMD_SET_SETPOINT, NULL, 4U, &FAKE_ACTIONS) == USB_PROTO_RSP_PARAM_INVALIDO);
    CHECK(s_calls == 0);
}

static void test_set_datetime_validation(void)
{
    const uint8_t ok[7]      = { 0xEA, 0x07, 10, 22, 14, 5, 9 };  /* 22/10/2026 14:05:09 */
    const uint8_t leap[7]    = { 0xE8, 0x07, 2, 29, 0, 0, 0 };    /* 29/02/2024: bissexto */
    const uint8_t no_leap[7] = { 0xEA, 0x07, 2, 29, 0, 0, 0 };    /* 29/02/2026: invalido */
    const uint8_t bad_mon[7] = { 0xEA, 0x07, 13, 1, 0, 0, 0 };
    const uint8_t bad_hr[7]  = { 0xEA, 0x07, 1, 1, 24, 0, 0 };
    const uint8_t y1999[7]   = { 0xCF, 0x07, 1, 1, 0, 0, 0 };
    const uint8_t day31[7]   = { 0xEA, 0x07, 4, 31, 0, 0, 0 };   /* abril tem 30 */

    reset_fakes(USB_PROTO_RSP_OK);
    CHECK(call(USB_PROTO_CMD_SET_DATETIME, ok, 7U, &FAKE_ACTIONS) == USB_PROTO_RSP_OK);
    CHECK(s_calls == 1);
    CHECK((s_last_dt.year == 2026U) && (s_last_dt.month == 10U) && (s_last_dt.day == 22U));
    CHECK((s_last_dt.hour == 14U) && (s_last_dt.minute == 5U) && (s_last_dt.second == 9U));
    CHECK(call(USB_PROTO_CMD_SET_DATETIME, leap, 7U, &FAKE_ACTIONS) == USB_PROTO_RSP_OK);

    reset_fakes(USB_PROTO_RSP_OK);
    CHECK(call(USB_PROTO_CMD_SET_DATETIME, no_leap, 7U, &FAKE_ACTIONS) == USB_PROTO_RSP_PARAM_INVALIDO);
    CHECK(call(USB_PROTO_CMD_SET_DATETIME, bad_mon, 7U, &FAKE_ACTIONS) == USB_PROTO_RSP_PARAM_INVALIDO);
    CHECK(call(USB_PROTO_CMD_SET_DATETIME, bad_hr, 7U, &FAKE_ACTIONS) == USB_PROTO_RSP_PARAM_INVALIDO);
    CHECK(call(USB_PROTO_CMD_SET_DATETIME, y1999, 7U, &FAKE_ACTIONS) == USB_PROTO_RSP_PARAM_INVALIDO);
    CHECK(call(USB_PROTO_CMD_SET_DATETIME, day31, 7U, &FAKE_ACTIONS) == USB_PROTO_RSP_PARAM_INVALIDO);
    CHECK(call(USB_PROTO_CMD_SET_DATETIME, ok, 6U, &FAKE_ACTIONS) == USB_PROTO_RSP_PARAM_INVALIDO);
    CHECK(s_calls == 0);
}

static void test_start_and_abort(void)
{
    const uint8_t junk[3] = { 1, 2, 3 };

    reset_fakes(USB_PROTO_RSP_OK);
    CHECK(call(USB_PROTO_CMD_START_TEST, NULL, 0U, &FAKE_ACTIONS) == USB_PROTO_RSP_OK);
    CHECK(s_calls == 1);
    /* START com payload: recusado sem acionar nada */
    CHECK(call(USB_PROTO_CMD_START_TEST, junk, 3U, &FAKE_ACTIONS) == USB_PROTO_RSP_PARAM_INVALIDO);
    CHECK(s_calls == 1);
    /* ABORT ignora payload de proposito - desinflar sempre passa */
    CHECK(call(USB_PROTO_CMD_ABORT_TEST, junk, 3U, &FAKE_ACTIONS) == USB_PROTO_RSP_OK);
    CHECK(s_calls == 2);

    reset_fakes(USB_PROTO_RSP_ESTADO_INVALIDO);
    CHECK(call(USB_PROTO_CMD_START_TEST, NULL, 0U, &FAKE_ACTIONS) == USB_PROTO_RSP_ESTADO_INVALIDO);
}

static void test_set_operator(void)
{
    const uint8_t ok[4]       = { 0x03, 0x00, 0x25, 0x00 }; /* OP03, P037 */
    const uint8_t zero_op[4]  = { 0x00, 0x00, 0x25, 0x00 };
    const uint8_t big_part[4] = { 0x03, 0x00, 0x10, 0x27 }; /* 10000 > 9999 */

    reset_fakes(USB_PROTO_RSP_OK);
    CHECK(call(USB_PROTO_CMD_SET_OPERATOR, ok, 4U, &FAKE_ACTIONS) == USB_PROTO_RSP_OK);
    CHECK((s_last_op == 3U) && (s_last_part == 37U));

    reset_fakes(USB_PROTO_RSP_OK);
    CHECK(call(USB_PROTO_CMD_SET_OPERATOR, zero_op, 4U, &FAKE_ACTIONS) == USB_PROTO_RSP_FORA_DE_FAIXA);
    CHECK(call(USB_PROTO_CMD_SET_OPERATOR, big_part, 4U, &FAKE_ACTIONS) == USB_PROTO_RSP_FORA_DE_FAIXA);
    CHECK(call(USB_PROTO_CMD_SET_OPERATOR, ok, 2U, &FAKE_ACTIONS) == USB_PROTO_RSP_PARAM_INVALIDO);
    CHECK(s_calls == 0);
}

/* ---- Fase 7.4: STREAM e GET_LOG_TAIL ---------------------------------- */

static UsbTelemetry_t s_tel;

static size_t call_full(uint16_t cmd, const uint8_t *payload, uint16_t len,
                        UsbProtoDispatchCtx_t *ctx, uint8_t *rsp, size_t cap)
{
    return UsbProtoDispatch_Handle(cmd, payload, len, ctx, rsp, cap);
}

static void test_stream_rate_limits_by_role(void)
{
    UsbProtoAuthSession_t sess;
    UsbProtoDispatchCtx_t ctx = { .auth = &sess, .telemetry = &s_tel };
    const uint8_t r50[2] = { 50U, 0U };
    const uint8_t r51[2] = { 51U, 0U };
    const uint8_t r1000[2] = { 0xE8U, 0x03U };
    const uint8_t r0[2] = { 0U, 0U };
    uint8_t rsp[8];
    size_t n;

    UsbTelemetry_Init(&s_tel);
    UsbProtoAuth_Init(&sess, KEY, 1U);

    n = call_full(USB_PROTO_CMD_STREAM_START, r50, 2U, &ctx, rsp, sizeof(rsp));
    CHECK((n == 3U) && (rsp[0] == (uint8_t)USB_PROTO_RSP_OK));
    CHECK((rsp[1] | (rsp[2] << 8)) == 50);
    CHECK(UsbTelemetry_IsRunning(&s_tel) == 1U);

    n = call_full(USB_PROTO_CMD_STREAM_START, r51, 2U, &ctx, rsp, sizeof(rsp));
    CHECK((n == 1U) && (rsp[0] == (uint8_t)USB_PROTO_RSP_FORA_DE_FAIXA)); /* USER <= 50 Hz */
    n = call_full(USB_PROTO_CMD_STREAM_START, r0, 2U, &ctx, rsp, sizeof(rsp));
    CHECK((n == 1U) && (rsp[0] == (uint8_t)USB_PROTO_RSP_PARAM_INVALIDO));

    sess.role = USB_PROTO_ROLE_DEV; /* atalho: elevacao ja' coberta em test_usb_proto_auth */
    n = call_full(USB_PROTO_CMD_STREAM_START, r1000, 2U, &ctx, rsp, sizeof(rsp));
    CHECK((n == 3U) && (rsp[0] == (uint8_t)USB_PROTO_RSP_OK));
    CHECK((rsp[1] | (rsp[2] << 8)) == 1000);

    n = call_full(USB_PROTO_CMD_STREAM_STOP, NULL, 0U, &ctx, rsp, sizeof(rsp));
    CHECK((n == 1U) && (rsp[0] == (uint8_t)USB_PROTO_RSP_OK));
    CHECK(UsbTelemetry_IsRunning(&s_tel) == 0U);

    ctx.telemetry = NULL;
    n = call_full(USB_PROTO_CMD_STREAM_START, r50, 2U, &ctx, rsp, sizeof(rsp));
    CHECK((n == 1U) && (rsp[0] == (uint8_t)USB_PROTO_RSP_OCUPADO));
}

/* Log falso: 5 linhas, a de offset 1 e' de auditoria. */
static uint8_t fake_log(uint32_t off, UsbProtoLogEntry_t *out)
{
    if (off >= 5U) {
        return 0U;
    }
    memset(out, 0, sizeof(*out));
    out->seq = 100U - off; /* mais recente = 100 */
    out->year = 2026U;
    out->month = 10U;
    out->day = 22U;
    out->level = (off == 1U) ? USB_PROTO_LOG_LEVEL_AUDIT : USB_PROTO_LOG_LEVEL_EVT;
    if (off == 2U) {
        out->level = USB_PROTO_LOG_LEVEL_ERR;
        strcpy(out->code, "E003");
    }
    strcpy(out->message, "abc");
    return 1U;
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

#define LOG_ENTRY_LEN(msg_len) (4U + 2U + 5U + 1U + 5U + 1U + (msg_len))

static void test_log_tail_user_hides_audit(void)
{
    UsbProtoAuthSession_t sess;
    UsbProtoDispatchCtx_t ctx = { .auth = &sess, .get_log_entry = fake_log };
    const uint8_t req[2] = { 10U, 0U };
    uint8_t rsp[512];
    size_t n;

    UsbProtoAuth_Init(&sess, KEY, 1U);
    n = call_full(USB_PROTO_CMD_GET_LOG_TAIL, req, 2U, &ctx, rsp, sizeof(rsp));
    CHECK(rsp[0] == (uint8_t)USB_PROTO_RSP_OK);
    CHECK(rsp[1] == 4U);                                  /* 5 linhas - 1 de auditoria */
    CHECK(n == 2U + 4U * LOG_ENTRY_LEN(3U));
    CHECK(rd32(&rsp[2]) == 100U);                         /* mais recente primeiro */
    CHECK(rd32(&rsp[2 + LOG_ENTRY_LEN(3U)]) == 98U);      /* pulou a 99 (auditoria) */
    CHECK(memcmp(&rsp[2 + LOG_ENTRY_LEN(3U) + 12U], "E003", 4U) == 0); /* codigo da linha de erro */

    sess.role = USB_PROTO_ROLE_DEV;
    (void)call_full(USB_PROTO_CMD_GET_LOG_TAIL, req, 2U, &ctx, rsp, sizeof(rsp));
    CHECK(rsp[1] == 5U);                                  /* DEV ve a auditoria */
}

static void test_log_tail_paging_and_limits(void)
{
    UsbProtoDispatchCtx_t ctx = { .get_log_entry = fake_log };
    const uint8_t page2[2] = { 2U, 2U };  /* 2 linhas, pulando 2 visiveis */
    const uint8_t zero[2] = { 0U, 0U };
    const uint8_t too_many[2] = { (uint8_t)(USB_PROTO_LOG_TAIL_MAX + 1U), 0U };
    uint8_t rsp[512];
    uint8_t small[2U + LOG_ENTRY_LEN(3U) + 3U]; /* cabe so' 1 linha */

    (void)call_full(USB_PROTO_CMD_GET_LOG_TAIL, page2, 2U, &ctx, rsp, sizeof(rsp));
    CHECK(rsp[1] == 2U);
    CHECK(rd32(&rsp[2]) == 97U); /* visiveis (sem auth = USER): 100, 98, 97, 96 */

    CHECK(call_full(USB_PROTO_CMD_GET_LOG_TAIL, zero, 2U, &ctx, rsp, sizeof(rsp)) == 1U);
    CHECK(rsp[0] == (uint8_t)USB_PROTO_RSP_FORA_DE_FAIXA);
    (void)call_full(USB_PROTO_CMD_GET_LOG_TAIL, too_many, 2U, &ctx, rsp, sizeof(rsp));
    CHECK(rsp[0] == (uint8_t)USB_PROTO_RSP_FORA_DE_FAIXA);
    (void)call_full(USB_PROTO_CMD_GET_LOG_TAIL, zero, 1U, &ctx, rsp, sizeof(rsp));
    CHECK(rsp[0] == (uint8_t)USB_PROTO_RSP_PARAM_INVALIDO);

    (void)call_full(USB_PROTO_CMD_GET_LOG_TAIL, (const uint8_t[2]){ 10U, 0U }, 2U, &ctx, small, sizeof(small));
    CHECK(small[0] == (uint8_t)USB_PROTO_RSP_OK);
    CHECK(small[1] == 1U); /* parou quando nao cabia mais */
}

/* ---- Fase 7.5: FS_* com operacoes falsas ------------------------------ */

static char s_fs_last_path[128];
static uint32_t s_fs_last_offset;
static uint16_t s_fs_last_len;
static uint16_t s_fs_last_start;
static UsbProtoFs_Result_t s_fs_result;
static uint8_t s_fs_entries_avail; /* quantas entradas a listagem falsa tem */

static UsbProtoFs_Result_t ffs_list(const char *path, uint16_t start, UsbProtoFsEntry_t *out,
                                    uint8_t max, uint8_t *n, uint8_t *more)
{
    uint8_t i;
    strcpy(s_fs_last_path, path);
    s_fs_last_start = start;
    *n = 0U;
    *more = 0U;
    for (i = 0U; (i < max) && ((uint32_t)start + i < s_fs_entries_avail); i++) {
        memset(&out[i], 0, sizeof(out[i]));
        out[i].is_dir = (i == 0U) ? 1U : 0U;
        out[i].size = 1000U + i;
        out[i].fdate = 0x5B56U;
        out[i].ftime = 0x7000U;
        snprintf(out[i].name, sizeof(out[i].name), "T%08u.rec", (unsigned)(start + i));
        (*n)++;
    }
    *more = ((uint32_t)start + *n < s_fs_entries_avail) ? 1U : 0U;
    return s_fs_result;
}

static UsbProtoFs_Result_t ffs_stat(const char *path, UsbProtoFsEntry_t *out)
{
    strcpy(s_fs_last_path, path);
    memset(out, 0, sizeof(*out));
    out->size = 123456U;
    out->fdate = 0x5B56U;
    return s_fs_result;
}

static UsbProtoFs_Result_t ffs_read(const char *path, uint32_t offset, uint8_t *dst,
                                    uint16_t len, uint16_t *n_read)
{
    uint16_t i;
    uint16_t avail = (offset >= 2500U) ? 0U : (uint16_t)(2500U - offset); /* arquivo de 2500 B */
    strcpy(s_fs_last_path, path);
    s_fs_last_offset = offset;
    s_fs_last_len = len;
    *n_read = (len < avail) ? len : avail;
    for (i = 0U; i < *n_read; i++) {
        dst[i] = (uint8_t)((offset + i) & 0xFFU);
    }
    return s_fs_result;
}

static UsbProtoFs_Result_t ffs_hash(const char *path, uint32_t *size, uint32_t *crc32, uint8_t sha256[32])
{
    strcpy(s_fs_last_path, path);
    *size = 2500U;
    *crc32 = 0xCAFEBABEU;
    memset(sha256, 0xAB, 32U);
    return s_fs_result;
}

static const UsbProtoFsOps_t FAKE_FS = { ffs_list, ffs_stat, ffs_read, ffs_hash };

static size_t fs_call(uint16_t cmd, const uint8_t *req, uint16_t len, UsbProtoRole_t role, uint8_t *rsp, size_t cap)
{
    UsbProtoAuthSession_t sess;
    UsbProtoDispatchCtx_t ctx = { .auth = &sess, .fs = &FAKE_FS };
    UsbProtoAuth_Init(&sess, KEY, 1U);
    sess.role = role;
    return UsbProtoDispatch_Handle(cmd, req, len, &ctx, rsp, cap);
}

static void test_fs_list_paging(void)
{
    uint8_t req[2 + 6] = { 0U, 0U, '/', 't', 'e', 's', 't', 's' };
    uint8_t rsp[1024];
    size_t n;

    s_fs_result = USB_PROTO_FS_OK;
    s_fs_entries_avail = 20U;
    n = fs_call(USB_PROTO_CMD_FS_LIST, req, sizeof(req), USB_PROTO_ROLE_USER, rsp, sizeof(rsp));
    CHECK(rsp[0] == (uint8_t)USB_PROTO_RSP_OK);
    CHECK(rsp[1] == USB_PROTO_FS_LIST_MAX);
    CHECK(rsp[2] == 1U); /* mais = sim */
    CHECK(strcmp(s_fs_last_path, "/tests") == 0);
    CHECK(rsp[3] == 1U);                       /* 1a e' diretorio */
    CHECK(rd32(&rsp[4]) == 1000U);
    CHECK(rsp[12] == 13U);                     /* "T00000000.rec" */
    CHECK(memcmp(&rsp[13], "T00000000.rec", 13U) == 0);
    CHECK(n == 3U + USB_PROTO_FS_LIST_MAX * (10U + 13U));

    req[0] = 12U; /* 2a pagina */
    (void)fs_call(USB_PROTO_CMD_FS_LIST, req, sizeof(req), USB_PROTO_ROLE_USER, rsp, sizeof(rsp));
    CHECK(s_fs_last_start == 12U);
    CHECK((rsp[1] == 8U) && (rsp[2] == 0U));
}

static void test_fs_permissions_and_errors(void)
{
    const uint8_t audit[2 + 11] = { 0U, 0U, '/', 'l', 'o', 'g', 's', '/', 'a', 'u', 'd', 'i', 't' };
    const uint8_t bad[2 + 4] = { 0U, 0U, '/', '.', '.', '/' };
    const uint8_t stat_req[] = { '/', 't', 'e', 's', 't', 's', '/', 'x' };
    uint8_t rsp[64];

    s_fs_result = USB_PROTO_FS_OK;
    (void)fs_call(USB_PROTO_CMD_FS_LIST, audit, sizeof(audit), USB_PROTO_ROLE_USER, rsp, sizeof(rsp));
    CHECK(rsp[0] == (uint8_t)USB_PROTO_RSP_SEM_PERMISSAO);
    (void)fs_call(USB_PROTO_CMD_FS_LIST, audit, sizeof(audit), USB_PROTO_ROLE_DEV, rsp, sizeof(rsp));
    CHECK(rsp[0] == (uint8_t)USB_PROTO_RSP_OK);
    (void)fs_call(USB_PROTO_CMD_FS_LIST, bad, sizeof(bad), USB_PROTO_ROLE_DEV, rsp, sizeof(rsp));
    CHECK(rsp[0] == (uint8_t)USB_PROTO_RSP_PARAM_INVALIDO);

    s_fs_result = USB_PROTO_FS_NOT_FOUND;
    CHECK(fs_call(USB_PROTO_CMD_FS_STAT, stat_req, sizeof(stat_req), USB_PROTO_ROLE_USER, rsp, sizeof(rsp)) == 1U);
    CHECK(rsp[0] == (uint8_t)USB_PROTO_RSP_NAO_ENCONTRADO);
    s_fs_result = USB_PROTO_FS_NO_SD;
    (void)fs_call(USB_PROTO_CMD_FS_STAT, stat_req, sizeof(stat_req), USB_PROTO_ROLE_USER, rsp, sizeof(rsp));
    CHECK(rsp[0] == (uint8_t)USB_PROTO_RSP_ERRO_SD);
    s_fs_result = USB_PROTO_FS_BUSY;
    (void)fs_call(USB_PROTO_CMD_FS_STAT, stat_req, sizeof(stat_req), USB_PROTO_ROLE_USER, rsp, sizeof(rsp));
    CHECK(rsp[0] == (uint8_t)USB_PROTO_RSP_OCUPADO);

    s_fs_result = USB_PROTO_FS_OK;
    CHECK(fs_call(USB_PROTO_CMD_FS_STAT, stat_req, sizeof(stat_req), USB_PROTO_ROLE_USER, rsp, sizeof(rsp)) == 10U);
    CHECK(rd32(&rsp[2]) == 123456U);
}

static void test_fs_read_chunks_and_eof(void)
{
    uint8_t req[6 + 8] = { 0U, 0U, 0U, 0U, 0xF8U, 0x03U, '/', 't', 'e', 's', 't', 's', '/', 'x' }; /* 1016 B */
    uint8_t rsp[1024];
    size_t n;

    s_fs_result = USB_PROTO_FS_OK;
    n = fs_call(USB_PROTO_CMD_FS_READ, req, sizeof(req), USB_PROTO_ROLE_USER, rsp, sizeof(rsp));
    CHECK(n == 7U + 1016U);
    CHECK((rsp[5] | (rsp[6] << 8)) == 1016);
    CHECK(rsp[7] == 0U && rsp[7 + 255] == 255U && rsp[7 + 256] == 0U);

    req[0] = (uint8_t)(2460U & 0xFFU); /* offset 2460: faltam 40 B para o fim */
    req[1] = (uint8_t)(2460U >> 8);
    n = fs_call(USB_PROTO_CMD_FS_READ, req, sizeof(req), USB_PROTO_ROLE_USER, rsp, sizeof(rsp));
    CHECK(rd32(&rsp[1]) == 2460U);
    CHECK((rsp[5] | (rsp[6] << 8)) == 40); /* n < pedido = fim do arquivo */
    CHECK(n == 7U + 40U);

    req[4] = 0xF9U; /* 1017 > maximo */
    (void)fs_call(USB_PROTO_CMD_FS_READ, req, sizeof(req), USB_PROTO_ROLE_USER, rsp, sizeof(rsp));
    CHECK(rsp[0] == (uint8_t)USB_PROTO_RSP_FORA_DE_FAIXA);
    req[4] = 0U; req[5] = 0U;   /* 0 */
    (void)fs_call(USB_PROTO_CMD_FS_READ, req, sizeof(req), USB_PROTO_ROLE_USER, rsp, sizeof(rsp));
    CHECK(rsp[0] == (uint8_t)USB_PROTO_RSP_FORA_DE_FAIXA);
    (void)fs_call(USB_PROTO_CMD_FS_READ, req, 6U, USB_PROTO_ROLE_USER, rsp, sizeof(rsp)); /* sem caminho */
    CHECK(rsp[0] == (uint8_t)USB_PROTO_RSP_PARAM_INVALIDO);
}

static void test_fs_crc(void)
{
    const uint8_t req[] = { '/', 't', 'e', 's', 't', 's', '/', 'x' };
    uint8_t rsp[64];

    s_fs_result = USB_PROTO_FS_OK;
    CHECK(fs_call(USB_PROTO_CMD_FS_CRC, req, sizeof(req), USB_PROTO_ROLE_USER, rsp, sizeof(rsp)) == 41U);
    CHECK(rd32(&rsp[1]) == 2500U);
    CHECK(rd32(&rsp[5]) == 0xCAFEBABEU);
    CHECK((rsp[9] == 0xABU) && (rsp[40] == 0xABU));
}

static void test_fs_without_ops_is_ocupado(void)
{
    UsbProtoDispatchCtx_t ctx = { 0 };
    const uint8_t req[] = { '/' };
    uint8_t rsp[8];
    CHECK(UsbProtoDispatch_Handle(USB_PROTO_CMD_FS_STAT, req, 1U, &ctx, rsp, sizeof(rsp)) == 1U);
    CHECK(rsp[0] == (uint8_t)USB_PROTO_RSP_OCUPADO);
}

int main(void)
{
    test_fs_list_paging();
    test_fs_permissions_and_errors();
    test_fs_read_chunks_and_eof();
    test_fs_crc();
    test_fs_without_ops_is_ocupado();
    test_stream_rate_limits_by_role();
    test_log_tail_user_hides_audit();
    test_log_tail_paging_and_limits();
    test_control_cmds_without_actions_are_ocupado();
    test_set_setpoint_decodes_float_le();
    test_set_datetime_validation();
    test_start_and_abort();
    test_set_operator();
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
