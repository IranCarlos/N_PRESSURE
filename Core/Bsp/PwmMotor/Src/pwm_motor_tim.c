/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-09-20
 */

#include "pwm_motor_tim.h"

extern void Error_Handler(void);

static TIM_HandleTypeDef *s_htim;
static uint32_t           s_channel;

static void tim_set_compare(void *ctx, uint32_t compare)
{
    (void)ctx;
    __HAL_TIM_SET_COMPARE(s_htim, s_channel, compare);
}

void PwmMotor_InitTim(TIM_HandleTypeDef *htim, uint32_t channel)
{
    PwmMotor_Hw_t hw;

    s_htim = htim;
    s_channel = channel;
    hw.set_compare = tim_set_compare;
    hw.ctx = NULL;
    hw.period_counts = htim->Init.Period + 1u;
    PwmMotor_Init(&hw);                 /* CCR = 0 antes de habilitar a saida */

    if (HAL_TIM_PWM_Start(htim, channel) != HAL_OK) {
        Error_Handler();
    }
}
