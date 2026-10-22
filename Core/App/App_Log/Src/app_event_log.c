/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-22
 */

#include "app_event_log.h"
#include "event_watch.h"
#include "control_loop.h"
#include "storage_task.h"
#include "rtc_service.h"
#include <stdio.h>

/* App nao inclui main.h (ver CLAUDE.md): handle do CubeMX declarado extern. */
extern RTC_HandleTypeDef hrtc;

#define APP_LOG_TZ_OFFSET_HOURS (-3) /* America/Sao_Paulo - ver storage_format.h */

static const SystemRuntime_t * s_rt;
static EventWatch_t s_watch;

void AppEventLog_Init(const SystemRuntime_t * runtime)
{
    s_rt = runtime;
    EventWatch_Init(&s_watch);
}

/* Texto curto pt-BR do erro (Apendice D) para a linha de log. */
static const char * error_text(uint8_t code)
{
    switch (code) {
        case 1u:  return "Sobrepressao";
        case 2u:  return "Falha do sensor de pressao";
        case 3u:  return "Timeout de inflar";
        case 4u:  return "Timeout de desinflar";
        case 5u:  return "Falha do cartao SD";
        case 13u: return "Vazamento durante a manutencao";
        default:  return "Erro";
    }
}

static void log_line(StorageFormat_LogLevel_t level, const char * code, const char * message)
{
    StorageFormat_LogEntry_t e = { 0 };
    RtcValidity_DateTime_t now;

    RtcService_Now(&hrtc, &now);
    e.datetime.year = (uint16_t)(2000u + now.year_2d);
    e.datetime.month = now.month;
    e.datetime.day = now.day;
    e.datetime.hour = now.hour;
    e.datetime.minute = now.minute;
    e.datetime.second = now.second;
    e.tz_offset_hours = APP_LOG_TZ_OFFSET_HOURS;
    e.level = level;
    e.code = code;
    e.test_id = 0u;
    e.state_name = EventWatch_StateName(s_rt->state);
    e.pressure_mmhg = s_rt->pressure_display;
    e.message = message;

    if (level == STORAGE_LOG_LEVEL_ERR) {
        (void)StorageTask_LogError(&e);
    } else {
        (void)StorageTask_LogEvent(&e);
    }
}

void AppEventLog_Poll(void)
{
    EventWatch_Snapshot_t now;
    EventWatch_Snapshot_t prev;
    const ControlCore_CycleStats_t * stats;
    uint32_t mask;
    char msg[64];
    char code[8];

    if (s_rt == NULL) {
        return;
    }

    stats = ControlLoop_GetCycleStats();
    now.state = s_rt->state;
    now.error = (uint8_t)ControlLoop_GetLastError();
    now.sd_mounted = StorageTask_GetStats().sd_mounted;
    now.cycle_finalized = (stats != NULL) ? stats->finalized : 0u;

    mask = EventWatch_Poll(&s_watch, &now, &prev);

    if ((mask & EVENT_WATCH_STATE_CHANGED) != 0u) {
        (void)snprintf(msg, sizeof(msg), "Estado %s -> %s",
                       EventWatch_StateName(prev.state), EventWatch_StateName(now.state));
        log_line(STORAGE_LOG_LEVEL_EVT, "", msg);
    }
    if ((mask & EVENT_WATCH_ERROR_RAISED) != 0u) {
        (void)snprintf(code, sizeof(code), "E%03u", (unsigned)now.error);
        log_line(STORAGE_LOG_LEVEL_ERR, code, error_text(now.error));
    }
    if ((mask & EVENT_WATCH_ERROR_CLEARED) != 0u) {
        (void)snprintf(msg, sizeof(msg), "Erro E%03u encerrado", (unsigned)prev.error);
        log_line(STORAGE_LOG_LEVEL_EVT, "", msg);
    }
    if ((mask & EVENT_WATCH_SD_INSERTED) != 0u) {
        log_line(STORAGE_LOG_LEVEL_EVT, "", "Cartao SD montado");
    }
    if ((mask & EVENT_WATCH_SD_REMOVED) != 0u) {
        /* Nao chega ao arquivo (o cartao saiu), mas fica na cauda em RAM
         * (GET_LOG_TAIL) e documenta o E005 do Apendice D. */
        log_line(STORAGE_LOG_LEVEL_ERR, "E005", error_text(5u));
    }
    if (((mask & EVENT_WATCH_CYCLE_FINISHED) != 0u) && (stats != NULL)) {
        (void)snprintf(msg, sizeof(msg), "Ensaio concluido: pico %d mmHg, alvo %d mmHg%s",
                       (int)stats->peak_mmhg, (int)s_rt->setpoint_mmhg,
                       (stats->aborted_by_operator != 0u) ? ", abortado" : "");
        log_line(STORAGE_LOG_LEVEL_EVT, "", msg);
    }
}
