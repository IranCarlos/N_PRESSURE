/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-07-07
 * Modified: 2026-08-03
 */

#include "eeprom_port_stm32_hal.h"

/* ---- Private callbacks -------------------------------------------------- */

static int hal_write(void           *ctx,
                     uint8_t         dev_addr,
                     uint16_t        mem_addr,
                     uint8_t         addr_size,
                     const uint8_t  *data,
                     uint16_t        len)
{
    I2C_HandleTypeDef *hi2c   = (I2C_HandleTypeDef *)ctx;
    uint16_t           mem_sz = (addr_size == 2U) ? I2C_MEMADD_SIZE_16BIT
                                                   : I2C_MEMADD_SIZE_8BIT;

    HAL_StatusTypeDef status = HAL_I2C_Mem_Write(
        hi2c,
        (uint16_t)(dev_addr << 1U),   /* HAL expects 8-bit shifted address */
        mem_addr,
        mem_sz,
        (uint8_t *)data,              /* HAL_I2C_Mem_Write takes non-const */
        len,
        EEPROM_I2C_TIMEOUT_MS
    );

    return (status == HAL_OK) ? 0 : -1;
}

static int hal_read(void     *ctx,
                    uint8_t   dev_addr,
                    uint16_t  mem_addr,
                    uint8_t   addr_size,
                    uint8_t  *data,
                    uint16_t  len)
{
    I2C_HandleTypeDef *hi2c   = (I2C_HandleTypeDef *)ctx;
    uint16_t           mem_sz = (addr_size == 2U) ? I2C_MEMADD_SIZE_16BIT
                                                   : I2C_MEMADD_SIZE_8BIT;

    HAL_StatusTypeDef status = HAL_I2C_Mem_Read(
        hi2c,
        (uint16_t)(dev_addr << 1U),
        mem_addr,
        mem_sz,
        data,
        len,
        EEPROM_I2C_TIMEOUT_MS
    );

    return (status == HAL_OK) ? 0 : -1;
}

static void hal_delay(uint32_t ms)
{
    HAL_Delay(ms);
}

/* ---- Public API --------------------------------------------------------- */

EEPROM_Status_t EEPROM_HAL_Init(EEPROM_Handle_t    *handle,
                                  I2C_HandleTypeDef  *hi2c,
                                  uint8_t             addr_pins)
{
    if (hi2c == NULL)
        return EEPROM_INVALID;

    return EEPROM_Init(handle,
                       (void *)hi2c,   /* ctx = I2C handle */
                       addr_pins,
                       hal_write,
                       hal_read,
                       hal_delay);
}

