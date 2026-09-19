/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-09-17
 */

#ifndef __PEDAL_H
#define __PEDAL_H

#include "stm32h7xx_hal.h"
#include <stdint.h>

/*
 * Modulo novo: nao existe equivalente no projeto STM32L476 original.
 * O pedal e uma entrada de hardware exclusiva do N_PRESSURE (H723), lida por
 * captura de borda no TIM4 CH1 (pino PEDAL / PB6). Funcionalmente ele age
 * como um substituto do botao de usuario (BT_USER): pressiona-lo dispara o
 * mesmo efeito de Encoder_GetUserBtnEvent().
 */

/** Debounce por tempo minimo entre bordas de captura aceitas, em ms. */
#define PEDAL_DEBOUNCE_MS   150u

/**
 * @brief Inicializa a captura de borda do pedal.
 * @param htim Ponteiro para o handle do TIM configurado em Input Capture
 *             (canal 1), conforme gerado pelo CubeMX (TIM4).
 */
void Pedal_Init(TIM_HandleTypeDef *htim);

/**
 * @brief Notifica uma captura do TIM ao módulo de pedal.
 *        Deve ser chamada a partir de HAL_TIM_IC_CaptureCallback().
 * @param htim Handle recebido pelo callback do HAL.
 */
void Pedal_OnCaptureIrq(TIM_HandleTypeDef *htim);

/**
 * @brief Consome o evento de acionamento do pedal.
 * @return 1 se o pedal foi acionado desde a última chamada, 0 caso contrário.
 *         Chamar esta função limpa o evento interno.
 */
uint8_t Pedal_GetEvent(void);

#endif /* __PEDAL_H */
