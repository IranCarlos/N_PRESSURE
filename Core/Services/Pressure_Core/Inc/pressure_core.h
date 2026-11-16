/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-09-20
 *
 * Logica pura do sensor de pressao (modelo em pressure_sensor_model.h:
 * MPXV7025DP ou MPXV5100GP): contagens do ADC -> kPa,
 * baseline atmosferico, calibracao e deteccao de sensor invalido.
 * SEM dependencia de HAL/CMSIS: compila no host (test/host/) e no alvo.
 * O acesso ao ADC/DMA fica em ADC_Acq, que apenas alimenta este modulo.
 */

#ifndef PRESSURE_CORE_H
#define PRESSURE_CORE_H

#include <stdint.h>
#include <stddef.h>
/* Curva, clamp e faixa valida (PRESSURE_SENSOR_*, PRESSURE_VALID_*) do
 * sensor selecionado. */
#include "pressure_sensor_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Tensao vista pelo sensor = tensao no ADC * (VSS / VREF): divisor
 * bufferizado 5 V -> 3.3 V. Valor nominal; o erro real e' absorvido pela
 * calibracao (scale/offset), que deve ser medida empiricamente. */
#define PRESSURE_ADC_FULL_SCALE_V    (3.3f)
#define PRESSURE_SENSOR_VSS_V        (5.0f)
#define PRESSURE_ADC_MAX_COUNTS      (65535.0f)

/* Amostras consecutivas com pico-a-pico == 0 para acusar sensor travado.
 * Com ADC de 16 bits + oversampling o ruido real nunca e' zero. */
#define PRESSURE_STUCK_BLOCKS        (500u)

/* Rastreio do zero atmosferico: chamado a cada bloco (1 ms), entao alpha
 * 0.0002 = constante de tempo ~5 s (so' deriva termica). Alem disso so'
 * acompanha desvios pequenos: pressao aplicada (> janela) nunca vira "zero". */
#define PRESSURE_ATM_ALPHA           (0.0002f)
#define PRESSURE_ATM_TRACK_WINDOW_KPA (0.5f)
/* Leitura ABAIXO do zero por mais que isto = o zero estava alto (pressao
 * relativa negativa num manguito aberto para o ar nao existe): o zero desce
 * rapido ate' a leitura, sem limite de janela. Cobre o boot com o tubo ja'
 * pressurizado (a 1a amostra vira o zero e, quando a pressao escoa pelas
 * valvulas abertas, saia da janela acima e o zero nunca mais corrigia —
 * achado 2026-10-17). Margem acima do ruido para nao puxar o zero pelos
 * picos negativos; ALPHA rapido (~100 blocos de constante de tempo). */
#define PRESSURE_ATM_DOWN_MARGIN_KPA (0.1f)
#define PRESSURE_ATM_DOWN_ALPHA      (0.01f)

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
    uint8_t atm_following_down; /* 1 = zero descendo ate' a leitura (ver PRESSURE_ATM_DOWN_*) */

    uint16_t last_mean_counts;
    uint16_t last_min_counts;
    uint16_t last_max_counts;
    uint16_t last_p2p_counts;    /* ruido instantaneo do bloco */
    uint32_t stuck_blocks;
    uint32_t flags;              /* PressureFlags_t */

    /* Fase 7.8: media lenta do diferencial (antes da calibracao) e seu
     * desvio medio, para capturar pontos de calibracao/verificacao: uma
     * leitura instantanea carrega a ondulacao da bomba de embolo. Constante
     * de tempo ~0,5 s (1 bloco por ms). cal_dev alto = pressao ainda mexendo. */
    float   cal_avg_kpa;
    float   cal_dev_kpa;
    uint8_t cal_avg_valid;
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
