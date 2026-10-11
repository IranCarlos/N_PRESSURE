/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-11
 *
 * Avaliacao de conformidade clinica do ciclo (NC00-NC08), conforme
 * Docs/dev/formato-logs.md (extraido de
 * "Docs/Trilha eletronica de auditoria..."). C puro, sem HAL/FatFS — so'
 * decide o codigo NC a partir das metricas medidas; quem monta/grava o
 * registro .rec e' outra camada (Fase 5.2).
 *
 * IMPORTANTE — decisoes de interpretacao ainda NAO confirmadas pelo
 * usuario (documentadas em 00-auditoria.md, revisar antes de usar em
 * ensaio real):
 *  - Os campos "ate' 10 s" (insuflacao, plato) e "1 a 2 s" (descompressao)
 *    da tabela-fonte sao tratados como limite SUPERIOR apenas (mais rapido
 *    nunca e' no-conformidade) — confirmado pelo proprio exemplo do
 *    documento-fonte: ID000123 (conforme) tem descompressao de 0,94 s,
 *    abaixo do "1 s" nominal.
 *  - Overshoot (pico > 105% do alvo) e' tratado como parte de NC02
 *    (instabilidade), ja' que a tabela de codigos NC nao tem um codigo
 *    dedicado so' para overshoot.
 *  - NC01 (alvo nao atingido) = media do plato fora da banda de ±2%;
 *    NC02 (instabilidade) = minimo/maximo do plato (ou o pico) fora dessa
 *    banda mesmo com a media ok. Divisao nao e' explicita na tabela-fonte.
 *  - Prioridade quando mais de um NC falha ao mesmo tempo: NC08 > NC07 >
 *    NC01 > NC02 > NC03 > NC04 > NC05 > NC06 (meu julgamento de
 *    severidade decrescente; a tabela-fonte nao define ordem).
 */
#ifndef CYCLE_RECORD_H
#define CYCLE_RECORD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CYCLE_NC00_CONFORME = 0,
    CYCLE_NC01_ALVO_NAO_ATINGIDO,
    CYCLE_NC02_INSTABILIDADE_PLATO,
    CYCLE_NC03_INSUFLACAO_FORA_FAIXA,
    CYCLE_NC04_PLATO_FORA_FAIXA,
    CYCLE_NC05_DESCOMPRESSAO_FORA_FAIXA,
    CYCLE_NC06_RESIDUAL_ACIMA_LIMITE,
    CYCLE_NC07_INTERROMPIDO_OPERADOR,
    CYCLE_NC08_FALHA_TECNICA
} CycleRecord_NcCode_t;

/* Limites da fase piloto (Docs/dev/formato-logs.md §1) — explicitamente
 * preliminares no documento-fonte; revisar/congelar antes da coleta
 * definitiva. */
#define CYCLE_OVERSHOOT_MAX_RATIO   (1.05f)  /* pico <= alvo * 1.05 */
#define CYCLE_PLATEAU_BAND_RATIO    (0.02f)  /* banda do plato: alvo * (1 +- 0.02) */
#define CYCLE_INFLATE_TIME_MAX_S    (10.0f)
#define CYCLE_PLATEAU_TIME_MAX_S    (10.0f)
#define CYCLE_DEFLATE_TIME_MAX_S    (2.0f)
#define CYCLE_RESIDUAL_MAX_MMHG     (5.0f)   /* residual < 5 mmHg passa */

typedef struct {
    float setpoint_mmhg;
    float peak_mmhg;
    float plateau_mean_mmhg;
    float plateau_min_mmhg;
    float plateau_max_mmhg;
    float inflate_time_s;
    float plateau_time_s;
    float deflate_time_s;
    float residual_mmhg;
    uint8_t aborted_by_operator; /* 1 = NC07 (pedal/bt_user durante o ensaio) */
    uint8_t technical_fault;     /* 1 = ocorreu E0xx durante o ciclo -> NC08 */
} CycleRecord_Metrics_t;

CycleRecord_NcCode_t CycleRecord_Evaluate(const CycleRecord_Metrics_t *m);

/* "NC00".."NC08" (4 chars + terminador). */
void CycleRecord_NcCodeToString(CycleRecord_NcCode_t code, char *out, uint32_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* CYCLE_RECORD_H */
