/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-22
 */

#include "cal_manager.h"
#include "adc_acq.h"
#include "eeprom_storage_io.h"
#include "rtc_service.h"
#include "storage_task.h"
#include <stdio.h>
#include <string.h>

/* App nao inclui main.h (ver CLAUDE.md): handle do CubeMX declarado extern. */
extern RTC_HandleTypeDef hrtc;

#define CAL_MANAGER_TZ_OFFSET_HOURS (-3) /* America/Sao_Paulo - ver storage_format.h */

static SensorConfig_t * s_target;
static Cal_Record_t s_current;

static void apply_to_target(const Cal_Coeffs_t * c)
{
    if (s_target != NULL) {
        /* Dois floats alinhados: o ADC (controlTask) pode ler entre as duas
         * escritas e usar escala nova com offset velho por UM bloco de 1 ms -
         * diferenca de poucos decimos de mmHg num unico bloco, sem efeito
         * pratico (e so' acontece com o controle parado). */
        s_target->scale = c->scale;
        s_target->offset_kpa = c->offset_kpa;
    }
}

void CalManager_Init(SensorConfig_t * target, const Cal_Record_t * loaded)
{
    s_target = target;
    if (loaded != NULL) {
        s_current = *loaded;
    } else {
        Cal_RecordFactory(&s_current);
    }
    apply_to_target(&s_current.coeffs);
}

const Cal_Record_t * CalManager_Get(void)
{
    return &s_current;
}

uint8_t CalManager_Capture(float * d_kpa, float * deviation_mmhg)
{
    const PressureCore_t * diag = ADC_Acq_GetDiagnostics();

    if ((diag == NULL) || (diag->flags != 0u) || (diag->cal_avg_valid == 0u)) {
        return 0u;
    }
    *d_kpa = diag->cal_avg_kpa;
    *deviation_mmhg = diag->cal_dev_kpa * CAL_MMHG_PER_KPA;
    return 1u;
}

void CalManager_Log(uint8_t is_error, const char * code, const char * message)
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
    e.tz_offset_hours = CAL_MANAGER_TZ_OFFSET_HOURS;
    e.level = (is_error != 0u) ? STORAGE_LOG_LEVEL_ERR : STORAGE_LOG_LEVEL_AUDIT;
    e.code = (code != NULL) ? code : "";
    e.state_name = "";
    e.message = message;
    if (is_error != 0u) {
        (void)StorageTask_LogError(&e);
    } else {
        (void)StorageTask_LogAudit(&e);
    }
}

uint8_t CalManager_Commit(const Cal_Coeffs_t * coeffs, Cal_Source_t source, uint8_t n_points,
                          float max_residual_mmhg, const char * audit_prefix)
{
    Cal_Record_t rec;
    Cal_Coeffs_t before = s_current.coeffs;
    RtcValidity_DateTime_t now;
    char msg[64];

    rec = s_current;
    rec.coeffs = *coeffs;
    rec.source = (uint8_t)source;
    rec.n_points = n_points;
    rec.max_residual_mmhg = max_residual_mmhg;
    RtcService_Now(&hrtc, &now);
    rec.year = (uint16_t)(2000u + now.year_2d);
    rec.month = now.month;
    rec.day = now.day;
    rec.hour = now.hour;
    rec.minute = now.minute;
    rec.second = now.second;

    if (EEPROM_Storage_SaveCal(&rec) == 0u) {
        (void)snprintf(msg, sizeof(msg), "%s: falha ao gravar na EEPROM (nada mudou)", audit_prefix);
        CalManager_Log(1u, "E007", msg);
        return 0u;
    }
    s_current = rec;
    apply_to_target(&rec.coeffs);

    /* Antes -> depois, em duas linhas (cada linha do log tem ate' 63 caracteres). */
    (void)snprintf(msg, sizeof(msg), "%s: escala %.5f -> %.5f", audit_prefix,
                   (double)before.scale, (double)rec.coeffs.scale);
    CalManager_Log(0u, "", msg);
    (void)snprintf(msg, sizeof(msg), "%s: offset %.2f -> %.2f mmHg (%u pt, res %.2f)", audit_prefix,
                   (double)(before.offset_kpa * CAL_MMHG_PER_KPA),
                   (double)(rec.coeffs.offset_kpa * CAL_MMHG_PER_KPA),
                   (unsigned)n_points, (double)max_residual_mmhg);
    CalManager_Log(0u, "", msg);
    return 1u;
}
