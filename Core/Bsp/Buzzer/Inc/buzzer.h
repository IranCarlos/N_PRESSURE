/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-09-18
 */

#ifndef BUZZER_H
#define BUZZER_H

#include "main.h"

/**
 * @brief Inicializa o driver do buzzer.
 *
 * @param p_htim_buzzer Handle do timer configurado em PWM no canal usado
 *        pelo buzzer (TIM8 canal 2 / pino PC7 nesta placa — ver
 *        Docs/dev/hardware.md, secao "Audio"). O modulo guarda o ponteiro;
 *        quem o possui (main.c) e' quem conhece o handle fisico, mesmo
 *        principio ja usado por Encoder_Init()/ControlLoop_Init().
 */
void Buzzer_Init(TIM_HandleTypeDef *p_htim_buzzer);

/**
 * @brief Gera um tom no buzzer PASSIVO por PWM de hardware em freq_hz,
 *        durante duration_ms. Bloqueia a tarefa chamadora por essa duracao
 *        via osDelay() (nao ocupa a CPU — o tom continua saindo por
 *        hardware sem intervencao da CPU enquanto isso).
 */
void Buzzer_Beep(uint32_t freq_hz, uint32_t duration_ms);

#endif /* BUZZER_H */
