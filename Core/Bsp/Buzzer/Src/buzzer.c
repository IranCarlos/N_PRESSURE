/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-09-18
 * Modified: 2026-09-19
 */

#include "buzzer.h"
#include "cmsis_os2.h"

/** @brief Canal do TIM8 ligado ao buzzer (PC7, ver hardware.md). */
#define BUZZER_TIM_CHANNEL (TIM_CHANNEL_2)

static TIM_HandleTypeDef *s_htim_buzzer;

/** @brief Clock de kernel do TIM8 (dominio APB2), lido do RCC em tempo de
 *         execucao — nao assume nenhum divisor especifico de SystemClock_Config().
 *
 * Regra do RM0468 (RCC_CFGR.TIMPRE): com TIMPRE=0, o timer roda a 2x PCLK2
 * quando o prescaler de APB2 e' diferente de 1, senao a 1x PCLK2. Com
 * TIMPRE=1, roda a HCLK se o prescaler de APB2 for 1, 2 ou 4, senao a 4x
 * PCLK2. Codificacao de D2PPRE2[2:0]: 0xx=/1, 100=/2, 101=/4, 110=/8, 111=/16.
 * (Antes este modulo assumia SystemCoreClock/2, valido so' com AHB=/2 e
 * APB2=/2 — com outros divisores o tom saia uma oitava fora.)
 */
static uint32_t get_tim8_kernel_clock_hz(void) {
    uint32_t pclk2 = HAL_RCC_GetPCLK2Freq();
    uint32_t ppre2 = (RCC->D2CFGR & RCC_D2CFGR_D2PPRE2) >> RCC_D2CFGR_D2PPRE2_Pos;
    uint32_t presc_is_1 = ((ppre2 & 0x4u) == 0u) ? 1u : 0u;

    if ((RCC->CFGR & RCC_CFGR_TIMPRE) == 0u) {
        return (presc_is_1 != 0u) ? pclk2 : (pclk2 * 2u);
    }
    return (ppre2 <= 5u) ? HAL_RCC_GetHCLKFreq() : (pclk2 * 4u);
}

/** @brief Ajusta prescaler/ARR do timer para gerar freq_hz na saida de PWM.
 *
 * TIM8 e' de 16 bits (PSC e ARR limitados a 0..65535) — para cobrir toda a
 * faixa audivel (na pratica ~100 Hz a poucos kHz) com um unico Prescaler
 * fixo nao daria: em 200 Hz precisariamos de um ARR bem acima de 65535 com
 * Prescaler=0. Por isso o prescaler e' recalculado a cada tom.
 */
static void set_tone_frequency(uint32_t freq_hz) {
    uint32_t timer_clk_hz = get_tim8_kernel_clock_hz();
    uint32_t total_div = timer_clk_hz / freq_hz;
    uint32_t psc = total_div / 65536u; /* menor prescaler que faz o ARR caber em 16 bits */
    uint32_t arr = (total_div / (psc + 1u));

    if (arr > 0u) {
        arr -= 1u;
    }

    __HAL_TIM_SET_PRESCALER(s_htim_buzzer, psc);
    __HAL_TIM_SET_AUTORELOAD(s_htim_buzzer, arr);
    __HAL_TIM_SET_COMPARE(s_htim_buzzer, BUZZER_TIM_CHANNEL, arr / 2u); /* duty 50% */
}

void Buzzer_Init(TIM_HandleTypeDef *p_htim_buzzer) {
    s_htim_buzzer = p_htim_buzzer;
}

void Buzzer_Beep(uint32_t freq_hz, uint32_t duration_ms) {
    if (freq_hz == 0u) {
        return;
    }

    set_tone_frequency(freq_hz);
    HAL_TIM_PWM_Start(s_htim_buzzer, BUZZER_TIM_CHANNEL);
    osDelay(duration_ms);
    HAL_TIM_PWM_Stop(s_htim_buzzer, BUZZER_TIM_CHANNEL);
}
