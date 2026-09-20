/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-09-20
 *
 * Logica pura do sensor de pressao (MPXV7025DP): contagens do ADC -> kPa,
 * baseline atmosferico, calibracao e deteccao de sensor invalido.
 * SEM dependencia de HAL/CMSIS: compila no host (test/host/) e no alvo.
 * O acesso ao ADC/DMA fica em ADC_Acq, que apenas alimenta este modulo.
 */

#ifndef PRESSURE_CORE_H
#define PRESSURE_CORE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Curva do sensor diferencial: 0.2 V = -25 kPa ... 4.7 V = +25 kPa (5 V). */
#define PRESSURE_SENSOR_MIN_V        (0.2f)
#define PRESSURE_SENSOR_MAX_V        (4.7f)
#define PRESSURE_SENSOR_MIN_KPA      (-25.0f)
#define PRESSURE_SENSOR_MAX_KPA      (25.0f)

/* Tensao vista pelo sensor = tensao no ADC * (VSS / VREF): divisor
 * bufferizado 5 V -> 3.3 V. Valor nominal; o erro real e' absorvido pela
 * calibracao (scale/offset), que deve ser medida empiricamente. */
#define PRESSURE_ADC_FULL_SCALE_V    (3.3f)
#define PRESSURE_SENSOR_VSS_V        (5.0f)
#define PRESSURE_ADC_MAX_COUNTS      (65535.0f)

/* Fora desta faixa (V no sensor) o sinal e' considerado cabo aberto/curto. */
#define PRESSURE_VALID_MIN_V         (0.10f)
#define PRESSURE_VALID_MAX_V         (4.90f)

/* Amostras consecutivas com pico-a-pico == 0 para acusar sensor travado.
 * Com ADC de 16 bits + oversampling o ruido real nunca e' zero. */
#define PRESSURE_STUCK_BLOCKS        (500u)

#define PRESSURE_ATM_ALPHA           (0.02f)

typedef enum {
    PRESSURE_FLAG_NONE       = 0u,
    PRESSURE_FLAG_OUT_OF_RANGE = (1u << 0), /* tensao fora de VALID_MIN..MAX */
    PRESSURE_FLAG_STUCK        = (1u << 1)  /* leitura constante */
} PressureFlags_t;

typedef struct {
    float offset_kpa; /* somado apos a escala */
    float scale;      /* ganho sobre o diferencial (1.0 = sem correcao) */
} PressureCal_t;

typedef struct {
    PressureCal_t cal;

    float   pressure_kpa;        /* saida final, calibrada */
    float   raw_kpa;             /* sem baseline nem calibracao */
    float   sensor_voltage;      /* V no sensor, antes do clamp */
    float   atmospheric_kpa;
    uint8_t atmospheric_valid;

    uint16_t last_mean_counts;
    uint16_t last_min_counts;
    uint16_t last_max_counts;
    uint16_t last_p2p_counts;    /* ruido instantaneo do bloco */
    uint32_t stuck_blocks;
    uint32_t flags;              /* PressureFlags_t */
} PressureCore_t;

void PressureCore_Init(PressureCore_t *pc, const PressureCal_t *cal);

/* Processa um bloco de n amostras (n >= 1). track_baseline != 0 atualiza o
 * zero atmosferico (estados sem insuflacao). */
void PressureCore_ProcessBlock(PressureCore_t *pc, const uint16_t *raw,
                               size_t n, uint8_t track_baseline);

/* Forca o baseline para a leitura atual (auto-zero manual, tecla de zerar). */
void PressureCore_ZeroNow(PressureCore_t *pc);

/* Calibracao de 2 pontos. d1/d2 = leitura diferencial (raw_kpa - baseline,
 * em kPa) medida enquanto o manometro de referencia (V3) marcava ref1/ref2.
 * Retorna 0 se ok, -1 se os pontos forem degenerados (d1 ~= d2). */
int PressureCore_CalibrateTwoPoint(float ref1_kpa, float d1_kpa,
                                   float ref2_kpa, float d2_kpa,
                                   PressureCal_t *out);

#ifdef __cplusplus
}
#endif

#endif /* PRESSURE_CORE_H */
