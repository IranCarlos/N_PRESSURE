/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-22
 */

/**
 * @file calibration.h
 * @brief Fase 7.8: calculo e validacao da calibracao do sensor de pressao.
 *        C puro (sem HAL), testado no host.
 *
 * Dois usos, decisao do usuario (2026-10-22):
 *  - App DEV (PC): calibracao COMPLETA - 3 a 8 pontos contra o manometro de
 *    referencia na V3, reta por minimos quadrados, conferencia de
 *    linearidade, de faixa plausivel e de estabilidade de cada ponto.
 *  - IHM: VERIFICACAO pelo operador - 2 pontos. Erro pequeno: nada muda;
 *    erro aceitavel: o equipamento corrige sozinho; erro grande: alerta
 *    (procurar assistencia tecnica), sem mexer na calibracao.
 *
 * Modelo (o mesmo de Pressure_Core): pressao_kpa = d_kpa * escala + offset,
 * com d_kpa = leitura bruta - zero atmosferico.
 *
 * TODO(BANCADA): fluxo completo (USB + IHM) ainda nao validado contra o
 * manometro de referencia - checklist em Docs/dev/00-auditoria.md (Fase 7.8).
 */

#ifndef CALIBRATION_H
#define CALIBRATION_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAL_MMHG_PER_KPA       (7.50062f)
#define CAL_MAX_POINTS         (8U)
#define CAL_PC_MIN_POINTS      (3U)
/* Faixa minima entre o menor e o maior ponto do PC: com pontos muito
 * proximos a inclinacao fica mal determinada. */
#define CAL_PC_MIN_SPAN_MMHG   (50.0f)
/* Pontos do PC precisam diferir pelo menos isto (repetir um ponto nao
 * acrescenta informacao e distorce o peso da reta). */
#define CAL_PC_MIN_POINT_GAP_MMHG (5.0f)
/* Sanidade da reta: o sensor ja' sai convertido pela curva do datasheet,
 * entao a correcao real e' pequena. Fora disso e' montagem/sensor errado,
 * nao calibracao. */
#define CAL_SCALE_MIN          (0.80f)
#define CAL_SCALE_MAX          (1.25f)
#define CAL_OFFSET_MAX_MMHG    (15.0f)
/* Verificacao da IHM: abaixo disto de distancia entre os 2 pontos so' o
 * offset e' corrigido (a inclinacao de 2 pontos proximos e' ruido). */
#define CAL_VERIFY_MIN_SLOPE_SPAN_MMHG (30.0f)

typedef struct {
    float offset_kpa;
    float scale;
} Cal_Coeffs_t;

typedef struct {
    float ref_kpa;  /* pressao do manometro de referencia */
    float d_kpa;    /* leitura diferencial do sensor (antes da calibracao) */
} Cal_Point_t;

typedef enum {
    CAL_FIT_OK = 0,
    CAL_FIT_TOO_FEW_POINTS,
    CAL_FIT_SPAN_TOO_SMALL,
    CAL_FIT_DEGENERATE,      /* todos os d iguais */
    CAL_FIT_SCALE_RANGE,     /* escala fora de [CAL_SCALE_MIN, CAL_SCALE_MAX] */
    CAL_FIT_OFFSET_RANGE,    /* |offset| > CAL_OFFSET_MAX_MMHG */
    CAL_FIT_NONLINEAR        /* residuo maximo > limite de linearidade */
} Cal_FitResult_t;

typedef struct {
    Cal_Coeffs_t coeffs;
    float max_residual_mmhg; /* maior |referencia - reta| entre os pontos */
    float span_mmhg;
    uint8_t n;
} Cal_Fit_t;

/* Reta por minimos quadrados + validacao para a calibracao do PC.
 * `out` e' preenchido mesmo quando reprova (o PC mostra o motivo). */
Cal_FitResult_t Cal_FitPc(const Cal_Point_t *pts, uint8_t n, float linearity_max_mmhg,
                          Cal_Fit_t *out);

/* 1 se a reta e' plausivel (escala/offset dentro da sanidade). */
uint8_t Cal_CoeffsPlausible(const Cal_Coeffs_t *c);

/* Pressao em mmHg que a calibracao `c` daria para a leitura diferencial d. */
float Cal_ApplyMmHg(const Cal_Coeffs_t *c, float d_kpa);

/* 1 se `ref_kpa` esta' a menos de CAL_PC_MIN_POINT_GAP_MMHG de algum ponto. */
uint8_t Cal_IsDuplicatePoint(const Cal_Point_t *pts, uint8_t n, float ref_kpa);

typedef enum {
    CAL_VERIFY_OK = 0,        /* erro <= tolerancia: nada a fazer */
    CAL_VERIFY_CORRECTED,     /* erro aceitavel: `new_coeffs` corrige */
    CAL_VERIFY_ALERT,         /* erro grande: alertar, NAO corrigir */
    CAL_VERIFY_INVALID        /* pontos iguais/correcao implausivel: alertar */
} Cal_VerifyResult_t;

typedef struct {
    float err1_mmhg;          /* leitura - referencia no ponto 1 (com a calibracao atual) */
    float err2_mmhg;
    float max_abs_err_mmhg;
    uint8_t slope_corrected;  /* 1 = corrigiu escala e offset; 0 = so' offset */
    Cal_Coeffs_t new_coeffs;  /* valido quando CAL_VERIFY_CORRECTED */
} Cal_Verify_t;

/* Verificacao da IHM com 2 pontos contra a calibracao atual. */
Cal_VerifyResult_t Cal_Verify(const Cal_Coeffs_t *current, const Cal_Point_t *p1,
                              const Cal_Point_t *p2, float ok_mmhg, float fix_max_mmhg,
                              Cal_Verify_t *out);

/* ---- Registro persistente (EEPROM A/B, ver eeprom_storage.h) ------------- */

typedef enum {
    CAL_SOURCE_FACTORY = 0,   /* nunca calibrado: escala 1, offset 0 */
    CAL_SOURCE_PC = 1,        /* calibracao completa pelo app DEV */
    CAL_SOURCE_HMI_FIX = 2    /* correcao automatica da verificacao na IHM */
} Cal_Source_t;

typedef struct {
    Cal_Coeffs_t coeffs;
    uint16_t year;            /* data/hora da calibracao (RTC) */
    uint8_t  month, day, hour, minute, second;
    uint8_t  source;          /* Cal_Source_t */
    uint8_t  n_points;
    float    max_residual_mmhg;
} Cal_Record_t;

#define CAL_RECORD_BLOB_LEN (40U)

void Cal_RecordFactory(Cal_Record_t *r);
size_t Cal_RecordSerialize(const Cal_Record_t *r, uint32_t seq, uint8_t *out, size_t cap);
/* 1 se magic/versao/CRC conferem e a reta e' plausivel. */
uint8_t Cal_RecordParse(const uint8_t *in, size_t len, Cal_Record_t *r, uint32_t *seq);

#ifdef __cplusplus
}
#endif

#endif /* CALIBRATION_H */
