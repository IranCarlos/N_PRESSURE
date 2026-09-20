/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-07-07
 * Modified: 2026-09-20
 */

#include "adc_acq.h"
#include "pressure_core.h"

/* Definido em main.c (gerado pelo CubeMX). Declarado aqui em vez de
 * incluir main.h para nao acoplar a camada Services ao arquivo top-level
 * da aplicacao — mesmo tratamento de falha que todo MX_*_Init() ja usa. */
extern void Error_Handler(void);

/**
 * Buffer do DMA circular do ADC2 (disparado pelo TIM15 a 1 kHz: cada posicao
 * tem 1 ms, a janela inteira cobre 16 ms). Ocupa exatamente uma linha de
 * D-Cache (32 B) e fica FORA da struct de estado: SCB_InvalidateDCache_by_Addr()
 * descarta dados nao sincronizados da mesma linha.
 * DMA_BUFFER: RAM_D1 (alcancavel por DMA1), nao DTCM — ver system_types.h.
 */
static volatile uint16_t s_adc_dma_raw[ADC_ACQ_DMA_SAMPLES] __attribute__((aligned(32))) DMA_BUFFER;

static struct {
    const SystemRuntime_t *runtime_data;
    const SensorConfig_t  *sensor_cal;
    PressureCore_t         core;
} s_acq;

/* Diagnostico (Live Watch): se half/full nao sobem, o DMA nao esta
 * transferindo; se err sobe, veja adc_err/dma_err (HAL ErrorCode). */
volatile struct {
    uint32_t half;
    uint32_t full;
    uint32_t err;
    uint32_t adc_err;
    uint32_t dma_err;
} g_adc_dbg;

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
    (void)hadc;
    g_adc_dbg.half++;
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    (void)hadc;
    g_adc_dbg.full++;
}

void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc)
{
    g_adc_dbg.err++;
    g_adc_dbg.adc_err = hadc->ErrorCode;
    g_adc_dbg.dma_err = (hadc->DMA_Handle != NULL) ? hadc->DMA_Handle->ErrorCode : 0u;
    if ((CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) != 0u) {
        __BKPT(0);
    }
}

static uint8_t adc_is_baseline_tracking_state(SystemState_t state)
{
    return (state == SYSTEM_STATE_BOOT) ||
           (state == SYSTEM_STATE_IDLE) ||
           (state == SYSTEM_STATE_CONFIG) ||
           (state == SYSTEM_STATE_FAULT);
}

void ADC_Acq_Init(ADC_HandleTypeDef *hadc,
                  TIM_HandleTypeDef *htim_trigger,
                  const SensorConfig_t *p_sensor_cal,
                  const SystemRuntime_t *p_runtime_data)
{
    PressureCal_t cal = { .offset_kpa = 0.0f, .scale = 1.0f };

    s_acq.runtime_data = p_runtime_data;
    s_acq.sensor_cal = p_sensor_cal;
    if (p_sensor_cal != NULL) {
        cal.offset_kpa = p_sensor_cal->offset_kpa;
        cal.scale = p_sensor_cal->scale;
    }
    PressureCore_Init(&s_acq.core, &cal);

    /* .dma_buffer e' NOLOAD (nao zerada no boot) e a RAM_D1 tem ECC: o DMA
     * escreve meias-palavras, o que forca read-modify-write de uma palavra
     * nunca inicializada. Inicializa com a CPU e publica no barramento. */
    for (size_t i = 0; i < ADC_ACQ_DMA_SAMPLES; i++) {
        s_adc_dma_raw[i] = 0u;
    }
    SCB_CleanDCache_by_Addr((uint32_t *)&s_adc_dma_raw[0], sizeof(s_adc_dma_raw));

    /* Calibracao de offset/linearidade do ADC (obrigatoria no H7 apos reset). */
    if (HAL_ADCEx_Calibration_Start(hadc, ADC_CALIB_OFFSET_LINEARITY,
                                    ADC_SINGLE_ENDED) != HAL_OK) {
        Error_Handler();
    }

    /* DMA circular armado primeiro; so' entao o timer comeca a disparar. */
    if (HAL_ADC_Start_DMA(hadc, (uint32_t *)&s_adc_dma_raw[0],
                          ADC_ACQ_DMA_SAMPLES) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_TIM_Base_Start(htim_trigger) != HAL_OK) {
        Error_Handler();
    }
}

void ADC_Acq_Update(void)
{
    uint16_t snapshot[ADC_ACQ_DMA_SAMPLES];
    uint8_t track = 0u;

    /* O DMA escreve em segundo plano sem avisar a CPU (D-Cache do M7). */
    SCB_InvalidateDCache_by_Addr((uint32_t *)&s_adc_dma_raw[0], sizeof(s_adc_dma_raw));
    for (size_t i = 0; i < ADC_ACQ_DMA_SAMPLES; i++) {
        snapshot[i] = s_adc_dma_raw[i];
    }

    if ((s_acq.runtime_data != NULL) &&
        (adc_is_baseline_tracking_state(s_acq.runtime_data->state) != 0U)) {
        track = 1u;
    }

    /* Reflete recalibracao feita em runtime (menu de calibracao / EEPROM). */
    if (s_acq.sensor_cal != NULL) {
        s_acq.core.cal.offset_kpa = s_acq.sensor_cal->offset_kpa;
        s_acq.core.cal.scale = s_acq.sensor_cal->scale;
    }

    PressureCore_ProcessBlock(&s_acq.core, snapshot, ADC_ACQ_DMA_SAMPLES, track);
}

float ADC_Acq_GetPressureKpa(void)
{
    return s_acq.core.pressure_kpa;
}

void ADC_Acq_ZeroNow(void)
{
    PressureCore_ZeroNow(&s_acq.core);
}

const PressureCore_t *ADC_Acq_GetDiagnostics(void)
{
    return &s_acq.core;
}

float ADC_Acq_ConvertPressure(float pressure_kpa, PressureUnit_t target_unit)
{
    switch (target_unit) {
        case SYS_PRESSURE_UNIT_MMHG:  return pressure_kpa * 7.50062f;
        case SYS_PRESSURE_UNIT_HPA:   return pressure_kpa * 10.0f;
        case SYS_PRESSURE_UNIT_PSI:   return pressure_kpa * 0.145038f;
        case SYS_PRESSURE_UNIT_CMH2O: return pressure_kpa * 10.1972f;
        case SYS_PRESSURE_UNIT_BAR:   return pressure_kpa * 0.01f;
        case SYS_PRESSURE_UNIT_KPA:
        default:                      return pressure_kpa;
    }
}
