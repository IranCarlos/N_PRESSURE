/**
 * @file ui_backend_mock.c
 * @brief ui_backend.h simulado para o PC: maquina de estados fake que
 *        imita o Control_Loop o suficiente para exercitar todas as telas.
 *
 * NAO e' um modelo da planta pneumatica — rampas lineares so' para a UI ter
 * o que mostrar. Nenhum valor daqui vale para sintonia ou validacao.
 */

#include "ui_backend.h"
#include "ui_backend_mock.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define MOCK_INFLATE_RATE_MMHG_S  (20.0f)
#define MOCK_DEFLATE_RATE_MMHG_S  (45.0f)
#define MOCK_STABILIZE_MS         (2000u)
#define MOCK_BOOT_MS              (300u)
#define MOCK_LEAK_TARGET_MMHG     (60.0f)
#define MOCK_LEAK_RATE_MMHG_S     (0.15f)
#define MOCK_LEAK_HOLD_MS         (10000u)
#define MOCK_LEAK_MAX_DROP_MMHG   (3.0f)
#define MOCK_MAX_SETPOINT_MMHG    (300u)

static UiState_t s_state = UI_STATE_BOOT;
static float s_pressure;
static float s_setpoint = 160.0f;
static UiUnit_t s_unit = UI_UNIT_MMHG;
static uint16_t s_error = UI_ERR_NONE;
static bool s_sd_ok = true;
static bool s_rtc_ok = true;
static bool s_user_btn;
static uint32_t s_state_tick;
static uint32_t s_last_tick;
static long s_time_offset_s;

/* ciclo */
static bool s_cycle_active;
static float s_cycle_peak;
static uint32_t s_cycle_start;
static uint32_t s_inflate_ms;
static bool s_aborted;
static bool s_fault;
static bool s_result_ready;
static UiCycleResult_t s_result;

/* teste de vazamento */
static bool s_leak_holding;

/* calibracao */
static uint8_t s_cal_pump_pct;
static float s_cal_offset_mmhg;

static void mock_set_state(UiState_t st)
{
    s_state = st;
    s_state_tick = SDL_GetTicks();
}

static void mock_finish_cycle(void)
{
    float overshoot = s_cycle_peak - s_setpoint;

    if (!s_cycle_active) {
        return;
    }
    s_cycle_active = false;

    if (overshoot < 0.0f) {
        overshoot = 0.0f;
    }
    memset(&s_result, 0, sizeof(s_result));
    s_result.setpoint_mmhg = (int32_t)s_setpoint;
    s_result.peak_mmhg = (int32_t)s_cycle_peak;
    s_result.overshoot_mmhg = (int32_t)overshoot;
    s_result.inflate_time_s = (int32_t)(s_inflate_ms / 1000u);
    if (s_fault) {
        (void)snprintf(s_result.nc_code, sizeof(s_result.nc_code), "NC08");
    } else if (s_aborted) {
        (void)snprintf(s_result.nc_code, sizeof(s_result.nc_code), "NC07");
    } else {
        (void)snprintf(s_result.nc_code, sizeof(s_result.nc_code), "NC00");
    }
    s_result.conforme = (!s_fault && !s_aborted);
    s_result_ready = true;
}

static float mock_approach(float value, float target, float rate_per_s, float dt_s)
{
    float step = rate_per_s * dt_s;

    if (value < target) {
        value += step;
        if (value > target) {
            value = target;
        }
    } else {
        value -= step;
        if (value < target) {
            value = target;
        }
    }
    return value;
}

void UiMock_Init(void)
{
    s_last_tick = SDL_GetTicks();
    mock_set_state(UI_STATE_BOOT);
}

void UiMock_Step(void)
{
    uint32_t now = SDL_GetTicks();
    uint32_t in_state = now - s_state_tick;
    float dt = (float)(now - s_last_tick) / 1000.0f;

    s_last_tick = now;

    switch (s_state) {
        case UI_STATE_BOOT:
            if (in_state >= MOCK_BOOT_MS) {
                mock_set_state(UI_STATE_IDLE);
            }
            break;

        case UI_STATE_IDLE:
            s_pressure = mock_approach(s_pressure, 0.0f, MOCK_DEFLATE_RATE_MMHG_S, dt);
            break;

        case UI_STATE_INFLATING:
            s_pressure = mock_approach(s_pressure, s_setpoint, MOCK_INFLATE_RATE_MMHG_S, dt);
            if (s_user_btn) {
                s_user_btn = false;
                s_aborted = true;
                mock_set_state(UI_STATE_DEFLATING_FAST);
            } else if (s_pressure >= s_setpoint) {
                s_inflate_ms = now - s_cycle_start;
                mock_set_state(UI_STATE_STABILIZING);
            } else {
                /* subindo */
            }
            break;

        case UI_STATE_STABILIZING:
        case UI_STATE_STABILIZED:
            /* +3 mmHg de overshoot que decai, depois oscila +-2 */
            s_pressure = s_setpoint + (2.0f * sinf((float)now / 700.0f)) +
                         ((s_state == UI_STATE_STABILIZING) ? 3.0f : 0.0f);
            if (s_user_btn) {
                s_user_btn = false;
                s_aborted = (s_state == UI_STATE_STABILIZING);
                mock_set_state(UI_STATE_DEFLATING_FAST);
            } else if ((s_state == UI_STATE_STABILIZING) && (in_state >= MOCK_STABILIZE_MS)) {
                mock_set_state(UI_STATE_STABILIZED);
            } else {
                /* mantendo */
            }
            break;

        case UI_STATE_DEFLATING_FAST:
            s_pressure = mock_approach(s_pressure, 0.0f, MOCK_DEFLATE_RATE_MMHG_S, dt);
            if (s_pressure <= 0.5f) {
                s_pressure = 0.0f;
                mock_finish_cycle();
                mock_set_state(UI_STATE_IDLE);
            }
            break;

        case UI_STATE_LEAK_TEST:
            if (s_user_btn) {
                s_user_btn = false;
                s_pressure = 0.0f;
                mock_set_state(UI_STATE_CONFIG);
            } else if (!s_leak_holding) {
                s_pressure = mock_approach(s_pressure, MOCK_LEAK_TARGET_MMHG, 30.0f, dt);
                if (s_pressure >= MOCK_LEAK_TARGET_MMHG) {
                    s_leak_holding = true;
                    s_state_tick = now;
                }
            } else if (in_state < MOCK_LEAK_HOLD_MS) {
                s_pressure -= MOCK_LEAK_RATE_MMHG_S * dt;
            } else {
                s_pressure = 0.0f;
                mock_set_state(UI_STATE_CONFIG);
            }
            break;

        case UI_STATE_CONFIG:
            /* calibracao: bomba manual leva a pressao a ~3 mmHg por % */
            s_pressure = mock_approach(s_pressure, 3.0f * (float)s_cal_pump_pct, 30.0f, dt);
            break;

        case UI_STATE_FAULT:
        default:
            s_pressure = mock_approach(s_pressure, 0.0f, MOCK_DEFLATE_RATE_MMHG_S, dt);
            break;
    }

    if (s_cycle_active && (s_pressure > s_cycle_peak)) {
        s_cycle_peak = s_pressure;
    }
}

void UiMock_UserButton(void)
{
    /* Igual a inputTask do firmware: em IDLE inicia direto; fora de IDLE
     * vira bt_user_pressed (consumido pelo controle ou pela UI). */
    if (s_state == UI_STATE_IDLE) {
        UiBackend_StartInflation();
    } else {
        s_user_btn = true;
    }
}

void UiMock_InjectError(uint16_t code)
{
    s_error = code;
    printf("[mock] erro E%03u injetado\n", (unsigned int)code);

    if (s_cycle_active) {
        s_fault = true;
    }
    if ((code == 1u) || (code == 2u)) {
        mock_set_state(UI_STATE_FAULT);
        s_cycle_active = false; /* estado seguro: ciclo invalidado sem resultado */
    } else if ((s_state != UI_STATE_IDLE) && (s_state != UI_STATE_CONFIG)) {
        mock_set_state(UI_STATE_DEFLATING_FAST);
    } else {
        /* parado: so' registra */
    }
}

void UiMock_ClearError(void)
{
    s_error = UI_ERR_NONE;
    if (s_state == UI_STATE_FAULT) {
        mock_set_state(UI_STATE_IDLE);
    }
    printf("[mock] erro limpo\n");
}

void UiMock_ToggleSd(void)
{
    s_sd_ok = !s_sd_ok;
    printf("[mock] SD %s\n", s_sd_ok ? "montado" : "ausente");
}

void UiMock_ToggleRtc(void)
{
    s_rtc_ok = !s_rtc_ok;
    printf("[mock] RTC %s\n", s_rtc_ok ? "valido" : "invalido");
}

/* ------------------------------------------------------------------------ */
/* ui_backend.h                                                             */
/* ------------------------------------------------------------------------ */

UiState_t UiBackend_GetState(void)        { return s_state; }
float     UiBackend_GetPressureMmHg(void) { return s_pressure; }
float     UiBackend_GetSetpointMmHg(void) { return s_setpoint; }
uint16_t  UiBackend_GetMaxSetpointMmHg(void) { return MOCK_MAX_SETPOINT_MMHG; }
uint32_t  UiBackend_GetTickMs(void)       { return SDL_GetTicks(); }
bool      UiBackend_IsSdMounted(void)     { return s_sd_ok; }
bool      UiBackend_IsRtcValid(void)      { return s_rtc_ok; }
uint16_t  UiBackend_GetLastError(void)    { return s_error; }
UiUnit_t  UiBackend_GetUnit(void)         { return s_unit; }
uint32_t  UiBackend_GetLeakHoldTimeMs(void) { return MOCK_LEAK_HOLD_MS; }
float     UiBackend_GetLeakMaxDropMmHg(void) { return MOCK_LEAK_MAX_DROP_MMHG; }
const char * UiBackend_GetDeviceId(void)  { return "myPRESSURE NP001 (SIM)"; }
const char * UiBackend_GetFirmwareVersion(void) { return "0.6.0"; }

float UiBackend_GetRawPressureMmHg(void)
{
    return (s_pressure * 1.04f) + s_cal_offset_mmhg; /* "erro" de ganho de 4% */
}

bool UiBackend_GetDateTime(UiDateTime_t * out)
{
    time_t now = time(NULL) + (time_t)s_time_offset_s;
    struct tm * lt = localtime(&now);

    if ((out == NULL) || (lt == NULL)) {
        return false;
    }
    out->year = (uint16_t)(lt->tm_year + 1900);
    out->month = (uint8_t)(lt->tm_mon + 1);
    out->day = (uint8_t)lt->tm_mday;
    out->hour = (uint8_t)lt->tm_hour;
    out->minute = (uint8_t)lt->tm_min;
    out->second = (uint8_t)lt->tm_sec;
    return true;
}

bool UiBackend_SetDateTime(const UiDateTime_t * dt)
{
    struct tm t;
    time_t target;

    if ((dt == NULL) || (dt->month < 1u) || (dt->month > 12u) || (dt->day < 1u) || (dt->day > 31u)) {
        return false;
    }
    memset(&t, 0, sizeof(t));
    t.tm_year = (int)dt->year - 1900;
    t.tm_mon = (int)dt->month - 1;
    t.tm_mday = (int)dt->day;
    t.tm_hour = (int)dt->hour;
    t.tm_min = (int)dt->minute;
    t.tm_sec = (int)dt->second;
    t.tm_isdst = -1;
    target = mktime(&t);
    if (target == (time_t)-1) {
        return false;
    }
    s_time_offset_s = (long)(target - time(NULL));
    s_rtc_ok = true;
    return true;
}

bool UiBackend_ConsumeUserButton(void)
{
    bool pressed = s_user_btn;

    s_user_btn = false;
    return pressed;
}

bool UiBackend_PollCycleResult(UiCycleResult_t * out)
{
    if (!s_result_ready || (out == NULL)) {
        return false;
    }
    s_result_ready = false;
    *out = s_result;
    return true;
}

void UiBackend_SetSetpoint(float mmhg)
{
    if (mmhg < 0.0f) {
        mmhg = 0.0f;
    }
    if (mmhg > (float)MOCK_MAX_SETPOINT_MMHG) {
        mmhg = (float)MOCK_MAX_SETPOINT_MMHG;
    }
    s_setpoint = mmhg;
}

void UiBackend_StartInflation(void)
{
    if ((s_state != UI_STATE_IDLE) || (s_error == 2u)) {
        return; /* E002 bloqueia novos ensaios */
    }
    s_cycle_active = true;
    s_cycle_peak = s_pressure;
    s_cycle_start = SDL_GetTicks();
    s_inflate_ms = 0u;
    s_aborted = false;
    s_fault = false;
    mock_set_state(UI_STATE_INFLATING);
}

void UiBackend_EnterConfigMode(void)
{
    if (s_state == UI_STATE_IDLE) {
        mock_set_state(UI_STATE_CONFIG);
    }
}

void UiBackend_ExitConfigMode(void)
{
    s_cal_pump_pct = 0u;
    if ((s_state == UI_STATE_CONFIG) || (s_state == UI_STATE_LEAK_TEST)) {
        mock_set_state(UI_STATE_IDLE);
    }
    printf("[mock] configuracao salva (EEPROM simulada)\n");
}

void UiBackend_StartLeakTest(void)
{
    if (s_state == UI_STATE_CONFIG) {
        s_leak_holding = false;
        mock_set_state(UI_STATE_LEAK_TEST);
    }
}

void UiBackend_SetUnit(UiUnit_t unit)
{
    if (unit < UI_UNIT_COUNT) {
        s_unit = unit;
    }
}

void UiBackend_BuzzerBeep(uint16_t freq_hz, uint16_t duration_ms)
{
    printf("[buzzer] %u Hz / %u ms\n", (unsigned int)freq_hz, (unsigned int)duration_ms);
}

void UiBackend_SubmitCycleRecord(uint16_t operator_id, uint16_t participant_id,
                                 uint16_t cycle_number)
{
    printf("[mock] registro gravado: OP%02u P%03u ciclo %u (%s)\n",
           (unsigned int)operator_id, (unsigned int)participant_id,
           (unsigned int)cycle_number, s_result.nc_code);
}

void UiBackend_CalZero(void)
{
    s_cal_offset_mmhg = -(s_pressure * 1.04f);
}

void UiBackend_CalSetPumpPct(uint8_t pct)
{
    s_cal_pump_pct = (pct > 100u) ? 100u : pct;
}

void UiBackend_CalAbort(void)
{
    s_cal_pump_pct = 0u;
}

void UiBackend_CalCapture(uint8_t point, float ref_mmhg)
{
    printf("[mock] cal P%u: ref %d mmHg, bruto %d mmHg\n", (unsigned int)point,
           (int)ref_mmhg, (int)UiBackend_GetRawPressureMmHg());
}

bool UiBackend_CalComputeAndSave(char * msg, size_t msg_len)
{
    s_cal_pump_pct = 0u;
    if ((msg != NULL) && (msg_len > 0u)) {
        (void)snprintf(msg, msg_len, "Calibracao salva.");
    }
    return true;
}
