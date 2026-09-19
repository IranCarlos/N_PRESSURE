/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-07-07
 * Modified: 2026-08-03
 */

#ifndef __EEPROM_H
#define __EEPROM_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Chip configuration - edit here to change EEPROM model
 * ------------------------------------------------------------------------- */

/** Total EEPROM capacity in bytes. FT24C256A = 32 768 bytes (256 Kbit). */
#define EEPROM_CAPACITY          32768U

/** Page write buffer size in bytes. FT24C256A = 64 bytes. */
#define EEPROM_PAGE_SIZE         64U

/** Memory address width: 1 byte for <= 2 KB chips, 2 bytes for >= 4 KB chips. */
#define EEPROM_ADDR_SIZE         2U

/** Base 7-bit I2C device address (A2=A1=A0=0).
 *  All 24Cxx devices use 0x50 as the base address. */
#define EEPROM_BASE_ADDR         0x50U

/** Internal write cycle time in milliseconds (from datasheet, worst case). */
#define EEPROM_WRITE_CYCLE_MS    5U

/** I2C transfer timeout in milliseconds. */
#define EEPROM_I2C_TIMEOUT_MS    10U

/* -------------------------------------------------------------------------
 * Public types
 * ------------------------------------------------------------------------- */

/** Return codes for all EEPROM functions. */
typedef enum {
    EEPROM_OK      = 0, /**< Operation completed successfully. */
    EEPROM_ERROR   = 1, /**< I2C transfer error. */
    EEPROM_TIMEOUT = 2, /**< I2C or write-cycle timeout. */
    EEPROM_INVALID = 3  /**< NULL pointer or address/length out of range. */
} EEPROM_Status_t;

/**
 * @brief Platform I2C write callback.
 *
 * @param ctx       Platform context passed from EEPROM_Handle_t.ctx.
 * @param dev_addr  7-bit device address (NOT shifted).
 * @param mem_addr  Target memory address inside the EEPROM.
 * @param addr_size Address width in bytes (EEPROM_ADDR_SIZE).
 * @param data      Data to write (caller owns the buffer).
 * @param len       Number of bytes to write (<= EEPROM_PAGE_SIZE).
 * @return 0 on success, non-zero on failure.
 */
typedef int (*EEPROM_I2C_Write_t)(void *ctx,
                                  uint8_t dev_addr,
                                  uint16_t mem_addr,
                                  uint8_t addr_size,
                                  const uint8_t *data,
                                  uint16_t len);

/**
 * @brief Platform I2C read callback (sequential read, no page limit).
 */
typedef int (*EEPROM_I2C_Read_t)(void *ctx,
                                 uint8_t dev_addr,
                                 uint16_t mem_addr,
                                 uint8_t addr_size,
                                 uint8_t *data,
                                 uint16_t len);

/**
 * @brief Platform millisecond delay callback.
 */
typedef void (*EEPROM_Delay_t)(uint32_t ms);

/** EEPROM driver handle. Treat as opaque; initialise via EEPROM_Init(). */
typedef struct {
    void              *ctx;      /**< Platform context (e.g. I2C handle). */
    uint8_t            dev_addr; /**< Resolved 7-bit I2C address. */
    EEPROM_I2C_Write_t write;    /**< Platform write callback. */
    EEPROM_I2C_Read_t  read;     /**< Platform read callback. */
    EEPROM_Delay_t     delay;    /**< Platform delay callback. */
} EEPROM_Handle_t;

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

/**
 * @brief Initialise the EEPROM driver with platform callbacks.
 *
 * @param handle     Handle to populate.
 * @param ctx        Platform context forwarded to every callback (may be NULL).
 * @param addr_pins  Logic state of hardware pins A2:A1:A0 (0-7).
 * @param write_fn   Platform I2C write function.
 * @param read_fn    Platform I2C read function.
 * @param delay_fn   Platform millisecond delay function.
 * @return EEPROM_OK, or EEPROM_INVALID if any pointer argument is NULL.
 */
EEPROM_Status_t EEPROM_Init(EEPROM_Handle_t    *handle,
                            void               *ctx,
                            uint8_t             addr_pins,
                            EEPROM_I2C_Write_t  write_fn,
                            EEPROM_I2C_Read_t   read_fn,
                            EEPROM_Delay_t      delay_fn);

/**
 * @brief Write arbitrary bytes, handling page-boundary splits automatically.
 *
 * @param handle  Initialised EEPROM handle.
 * @param addr    Start address (0 ... EEPROM_CAPACITY - 1).
 * @param data    Source buffer.
 * @param len     Number of bytes to write.
 * @return EEPROM_OK on success.
 */
EEPROM_Status_t EEPROM_Write(EEPROM_Handle_t *handle,
                             uint16_t         addr,
                             const uint8_t   *data,
                             uint16_t         len);

/**
 * @brief Read arbitrary bytes (sequential read, no page limit).
 *
 * @param handle  Initialised EEPROM handle.
 * @param addr    Start address.
 * @param data    Destination buffer.
 * @param len     Number of bytes to read.
 * @return EEPROM_OK on success.
 */
EEPROM_Status_t EEPROM_Read(EEPROM_Handle_t *handle,
                            uint16_t         addr,
                            uint8_t         *data,
                            uint16_t         len);

/**
 * @brief Write a single byte.
 */
EEPROM_Status_t EEPROM_WriteByte(EEPROM_Handle_t *handle,
                                 uint16_t         addr,
                                 uint8_t          byte);

/**
 * @brief Read a single byte.
 */
EEPROM_Status_t EEPROM_ReadByte(EEPROM_Handle_t *handle,
                                uint16_t         addr,
                                uint8_t         *byte);

/**
 * @brief Erase the entire EEPROM by writing 0xFF to every page.
 *
 * Takes approximately (EEPROM_CAPACITY / EEPROM_PAGE_SIZE) * EEPROM_WRITE_CYCLE_MS ms.
 * For FT24C256A: 512 pages * 5 ms ~= 2.6 s.
 */
EEPROM_Status_t EEPROM_Erase(EEPROM_Handle_t *handle);

/* -------------------------------------------------------------------------
 * Convenience macros - write/read any C type by value
 * ------------------------------------------------------------------------- */

/**
 * @brief Write any typed variable (struct, int, float, etc.) to EEPROM.
 *
 * @param handle  Pointer to EEPROM_Handle_t.
 * @param addr    Destination address.
 * @param val     Variable to write (must be an lvalue or compound literal).
 *
 * @example
 *   float temperature = 36.6f;
 *   EEPROM_WriteValue(&heeprom, 0x0010, temperature);
 */
#define EEPROM_WriteValue(handle, addr, val) \
    EEPROM_Write((handle), (addr), (const uint8_t *)&(val), (uint16_t)sizeof(val))

/**
 * @brief Read any typed variable from EEPROM.
 *
 * @param handle  Pointer to EEPROM_Handle_t.
 * @param addr    Source address.
 * @param val     Variable to receive the data (must be an lvalue).
 *
 * @example
 *   float temperature;
 *   EEPROM_ReadValue(&heeprom, 0x0010, temperature);
 */
#define EEPROM_ReadValue(handle, addr, val) \
    EEPROM_Read((handle), (addr), (uint8_t *)&(val), (uint16_t)sizeof(val))

#ifdef __cplusplus
}
#endif

#endif /* __EEPROM_H */