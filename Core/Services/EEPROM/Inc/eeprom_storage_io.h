/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-07-24
 * Modified: 2026-08-03
 */

#ifndef EEPROM_STORAGE_IO_H
#define EEPROM_STORAGE_IO_H

#include <stdint.h>

#include "eeprom_storage.h"
#include "stm32h7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file eeprom_storage_io.h
 * @brief Persistent configuration load/save using EEPROM address map.
 */

/**
 * @brief Initialize EEPROM storage backend.
 * @param hi2c I2C handle used by EEPROM device.
 * @param addr_pins A2:A1:A0 hardware address bits (0-7).
 * @return 1 on success, 0 on failure.
 */
uint8_t EEPROM_Storage_Init(I2C_HandleTypeDef *hi2c, uint8_t addr_pins);

/**
 * @brief Load configuration from EEPROM map.
 * @param cfg Output configuration structure.
 * @return 1 if valid configuration was loaded, 0 otherwise.
 */
uint8_t EEPROM_Storage_Load(SystemConfig_t *cfg);

/**
 * @brief Save configuration to EEPROM map.
 * @param cfg Configuration to persist.
 * @return 1 on success, 0 on failure.
 */
uint8_t EEPROM_Storage_Save(const SystemConfig_t *cfg);

/**
 * @brief Fill configuration with safe default values.
 * @param cfg Configuration output pointer.
 */
void EEPROM_Storage_LoadDefaults(SystemConfig_t *cfg);

/**
 * @brief Load the USB protocol DEV auth key (Fase 7.2, usb_proto_auth.h).
 *        Regiao independente de SystemConfig_t - ver eeprom_storage.h.
 * @param key Output buffer, 32 bytes (USB_PROTO_AUTH_DEV_KEY_LEN).
 * @return 1 if a valid key was loaded (magic/versao/CRC ok), 0 otherwise
 *         (nunca provisionada, ou corrompida - key fica zerado).
 */
uint8_t EEPROM_Storage_LoadDevKey(uint8_t key[32]);

/**
 * @brief Save the USB protocol DEV auth key.
 * @param key 32 bytes (USB_PROTO_AUTH_DEV_KEY_LEN).
 * @return 1 on success, 0 on failure.
 */
uint8_t EEPROM_Storage_SaveDevKey(const uint8_t key[32]);

#ifdef __cplusplus
}
#endif

#endif /* EEPROM_STORAGE_IO_H */
