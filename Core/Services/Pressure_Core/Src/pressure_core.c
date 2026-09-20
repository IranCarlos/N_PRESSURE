/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-09-20
 */

#include "pressure_core.h"
#include <string.h>
#include <math.h>

static float counts_to_sensor_volts(float counts)
{
    return counts * (PRESSURE_SENSOR_VSS_V / PRESSURE_ADC_MAX_COUNTS);
}

static float volts_to_kpa(float v)
{
    if (v < PRESSURE_SENSOR_MIN_V) {
        v = PRESSURE_SENSOR_MIN_V;
    } else if (v > PRESSURE_SENSOR_MAX_V) {
        v = PRESSURE_SENSOR_MAX_V;
    }
    return PRESSURE_SENSOR_MIN_KPA +
           ((v - PRESSURE_SENSOR_MIN_V) *
            (PRESSURE_SENSOR_MAX_KPA - PRESSURE_SENSOR_MIN_KPA) /
            (PRESSURE_SENSOR_MAX_V - PRESSURE_SENSOR_MIN_V));
}

void PressureCore_Init(PressureCore_t *pc, const PressureCal_t *cal)
{
    memset(pc, 0, sizeof(*pc));
    if (cal != NULL) {
        pc->cal = *cal;
    } else {
        pc->cal.scale = 1.0f;
    }
}

void PressureCore_ProcessBlock(PressureCore_t *pc, const uint16_t *raw,
                               size_t n, uint8_t track_baseline)
{
    uint32_t sum = 0u;
    uint16_t lo = 0xFFFFu;
    uint16_t hi = 0u;
    float diff;

    if ((n == 0u) || (raw == NULL)) {
        return;
    }

    for (size_t i = 0; i < n; i++) {
        uint16_t s = raw[i];
        sum += s;
        if (s < lo) { lo = s; }
        if (s > hi) { hi = s; }
    }

    pc->last_mean_counts = (uint16_t)((sum + (n / 2u)) / n);
    pc->last_min_counts  = lo;
    pc->last_max_counts  = hi;
    pc->last_p2p_counts  = (uint16_t)(hi - lo);

    /* Validade do sinal: baseada na media, antes de qualquer clamp. */
    pc->sensor_voltage = counts_to_sensor_volts((float)sum / (float)n);
    if ((pc->sensor_voltage < PRESSURE_VALID_MIN_V) ||
        (pc->sensor_voltage > PRESSURE_VALID_MAX_V)) {
        pc->flags |= PRESSURE_FLAG_OUT_OF_RANGE;
    } else {
        pc->flags &= ~(uint32_t)PRESSURE_FLAG_OUT_OF_RANGE;
    }

    /* Travado: so' faz sentido com >= 2 amostras no bloco. */
    if ((n >= 2u) && (pc->last_p2p_counts == 0u)) {
        if (pc->stuck_blocks < 0xFFFFFFFFu) { pc->stuck_blocks++; }
    } else {
        pc->stuck_blocks = 0u;
    }
    if (pc->stuck_blocks >= PRESSURE_STUCK_BLOCKS) {
        pc->flags |= PRESSURE_FLAG_STUCK;
    } else {
        pc->flags &= ~(uint32_t)PRESSURE_FLAG_STUCK;
    }

    pc->raw_kpa = volts_to_kpa(pc->sensor_voltage);

    /* Baseline no valor bruto, para nao anular offset/escala da calibracao. */
    if (track_baseline != 0u) {
        if (pc->atmospheric_valid == 0u) {
            pc->atmospheric_kpa = pc->raw_kpa;
            pc->atmospheric_valid = 1u;
        } else {
            pc->atmospheric_kpa +=
                (pc->raw_kpa - pc->atmospheric_kpa) * PRESSURE_ATM_ALPHA;
        }
    }

    diff = pc->raw_kpa;
    if (pc->atmospheric_valid != 0u) {
        diff -= pc->atmospheric_kpa;
    }
    pc->pressure_kpa = (diff * pc->cal.scale) + pc->cal.offset_kpa;
}

void PressureCore_ZeroNow(PressureCore_t *pc)
{
    pc->atmospheric_kpa = pc->raw_kpa;
    pc->atmospheric_valid = 1u;
}

int PressureCore_CalibrateTwoPoint(float ref1_kpa, float d1_kpa,
                                   float ref2_kpa, float d2_kpa,
                                   PressureCal_t *out)
{
    float dd = d2_kpa - d1_kpa;

    if ((out == NULL) || (fabsf(dd) < 1e-3f)) {
        return -1;
    }
    out->scale = (ref2_kpa - ref1_kpa) / dd;
    out->offset_kpa = ref1_kpa - (d1_kpa * out->scale);
    return 0;
}
