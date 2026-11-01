/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-11
 */

#include "storage_format.h"
#include <stdio.h>
#include <string.h>

/* 1 casa decimal, sem usar %f — este projeto linka --specs=nano.specs sem
 * -u _printf_float (ver cmake/gcc-arm-none-eabi.cmake); %f em snprintf()
 * simplesmente nao funcionaria no alvo. Mesma tecnica de main.c
 * (format_1dp), duplicada aqui porque Core/Src nao e' uma dependencia que
 * Services possa importar (direcao inversa da camada). */
static void fmt_1dp(float v, char *buf, size_t n)
{
    uint8_t neg = (v < 0.0f) ? 1u : 0u;
    float av = neg ? -v : v;
    int whole = (int)av;
    int frac = (int)(((av - (float)whole) * 10.0f) + 0.5f);

    if (frac >= 10) { frac = 0; whole += 1; }
    snprintf(buf, n, "%s%d.%d", neg ? "-" : "", whole, frac);
}

void StorageFormat_BuildTestRecPath(uint32_t test_id, uint16_t year, uint8_t month,
                                    char *out, size_t out_size)
{
    snprintf(out, out_size, "/tests/%04u/%02u/T%08lu.rec",
             (unsigned)year, (unsigned)month, (unsigned long)test_id);
}

void StorageFormat_BuildTestCsvPath(uint32_t test_id, uint16_t year, uint8_t month,
                                    char *out, size_t out_size)
{
    snprintf(out, out_size, "/tests/%04u/%02u/T%08lu.csv",
             (unsigned)year, (unsigned)month, (unsigned long)test_id);
}

void StorageFormat_BuildIndexPath(char *out, size_t out_size)
{
    snprintf(out, out_size, "/tests/index.csv");
}

void StorageFormat_BuildLogPath(const char *subdir, uint16_t year, uint8_t month,
                                char *out, size_t out_size)
{
    snprintf(out, out_size, "/logs/%s/%04u-%02u.log", subdir, (unsigned)year, (unsigned)month);
}

CycleRecord_NcCode_t StorageFormat_BuildRecLine(const StorageFormat_RecInfo_t *info,
                                                char *out, size_t out_size)
{
    CycleRecord_NcCode_t nc = CycleRecord_Evaluate(&info->metrics);
    char nc_str[8];
    char setpoint[16];
    char peak[16];
    char plat_mean[16];
    char plat_min[16];
    char plat_max[16];
    char t_inf[16];
    char t_plat[16];
    char t_defl[16];
    char residual[16];
    const char *conforme = (nc == CYCLE_NC00_CONFORME) ? "Conforme" : "Nao conforme";
    const char *etech = (info->metrics.technical_fault != 0u) ? "E0XX" : "E000";
    /* ^ codigo E0xx exato nao esta' disponivel aqui (CycleRecord_Metrics_t
     * so' tem o flag binario technical_fault) — quem monta StorageFormat_RecInfo_t
     * a partir de ControlLoop_GetLastError() pode decidir estender este
     * campo com o codigo real; placeholder documentado. */

    CycleRecord_NcCodeToString(nc, nc_str, sizeof(nc_str));
    fmt_1dp(info->metrics.setpoint_mmhg, setpoint, sizeof(setpoint));
    fmt_1dp(info->metrics.peak_mmhg, peak, sizeof(peak));
    fmt_1dp(info->metrics.plateau_mean_mmhg, plat_mean, sizeof(plat_mean));
    /* Ciclo sem plato: min/max ficam com a sentinela de Control_Core. */
    if (info->metrics.plateau_min_mmhg > info->metrics.plateau_max_mmhg) {
        fmt_1dp(0.0f, plat_min, sizeof(plat_min));
        fmt_1dp(0.0f, plat_max, sizeof(plat_max));
    } else {
        fmt_1dp(info->metrics.plateau_min_mmhg, plat_min, sizeof(plat_min));
        fmt_1dp(info->metrics.plateau_max_mmhg, plat_max, sizeof(plat_max));
    }
    fmt_1dp(info->metrics.inflate_time_s, t_inf, sizeof(t_inf));
    fmt_1dp(info->metrics.plateau_time_s, t_plat, sizeof(t_plat));
    fmt_1dp(info->metrics.deflate_time_s, t_defl, sizeof(t_defl));
    fmt_1dp(info->metrics.residual_mmhg, residual, sizeof(residual));

    snprintf(out, out_size,
             "T%08lu | %s | %s | %02u/%02u/%04u %02u:%02u:%02u | Ciclo %u |"
             " Alvo %s mmHg | Max %s | Plato medio %s | Faixa %s-%s |"
             " Insuflacao %s s | Plato %s s | Descompressao %s s |"
             " Pressao residual %s mmHg | %s | %s | %s |"
             " %s | %s | ultima calibracao %02u/%02u/%04u %02u:%02u:%02u",
             (unsigned long)info->test_id, info->participant_id, info->operator_id,
             (unsigned)info->datetime.day, (unsigned)info->datetime.month, (unsigned)info->datetime.year,
             (unsigned)info->datetime.hour, (unsigned)info->datetime.minute, (unsigned)info->datetime.second,
             (unsigned)info->cycle_number,
             setpoint, peak, plat_mean, plat_min, plat_max,
             t_inf, t_plat, t_defl, residual,
             conforme, nc_str, etech,
             info->device_id, info->firmware_version,
             (unsigned)info->last_calibration.day, (unsigned)info->last_calibration.month,
             (unsigned)info->last_calibration.year, (unsigned)info->last_calibration.hour,
             (unsigned)info->last_calibration.minute, (unsigned)info->last_calibration.second);

    return nc;
}

void StorageFormat_BuildIndexLine(const StorageFormat_RecInfo_t *info,
                                  CycleRecord_NcCode_t nc_code, const char *hash_hex,
                                  char *out, size_t out_size)
{
    char nc_str[8];
    char setpoint[16];
    char peak[16];

    CycleRecord_NcCodeToString(nc_code, nc_str, sizeof(nc_str));
    fmt_1dp(info->metrics.setpoint_mmhg, setpoint, sizeof(setpoint));
    fmt_1dp(info->metrics.peak_mmhg, peak, sizeof(peak));

    snprintf(out, out_size, "T%08lu;%02u/%02u/%04u %02u:%02u:%02u;%s;%s;%s;%s;%s",
             (unsigned long)info->test_id,
             (unsigned)info->datetime.day, (unsigned)info->datetime.month, (unsigned)info->datetime.year,
             (unsigned)info->datetime.hour, (unsigned)info->datetime.minute, (unsigned)info->datetime.second,
             info->operator_id, setpoint, peak, nc_str, hash_hex);
}

void StorageFormat_BuildCurveLine(uint32_t index, int16_t pressure_tenths,
                                  char *out, size_t out_size)
{
    uint32_t centis = (index * STORAGE_FORMAT_CURVE_PERIOD_MS) / 10u;
    int32_t p = (int32_t)pressure_tenths;
    uint32_t mag = (uint32_t)((p < 0) ? -p : p);

    snprintf(out, out_size, "%lu.%02lu,%s%lu.%lu",
             (unsigned long)(centis / 100u), (unsigned long)(centis % 100u),
             (p < 0) ? "-" : "", (unsigned long)(mag / 10u), (unsigned long)(mag % 10u));
}

void StorageFormat_IndexScanFeed(StorageFormat_IndexScan_t *scan, const uint8_t *data, size_t len)
{
    size_t i;

    for (i = 0u; i < len; i++) {
        uint8_t c = data[i];

        if (c == (uint8_t)'\n') {
            scan->digits = 0u;
            scan->current = 0u;
            scan->skip_line = 0u;
        } else if (scan->skip_line != 0u) {
            /* resto da linha */
        } else if (scan->digits == 0u) {
            scan->digits = 1u;
            scan->skip_line = (uint8_t)((c == (uint8_t)'T') ? 0u : 1u);
        } else if ((scan->digits <= 8u) && (c >= (uint8_t)'0') && (c <= (uint8_t)'9')) {
            scan->current = (scan->current * 10u) + ((uint32_t)c - (uint32_t)'0');
            scan->digits++;
        } else {
            if ((scan->digits == 9u) && (c == (uint8_t)';') && (scan->current > scan->max_id)) {
                scan->max_id = scan->current;
            }
            scan->skip_line = 1u;
        }
    }
}

uint32_t StorageFormat_TailReadOffset(uint32_t size, uint32_t block)
{
    /* Arredonda para cima: para baixo o fim do arquivo ficava de fora, e a corrente
     * retomava da penultima linha (achado 2026-10-31 no app). */
    return (size <= block) ? 0u : (((size - block) + 3u) & ~(uint32_t)3u);
}

static uint8_t is_hex_lower(char c)
{
    return (((c >= '0') && (c <= '9')) || ((c >= 'a') && (c <= 'f'))) ? 1u : 0u;
}

uint8_t StorageFormat_FindLastHash(const char *text, size_t len,
                                   char out_hex[AUDIT_CHAIN_HASH_HEX_LEN + 1u])
{
    static const char tag[] = ";hash=";
    const size_t tag_len = sizeof(tag) - 1u;
    const size_t need = tag_len + AUDIT_CHAIN_HASH_HEX_LEN;
    size_t pos = len;

    while (pos >= need) {
        size_t start = pos - need;

        pos--;
        if (memcmp(&text[start], tag, tag_len) == 0) {
            size_t i;

            for (i = 0u; i < AUDIT_CHAIN_HASH_HEX_LEN; i++) {
                if (is_hex_lower(text[start + tag_len + i]) == 0u) {
                    return 0u;
                }
            }
            memcpy(out_hex, &text[start + tag_len], AUDIT_CHAIN_HASH_HEX_LEN);
            out_hex[AUDIT_CHAIN_HASH_HEX_LEN] = '\0';
            return 1u;
        }
    }
    return 0u;
}

void StorageFormat_BuildLogLine(const StorageFormat_LogEntry_t *entry,
                                AuditChain_t *chain,
                                char *out, size_t out_size)
{
    static const char *const level_names[] = { "EVT", "ERR", "AUDIT" };
    char body[STORAGE_FORMAT_LINE_MAX];
    char pressure_str[16];
    char hash_hex[AUDIT_CHAIN_HASH_HEX_LEN + 1u];
    const char *code = (entry->code != NULL) ? entry->code : "";
    const char *msg = (entry->message != NULL) ? entry->message : "";
    uint32_t level_idx = (uint32_t)entry->level;
    const char *level_name = (level_idx < (sizeof(level_names) / sizeof(level_names[0])))
        ? level_names[level_idx] : "?";

    fmt_1dp(entry->pressure_mmhg, pressure_str, sizeof(pressure_str));

    if (entry->test_id != 0u) {
        snprintf(body, sizeof(body),
                 "%04u-%02u-%02uT%02u:%02u:%02u%+03d:00;seq=%06lu;lvl=%s;code=%s;"
                 "test=T%08lu;state=%s;p=%s;msg=%s;prev=%s",
                 (unsigned)entry->datetime.year, (unsigned)entry->datetime.month, (unsigned)entry->datetime.day,
                 (unsigned)entry->datetime.hour, (unsigned)entry->datetime.minute, (unsigned)entry->datetime.second,
                 (int)entry->tz_offset_hours, (unsigned long)entry->seq, level_name, code,
                 (unsigned long)entry->test_id, entry->state_name, pressure_str, msg,
                 chain->prev_hash_hex);
    } else {
        snprintf(body, sizeof(body),
                 "%04u-%02u-%02uT%02u:%02u:%02u%+03d:00;seq=%06lu;lvl=%s;code=%s;"
                 "test=;state=%s;p=%s;msg=%s;prev=%s",
                 (unsigned)entry->datetime.year, (unsigned)entry->datetime.month, (unsigned)entry->datetime.day,
                 (unsigned)entry->datetime.hour, (unsigned)entry->datetime.minute, (unsigned)entry->datetime.second,
                 (int)entry->tz_offset_hours, (unsigned long)entry->seq, level_name, code,
                 entry->state_name, pressure_str, msg, chain->prev_hash_hex);
    }

    AuditChain_AppendLine(chain, body, hash_hex, sizeof(hash_hex));
    snprintf(out, out_size, "%s;hash=%s", body, hash_hex);
}
