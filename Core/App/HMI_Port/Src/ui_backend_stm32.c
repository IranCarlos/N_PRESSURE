/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-17
 */

/**
 * @file ui_backend_stm32.c
 * @brief ui_backend.h ligado aos servicos reais. Mesma logica que antes
 *        vivia espalhada pelas telas (Fase 6), agora concentrada aqui para
 *        a UI compilar tambem no simulador de PC.
 *
 * Roda so' na uiTask (mesma tarefa do lv_timer_handler()).
 */

#include "ui_backend.h"
#include "ui_backend_stm32.h"

#include "control_loop.h"
#include "adc_acq.h"
#include "pressure_core.h"
#include "eeprom_storage_io.h"
#include "rtc_service.h"
#include "storage_task.h"
#include "cycle_record.h"
#include "buzzer.h"
#include "fw_version.h"
#include <stdio.h>
#include <string.h>

/* Services/App nao incluem main.h (ver CLAUDE.md): handle do CubeMX
 * declarado extern aqui, mesmo criterio de FATFS/App/fatfs.c. */
extern RTC_HandleTypeDef hrtc;

#define UI_MMHG_PER_KPA (7.50062f)

static SystemRuntime_t * s_rt;
static SystemConfig_t * s_cfg;

/* Caixa de correio USB -> uiTask (SET_OPERATOR). Um produtor
 * (usbProtoTask) e um consumidor (uiTask): o produtor grava os IDs
 * empacotados numa palavra de 32 bits (escrita alinhada = atomica no M7)
 * e SO' DEPOIS incrementa a sequencia; o consumidor compara com a ultima
 * sequencia vista. Se um pedido novo chegar entre as duas leituras do
 * consumidor, ele le o valor mais novo e o reaplica na proxima volta —
 * inofensivo (mesmo valor). */
static volatile uint32_t s_remote_session_ids; /* operador << 16 | participante */
static volatile uint32_t s_remote_session_seq;
static uint32_t s_remote_session_seen;

/* Mesmo esquema (sequencia, produtor unico) para o aviso de inicio remoto. */
static volatile uint32_t s_remote_start_seq;
static uint32_t s_remote_start_seen;

static uint8_t s_last_finalized;
static bool s_metrics_valid;
static CycleRecord_Metrics_t s_last_metrics;

static uint8_t s_cal_point1_ok;
static uint8_t s_cal_point2_ok;
static float s_cal_ref1_kpa;
static float s_cal_d1_kpa;
static float s_cal_ref2_kpa;
static float s_cal_d2_kpa;

static float ui_diff_kpa(void);
static bool ui_date_is_valid(const UiDateTime_t * dt);

void UiBackendStm32_Bind(SystemRuntime_t * runtime, SystemConfig_t * config)
{
    const ControlCore_CycleStats_t * stats;

    s_rt = runtime;
    s_cfg = config;

    /* Nao gerar popup de um ciclo que ja' estava finalizado antes da IHM. */
    stats = ControlLoop_GetCycleStats();
    s_last_finalized = (stats != NULL) ? stats->finalized : 0u;
}

/* --- Leitura de estado ------------------------------------------------- */

UiState_t UiBackend_GetState(void)
{
    if (s_rt == NULL) {
        return UI_STATE_BOOT;
    }

    switch (s_rt->state) {
        case SYSTEM_STATE_IDLE:           return UI_STATE_IDLE;
        case SYSTEM_STATE_INFLATING:      return UI_STATE_INFLATING;
        case SYSTEM_STATE_STABILIZING:    return UI_STATE_STABILIZING;
        case SYSTEM_STATE_STABILIZED:     return UI_STATE_STABILIZED;
        case SYSTEM_STATE_DEFLATING_FAST: return UI_STATE_DEFLATING_FAST;
        case SYSTEM_STATE_LEAK_TEST:      return UI_STATE_LEAK_TEST;
        case SYSTEM_STATE_CONFIG:         return UI_STATE_CONFIG;
        case SYSTEM_STATE_FAULT:          return UI_STATE_FAULT;
        case SYSTEM_STATE_BOOT:
        default:                          return UI_STATE_BOOT;
    }
}

float UiBackend_GetPressureMmHg(void)
{
    return (s_rt != NULL) ? s_rt->pressure_display : 0.0f;
}

float UiBackend_GetSetpointMmHg(void)
{
    return (s_rt != NULL) ? s_rt->setpoint_mmhg : 0.0f;
}

uint16_t UiBackend_GetMaxSetpointMmHg(void)
{
    if ((s_cfg == NULL) || (s_cfg->max_setpoint_mmhg < 0.0f)) {
        return 300u;
    }
    return (uint16_t)s_cfg->max_setpoint_mmhg;
}

uint32_t UiBackend_GetTickMs(void)
{
    return HAL_GetTick();
}

bool UiBackend_GetDateTime(UiDateTime_t * out)
{
    RTC_TimeTypeDef t;
    RTC_DateTypeDef d;

    if (out == NULL) {
        return false;
    }
    /* Ordem obrigatoria no STM32: GetTime antes de GetDate (destrava as
     * shadow registers). */
    if ((HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN) != HAL_OK) ||
        (HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BIN) != HAL_OK)) {
        return false;
    }

    out->year = (uint16_t)(2000u + d.Year);
    out->month = d.Month;
    out->day = d.Date;
    out->hour = t.Hours;
    out->minute = t.Minutes;
    out->second = t.Seconds;
    return true;
}

bool UiBackend_IsSdMounted(void)
{
    return (StorageTask_GetStats().sd_mounted != 0u);
}

bool UiBackend_IsRtcValid(void)
{
    return (RtcService_GetFlags() == 0u);
}

uint16_t UiBackend_GetLastError(void)
{
    return (uint16_t)ControlLoop_GetLastError();
}

UiUnit_t UiBackend_GetUnit(void)
{
    if ((s_cfg == NULL) || ((uint32_t)s_cfg->unit >= (uint32_t)UI_UNIT_COUNT)) {
        return UI_UNIT_MMHG;
    }
    return (UiUnit_t)s_cfg->unit;
}

float UiBackend_GetRawPressureMmHg(void)
{
    return ui_diff_kpa() * UI_MMHG_PER_KPA;
}

uint32_t UiBackend_GetLeakHoldTimeMs(void)
{
    return (s_cfg != NULL) ? s_cfg->leak_test.hold_time_ms : 0u;
}

float UiBackend_GetLeakMaxDropMmHg(void)
{
    return (s_cfg != NULL) ? s_cfg->leak_test.max_drop_mmhg : 0.0f;
}

const char * UiBackend_GetDeviceId(void)
{
    return DEVICE_ID;
}

const char * UiBackend_GetFirmwareVersion(void)
{
    return FIRMWARE_VERSION;
}

bool UiBackend_ConsumeUserButton(void)
{
    if ((s_rt == NULL) || (s_rt->bt_user_pressed == 0u)) {
        return false;
    }
    s_rt->bt_user_pressed = 0u;
    return true;
}

void UiBackendStm32_PostRemoteSession(uint16_t operator_id, uint16_t participant_id)
{
    s_remote_session_ids = ((uint32_t)operator_id << 16) | (uint32_t)participant_id;
    __DMB(); /* IDs visiveis antes da sequencia nova */
    s_remote_session_seq = s_remote_session_seq + 1u;
}

bool UiBackend_PollRemoteSession(uint16_t * operator_id, uint16_t * participant_id)
{
    uint32_t seq = s_remote_session_seq;
    uint32_t ids;

    if ((operator_id == NULL) || (participant_id == NULL) || (seq == s_remote_session_seen)) {
        return false;
    }
    __DMB(); /* le os IDs depois da sequencia */
    ids = s_remote_session_ids;
    s_remote_session_seen = seq;
    *operator_id = (uint16_t)(ids >> 16);         /* metade alta */
    *participant_id = (uint16_t)(ids & 0xFFFFu);  /* metade baixa */
    return true;
}

void UiBackendStm32_PostRemoteStart(void)
{
    s_remote_start_seq = s_remote_start_seq + 1u;
}

bool UiBackend_PollRemoteStart(void)
{
    uint32_t seq = s_remote_start_seq;

    if (seq == s_remote_start_seen) {
        return false;
    }
    s_remote_start_seen = seq; /* varios inicios entre duas leituras = 1 aviso so' */
    return true;
}

bool UiBackend_PollCycleResult(UiCycleResult_t * out)
{
    const ControlCore_CycleStats_t * stats = ControlLoop_GetCycleStats();
    CycleRecord_Metrics_t m;
    CycleRecord_NcCode_t nc;
    uint8_t rising;
    float overshoot;

    if ((stats == NULL) || (out == NULL)) {
        return false;
    }

    /* control_core.h: `finalized` sobe quando o estado volta a IDLE. */
    rising = ((stats->finalized != 0u) && (s_last_finalized == 0u)) ? 1u : 0u;
    s_last_finalized = stats->finalized;
    if (rising == 0u) {
        return false;
    }

    m.setpoint_mmhg = UiBackend_GetSetpointMmHg();
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

    s_last_metrics = m;
    s_metrics_valid = true;

    nc = CycleRecord_Evaluate(&m);
    overshoot = m.peak_mmhg - m.setpoint_mmhg;
    if (overshoot < 0.0f) {
        overshoot = 0.0f;
    }

    out->setpoint_mmhg = (int32_t)m.setpoint_mmhg;
    out->peak_mmhg = (int32_t)m.peak_mmhg;
    out->overshoot_mmhg = (int32_t)overshoot;
    out->inflate_time_s = (int32_t)m.inflate_time_s;
    out->conforme = (nc == CYCLE_NC00_CONFORME);
    CycleRecord_NcCodeToString(nc, out->nc_code, (uint32_t)sizeof(out->nc_code));
    return true;
}

/* --- Acoes -------------------------------------------------------------- */

void UiBackend_SetSetpoint(float mmhg)
{
    float sp = mmhg;
    float max_sp = (float)UiBackend_GetMaxSetpointMmHg();

    if (sp < 0.0f) {
        sp = 0.0f;
    }
    if (sp > max_sp) {
        sp = max_sp;
    }
    if (s_cfg != NULL) {
        s_cfg->current_setpoint_mmhg = sp;
    }
    ControlLoop_SetSetpointMmHg(sp);
}

void UiBackend_StartInflation(void)
{
    ControlLoop_StartInflation();
}

void UiBackend_EnterConfigMode(void)
{
    ControlLoop_EnterConfigMode();
}

void UiBackend_ExitConfigMode(void)
{
    ControlLoop_DisableManualActuators();
    if (s_cfg != NULL) {
        (void)EEPROM_Storage_Save(s_cfg);
    }
    ControlLoop_ExitConfigMode();
}

void UiBackend_StartLeakTest(void)
{
    ControlLoop_StartLeakTest();
}

void UiBackend_SetUnit(UiUnit_t unit)
{
    if ((s_cfg != NULL) && ((uint32_t)unit < (uint32_t)SYS_PRESSURE_UNIT_COUNT)) {
        s_cfg->unit = (PressureUnit_t)unit;
    }
}

bool UiBackend_SetDateTime(const UiDateTime_t * dt)
{
    RtcValidity_DateTime_t rd;

    if ((dt == NULL) || !ui_date_is_valid(dt)) {
        return false;
    }

    rd.year_2d = (uint8_t)(dt->year - 2000u);
    rd.month = dt->month;
    rd.day = dt->day;
    rd.hour = dt->hour;
    rd.minute = dt->minute;
    rd.second = dt->second;
    /* Grava e reavalia o status (sem isso NEVER_SET do boot continuava
     * valendo e a IHM mostrava "calendario nao inicializado" ate' o reset). */
    return (RtcService_SetDateTime(&hrtc, &rd) != 0u);
}

void UiBackend_BuzzerBeep(uint16_t freq_hz, uint16_t duration_ms)
{
    Buzzer_Beep((uint32_t)freq_hz, (uint32_t)duration_ms);
}

void UiBackend_SubmitCycleRecord(uint16_t operator_id, uint16_t participant_id,
                                 uint16_t cycle_number)
{
    StorageFormat_RecInfo_t info;
    UiDateTime_t now;
    /* static: StorageTask_SubmitTestRecord() copia os campos para a fila,
     * mas manter as strings vivas evita depender disso. */
    static char participant_str[8];
    static char operator_str[8];

    if (!s_metrics_valid) {
        return;
    }

    (void)snprintf(participant_str, sizeof(participant_str), "P%03u", (unsigned int)participant_id);
    (void)snprintf(operator_str, sizeof(operator_str), "OP%02u", (unsigned int)operator_id);

    memset(&info, 0, sizeof(info));
    info.test_id = cycle_number; /* placeholder: numeracao persistente ainda nao existe */
    info.participant_id = participant_str;
    info.operator_id = operator_str;
    info.cycle_number = cycle_number;
    info.metrics = s_last_metrics;
    info.device_id = DEVICE_ID;
    info.firmware_version = FIRMWARE_VERSION;
    /* last_calibration fica zerado — data da calibracao ainda nao e' gravada. */

    if (UiBackend_GetDateTime(&now)) {
        info.datetime.year = now.year;
        info.datetime.month = now.month;
        info.datetime.day = now.day;
        info.datetime.hour = now.hour;
        info.datetime.minute = now.minute;
        info.datetime.second = now.second;
    }

    (void)StorageTask_SubmitTestRecord(&info);
    s_metrics_valid = false;
}

/* --- Calibracao --------------------------------------------------------- */

void UiBackend_CalZero(void)
{
    ADC_Acq_ZeroNow();
}

void UiBackend_CalSetPumpPct(uint8_t pct)
{
    uint8_t p = (pct > 100u) ? 100u : pct;

    /* V1+V2+V3 abertas de proposito: V3 liga o manometro externo; V1/V2
     * abertas evitam acumular pressao alem do que a pressao de teste pede. */
    ControlLoop_SetManualActuators((float)p / 100.0f, 1u, 1u, 1u);
}

void UiBackend_CalAbort(void)
{
    ControlLoop_DisableManualActuators();
    s_cal_point1_ok = 0u;
    s_cal_point2_ok = 0u;
}

void UiBackend_CalCapture(uint8_t point, float ref_mmhg)
{
    float ref_kpa = ref_mmhg / UI_MMHG_PER_KPA;
    float d_kpa = ui_diff_kpa();

    if (point == 1u) {
        s_cal_ref1_kpa = ref_kpa;
        s_cal_d1_kpa = d_kpa;
        s_cal_point1_ok = 1u;
    } else if (point == 2u) {
        s_cal_ref2_kpa = ref_kpa;
        s_cal_d2_kpa = d_kpa;
        s_cal_point2_ok = 1u;
    } else {
        /* ponto invalido: ignorado */
    }
}

bool UiBackend_CalComputeAndSave(char * msg, size_t msg_len)
{
    PressureCal_t new_cal;
    const char * text;
    bool ok = false;

    if ((s_cal_point1_ok == 0u) || (s_cal_point2_ok == 0u)) {
        text = "Capture P1 e P2 antes.";
    } else if (PressureCore_CalibrateTwoPoint(s_cal_ref1_kpa, s_cal_d1_kpa,
                                              s_cal_ref2_kpa, s_cal_d2_kpa, &new_cal) != 0) {
        text = "Pontos invalidos (P1=P2?).";
    } else {
        if (s_cfg != NULL) {
            s_cfg->sensor_cal.offset_kpa = new_cal.offset_kpa;
            s_cfg->sensor_cal.scale = new_cal.scale;
            (void)EEPROM_Storage_Save(s_cfg);
        }
        /* Calibracao concluida: bomba off, valvulas de volta ao controle. */
        UiBackend_CalAbort();
        text = "Calibracao salva.";
        ok = true;
    }

    if ((msg != NULL) && (msg_len > 0u)) {
        (void)snprintf(msg, msg_len, "%s", text);
    }
    return ok;
}

/* --- Internos ------------------------------------------------------------ */

static float ui_diff_kpa(void)
{
    const PressureCore_t * diag = ADC_Acq_GetDiagnostics();

    if (diag == NULL) {
        return 0.0f;
    }
    return diag->raw_kpa - diag->atmospheric_kpa;
}

static bool ui_date_is_valid(const UiDateTime_t * dt)
{
    static const uint8_t k_days[12] = { 31u, 29u, 31u, 30u, 31u, 30u, 31u, 31u, 30u, 31u, 30u, 31u };
    uint8_t max_day;

    if ((dt->year < 2000u) || (dt->year > 2099u) || (dt->month < 1u) || (dt->month > 12u) ||
        (dt->hour > 23u) || (dt->minute > 59u) || (dt->second > 59u) || (dt->day < 1u)) {
        return false;
    }

    max_day = k_days[dt->month - 1u];
    if ((dt->month == 2u) && ((dt->year % 4u) != 0u)) {
        max_day = 28u;
    }
    return (dt->day <= max_day);
}
