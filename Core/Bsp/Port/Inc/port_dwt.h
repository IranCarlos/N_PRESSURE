/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-09-19
 */

#ifndef PORT_DWT_H
#define PORT_DWT_H

#include <stdint.h>
#include "stm32h7xx_hal.h" /* traz core_cm7.h (DWT/CoreDebug) transitivamente */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file port_dwt.h
 * @brief Contador de ciclos de CPU (DWT->CYCCNT) para medir jitter/latencia.
 *
 * Usado pelas Fases 3/4 do prompt de projeto para medir o jitter do loop de
 * controle de 1 ms e a latencia pedal->valvula (alvo <= 2 ms).
 */

/** @brief Habilita o contador de ciclos (DWT->CYCCNT), zerado a partir daqui. */
void PortDWT_Init(void);

/** @brief Le o contador de ciclos atual. Estoura (wrap) a cada 2^32 ciclos —
 *         a ~550 MHz isso e' ~7,8 s; para medir intervalos maiores que isso,
 *         subtrair como uint32_t ja trata o wraparound corretamente. */
static inline uint32_t PortDWT_GetCycles(void) {
    return DWT->CYCCNT;
}

#ifdef __cplusplus
}
#endif

#endif /* PORT_DWT_H */
