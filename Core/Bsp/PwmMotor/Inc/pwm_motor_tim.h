/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-09-20
 *
 * Binding STM32 HAL do driver da bomba: escreve o CCR do canal do timer.
 */
#ifndef PWM_MOTOR_TIM_H
#define PWM_MOTOR_TIM_H

#include "stm32h7xx_hal.h"
#include "pwm_motor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Inicia o PWM em potencia 0. Falha do HAL vai para Error_Handler (mesmo
 * criterio dos MX_*_Init). */
void PwmMotor_InitTim(TIM_HandleTypeDef *htim, uint32_t channel);

#ifdef __cplusplus
}
#endif

#endif /* PWM_MOTOR_TIM_H */
