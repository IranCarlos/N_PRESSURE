/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-31
 *
 * Leva o manguito a uma pressao conhecida e espera ela assentar. Usada pela
 * verificacao da calibracao e pelo teste de vazamento na IHM; e' a mesma logica
 * de pc/core/procedures.py, validada na bancada. Logica pura: compila no host.
 */
#ifndef PRESSURE_TARGET_H
#define PRESSURE_TARGET_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PRESSURE_TARGET_IDLE = 0,
    PRESSURE_TARGET_RISING,
    PRESSURE_TARGET_SETTLING,
    PRESSURE_TARGET_STABLE,
    PRESSURE_TARGET_FAILED
} PressureTarget_State_t;

typedef enum {
    PRESSURE_TARGET_FAIL_NONE = 0,
    PRESSURE_TARGET_FAIL_OVER,      /* passou do alvo + margem */
    PRESSURE_TARGET_FAIL_NO_RISE,   /* nao chegou ao alvo a tempo */
    PRESSURE_TARGET_FAIL_UNSTABLE   /* chegou perto, mas a pressao nao se manteve */
} PressureTarget_Fail_t;

typedef struct {
    PressureTarget_State_t state;
    PressureTarget_Fail_t  fail;
    uint8_t  phase;
    float    target_mmhg;
    float    pressure_mmhg;     /* ultima leitura; no STABLE, a pressao assentada */
    uint32_t elapsed_ms;
    uint32_t phase_ms;
    uint32_t phase_len_ms;
    uint32_t rate_ms;
    float    rate_p_mmhg;
    float    rate_mmhg_s;
    float    first_mmhg;
    float    middle_mmhg;
    float    burst_gain_mmhg_s;
    float    burst_start_mmhg;
} PressureTarget_t;

typedef struct {
    float   pump_power;  /* 0..1 */
    uint8_t vent_open;   /* 1 = abrir a valvula fina (V2) */
} PressureTarget_Output_t;

void PressureTarget_Start(PressureTarget_t *pt, float target_mmhg);
void PressureTarget_Stop(PressureTarget_t *pt);

/* Chamar a cada `dt_ms`. Fora de RISING/SETTLING a saida e' bomba parada. */
void PressureTarget_Step(PressureTarget_t *pt, float pressure_mmhg, uint32_t dt_ms,
                         PressureTarget_Output_t *out);

#ifdef __cplusplus
}
#endif

#endif /* PRESSURE_TARGET_H */
