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
#include "params.h"
#include "eeprom_storage_io.h"
#include "valve.h"
#include "pwm_motor.h"
#include "cycle_record.h"
#include <stdio.h>
#include <string.h>

/* App nao inclui main.h (ver CLAUDE.md): handle do CubeMX declarado extern,
 * mesmo criterio de ui_backend_stm32.c. */
extern RTC_HandleTypeDef hrtc;

#define USB_ACTIONS_TZ_OFFSET_HOURS (-3) /* America/Sao_Paulo - ver storage_format.h */
#define USB_ACTIONS_MMHG_PER_KPA    (7.50062f)

static SystemRuntime_t * s_rt;
static const SystemConfig_t * s_cfg;
static uint32_t s_log_seq; /* placeholder local, mesmo criterio do log de AUTH em main.c */

static UsbProtoRspStatus_t act_set_setpoint(float setpoint_mmhg, uint8_t * warn);
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

static UsbProtoRspStatus_t act_set_setpoint(float setpoint_mmhg, uint8_t * warn)
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
    *warn = (setpoint_mmhg > s_cfg->setpoint_warn_mmhg) ? 1u : 0u;
    (void)snprintf(msg, sizeof(msg), "USB: setpoint %d mmHg%s", (int)setpoint_mmhg,
                   (*warn != 0u) ? " (ALERTA: margem reduzida)" : "");
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
    if ((raise_abort != 0u) && (s_rt->state == SYSTEM_STATE_MANUAL)) {
        ControlLoop_ExitRemoteManual(); /* bomba off, valvulas abertas, STANDBY */
        log_usb_command(STORAGE_LOG_LEVEL_AUDIT, "USB: modo manual encerrado (ABORT)");
    } else if (raise_abort != 0u) {
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

/* --- Fase 7.4 ----------------------------------------------------------- */

float UsbActionsStm32_PressureMmHg(void)
{
    return (s_rt != NULL) ? (s_rt->pressure_kpa * USB_ACTIONS_MMHG_PER_KPA) : 0.0f;
}

void UsbActionsStm32_FillTelemetrySample(UsbTelemetrySample_t * s)
{
    float pump = PwmMotor_GetPower(); /* 0..1 */
    uint8_t valves = 0u;

    s->t_ms = HAL_GetTick();
    s->pressure_mmhg = UsbActionsStm32_PressureMmHg();
    s->setpoint_mmhg = (s_rt != NULL) ? s_rt->setpoint_mmhg : 0.0f;
    s->state = (s_rt != NULL) ? (uint8_t)s_rt->state : 0u;
    if (pump < 0.0f) {
        pump = 0.0f;
    }
    if (pump > 1.0f) {
        pump = 1.0f;
    }
    s->pump_pct = (uint8_t)((pump * 100.0f) + 0.5f);
    /* Mascara de valvulas abertas: bit0=V1, bit1=V2, bit2=V3 (usb_telemetry.h). */
    if (Valve_Get(VALVE_1) == VALVE_OPEN) {
        valves |= USB_TELEMETRY_VALVE_V1;
    }
    if (Valve_Get(VALVE_2) == VALVE_OPEN) {
        valves |= USB_TELEMETRY_VALVE_V2;
    }
    if (Valve_Get(VALVE_3) == VALVE_OPEN) {
        valves |= USB_TELEMETRY_VALVE_V3;
    }
    s->valves = valves;
    s->error = (uint8_t)ControlLoop_GetLastError();
}

void UsbActionsStm32_FillWatchSnapshot(EventWatch_Snapshot_t * snap)
{
    const ControlCore_CycleStats_t * stats = ControlLoop_GetCycleStats();

    snap->state = (s_rt != NULL) ? s_rt->state : SYSTEM_STATE_BOOT;
    snap->error = (uint8_t)ControlLoop_GetLastError();
    snap->sd_mounted = StorageTask_GetStats().sd_mounted;
    snap->cycle_finalized = (stats != NULL) ? stats->finalized : 0u;
}

void UsbActionsStm32_FillCycleEvent(UsbProtoEvtCycle_t * c)
{
    const ControlCore_CycleStats_t * stats = ControlLoop_GetCycleStats();
    CycleRecord_Metrics_t m;

    memset(c, 0, sizeof(*c));
    if ((stats == NULL) || (s_rt == NULL)) {
        return;
    }

    /* Mesmas metricas que a IHM usa para o popup/registro do ciclo
     * (UiBackend_PollCycleResult) - o PC ve a mesma classificacao NCxx. */
    memset(&m, 0, sizeof(m));
    m.setpoint_mmhg = s_rt->setpoint_mmhg;
    m.peak_mmhg = stats->peak_mmhg;
    m.plateau_mean_mmhg = (stats->plateau_sample_count > 0u)
        ? (stats->plateau_sum_mmhg / (float)stats->plateau_sample_count) : 0.0f;
    m.plateau_min_mmhg = stats->plateau_min_mmhg;
    m.plateau_max_mmhg = stats->plateau_max_mmhg;
    m.inflate_time_s = (float)stats->inflate_time_ms / 1000.0f;
    m.plateau_time_s = (float)stats->plateau_time_ms / 1000.0f;
    m.deflate_time_s = (float)stats->deflate_time_ms / 1000.0f;
    m.residual_mmhg = stats->residual_mmhg;
    m.aborted_by_operator = stats->aborted_by_operator;
    m.technical_fault = stats->technical_fault;

    c->setpoint_mmhg = m.setpoint_mmhg;
    c->peak_mmhg = stats->peak_mmhg;
    c->residual_mmhg = stats->residual_mmhg;
    c->inflate_time_ms = stats->inflate_time_ms;
    c->plateau_time_ms = stats->plateau_time_ms;
    c->deflate_time_ms = stats->deflate_time_ms;
    c->aborted_by_operator = stats->aborted_by_operator;
    c->technical_fault = stats->technical_fault;
    c->nc_code = (uint8_t)CycleRecord_Evaluate(&m);
}

uint8_t UsbActionsStm32_GetLogEntry(uint32_t newest_offset, UsbProtoLogEntry_t * out)
{
    StorageTask_LogTailEntry_t e;

    if ((out == NULL) || (StorageTask_GetLogTail(newest_offset, &e) == 0u)) {
        return 0u;
    }
    out->seq = e.seq;
    out->year = e.datetime.year;
    out->month = e.datetime.month;
    out->day = e.datetime.day;
    out->hour = e.datetime.hour;
    out->minute = e.datetime.minute;
    out->second = e.datetime.second;
    /* StorageFormat_LogLevel_t (EVT=0, ERR=1, AUDIT=2) == USB_PROTO_LOG_LEVEL_* */
    out->level = e.level;
    memcpy(out->code, e.code, sizeof(out->code));
    out->code[sizeof(out->code) - 1u] = '\0';
    memcpy(out->message, e.message, sizeof(out->message));
    out->message[sizeof(out->message) - 1u] = '\0';
    return 1u;
}

void UsbActionsStm32_LogAuthExpired(void)
{
    log_usb_command(STORAGE_LOG_LEVEL_AUDIT, "USB: sessao DEV expirada por inatividade");
}

/* --- Fase 7.5 ----------------------------------------------------------- */

#define USB_FS_TIMEOUT_MS      (500u)    /* LIST/STAT/READ: um setor ou um diretorio */
#define USB_FS_HASH_TIMEOUT_MS (15000u)  /* FS_CRC le o arquivo inteiro */

static UsbProtoFs_Result_t fs_result(StorageFs_Result_t r)
{
    switch (r) {
        case STORAGE_FS_OK:        return USB_PROTO_FS_OK;
        case STORAGE_FS_NO_SD:     return USB_PROTO_FS_NO_SD;
        case STORAGE_FS_NOT_FOUND: return USB_PROTO_FS_NOT_FOUND;
        case STORAGE_FS_BUSY:      return USB_PROTO_FS_BUSY;
        case STORAGE_FS_IO_ERROR:
        default:                   return USB_PROTO_FS_IO_ERROR;
    }
}

static void copy_entry(UsbProtoFsEntry_t * dst, const StorageFs_Entry_t * src)
{
    dst->is_dir = src->is_dir;
    dst->size = src->size;
    dst->fdate = src->fdate;
    dst->ftime = src->ftime;
    memcpy(dst->name, src->name, sizeof(dst->name)); /* mesmo tamanho (64 + 1) */
    dst->name[sizeof(dst->name) - 1u] = '\0';
}

static UsbProtoFs_Result_t fs_list(const char * path, uint16_t start, UsbProtoFsEntry_t * out,
                                   uint8_t max, uint8_t * n, uint8_t * more)
{
    /* static: 12 entradas nao vao para a pilha da usbProtoTask */
    static StorageFs_Entry_t s_tmp[STORAGE_FS_LIST_MAX];
    StorageFs_Result_t r;
    uint8_t i;

    *n = 0u;
    *more = 0u;
    r = StorageTask_FsList(path, start, s_tmp, max, n, more, USB_FS_TIMEOUT_MS);
    if (r == STORAGE_FS_OK) {
        for (i = 0u; i < *n; i++) {
            copy_entry(&out[i], &s_tmp[i]);
        }
    }
    return fs_result(r);
}

static UsbProtoFs_Result_t fs_stat(const char * path, UsbProtoFsEntry_t * out)
{
    StorageFs_Entry_t e;
    StorageFs_Result_t r = StorageTask_FsStat(path, &e, USB_FS_TIMEOUT_MS);

    if (r == STORAGE_FS_OK) {
        copy_entry(out, &e);
    }
    return fs_result(r);
}

static UsbProtoFs_Result_t fs_read(const char * path, uint32_t offset, uint8_t * dst,
                                   uint16_t len, uint16_t * n_read)
{
    *n_read = 0u;
    return fs_result(StorageTask_FsRead(path, offset, dst, len, n_read, USB_FS_TIMEOUT_MS));
}

static UsbProtoFs_Result_t fs_hash(const char * path, uint32_t * size, uint32_t * crc32, uint8_t sha256[32])
{
    return fs_result(StorageTask_FsHash(path, size, crc32, sha256, USB_FS_HASH_TIMEOUT_MS));
}

static const UsbProtoFsOps_t s_fs_ops = { fs_list, fs_stat, fs_read, fs_hash };

const UsbProtoFsOps_t * UsbActionsStm32_GetFsOps(void)
{
    return &s_fs_ops;
}

/* --- Fase 7.6: tabela de parametros (DEV) -------------------------------- */

static uint16_t prm_count(void)
{
    return Params_Count();
}

static uint8_t prm_info_at(uint16_t index, UsbProtoParamInfo_t * out)
{
    const Param_Desc_t * d = Params_DescAt(index);

    if ((d == NULL) || (out == NULL)) {
        return 0u;
    }
    out->id = d->id;
    out->type = d->type;
    out->group = d->group;
    out->min = d->min;
    out->max = d->max;
    out->def = Params_GetDefault(index);
    out->value = Params_Get(index);
    out->name = d->name;
    out->unit = d->unit;
    return 1u;
}

static uint8_t prm_is_dirty(void)
{
    return Params_IsDirty();
}

static uint8_t prm_get(uint16_t id, float * value)
{
    int32_t idx = Params_IndexOf(id);

    if (idx < 0) {
        return (uint8_t)USB_PROTO_RSP_PARAM_INVALIDO;
    }
    *value = Params_Get((uint16_t)idx);
    return (uint8_t)USB_PROTO_RSP_OK;
}

/* Mudar parametro so' com o controle parado: o novo valor vale no proximo
 * passo de 1 ms, e um ensaio nao pode trocar de ganho/limite no meio. */
static uint8_t prm_standby_only(void)
{
    return (uint8_t)UsbProtoPolicy_IdleOnly((s_rt != NULL) ? s_rt->state : SYSTEM_STATE_BOOT);
}

static uint8_t prm_set(uint16_t id, float value, float * now_value)
{
    int32_t idx = Params_IndexOf(id);
    uint8_t st;
    float before;
    Params_Result_t r;
    char msg[64];

    if (idx < 0) {
        return (uint8_t)USB_PROTO_RSP_PARAM_INVALIDO;
    }
    before = Params_Get((uint16_t)idx);
    *now_value = before;
    st = prm_standby_only();
    if (st != (uint8_t)USB_PROTO_RSP_OK) {
        return st;
    }

    r = Params_Set((uint16_t)idx, value);
    *now_value = Params_Get((uint16_t)idx);
    if (r == PARAMS_ERR_RANGE) {
        return (uint8_t)USB_PROTO_RSP_FORA_DE_FAIXA;
    }
    if (r == PARAMS_ERR_CONSISTENCY) {
        return (uint8_t)USB_PROTO_RSP_CONFLITO;
    }
    if (r != PARAMS_OK) {
        return (uint8_t)USB_PROTO_RSP_PARAM_INVALIDO;
    }
    /* Auditoria (Parte B 2.6: "toda escrita de parametro no log"), com o
     * valor de antes e o de depois. */
    (void)snprintf(msg, sizeof(msg), "PARAM %s: %.4g -> %.4g (nao salvo)",
                   Params_DescAt((uint16_t)idx)->name, (double)before, (double)*now_value);
    log_usb_command(STORAGE_LOG_LEVEL_AUDIT, msg);
    return (uint8_t)USB_PROTO_RSP_OK;
}

static uint8_t prm_commit(uint32_t * seq)
{
    uint8_t st = prm_standby_only();
    char msg[48];

    if (st != (uint8_t)USB_PROTO_RSP_OK) {
        return st;
    }
    if (EEPROM_Storage_SaveParams(seq) == 0u) {
        log_usb_command(STORAGE_LOG_LEVEL_AUDIT, "PARAM: falha ao gravar na EEPROM");
        return (uint8_t)USB_PROTO_RSP_ERRO_SD; /* "falha de gravacao" - ver usb_proto_dispatch.h */
    }
    (void)snprintf(msg, sizeof(msg), "PARAM: parametros gravados (seq %lu)", (unsigned long)*seq);
    log_usb_command(STORAGE_LOG_LEVEL_AUDIT, msg);
    return (uint8_t)USB_PROTO_RSP_OK;
}

static uint8_t prm_revert(uint8_t to_factory)
{
    uint8_t st = prm_standby_only();

    if (st != (uint8_t)USB_PROTO_RSP_OK) {
        return st;
    }
    if (to_factory != 0u) {
        Params_RestoreDefaults();
        log_usb_command(STORAGE_LOG_LEVEL_AUDIT, "PARAM: valores de fabrica aplicados (nao salvos)");
    } else {
        Params_RevertToSaved();
        log_usb_command(STORAGE_LOG_LEVEL_AUDIT, "PARAM: alteracoes nao salvas descartadas");
    }
    return (uint8_t)USB_PROTO_RSP_OK;
}

static const UsbProtoParamOps_t s_param_ops = {
    prm_count, prm_info_at, prm_is_dirty, prm_get, prm_set, prm_commit, prm_revert
};

const UsbProtoParamOps_t * UsbActionsStm32_GetParamOps(void)
{
    return &s_param_ops;
}

/* --- Fase 7.7: modo manual remoto (DEV) ---------------------------------- */

static uint8_t man_enter(void)
{
    if (ControlLoop_EnterRemoteManual() == 0u) {
        return (uint8_t)USB_PROTO_RSP_ESTADO_INVALIDO; /* so' a partir de STANDBY */
    }
    log_usb_command(STORAGE_LOG_LEVEL_AUDIT, "USB: modo manual iniciado (DEV)");
    return (uint8_t)USB_PROTO_RSP_OK;
}

static uint8_t man_exit(void)
{
    if ((s_rt != NULL) && (s_rt->state == SYSTEM_STATE_MANUAL)) {
        ControlLoop_ExitRemoteManual();
        log_usb_command(STORAGE_LOG_LEVEL_AUDIT, "USB: modo manual encerrado");
    }
    return (uint8_t)USB_PROTO_RSP_OK;
}

static uint8_t man_set(uint8_t pump_pct, uint8_t valve_mask)
{
    if ((s_rt == NULL) || (s_rt->state != SYSTEM_STATE_MANUAL)) {
        return (uint8_t)USB_PROTO_RSP_ESTADO_INVALIDO;
    }
    ControlLoop_SetManualActuators((float)pump_pct / 100.0f,
                                   (uint8_t)(valve_mask & 0x01u),          /* V1 */
                                   (uint8_t)((valve_mask >> 1) & 0x01u),   /* V2 */
                                   (uint8_t)((valve_mask >> 2) & 0x01u));  /* V3 */
    ControlLoop_RemoteManualHeartbeat();
    return (uint8_t)USB_PROTO_RSP_OK;
}

static uint8_t man_heartbeat(void)
{
    if ((s_rt == NULL) || (s_rt->state != SYSTEM_STATE_MANUAL)) {
        return (uint8_t)USB_PROTO_RSP_ESTADO_INVALIDO;
    }
    ControlLoop_RemoteManualHeartbeat();
    return (uint8_t)USB_PROTO_RSP_OK;
}

static const UsbProtoManualOps_t s_manual_ops = { man_enter, man_exit, man_set, man_heartbeat };

const UsbProtoManualOps_t * UsbActionsStm32_GetManualOps(void)
{
    return &s_manual_ops;
}

void UsbActionsStm32_EnforceManualSession(uint8_t dev_session_active)
{
    /* Manual so' existe dentro de uma sessao DEV: logout, expiracao, porta
     * fechada ou cabo puxado (todos derrubam a sessao) encerram o manual na
     * hora - sem esperar os 300 ms do sinal de vida. */
    if ((dev_session_active == 0u) && (s_rt != NULL) && (s_rt->state == SYSTEM_STATE_MANUAL)) {
        ControlLoop_ExitRemoteManual();
        log_usb_command(STORAGE_LOG_LEVEL_AUDIT, "USB: modo manual encerrado (sessao DEV terminou)");
    }
}
