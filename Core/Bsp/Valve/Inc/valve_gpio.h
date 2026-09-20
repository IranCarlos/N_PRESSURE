/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-09-20
 *
 * Binding STM32 HAL do driver de valvulas (GPIO push-pull).
 */
#ifndef VALVE_GPIO_H
#define VALVE_GPIO_H

#include "stm32h7xx_hal.h"
#include "valve.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
} Valve_GpioPin_t;

typedef struct {
    Valve_GpioPin_t valve[VALVE_COUNT];
} Valve_GpioMap_t;

/* Placa NA com pino LOW = aberta (open_level = 0). Abre todas as valvulas. */
void Valve_InitGpio(const Valve_GpioMap_t *map);

#ifdef __cplusplus
}
#endif

#endif /* VALVE_GPIO_H */
