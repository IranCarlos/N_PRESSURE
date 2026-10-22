/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-14
 */
#include "usb_proto_dispatch.h"
#include "usb_proto_frame.h"
#include "fw_version.h"
#include <string.h>  /* memcpy: serializa float sem type punning */

static void write_u32_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
    p[2] = (uint8_t)((v >> 16) & 0xFFU);
    p[3] = (uint8_t)((v >> 24) & 0xFFU);
}

static void write_f32_le(uint8_t *p, float v)
{
    uint32_t u;
    memcpy(&u, &v, sizeof(u));
    write_u32_le(p, u);
}

/* Copia src (string C terminada em '\0') pra dst, truncando em field_len e
 * preenchendo o resto com 0x00. Nao escreve terminador se src ocupar o
 * campo inteiro - dispositivo e PC sabem o tamanho fixo de antemao. */
static void copy_padded_field(uint8_t *dst, size_t field_len, const char *src)
{
    size_t i = 0U;
    while ((i < field_len) && (src[i] != '\0')) {
        dst[i] = (uint8_t)src[i];
        i++;
    }
    while (i < field_len) {
        dst[i] = 0x00U;
        i++;
    }
}

static size_t handle_hello(const UsbProtoDispatchCtx_t *ctx, uint8_t *rsp_payload, size_t rsp_cap)
{
    size_t needed = 1U + 1U + USB_PROTO_DEVICE_ID_FIELD_LEN + USB_PROTO_FW_VERSION_FIELD_LEN
                    + 1U + USB_PROTO_AUTH_SERIAL_LEN;
    size_t idx;
    UsbProtoRole_t role = USB_PROTO_ROLE_USER;

    if ((ctx == NULL) || (ctx->device_serial == NULL)) {
        rsp_payload[0] = (uint8_t)USB_PROTO_RSP_OCUPADO;
        return 1U;
    }
    if (rsp_cap < needed) {
        rsp_payload[0] = (uint8_t)USB_PROTO_RSP_OCUPADO;
        return 1U;
    }
    if (ctx->auth != NULL) {
        role = UsbProtoAuth_GetRole(ctx->auth);
    }

    idx = 0U;
    rsp_payload[idx] = (uint8_t)USB_PROTO_RSP_OK;
    idx++;
    rsp_payload[idx] = (uint8_t)USB_PROTO_VERSION;
    idx++;
    copy_padded_field(&rsp_payload[idx], USB_PROTO_DEVICE_ID_FIELD_LEN, DEVICE_ID);
    idx += USB_PROTO_DEVICE_ID_FIELD_LEN;
    copy_padded_field(&rsp_payload[idx], USB_PROTO_FW_VERSION_FIELD_LEN, FIRMWARE_VERSION);
    idx += USB_PROTO_FW_VERSION_FIELD_LEN;
    rsp_payload[idx] = (uint8_t)role;
    idx++;
    memcpy(&rsp_payload[idx], ctx->device_serial, USB_PROTO_AUTH_SERIAL_LEN);
    idx += USB_PROTO_AUTH_SERIAL_LEN;
    return idx;
}

/* AUTH_CHALLENGE: so' em sessao USER (Apendice A). Resposta: status(1) +
 * nonce(16). ctx->challenge_random (16 bytes "aleatorios" injetados pelo
 * chamador) e ctx->now_ms sao obrigatorios - sem eles, OCUPADO. */
static size_t handle_auth_challenge(const UsbProtoDispatchCtx_t *ctx,
                                     uint8_t *rsp_payload, size_t rsp_cap)
{
    size_t needed = 1U + USB_PROTO_AUTH_NONCE_LEN;
    uint8_t nonce[USB_PROTO_AUTH_NONCE_LEN];

    if ((ctx == NULL) || (ctx->auth == NULL) || (ctx->challenge_random == NULL)) {
        rsp_payload[0] = (uint8_t)USB_PROTO_RSP_OCUPADO;
        return 1U;
    }
    if (UsbProtoAuth_GetRole(ctx->auth) != USB_PROTO_ROLE_USER) {
        rsp_payload[0] = (uint8_t)USB_PROTO_RSP_SEM_PERMISSAO;
        return 1U;
    }
    if (rsp_cap < needed) {
        rsp_payload[0] = (uint8_t)USB_PROTO_RSP_OCUPADO;
        return 1U;
    }

    UsbProtoAuth_HandleChallenge(ctx->auth, ctx->now_ms, ctx->challenge_random, nonce);
    rsp_payload[0] = (uint8_t)USB_PROTO_RSP_OK;
    memcpy(&rsp_payload[1], nonce, USB_PROTO_AUTH_NONCE_LEN);
    return needed;
}

/* AUTH_RESPONSE: so' em sessao USER. Requisicao: HMAC-SHA256(chave_dev,
 * nonce||serial) de 32 bytes (payload inteiro). Resposta: status(1) +
 * papel-depois(1), pro cliente confirmar a elevacao sem precisar de um
 * GET_STATUS extra. */
static size_t handle_auth_response(const UsbProtoDispatchCtx_t *ctx,
                                    const uint8_t *req_payload, uint16_t req_len,
                                    uint8_t *rsp_payload, size_t rsp_cap)
{
    size_t needed = 1U + 1U;
    UsbProtoRole_t role;
    UsbProtoAuth_Status_t st;

    if ((ctx == NULL) || (ctx->auth == NULL) || (ctx->device_serial == NULL)) {
        rsp_payload[0] = (uint8_t)USB_PROTO_RSP_OCUPADO;
        return 1U;
    }
    if (UsbProtoAuth_GetRole(ctx->auth) != USB_PROTO_ROLE_USER) {
        rsp_payload[0] = (uint8_t)USB_PROTO_RSP_SEM_PERMISSAO;
        return 1U;
    }
    if (rsp_cap < needed) {
        rsp_payload[0] = (uint8_t)USB_PROTO_RSP_OCUPADO;
        return 1U;
    }
    if ((req_payload == NULL) || (req_len < (uint16_t)USB_PROTO_AUTH_HMAC_LEN)) {
        rsp_payload[0] = (uint8_t)USB_PROTO_RSP_PARAM_INVALIDO;
        rsp_payload[1] = (uint8_t)UsbProtoAuth_GetRole(ctx->auth);
        return needed;
    }

    st = UsbProtoAuth_HandleResponse(ctx->auth, ctx->now_ms, ctx->device_serial, req_payload, &role);
    switch (st) {
        case USB_PROTO_AUTH_OK:
            rsp_payload[0] = (uint8_t)USB_PROTO_RSP_OK;
            break;
        case USB_PROTO_AUTH_ERR_NO_CHALLENGE:
        case USB_PROTO_AUTH_ERR_EXPIRED:
            rsp_payload[0] = (uint8_t)USB_PROTO_RSP_ESTADO_INVALIDO;
            break;
        case USB_PROTO_AUTH_ERR_NO_KEY:
        case USB_PROTO_AUTH_ERR_HMAC_MISMATCH:
        case USB_PROTO_AUTH_ERR_WRONG_ROLE:
        default:
            rsp_payload[0] = (uint8_t)USB_PROTO_RSP_NAO_AUTENTICADO;
            break;
    }
    rsp_payload[1] = (uint8_t)role;
    return needed;
}

/* AUTH_LOGOUT: so' em sessao DEV (Apendice A) - UsbProtoAuth_HandleLogout
 * ja' recusa (ERR_WRONG_ROLE) se a sessao nao for DEV, so' traduzimos pro
 * status do protocolo aqui. */
static size_t handle_auth_logout(const UsbProtoDispatchCtx_t *ctx,
                                  uint8_t *rsp_payload, size_t rsp_cap)
{
    size_t needed = 1U + 1U;
    UsbProtoRole_t role;
    UsbProtoAuth_Status_t st;

    if ((ctx == NULL) || (ctx->auth == NULL)) {
        rsp_payload[0] = (uint8_t)USB_PROTO_RSP_OCUPADO;
        return 1U;
    }
    if (rsp_cap < needed) {
        rsp_payload[0] = (uint8_t)USB_PROTO_RSP_OCUPADO;
        return 1U;
    }

    st = UsbProtoAuth_HandleLogout(ctx->auth, &role);
    rsp_payload[0] = (st == USB_PROTO_AUTH_OK) ? (uint8_t)USB_PROTO_RSP_OK : (uint8_t)USB_PROTO_RSP_SEM_PERMISSAO;
    rsp_payload[1] = (uint8_t)role;
    return needed;
}

static size_t handle_get_status(const UsbProtoDispatchCtx_t *ctx,
                                 uint8_t *rsp_payload, size_t rsp_cap)
{
    const UsbProtoStatusSnapshot_t *s;
    size_t needed = 1U + 1U + 4U + 4U + 1U + 1U + 1U + 4U;
    size_t idx;

    if ((ctx == NULL) || (ctx->status == NULL)) {
        rsp_payload[0] = (uint8_t)USB_PROTO_RSP_OCUPADO;
        return 1U;
    }
    s = ctx->status;

    if (rsp_cap < needed) {
        rsp_payload[0] = (uint8_t)USB_PROTO_RSP_OCUPADO;
        return 1U;
    }

    idx = 0U;
    rsp_payload[idx] = (uint8_t)USB_PROTO_RSP_OK;
    idx++;
    rsp_payload[idx] = s->system_state;
    idx++;
    write_f32_le(&rsp_payload[idx], s->pressure_mmhg);
    idx += 4U;
    write_f32_le(&rsp_payload[idx], s->setpoint_mmhg);
    idx += 4U;
    rsp_payload[idx] = s->rtc_valid;
    idx++;
    rsp_payload[idx] = s->sd_mounted;
    idx++;
    rsp_payload[idx] = s->active_error;
    idx++;
    write_u32_le(&rsp_payload[idx], s->cycle_number);
    idx += 4U;
    return idx;
}

/* ---- Fase 7.3: comandos de controle ----------------------------------- */

static uint16_t read_u16_le(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
}

static float read_f32_le(const uint8_t *p)
{
    uint32_t u = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    float v;
    memcpy(&v, &u, sizeof(v)); /* sem type punning */
    return v;
}

static uint8_t days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t k_days[12] = { 31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U };
    /* 2000..2099: bissexto = divisivel por 4 (2000 e' bissexto). */
    if ((month == 2U) && ((year % 4U) == 0U)) {
        return 29U;
    }
    return k_days[month - 1U];
}

static uint8_t datetime_is_valid(const UsbProtoDateTime_t *dt)
{
    if ((dt->year < 2000U) || (dt->year > 2099U) || (dt->month < 1U) || (dt->month > 12U) ||
        (dt->day < 1U) || (dt->hour > 23U) || (dt->minute > 59U) || (dt->second > 59U)) {
        return 0U;
    }
    return (dt->day <= days_in_month(dt->year, dt->month)) ? 1U : 0U;
}

/* Resposta de 1 byte (so' status) - todos os comandos da 7.3. */
static size_t rsp_status(uint8_t *rsp_payload, UsbProtoRspStatus_t st)
{
    rsp_payload[0] = (uint8_t)st;
    return 1U;
}

static size_t handle_set_setpoint(const UsbProtoDispatchCtx_t *ctx,
                                   const uint8_t *req_payload, uint16_t req_len,
                                   uint8_t *rsp_payload)
{
    if ((ctx == NULL) || (ctx->actions == NULL) || (ctx->actions->set_setpoint == NULL)) {
        return rsp_status(rsp_payload, USB_PROTO_RSP_OCUPADO);
    }
    if ((req_payload == NULL) || (req_len != (uint16_t)USB_PROTO_SET_SETPOINT_LEN)) {
        return rsp_status(rsp_payload, USB_PROTO_RSP_PARAM_INVALIDO);
    }
    return rsp_status(rsp_payload, ctx->actions->set_setpoint(read_f32_le(req_payload)));
}

static size_t handle_set_datetime(const UsbProtoDispatchCtx_t *ctx,
                                   const uint8_t *req_payload, uint16_t req_len,
                                   uint8_t *rsp_payload)
{
    UsbProtoDateTime_t dt;

    if ((ctx == NULL) || (ctx->actions == NULL) || (ctx->actions->set_datetime == NULL)) {
        return rsp_status(rsp_payload, USB_PROTO_RSP_OCUPADO);
    }
    if ((req_payload == NULL) || (req_len != (uint16_t)USB_PROTO_SET_DATETIME_LEN)) {
        return rsp_status(rsp_payload, USB_PROTO_RSP_PARAM_INVALIDO);
    }

    dt.year = read_u16_le(&req_payload[0]);
    dt.month = req_payload[2];
    dt.day = req_payload[3];
    dt.hour = req_payload[4];
    dt.minute = req_payload[5];
    dt.second = req_payload[6];
    if (datetime_is_valid(&dt) == 0U) {
        return rsp_status(rsp_payload, USB_PROTO_RSP_PARAM_INVALIDO);
    }
    return rsp_status(rsp_payload, ctx->actions->set_datetime(&dt));
}

static size_t handle_start_test(const UsbProtoDispatchCtx_t *ctx, uint16_t req_len, uint8_t *rsp_payload)
{
    if ((ctx == NULL) || (ctx->actions == NULL) || (ctx->actions->start_test == NULL)) {
        return rsp_status(rsp_payload, USB_PROTO_RSP_OCUPADO);
    }
    if (req_len != 0U) {
        return rsp_status(rsp_payload, USB_PROTO_RSP_PARAM_INVALIDO);
    }
    return rsp_status(rsp_payload, ctx->actions->start_test());
}

/* ABORT ignora o payload de proposito: um PC com bug que mande lixo no
 * payload ainda consegue desinflar ("desinflar sempre tem prioridade"). */
static size_t handle_abort_test(const UsbProtoDispatchCtx_t *ctx, uint8_t *rsp_payload)
{
    if ((ctx == NULL) || (ctx->actions == NULL) || (ctx->actions->abort_test == NULL)) {
        return rsp_status(rsp_payload, USB_PROTO_RSP_OCUPADO);
    }
    return rsp_status(rsp_payload, ctx->actions->abort_test());
}

static size_t handle_set_operator(const UsbProtoDispatchCtx_t *ctx,
                                   const uint8_t *req_payload, uint16_t req_len,
                                   uint8_t *rsp_payload)
{
    uint16_t op;
    uint16_t part;

    if ((ctx == NULL) || (ctx->actions == NULL) || (ctx->actions->set_operator == NULL)) {
        return rsp_status(rsp_payload, USB_PROTO_RSP_OCUPADO);
    }
    if ((req_payload == NULL) || (req_len != (uint16_t)USB_PROTO_SET_OPERATOR_LEN)) {
        return rsp_status(rsp_payload, USB_PROTO_RSP_PARAM_INVALIDO);
    }

    op = read_u16_le(&req_payload[0]);
    part = read_u16_le(&req_payload[2]);
    if ((op == 0U) || (op > USB_PROTO_SESSION_ID_MAX) || (part == 0U) || (part > USB_PROTO_SESSION_ID_MAX)) {
        return rsp_status(rsp_payload, USB_PROTO_RSP_FORA_DE_FAIXA);
    }
    return rsp_status(rsp_payload, ctx->actions->set_operator(op, part));
}

size_t UsbProtoDispatch_Handle(uint16_t cmd,
                                const uint8_t *req_payload, uint16_t req_len,
                                const UsbProtoDispatchCtx_t *ctx,
                                uint8_t *rsp_payload, size_t rsp_cap)
{
    if (rsp_cap < 1U) {
        return 0U;
    }

    switch (cmd) {
        case USB_PROTO_CMD_HELLO:
            return handle_hello(ctx, rsp_payload, rsp_cap);
        case USB_PROTO_CMD_GET_STATUS:
            return handle_get_status(ctx, rsp_payload, rsp_cap);
        case USB_PROTO_CMD_AUTH_CHALLENGE:
            return handle_auth_challenge(ctx, rsp_payload, rsp_cap);
        case USB_PROTO_CMD_AUTH_RESPONSE:
            return handle_auth_response(ctx, req_payload, req_len, rsp_payload, rsp_cap);
        case USB_PROTO_CMD_AUTH_LOGOUT:
            return handle_auth_logout(ctx, rsp_payload, rsp_cap);
        case USB_PROTO_CMD_SET_SETPOINT:
            return handle_set_setpoint(ctx, req_payload, req_len, rsp_payload);
        case USB_PROTO_CMD_SET_DATETIME:
            return handle_set_datetime(ctx, req_payload, req_len, rsp_payload);
        case USB_PROTO_CMD_START_TEST:
            return handle_start_test(ctx, req_len, rsp_payload);
        case USB_PROTO_CMD_ABORT_TEST:
            return handle_abort_test(ctx, rsp_payload);
        case USB_PROTO_CMD_SET_OPERATOR:
            return handle_set_operator(ctx, req_payload, req_len, rsp_payload);
        default:
            rsp_payload[0] = (uint8_t)USB_PROTO_RSP_CMD_DESCONHECIDO;
            return 1U;
    }
}
