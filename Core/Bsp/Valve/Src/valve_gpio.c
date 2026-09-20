/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-09-20
 */

#include "valve_gpio.h"

static Valve_GpioMap_t s_map;

static void gpio_write(void *ctx, Valve_Id_t id, uint8_t pin_level)
{
    (void)ctx;
    HAL_GPIO_WritePin(s_map.valve[id].port, s_map.valve[id].pin,
                      (pin_level != 0u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void Valve_InitGpio(const Valve_GpioMap_t *map)
{
    Valve_Hw_t hw = { .write = gpio_write, .ctx = NULL, .open_level = 0u };

    s_map = *map;
    Valve_Init(&hw);
}
