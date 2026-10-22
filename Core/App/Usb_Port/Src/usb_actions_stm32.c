/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-22
 */

#include "usb_actions_stm32.h"
#include "usb_proto_policy.h"
#include "control_loop.h"
#include "rtc_service.h"
#include "storage_task.h"
#include "ui_backend_stm32.h"
#include <stdio.h>

/* App nao inclui main.h (ver CLAUDE.md): handle do CubeMX declarado extern,
 * mesmo criterio de ui_backend_stm32.c. */
extern RTC_HandleTypeDef hrtc;

#define USB_ACTIONS_TZ_OFFSET_HOURS (-3) /* America/Sao_Paulo - ver storage_format.h */

static SystemRuntime_t * s_rt;
static const SystemConfig_t * s_cfg;
static uint32_t s_log_seq; /* placeholder local, mesmo criterio do log de AUTH em main.c */

static UsbProtoRspStatus_t act_set_setpoint(float setpoint_mmhg);
static UsbProtoRspStatus_t act_set_datetime(const UsbProtoDateTime_t * dt);
static UsbProtoRspStatus_t act_start_test(void);
static UsbProtoRspStatus_t act_abort_test(void);
static UsbProtoRspStatus_t act_set_operator(uint16_t operator_id, uint16_t participant_id);

static const UsbProtoActions_t s_actions = {
    act_set_setpoint,
    act_set_datetime,
    act_start_test,
    act_abort_test,
    act_set_operator
};

void UsbActionsStm32_Bind(SystemRuntime_t * runtime, const SystemConfig_t * config)
{
    s_rt = runtime;
    s_cfg = config;
}

const UsbProtoActions_t * UsbActionsStm32_Get(void)
{
    return &s_actions;
}

/* --- Log ---------------------------------------------------------------- */

static void read_datetime_now(StorageFormat_DateTime_t * out)
{
    RTC_TimeTypeDef t = { 0 };
    RTC_DateTypeDef d = { 0 };

    /* GetTime antes de GetDate: destrava os registradores-sombra (HAL). */
    (void)HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN);
    (void)HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BIN);

    out->year = (uint16_t)(2000u + d.Year);
    out->month = d.Month;
    out->day = d.Date;
    out->hour = t.Hours;
    out->minute = t.Minutes;
    out->second = t.Seconds;
}

/* Linha de log de um comando aceito via USB. A mensagem e' copiada pela
 * StorageTask (ate' 63 caracteres), pode vir da pilha. Fila cheia ou SD
 * ausente: o comando continua valendo (o controle nao depende do SD). */
static void log_usb_command(StorageFormat_LogLevel_t level, const char * message)
{
    StorageFormat_LogEntry_t entry = { 0 };

    read_datetime_now(&entry.datetime);
    entry.tz_offset_hours = USB_ACTIONS_TZ_OFFSET_HOURS;
    entry.seq = ++s_log_seq;
    entry.level = level;
    entry.code = "";
    entry.test_id = 0u;
    entry.state_name = "";
    entry.pressure_mmhg = (s_rt != NULL) ? s_rt->pressure_display : 0.0f;
    entry.message = message;
    if (level == STORAGE_LOG_LEVEL_AUDIT) {
        (void)StorageTask_LogAudit(&entry);
    } else {
        (void)StorageTask_LogEvent(&entry);
    }
}

/* --- Acoes -------------------------------------------------------------- */

static UsbProtoRspStatus_t act_set_setpoint(float setpoint_mmhg)
{
    UsbProtoRspStatus_t st;
    char msg[48];

    if ((s_rt == NULL) || (s_cfg == NULL)) {
        return USB_PROTO_RSP_OCUPADO;
    }
    st = UsbProtoPolicy_IdleOnly(s_rt->state);
    if (st == USB_PROTO_RSP_OK) {
        st = UsbProtoPolicy_Setpoint(setpoint_mmhg, s_cfg->max_setpoint_mmhg);
    }
    if (st != USB_PROTO_RSP_OK) {
        return st;
    }

    ControlLoop_SetSetpointMmHg(setpoint_mmhg);
    (void)snprintf(msg, sizeof(msg), "USB: setpoint %d mmHg", (int)setpoint_mmhg);
    log_usb_command(STORAGE_LOG_LEVEL_EVT, msg);
    return USB_PROTO_RSP_OK;
}

static UsbProtoRspStatus_t act_set_datetime(const UsbProtoDateTime_t * dt)
{
    UsbProtoRspStatus_t st;
    StorageFormat_DateTime_t before;
    RtcValidity_DateTime_t rd;
    char msg[64];

    if ((s_rt == NULL) || (dt == NULL)) {
        return USB_PROTO_RSP_OCUPADO;
    }
    st = UsbProtoPolicy_IdleOnly(s_rt->state);
    if (st != USB_PROTO_RSP_OK) {
        return st;
    }

    read_datetime_now(&before);
    rd.year_2d = (uint8_t)(dt->year - 2000u);
    rd.month = dt->month;
    rd.day = dt->day;
    rd.hour = dt->hour;
    rd.minute = dt->minute;
    rd.second = dt->second;
    if (RtcService_SetDateTime(&hrtc, &rd) == 0u) {
        return USB_PROTO_RSP_OCUPADO; /* HAL recusou (timeout de INITF) - PC pode repetir */
    }

    /* Apendice A: SET_DATETIME "ajusta RTC e registra evento" - na
     * auditoria, com a hora de ANTES (a linha ja' sai com a hora nova). */
    (void)snprintf(msg, sizeof(msg), "USB: data/hora ajustada (antes %02u/%02u/%04u %02u:%02u:%02u)",
                   (unsigned)before.day, (unsigned)before.month, (unsigned)before.year,
                   (unsigned)before.hour, (unsigned)before.minute, (unsigned)before.second);
    log_usb_command(STORAGE_LOG_LEVEL_AUDIT, msg);
    return USB_PROTO_RSP_OK;
}

static UsbProtoRspStatus_t act_start_test(void)
{
    UsbProtoRspStatus_t st;

    if (s_rt == NULL) {
        return USB_PROTO_RSP_OCUPADO;
    }
    st = UsbProtoPolicy_StartTest(s_rt->state);
    if (st != USB_PROTO_RSP_OK) {
        return st;
    }

    /* Mesmo caminho do pedal. ControlLoop_StartInflation() checa IDLE de
     * novo por dentro: se o estado mudou desde a politica, nao faz nada. */
    ControlLoop_StartInflation();
    UiBackendStm32_PostRemoteStart(); /* aviso sonoro local, tocado pela uiTask */
    log_usb_command(STORAGE_LOG_LEVEL_EVT, "USB: ensaio iniciado");
    return USB_PROTO_RSP_OK;
}

static UsbProtoRspStatus_t act_abort_test(void)
{
    UsbProtoRspStatus_t st;
    uint8_t raise_abort = 0u;

    if (s_rt == NULL) {
        return USB_PROTO_RSP_OCUPADO;
    }
    st = UsbProtoPolicy_AbortTest(s_rt->state, &raise_abort);
    if (raise_abort != 0u) {
        /* Mesmo flag do pedal/bt_user (system_types.h): o Control_Loop o
         * consome no proximo tick de 1 ms e vai a DEFLATING_FAST. Escrita de
         * 1 byte = atomica; o controle so' le e zera. */
        s_rt->bt_user_pressed = 1u;
        log_usb_command(STORAGE_LOG_LEVEL_EVT, "USB: ensaio abortado (desinflar)");
    }
    return st;
}

static UsbProtoRspStatus_t act_set_operator(uint16_t operator_id, uint16_t participant_id)
{
    UsbProtoRspStatus_t st;
    char msg[64];

    if (s_rt == NULL) {
        return USB_PROTO_RSP_OCUPADO;
    }
    st = UsbProtoPolicy_IdleOnly(s_rt->state);
    if (st != USB_PROTO_RSP_OK) {
        return st;
    }

    UiBackendStm32_PostRemoteSession(operator_id, participant_id);
    (void)snprintf(msg, sizeof(msg), "USB: operador OP%02u, participante P%03u",
                   (unsigned)operator_id, (unsigned)participant_id);
    log_usb_command(STORAGE_LOG_LEVEL_EVT, msg);
    return USB_PROTO_RSP_OK;
}
