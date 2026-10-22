/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-14
 */
#include "usb_proto_dispatch.h"
#include "usb_proto_frame.h"
#include "fw_version.h"
#include "usb_proto_fs.h"
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
    {
        uint8_t warn = 0U;
        UsbProtoRspStatus_t st = ctx->actions->set_setpoint(read_f32_le(req_payload), &warn);
        if (st != USB_PROTO_RSP_OK) {
            return rsp_status(rsp_payload, st);
        }
        /* Fase 7.6: aceito + alerta (1 = acima do limiar de margem reduzida;
         * o app do PC deve avisar o operador, como a IHM faz). */
        rsp_payload[0] = (uint8_t)USB_PROTO_RSP_OK;
        rsp_payload[1] = warn;
        return 2U;
    }
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

/* ---- Fase 7.4: telemetria e log ---------------------------------------- */

static UsbProtoRole_t ctx_role(const UsbProtoDispatchCtx_t *ctx)
{
    return ((ctx != NULL) && (ctx->auth != NULL)) ? UsbProtoAuth_GetRole(ctx->auth) : USB_PROTO_ROLE_USER;
}

static size_t handle_stream_start(const UsbProtoDispatchCtx_t *ctx,
                                   const uint8_t *req_payload, uint16_t req_len,
                                   uint8_t *rsp_payload, size_t rsp_cap)
{
    uint16_t rate;
    uint32_t max_rate;
    uint32_t divisor;
    uint16_t effective;

    if ((ctx == NULL) || (ctx->telemetry == NULL) || (rsp_cap < 3U)) {
        return rsp_status(rsp_payload, USB_PROTO_RSP_OCUPADO);
    }
    if ((req_payload == NULL) || (req_len != 2U)) {
        return rsp_status(rsp_payload, USB_PROTO_RSP_PARAM_INVALIDO);
    }
    rate = read_u16_le(req_payload);
    max_rate = (ctx_role(ctx) == USB_PROTO_ROLE_DEV) ? USB_TELEMETRY_MAX_RATE_DEV_HZ
                                                     : USB_TELEMETRY_MAX_RATE_USER_HZ;
    if (rate == 0U) {
        return rsp_status(rsp_payload, USB_PROTO_RSP_PARAM_INVALIDO);
    }
    if ((uint32_t)rate > max_rate) {
        return rsp_status(rsp_payload, USB_PROTO_RSP_FORA_DE_FAIXA);
    }

    divisor = UsbTelemetry_DivisorForRate(rate);
    UsbTelemetry_Start(ctx->telemetry, divisor);
    effective = (uint16_t)(USB_TELEMETRY_TICK_HZ / divisor);
    rsp_payload[0] = (uint8_t)USB_PROTO_RSP_OK;
    rsp_payload[1] = (uint8_t)(effective & 0xFFU);
    rsp_payload[2] = (uint8_t)((effective >> 8) & 0xFFU);
    return 3U;
}

static size_t handle_stream_stop(const UsbProtoDispatchCtx_t *ctx, uint8_t *rsp_payload)
{
    if ((ctx == NULL) || (ctx->telemetry == NULL)) {
        return rsp_status(rsp_payload, USB_PROTO_RSP_OCUPADO);
    }
    UsbTelemetry_Stop(ctx->telemetry);
    return rsp_status(rsp_payload, USB_PROTO_RSP_OK);
}

/* strnlen() e' POSIX, nao C11 - equivalente local. */
static size_t bounded_strlen(const char *s, size_t max_len)
{
    size_t n = 0U;
    while ((n < max_len) && (s[n] != '\0')) {
        n++;
    }
    return n;
}

static size_t log_entry_wire_len(const UsbProtoLogEntry_t *e)
{
    return 4U + 2U + 5U + 1U + USB_PROTO_LOG_CODE_LEN + 1U + bounded_strlen(e->message, USB_PROTO_LOG_MSG_MAX);
}

static size_t put_log_entry(uint8_t *p, const UsbProtoLogEntry_t *e)
{
    size_t msg_len = bounded_strlen(e->message, USB_PROTO_LOG_MSG_MAX);
    size_t idx = 0U;

    write_u32_le(&p[idx], e->seq);
    idx += 4U;
    p[idx] = (uint8_t)(e->year & 0xFFU);
    p[idx + 1U] = (uint8_t)((e->year >> 8) & 0xFFU);
    idx += 2U;
    p[idx] = e->month;
    p[idx + 1U] = e->day;
    p[idx + 2U] = e->hour;
    p[idx + 3U] = e->minute;
    p[idx + 4U] = e->second;
    idx += 5U;
    p[idx] = e->level;
    idx++;
    copy_padded_field(&p[idx], USB_PROTO_LOG_CODE_LEN, e->code);
    idx += USB_PROTO_LOG_CODE_LEN;
    p[idx] = (uint8_t)msg_len;
    idx++;
    memcpy(&p[idx], e->message, msg_len);
    idx += msg_len;
    return idx;
}

static size_t handle_get_log_tail(const UsbProtoDispatchCtx_t *ctx,
                                   const uint8_t *req_payload, uint16_t req_len,
                                   uint8_t *rsp_payload, size_t rsp_cap)
{
    UsbProtoLogEntry_t e;
    uint8_t max_count;
    uint8_t skip;
    uint8_t count = 0U;
    uint8_t skipped = 0U;
    uint8_t include_audit;
    uint32_t offset;
    size_t idx = 2U;

    if ((ctx == NULL) || (ctx->get_log_entry == NULL) || (rsp_cap < 2U)) {
        return rsp_status(rsp_payload, USB_PROTO_RSP_OCUPADO);
    }
    if ((req_payload == NULL) || (req_len != 2U)) {
        return rsp_status(rsp_payload, USB_PROTO_RSP_PARAM_INVALIDO);
    }
    max_count = req_payload[0];
    skip = req_payload[1];
    if ((max_count == 0U) || (max_count > USB_PROTO_LOG_TAIL_MAX)) {
        return rsp_status(rsp_payload, USB_PROTO_RSP_FORA_DE_FAIXA);
    }
    include_audit = (ctx_role(ctx) == USB_PROTO_ROLE_DEV) ? 1U : 0U;

    /* Percorre da mais recente para a mais antiga ate' acabar o log, juntar
     * max_count linhas ou nao caber mais nenhuma no payload. */
    for (offset = 0U; (count < max_count) && (ctx->get_log_entry(offset, &e) != 0U); offset++) {
        if ((e.level == USB_PROTO_LOG_LEVEL_AUDIT) && (include_audit == 0U)) {
            continue;
        }
        if (skipped < skip) {
            skipped++;
            continue;
        }
        if ((idx + log_entry_wire_len(&e)) > rsp_cap) {
            break;
        }
        idx += put_log_entry(&rsp_payload[idx], &e);
        count++;
    }

    rsp_payload[0] = (uint8_t)USB_PROTO_RSP_OK;
    rsp_payload[1] = count;
    return idx;
}

/* ---- Fase 7.5: leitura do cartao SD ----------------------------------- */

static void write_u16_le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
}

static UsbProtoRspStatus_t fs_result_to_rsp(UsbProtoFs_Result_t r)
{
    switch (r) {
        case USB_PROTO_FS_OK:        return USB_PROTO_RSP_OK;
        case USB_PROTO_FS_NOT_FOUND: return USB_PROTO_RSP_NAO_ENCONTRADO;
        case USB_PROTO_FS_BUSY:      return USB_PROTO_RSP_OCUPADO;
        case USB_PROTO_FS_NO_SD:
        case USB_PROTO_FS_IO_ERROR:
        default:                     return USB_PROTO_RSP_ERRO_SD;
    }
}

/* Valida o caminho que ocupa payload[skip..len). Devolve OK e preenche
 * `path`, ou o status de recusa (PARAM_INVALIDO / SEM_PERMISSAO). */
static UsbProtoRspStatus_t fs_take_path(const UsbProtoDispatchCtx_t *ctx, const uint8_t *req_payload,
                                        uint16_t req_len, uint16_t skip, uint8_t for_listing, char *path)
{
    UsbProtoFs_PathCheck_t chk;

    if ((req_payload == NULL) || (req_len <= skip)) {
        return USB_PROTO_RSP_PARAM_INVALIDO;
    }
    chk = UsbProtoFs_CheckPath(&req_payload[skip], (size_t)(req_len - skip), ctx_role(ctx), for_listing, path);
    if (chk == USB_PROTO_FS_PATH_DENIED) {
        return USB_PROTO_RSP_SEM_PERMISSAO;
    }
    return (chk == USB_PROTO_FS_PATH_OK) ? USB_PROTO_RSP_OK : USB_PROTO_RSP_PARAM_INVALIDO;
}

static uint8_t fs_ops_ok(const UsbProtoDispatchCtx_t *ctx)
{
    return ((ctx != NULL) && (ctx->fs != NULL) && (ctx->fs->list != NULL) && (ctx->fs->stat != NULL) &&
            (ctx->fs->read != NULL) && (ctx->fs->hash != NULL)) ? 1U : 0U;
}

static size_t handle_fs_list(const UsbProtoDispatchCtx_t *ctx, const uint8_t *req_payload,
                             uint16_t req_len, uint8_t *rsp_payload, size_t rsp_cap)
{
    /* static: 12 x ~76 B nao cabem com folga na pilha de 2 KB da usbProtoTask;
     * o dispatch so' roda nessa tarefa. */
    static UsbProtoFsEntry_t s_entries[USB_PROTO_FS_LIST_MAX];
    char path[USB_PROTO_FS_PATH_MAX + 1U];
    UsbProtoRspStatus_t st;
    UsbProtoFs_Result_t r;
    uint8_t n = 0U;
    uint8_t more = 0U;
    uint8_t i;
    size_t idx = 3U;

    if ((fs_ops_ok(ctx) == 0U) || (rsp_cap < 3U)) {
        return rsp_status(rsp_payload, USB_PROTO_RSP_OCUPADO);
    }
    st = fs_take_path(ctx, req_payload, req_len, 2U, 1U, path);
    if (st != USB_PROTO_RSP_OK) {
        return rsp_status(rsp_payload, st);
    }

    r = ctx->fs->list(path, read_u16_le(req_payload), s_entries, (uint8_t)USB_PROTO_FS_LIST_MAX, &n, &more);
    if (r != USB_PROTO_FS_OK) {
        return rsp_status(rsp_payload, fs_result_to_rsp(r));
    }
    if (n > (uint8_t)USB_PROTO_FS_LIST_MAX) {
        n = (uint8_t)USB_PROTO_FS_LIST_MAX;
    }

    for (i = 0U; i < n; i++) {
        size_t name_len = bounded_strlen(s_entries[i].name, USB_PROTO_FS_NAME_MAX);
        if ((idx + 10U + name_len) > rsp_cap) {
            more = 1U; /* nao coube: o PC pede a proxima pagina a partir daqui */
            break;
        }
        rsp_payload[idx] = s_entries[i].is_dir;
        write_u32_le(&rsp_payload[idx + 1U], s_entries[i].size);
        write_u16_le(&rsp_payload[idx + 5U], s_entries[i].fdate);
        write_u16_le(&rsp_payload[idx + 7U], s_entries[i].ftime);
        rsp_payload[idx + 9U] = (uint8_t)name_len;
        memcpy(&rsp_payload[idx + 10U], s_entries[i].name, name_len);
        idx += 10U + name_len;
    }

    rsp_payload[0] = (uint8_t)USB_PROTO_RSP_OK;
    rsp_payload[1] = i;
    rsp_payload[2] = more;
    return idx;
}

static size_t handle_fs_stat(const UsbProtoDispatchCtx_t *ctx, const uint8_t *req_payload,
                             uint16_t req_len, uint8_t *rsp_payload, size_t rsp_cap)
{
    char path[USB_PROTO_FS_PATH_MAX + 1U];
    UsbProtoFsEntry_t e;
    UsbProtoRspStatus_t st;
    UsbProtoFs_Result_t r;

    if ((fs_ops_ok(ctx) == 0U) || (rsp_cap < 10U)) {
        return rsp_status(rsp_payload, USB_PROTO_RSP_OCUPADO);
    }
    st = fs_take_path(ctx, req_payload, req_len, 0U, 0U, path);
    if (st != USB_PROTO_RSP_OK) {
        return rsp_status(rsp_payload, st);
    }
    r = ctx->fs->stat(path, &e);
    if (r != USB_PROTO_FS_OK) {
        return rsp_status(rsp_payload, fs_result_to_rsp(r));
    }
    rsp_payload[0] = (uint8_t)USB_PROTO_RSP_OK;
    rsp_payload[1] = e.is_dir;
    write_u32_le(&rsp_payload[2], e.size);
    write_u16_le(&rsp_payload[6], e.fdate);
    write_u16_le(&rsp_payload[8], e.ftime);
    return 10U;
}

static size_t handle_fs_read(const UsbProtoDispatchCtx_t *ctx, const uint8_t *req_payload,
                             uint16_t req_len, uint8_t *rsp_payload, size_t rsp_cap)
{
    char path[USB_PROTO_FS_PATH_MAX + 1U];
    UsbProtoRspStatus_t st;
    UsbProtoFs_Result_t r;
    uint32_t offset;
    uint16_t len;
    uint16_t n_read = 0U;

    if ((fs_ops_ok(ctx) == 0U) || (rsp_cap < 7U)) {
        return rsp_status(rsp_payload, USB_PROTO_RSP_OCUPADO);
    }
    if ((req_payload == NULL) || (req_len < 7U)) {
        return rsp_status(rsp_payload, USB_PROTO_RSP_PARAM_INVALIDO);
    }
    offset = (uint32_t)req_payload[0] | ((uint32_t)req_payload[1] << 8) |
             ((uint32_t)req_payload[2] << 16) | ((uint32_t)req_payload[3] << 24);
    len = read_u16_le(&req_payload[4]);
    if ((len == 0U) || (len > USB_PROTO_FS_READ_MAX)) {
        return rsp_status(rsp_payload, USB_PROTO_RSP_FORA_DE_FAIXA);
    }
    if ((7U + (size_t)len) > rsp_cap) {
        len = (uint16_t)(rsp_cap - 7U);
    }
    st = fs_take_path(ctx, req_payload, req_len, 6U, 0U, path);
    if (st != USB_PROTO_RSP_OK) {
        return rsp_status(rsp_payload, st);
    }

    /* Le direto para dentro do payload da resposta (sem buffer extra). */
    r = ctx->fs->read(path, offset, &rsp_payload[7], len, &n_read);
    if (r != USB_PROTO_FS_OK) {
        return rsp_status(rsp_payload, fs_result_to_rsp(r));
    }
    if (n_read > len) {
        n_read = len;
    }
    rsp_payload[0] = (uint8_t)USB_PROTO_RSP_OK;
    write_u32_le(&rsp_payload[1], offset);
    write_u16_le(&rsp_payload[5], n_read);
    return 7U + (size_t)n_read;
}

static size_t handle_fs_crc(const UsbProtoDispatchCtx_t *ctx, const uint8_t *req_payload,
                            uint16_t req_len, uint8_t *rsp_payload, size_t rsp_cap)
{
    char path[USB_PROTO_FS_PATH_MAX + 1U];
    UsbProtoRspStatus_t st;
    UsbProtoFs_Result_t r;
    uint32_t size = 0U;
    uint32_t crc = 0U;

    if ((fs_ops_ok(ctx) == 0U) || (rsp_cap < 41U)) {
        return rsp_status(rsp_payload, USB_PROTO_RSP_OCUPADO);
    }
    st = fs_take_path(ctx, req_payload, req_len, 0U, 0U, path);
    if (st != USB_PROTO_RSP_OK) {
        return rsp_status(rsp_payload, st);
    }
    r = ctx->fs->hash(path, &size, &crc, &rsp_payload[9]);
    if (r != USB_PROTO_FS_OK) {
        return rsp_status(rsp_payload, fs_result_to_rsp(r));
    }
    rsp_payload[0] = (uint8_t)USB_PROTO_RSP_OK;
    write_u32_le(&rsp_payload[1], size);
    write_u32_le(&rsp_payload[5], crc);
    return 41U;
}

/* ---- Fase 7.6: PARAM_* (so' DEV) ----------------------------------------- */

/* Comum a todos os PARAM_*: sessao DEV e tabela disponivel. Devolve 0 e
 * escreve o status de recusa em rsp_payload[0]; 1 se pode seguir. */
static uint8_t param_gate(const UsbProtoDispatchCtx_t *ctx, uint8_t *rsp_payload)
{
    if (ctx_role(ctx) != USB_PROTO_ROLE_DEV) {
        rsp_payload[0] = (uint8_t)USB_PROTO_RSP_SEM_PERMISSAO;
        return 0U;
    }
    if (ctx->params == NULL) {
        rsp_payload[0] = (uint8_t)USB_PROTO_RSP_OCUPADO;
        return 0U;
    }
    return 1U;
}

static size_t bounded_len(const char *s, size_t max_len)
{
    size_t n = 0U;
    if (s == NULL) {
        return 0U;
    }
    while ((n < max_len) && (s[n] != '\0')) {
        n++;
    }
    return n;
}

static size_t handle_param_list(const UsbProtoDispatchCtx_t *ctx,
                                const uint8_t *req_payload, uint16_t req_len,
                                uint8_t *rsp_payload, size_t rsp_cap)
{
    const UsbProtoParamOps_t *ops;
    uint16_t start;
    uint16_t total;
    uint16_t i;
    uint8_t n = 0U;
    size_t idx;

    if (param_gate(ctx, rsp_payload) == 0U) {
        return 1U;
    }
    ops = ctx->params;
    if ((req_payload == NULL) || (req_len != 2U) || (rsp_cap < 5U)) {
        rsp_payload[0] = (uint8_t)USB_PROTO_RSP_PARAM_INVALIDO;
        return 1U;
    }
    start = read_u16_le(req_payload);
    total = ops->count();

    idx = 5U;
    for (i = start; (i < total) && (n < USB_PROTO_PARAM_LIST_MAX); i++) {
        UsbProtoParamInfo_t p;
        size_t name_len;
        size_t unit_len;
        size_t need;

        if (ops->info_at(i, &p) == 0U) {
            break;
        }
        name_len = bounded_len(p.name, USB_PROTO_PARAM_NAME_MAX);
        unit_len = bounded_len(p.unit, USB_PROTO_PARAM_UNIT_MAX);
        need = 20U + 1U + name_len + 1U + unit_len;
        if ((idx + need) > rsp_cap) {
            break; /* resto vem na proxima pagina */
        }
        write_u16_le(&rsp_payload[idx], p.id);
        rsp_payload[idx + 2U] = p.type;
        rsp_payload[idx + 3U] = p.group;
        write_f32_le(&rsp_payload[idx + 4U], p.min);
        write_f32_le(&rsp_payload[idx + 8U], p.max);
        write_f32_le(&rsp_payload[idx + 12U], p.def);
        write_f32_le(&rsp_payload[idx + 16U], p.value);
        idx += 20U;
        rsp_payload[idx] = (uint8_t)name_len;
        memcpy(&rsp_payload[idx + 1U], p.name, name_len);
        idx += 1U + name_len;
        rsp_payload[idx] = (uint8_t)unit_len;
        memcpy(&rsp_payload[idx + 1U], p.unit, unit_len);
        idx += 1U + unit_len;
        n++;
    }

    rsp_payload[0] = (uint8_t)USB_PROTO_RSP_OK;
    write_u16_le(&rsp_payload[1], total);
    rsp_payload[3] = ops->is_dirty();
    rsp_payload[4] = n;
    return idx;
}

static size_t handle_param_get(const UsbProtoDispatchCtx_t *ctx,
                               const uint8_t *req_payload, uint16_t req_len,
                               uint8_t *rsp_payload, size_t rsp_cap)
{
    uint16_t id;
    float value = 0.0f;
    uint8_t st;

    if (param_gate(ctx, rsp_payload) == 0U) {
        return 1U;
    }
    if ((req_payload == NULL) || (req_len != 2U) || (rsp_cap < 7U)) {
        rsp_payload[0] = (uint8_t)USB_PROTO_RSP_PARAM_INVALIDO;
        return 1U;
    }
    id = read_u16_le(req_payload);
    st = ctx->params->get(id, &value);
    rsp_payload[0] = st;
    if (st != (uint8_t)USB_PROTO_RSP_OK) {
        return 1U;
    }
    write_u16_le(&rsp_payload[1], id);
    write_f32_le(&rsp_payload[3], value);
    return 7U;
}

static size_t handle_param_set(const UsbProtoDispatchCtx_t *ctx,
                               const uint8_t *req_payload, uint16_t req_len,
                               uint8_t *rsp_payload, size_t rsp_cap)
{
    float now_value = 0.0f;
    uint8_t st;

    if (param_gate(ctx, rsp_payload) == 0U) {
        return 1U;
    }
    if ((req_payload == NULL) || (req_len != 6U) || (rsp_cap < 5U)) {
        rsp_payload[0] = (uint8_t)USB_PROTO_RSP_PARAM_INVALIDO;
        return 1U;
    }
    st = ctx->params->set(read_u16_le(req_payload), read_f32_le(&req_payload[2]), &now_value);
    rsp_payload[0] = st;
    if (st == (uint8_t)USB_PROTO_RSP_PARAM_INVALIDO) {
        return 1U; /* id desconhecido: nao ha' valor atual para devolver */
    }
    write_f32_le(&rsp_payload[1], now_value); /* recusado: o valor que continua valendo */
    return 5U;
}

static size_t handle_param_commit(const UsbProtoDispatchCtx_t *ctx, uint16_t req_len,
                                  uint8_t *rsp_payload, size_t rsp_cap)
{
    uint32_t seq = 0U;
    uint8_t st;

    if (param_gate(ctx, rsp_payload) == 0U) {
        return 1U;
    }
    if ((req_len != 0U) || (rsp_cap < 5U)) {
        rsp_payload[0] = (uint8_t)USB_PROTO_RSP_PARAM_INVALIDO;
        return 1U;
    }
    st = ctx->params->commit(&seq);
    rsp_payload[0] = st;
    if (st != (uint8_t)USB_PROTO_RSP_OK) {
        return 1U;
    }
    write_u32_le(&rsp_payload[1], seq);
    return 5U;
}

static size_t handle_param_revert(const UsbProtoDispatchCtx_t *ctx,
                                  const uint8_t *req_payload, uint16_t req_len,
                                  uint8_t *rsp_payload)
{
    if (param_gate(ctx, rsp_payload) == 0U) {
        return 1U;
    }
    if ((req_payload == NULL) || (req_len != 1U) || (req_payload[0] > 1U)) {
        rsp_payload[0] = (uint8_t)USB_PROTO_RSP_PARAM_INVALIDO;
        return 1U;
    }
    rsp_payload[0] = ctx->params->revert(req_payload[0]);
    return 1U;
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
        case USB_PROTO_CMD_STREAM_START:
            return handle_stream_start(ctx, req_payload, req_len, rsp_payload, rsp_cap);
        case USB_PROTO_CMD_STREAM_STOP:
            return handle_stream_stop(ctx, rsp_payload);
        case USB_PROTO_CMD_GET_LOG_TAIL:
            return handle_get_log_tail(ctx, req_payload, req_len, rsp_payload, rsp_cap);
        case USB_PROTO_CMD_FS_LIST:
            return handle_fs_list(ctx, req_payload, req_len, rsp_payload, rsp_cap);
        case USB_PROTO_CMD_FS_STAT:
            return handle_fs_stat(ctx, req_payload, req_len, rsp_payload, rsp_cap);
        case USB_PROTO_CMD_FS_READ:
            return handle_fs_read(ctx, req_payload, req_len, rsp_payload, rsp_cap);
        case USB_PROTO_CMD_FS_CRC:
            return handle_fs_crc(ctx, req_payload, req_len, rsp_payload, rsp_cap);
        case USB_PROTO_CMD_PARAM_LIST:
            return handle_param_list(ctx, req_payload, req_len, rsp_payload, rsp_cap);
        case USB_PROTO_CMD_PARAM_GET:
            return handle_param_get(ctx, req_payload, req_len, rsp_payload, rsp_cap);
        case USB_PROTO_CMD_PARAM_SET:
            return handle_param_set(ctx, req_payload, req_len, rsp_payload, rsp_cap);
        case USB_PROTO_CMD_PARAM_COMMIT:
            return handle_param_commit(ctx, req_len, rsp_payload, rsp_cap);
        case USB_PROTO_CMD_PARAM_REVERT:
            return handle_param_revert(ctx, req_payload, req_len, rsp_payload);
        default:
            rsp_payload[0] = (uint8_t)USB_PROTO_RSP_CMD_DESCONHECIDO;
            return 1U;
    }
}
