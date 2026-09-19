/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-09-18
 */

#include "buzzer.h"

static void delay_us(uint32_t us) {
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = (SystemCoreClock / 1000000u) * us;
    while ((DWT->CYCCNT - start) < cycles) {
        // aguarda
    }
}

void Buzzer_Init(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void Buzzer_Beep(uint32_t freq_hz, uint32_t duration_ms) {
    uint32_t half_period_us = 500000u / freq_hz;
    uint32_t n_cycles = (duration_ms * 1000u) / (half_period_us * 2u);
    uint32_t i;

    for (i = 0; i < n_cycles; i++) {
        HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_SET);
        delay_us(half_period_us);
        HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);
        delay_us(half_period_us);
    }
}
