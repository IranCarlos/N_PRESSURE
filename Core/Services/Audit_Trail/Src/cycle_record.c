/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-11
 */

#include "cycle_record.h"
#include <string.h>
#include <stdio.h>

CycleRecord_NcCode_t CycleRecord_Evaluate(const CycleRecord_Metrics_t *m)
{
    float band_lo = m->setpoint_mmhg * (1.0f - CYCLE_PLATEAU_BAND_RATIO);
    float band_hi = m->setpoint_mmhg * (1.0f + CYCLE_PLATEAU_BAND_RATIO);
    float overshoot_limit = m->setpoint_mmhg * CYCLE_OVERSHOOT_MAX_RATIO;

    if (m->technical_fault != 0u) {
        return CYCLE_NC08_FALHA_TECNICA;
    }
    if (m->aborted_by_operator != 0u) {
        return CYCLE_NC07_INTERROMPIDO_OPERADOR;
    }
    if (m->plateau_mean_mmhg < band_lo) {
        return CYCLE_NC01_ALVO_NAO_ATINGIDO;
    }
    if ((m->plateau_min_mmhg < band_lo) || (m->plateau_max_mmhg > band_hi) ||
        (m->peak_mmhg > overshoot_limit)) {
        return CYCLE_NC02_INSTABILIDADE_PLATO;
    }
    if (m->inflate_time_s > CYCLE_INFLATE_TIME_MAX_S) {
        return CYCLE_NC03_INSUFLACAO_FORA_FAIXA;
    }
    if (m->plateau_time_s > CYCLE_PLATEAU_TIME_MAX_S) {
        return CYCLE_NC04_PLATO_FORA_FAIXA;
    }
    if (m->deflate_time_s > CYCLE_DEFLATE_TIME_MAX_S) {
        return CYCLE_NC05_DESCOMPRESSAO_FORA_FAIXA;
    }
    if (m->residual_mmhg >= CYCLE_RESIDUAL_MAX_MMHG) {
        return CYCLE_NC06_RESIDUAL_ACIMA_LIMITE;
    }
    return CYCLE_NC00_CONFORME;
}

void CycleRecord_NcCodeToString(CycleRecord_NcCode_t code, char *out, uint32_t out_size)
{
    snprintf(out, out_size, "NC%02u", (unsigned)code);
}
