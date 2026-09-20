/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-09-20
 */

#include "pwm_motor.h"
#include <stddef.h>

static PwmMotor_Hw_t s_hw;
static uint8_t       s_ready;
static float         s_limit = 1.0f;
static float         s_power;

void PwmMotor_Init(const PwmMotor_Hw_t *hw)
{
    s_ready = 0u;
    s_limit = 1.0f;
    s_power = 0.0f;
    if ((hw == NULL) || (hw->set_compare == NULL) || (hw->period_counts == 0u)) {
        return;
    }
    s_hw = *hw;
    s_ready = 1u;
    s_hw.set_compare(s_hw.ctx, 0u);
}

void PwmMotor_SetLimit(float max_power)
{
    if (!(max_power >= 0.0f)) {        /* NaN ou negativo */
        max_power = 0.0f;
    }
    s_limit = (max_power > 1.0f) ? 1.0f : max_power;
}

float PwmMotor_SetPower(float power)
{
    uint32_t compare;

    if (s_ready == 0u) {
        return 0.0f;
    }
    if (!(power > 0.0f)) {             /* <= 0 ou NaN */
        power = 0.0f;
    }
    if (power > s_limit) {
        power = s_limit;
    }
    compare = (uint32_t)((power * (float)s_hw.period_counts) + 0.5f);
    if (compare > s_hw.period_counts) {
        compare = s_hw.period_counts;
    }
    s_hw.set_compare(s_hw.ctx, compare);
    s_power = power;
    return power;
}

float PwmMotor_GetPower(void)
{
    return s_power;
}

void PwmMotor_Stop(void)
{
    (void)PwmMotor_SetPower(0.0f);
}
