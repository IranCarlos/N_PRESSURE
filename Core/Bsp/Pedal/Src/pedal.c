/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-09-17
 */

#include "pedal.h"

/* Definido em main.c (gerado pelo CubeMX). Declarado aqui em vez de
 * incluir main.h para nao acoplar a camada Bsp ao arquivo top-level da
 * aplicacao — mesmo tratamento de falha que todo MX_*_Init() ja usa. */
extern void Error_Handler(void);

static volatile uint8_t  s_pedal_event;
static volatile uint32_t s_last_capture_ms;

void Pedal_Init(TIM_HandleTypeDef *htim)
{
    s_pedal_event     = 0u;
    s_last_capture_ms = 0u;

    if (HAL_TIM_IC_Start_IT(htim, TIM_CHANNEL_1) != HAL_OK) {
        Error_Handler();
    }
}

void Pedal_OnCaptureIrq(TIM_HandleTypeDef *htim)
{
    uint32_t now;

    if ((htim->Instance != TIM4) || (htim->Channel != HAL_TIM_ACTIVE_CHANNEL_1)) {
        return;
    }

    /* Debounce por tempo minimo: o filtro de entrada do TIM (ICFilter) ja
     * rejeita ruido de alta frequencia; isto cobre re-acionamentos mecanicos
     * do pedal em uma escala de tempo humana. */
    now = HAL_GetTick();
    if ((now - s_last_capture_ms) < PEDAL_DEBOUNCE_MS) {
        return;
    }
    s_last_capture_ms = now;
    s_pedal_event = 1u;
}

uint8_t Pedal_GetEvent(void)
{
    uint8_t ev = s_pedal_event;
    s_pedal_event = 0u;
    return ev;
}

/* Sobrescreve o callback fraco do HAL. TIM4 e usado exclusivamente pelo
 * pedal neste projeto, entao nao ha conflito com outro consumidor. */
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    Pedal_OnCaptureIrq(htim);
}
