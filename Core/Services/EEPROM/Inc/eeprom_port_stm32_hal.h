/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-07-07
 * Modified: 2026-08-03
 */

#ifndef __EEPROM_PORT_STM32_HAL_H
#define __EEPROM_PORT_STM32_HAL_H

#include "eeprom.h"
#include "stm32h7xx_hal.h"   /* I2C_HandleTypeDef */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise an EEPROM handle using the STM32 HAL I2C driver.
 *
 * This function is the only entry point needed to use the EEPROM library
 * on STM32.  After this call, use EEPROM_Write / EEPROM_Read / EEPROM_WriteValue /
 * EEPROM_ReadValue with the populated handle.
 *
 * @param handle     EEPROM handle to configure.
 * @param hi2c       Pointer to the STM32 HAL I2C handle (e.g. &hi2c1).
 * @param addr_pins  State of hardware address pins A2:A1:A0 (0-7).
 *                   Matches the physical wiring:
 *                     - All pins to GND -> 0
 *                     - A0 to VCC, others GND -> 1
 *                     - etc.
 * @return EEPROM_OK on success, EEPROM_INVALID if any pointer is NULL.
 */
EEPROM_Status_t EEPROM_HAL_Init(EEPROM_Handle_t    *handle,
                                  I2C_HandleTypeDef  *hi2c,
                                  uint8_t             addr_pins);

#ifdef __cplusplus
}
#endif

#endif /* __EEPROM_PORT_STM32_HAL_H */

