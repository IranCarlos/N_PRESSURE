/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-09-20
 *
 * Driver da bomba (PWM). Logica pura: o registrador de comparacao e' injetado,
 * entao compila no host. Binding STM32 (TIM3_CH1) em pwm_motor_tim.c.
 *
 * Potencia 0.0..1.0 -> CCR = potencia * (ARR + 1). Com ARR+1 = periodo, CCR ==
 * ARR+1 e' 100 % real (CCR == ARR seria 99,99 %). Entrada invalida (NaN) = 0.
 */
#ifndef PWM_MOTOR_H
#define PWM_MOTOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*PwmMotor_SetCompareFn)(void *ctx, uint32_t compare);

typedef struct {
    PwmMotor_SetCompareFn set_compare;
    void                 *ctx;
    uint32_t              period_counts; /* ARR + 1 */
} PwmMotor_Hw_t;

/* Registra o hardware e garante potencia 0. */
void PwmMotor_Init(const PwmMotor_Hw_t *hw);

/* Limita a potencia maxima permitida (0..1); protecao de bancada/config. */
void PwmMotor_SetLimit(float max_power);

/* Satura em [0, limite]; NaN -> 0. Retorna a potencia efetivamente aplicada. */
float PwmMotor_SetPower(float power);
float PwmMotor_GetPower(void);
void  PwmMotor_Stop(void);

#ifdef __cplusplus
}
#endif

#endif /* PWM_MOTOR_H */
