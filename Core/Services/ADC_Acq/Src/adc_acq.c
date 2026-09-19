/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-07-07
 * Modified: 2026-08-03
 */

#include "adc_acq.h"
#include <string.h> // Para memset

/* Definido em main.c (gerado pelo CubeMX). Declarado aqui em vez de
 * incluir main.h para nao acoplar a camada Services ao arquivo top-level
 * da aplicacao — mesmo tratamento de falha que todo MX_*_Init() ja usa. */
extern void Error_Handler(void);

/* --- Definições do Hardware e Sensor --- */

// Tensão de referência do ADC (VREF+), em Volts.
#define ADC_VREF_V                      (3.3f)
// Resolução do ADC (16 bits = 65536 níveis; ADC2 do H723 configurado em
// 16 bits com oversampling 4x no CubeMX). Não afeta a calibração da EEPROM,
// que é expressa em volts/kPa, não em contagens brutas.
#define ADC_MAX_VALUE                   (65535.0f)
// Tensão de alimentação do sensor, em Volts.
#define SENSOR_VSS_V                    (5.0f)
// Curva do sensor diferencial: 0.2V = -25kPa, 4.7V = +25kPa.
#define SENSOR_MIN_V                    (0.2f)
#define SENSOR_MAX_V                    (4.7f)
#define SENSOR_MIN_KPA                  (-25.0f)
#define SENSOR_MAX_KPA                  (25.0f)

// Atualização lenta do zero atmosférico para compensar offset térmico/deriva.
#define ATM_BASELINE_ALPHA              (0.02f)

/* --- Filtro de Média Móvel --- */

// Número de amostras para o filtro. Um valor maior suaviza mais, mas aumenta a latência.
#define FILTER_SAMPLES_COUNT            (1)

/**
 * @brief Buffer dedicado do destino do DMA circular do ADC.
 *
 * Alinhado e dimensionado para ocupar sozinho uma linha inteira de D-Cache do
 * Cortex-M7 (32 bytes). É mantido FORA da struct de estado porque
 * SCB_InvalidateDCache_by_Addr() descarta qualquer dado ainda não
 * sincronizado na mesma linha de cache — não pode compartilhar linha com
 * campos que a CPU também escreve (o L4 não tem cache, então esse cuidado
 * não existia no código original).
 *
 * DMA_BUFFER: coloca em RAM_D1 (alcancavel por DMA1) em vez de DTCMRAM
 * (default do linker, NAO alcancavel por DMA no H7) — ver system_types.h.
 */
static volatile uint16_t s_adc_dma_raw[16] __attribute__((aligned(32))) DMA_BUFFER;

/**
 * @brief Estrutura interna do estado do módulo de aquisição.
 */
static struct {
    ADC_HandleTypeDef* hadc;                ///< Ponteiro para o handle do ADC.
    const SensorConfig_t* sensor_cal;       ///< Ponteiro para os dados de calibração.
    const SystemRuntime_t* runtime_data;    ///< Ponteiro para estado do sistema.

    float pressure_kpa;                     ///< Pressão calculada e filtrada em kPa.
    float atmospheric_kpa;                  ///< Baseline atmosférico rastreado.
    uint8_t atmospheric_initialized;        ///< Indica se baseline já foi inicializado.

    float filter_buffer[FILTER_SAMPLES_COUNT]; ///< Buffer circular para o filtro.
    uint8_t filter_index;                   ///< Índice atual do buffer do filtro.
    float filter_sum;                       ///< Soma acumulada para cálculo da média.
    uint8_t is_filter_full;                 ///< Flag para indicar se o buffer do filtro já foi preenchido.
} s_adc_state;

static uint8_t adc_is_baseline_tracking_state(SystemState_t state)
{
    return (state == SYSTEM_STATE_BOOT) ||
           (state == SYSTEM_STATE_IDLE) ||
           (state == SYSTEM_STATE_CONFIG) ||
           (state == SYSTEM_STATE_FAULT);
}


void ADC_Acq_Init(ADC_HandleTypeDef* hadc,
                  const SensorConfig_t* p_sensor_cal,
                  const SystemRuntime_t* p_runtime_data) {
    memset(&s_adc_state, 0, sizeof(s_adc_state));

    s_adc_state.hadc = hadc;
    s_adc_state.sensor_cal = p_sensor_cal;
    s_adc_state.runtime_data = p_runtime_data;

    // Inicia a conversão do ADC em modo DMA. O valor será atualizado em background.
    // O DMA deve ser configurado como Circular.
    if (HAL_ADC_Start_DMA(s_adc_state.hadc, (uint32_t*)&s_adc_dma_raw[0], 1) != HAL_OK) {
        Error_Handler();
    }
}

void ADC_Acq_Update(void) {
    float sensor_voltage;
    float pressure_raw_kpa;
    float pressure_diff_kpa;
    float current_pressure_kpa;

    // 1. Converter valor cru do ADC para tensão (considerando o divisor resistivo)
    // A saída de 5V do sensor é dividida para 3.3V. A leitura do ADC reflete essa tensão.
    // V_adc = adc_raw * (VREF / ADC_MAX)
    // V_sensor = V_adc * (5.0 / 3.3)
    // Simplificando: V_sensor = adc_raw * (VREF / ADC_MAX) * (SENSOR_VSS / VREF)
    // V_sensor = adc_raw * (SENSOR_VSS / ADC_MAX)
    // Invalida a linha de cache antes de ler: o DMA circular escreve em
    // segundo plano sem avisar a CPU, então uma cópia em cache poderia estar
    // obsoleta (Cortex-M7 tem D-Cache; o L4 original não precisava disso).
    SCB_InvalidateDCache_by_Addr((uint32_t *)&s_adc_dma_raw[0], sizeof(s_adc_dma_raw));
    sensor_voltage = (float)s_adc_dma_raw[0] * (SENSOR_VSS_V / ADC_MAX_VALUE);

    // 2. Converter tensão para pressão diferencial (kPa) pela curva do sensor.
    if (sensor_voltage < SENSOR_MIN_V) {
        sensor_voltage = SENSOR_MIN_V;
    } else if (sensor_voltage > SENSOR_MAX_V) {
        sensor_voltage = SENSOR_MAX_V;
    }

    pressure_raw_kpa = SENSOR_MIN_KPA +
        ((sensor_voltage - SENSOR_MIN_V) * (SENSOR_MAX_KPA - SENSOR_MIN_KPA) / (SENSOR_MAX_V - SENSOR_MIN_V));

    // 3. Rastrear baseline atmosférico em estados sem insuflação.
    // O baseline é rastreado no valor bruto do sensor para não anular offset/escala de calibração.
    if ((s_adc_state.runtime_data != NULL) &&
        (adc_is_baseline_tracking_state(s_adc_state.runtime_data->state) != 0U)) {

        if (s_adc_state.atmospheric_initialized == 0U) {
            s_adc_state.atmospheric_kpa = pressure_raw_kpa;
            s_adc_state.atmospheric_initialized = 1U;
        } else {
            s_adc_state.atmospheric_kpa +=
                (pressure_raw_kpa - s_adc_state.atmospheric_kpa) * ATM_BASELINE_ALPHA;
        }
    }

    // 4. Remove o baseline do valor bruto para obter pressão diferencial real.
    pressure_diff_kpa = pressure_raw_kpa;
    if (s_adc_state.atmospheric_initialized != 0U) {
        pressure_diff_kpa -= s_adc_state.atmospheric_kpa;
    }

    // 5. Aplicar calibração da EEPROM (escala e offset) no diferencial.
    current_pressure_kpa = (pressure_diff_kpa * s_adc_state.sensor_cal->scale) + s_adc_state.sensor_cal->offset_kpa;

    // 6. Adicionar ao filtro de média móvel
    // Subtrai o valor antigo que está saindo da janela do filtro
    s_adc_state.filter_sum -= s_adc_state.filter_buffer[s_adc_state.filter_index];

    // Adiciona o novo valor
    s_adc_state.filter_buffer[s_adc_state.filter_index] = current_pressure_kpa;
    s_adc_state.filter_sum += current_pressure_kpa;

    // Avança o índice do buffer circular
    s_adc_state.filter_index++;
    if (s_adc_state.filter_index >= FILTER_SAMPLES_COUNT) {
        s_adc_state.filter_index = 0;
        s_adc_state.is_filter_full = 1; // O filtro está cheio, a média é válida
    }

    // 7. Calcular a média e atualizar a pressão final
    uint8_t samples_in_filter = s_adc_state.is_filter_full ? FILTER_SAMPLES_COUNT : s_adc_state.filter_index;
    if (samples_in_filter > 0) {
        s_adc_state.pressure_kpa = s_adc_state.filter_sum / samples_in_filter;
    }
}

float ADC_Acq_GetPressureKpa(void) {
    return s_adc_state.pressure_kpa;
}

float ADC_Acq_ConvertPressure(float pressure_kpa, PressureUnit_t target_unit) {
    switch (target_unit) {
        case SYS_PRESSURE_UNIT_MMHG:
            // 1 kPa = 7.50062 mmHg
            return pressure_kpa * 7.50062f;

        case SYS_PRESSURE_UNIT_HPA:
            // 1 kPa = 10 hPa
            return pressure_kpa * 10.0f;

        case SYS_PRESSURE_UNIT_PSI:
            // 1 kPa = 0.145038 PSI
            return pressure_kpa * 0.145038f;

        case SYS_PRESSURE_UNIT_CMH2O:
            // 1 kPa = 10.1972 cmH2O
            return pressure_kpa * 10.1972f;

        case SYS_PRESSURE_UNIT_BAR:
            // 1 kPa = 0.01 bar
            return pressure_kpa * 0.01f;

        case SYS_PRESSURE_UNIT_KPA:
        default:
            return pressure_kpa;
    }
}

// Não é necessário nenhum callback de DMA: como o DMA está em modo circular,
// `s_adc_dma_raw[0]` é atualizado automaticamente em background pelo
// periférico. A função Update() apenas invalida o cache e a consome.
