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
 * @brief Habilita o DWT cycle counter (Cortex-M7), necessario para o
 *        delay_us() usado por Buzzer_Beep(). Chame uma vez na inicializacao.
 */
void Buzzer_Init(void);

/**
 * @brief Gera um tom no buzzer PASSIVO alternando o pino em freq_hz por
 *        duration_ms (onda quadrada por software; bloqueia a thread por
 *        essa duracao). Um buzzer passivo precisa de um sinal AC nessa
 *        faixa de frequencia; ligar o pino em nivel DC produz apenas um
 *        estalo/ruido do capacitor carregando, nao um tom audivel.
 */
void Buzzer_Beep(uint32_t freq_hz, uint32_t duration_ms);

#endif /* BUZZER_H */
