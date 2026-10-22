/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-07-07
 * Modified: 2026-08-03
 */

#include "eeprom.h"
#include <string.h>

/* ---- Private helpers ---------------------------------------------------- */

/**
 * Return non-zero if the range [addr, addr+len) fits inside the EEPROM.
 */
static int eeprom_range_valid(uint16_t addr, uint16_t len)
{
    /* Use 32-bit arithmetic to avoid uint16_t overflow. */
    return (len > 0U) &&
           ((uint32_t)addr + (uint32_t)len <= (uint32_t)EEPROM_CAPACITY);
}

/* ---- Public API --------------------------------------------------------- */

EEPROM_Status_t EEPROM_Init(EEPROM_Handle_t    *handle,
                             void               *ctx,
                             uint8_t             addr_pins,
                             EEPROM_I2C_Write_t  write_fn,
                             EEPROM_I2C_Read_t   read_fn,
                             EEPROM_Delay_t      delay_fn)
{
    if ((handle == NULL) || (write_fn == NULL) ||
        (read_fn == NULL) || (delay_fn == NULL))
    {
        return EEPROM_INVALID;
    }

    handle->ctx      = ctx;
    handle->dev_addr = (uint8_t)(EEPROM_BASE_ADDR | (addr_pins & 0x07U));
    handle->write    = write_fn;
    handle->read     = read_fn;
    handle->delay    = delay_fn;

    return EEPROM_OK;
}

/* -------------------------------------------------------------------------
 * EEPROM_Write
 *
 * Splits the transfer into page-aligned chunks so that the EEPROM's
 * internal page buffer never wraps around. A write-cycle delay is
 * inserted after each page write.
 * ------------------------------------------------------------------------- */
EEPROM_Status_t EEPROM_Write(EEPROM_Handle_t *handle,
                              uint16_t         addr,
                              const uint8_t   *data,
                              uint16_t         len)
{
    if ((handle == NULL) || (data == NULL))
        return EEPROM_INVALID;

    if (!eeprom_range_valid(addr, len))
        return EEPROM_INVALID;

    uint16_t        remaining = len;
    uint16_t        cur_addr  = addr;
    const uint8_t  *src       = data;

    while (remaining > 0U)
    {
        /* How many bytes remain until the next page boundary? */
        uint16_t page_offset   = (uint16_t)(cur_addr % EEPROM_PAGE_SIZE);
        uint16_t space_in_page = (uint16_t)(EEPROM_PAGE_SIZE - page_offset);
        uint16_t write_len     = (remaining < space_in_page) ? remaining
                                                               : space_in_page;

        int ret = handle->write(handle->ctx,
                                handle->dev_addr,
                                cur_addr,
                                EEPROM_ADDR_SIZE,
                                src,
                                write_len);
        if (ret != 0)
            return EEPROM_ERROR;

        /* Wait for the internal write cycle to complete. */
        handle->delay(EEPROM_WRITE_CYCLE_MS);

        cur_addr  += write_len;
        src       += write_len;
        remaining -= write_len;
    }

    return EEPROM_OK;
}

/* -------------------------------------------------------------------------
 * EEPROM_Read
 *
 * EEPROM sequential read has no page-size restriction, so the entire
 * transfer is issued in a single I2C transaction.
 * ------------------------------------------------------------------------- */
EEPROM_Status_t EEPROM_Read(EEPROM_Handle_t *handle,
                             uint16_t         addr,
                             uint8_t         *data,
                             uint16_t         len)
{
    if ((handle == NULL) || (data == NULL))
        return EEPROM_INVALID;

    if (!eeprom_range_valid(addr, len))
        return EEPROM_INVALID;

    /* Leitura sequencial nao tem limite de pagina no chip, mas cada
     * transacao tem que caber em EEPROM_I2C_TIMEOUT_MS: 512 B a 400 kHz
     * levam ~11,5 ms (> 10 ms) e a leitura inteira falhava (achado de
     * bancada 2026-10-22, PARAM_COMMIT). Em blocos de EEPROM_PAGE_SIZE,
     * ~1,5 ms cada. */
    uint16_t remaining = len;
    uint16_t cur_addr  = addr;
    uint8_t *dst       = data;

    while (remaining > 0U)
    {
        uint16_t chunk = (remaining < EEPROM_PAGE_SIZE) ? remaining : EEPROM_PAGE_SIZE;
        int ret = handle->read(handle->ctx,
                               handle->dev_addr,
                               cur_addr,
                               EEPROM_ADDR_SIZE,
                               dst,
                               chunk);
        if (ret != 0)
            return EEPROM_ERROR;

        cur_addr  += chunk;
        dst       += chunk;
        remaining -= chunk;
    }

    return EEPROM_OK;
}

/* ------------------------------------------------------------------------- */

EEPROM_Status_t EEPROM_WriteByte(EEPROM_Handle_t *handle,
                                  uint16_t         addr,
                                  uint8_t          byte)
{
    return EEPROM_Write(handle, addr, &byte, 1U);
}

EEPROM_Status_t EEPROM_ReadByte(EEPROM_Handle_t *handle,
                                 uint16_t         addr,
                                 uint8_t         *byte)
{
    if (byte == NULL)
        return EEPROM_INVALID;
    return EEPROM_Read(handle, addr, byte, 1U);
}

/* -------------------------------------------------------------------------
 * EEPROM_Erase
 *
 * Writes 0xFF to every page sequentially.
 * Duration: (EEPROM_CAPACITY / EEPROM_PAGE_SIZE) * EEPROM_WRITE_CYCLE_MS.
 * FT24C256A: 512 pages * 5 ms ~= 2.6 s.
 * ------------------------------------------------------------------------- */
EEPROM_Status_t EEPROM_Erase(EEPROM_Handle_t *handle)
{
    if (handle == NULL)
        return EEPROM_INVALID;

    uint8_t  page_buf[EEPROM_PAGE_SIZE];
    uint16_t pages = (uint16_t)(EEPROM_CAPACITY / EEPROM_PAGE_SIZE);

    memset(page_buf, 0xFF, EEPROM_PAGE_SIZE);

    for (uint16_t p = 0U; p < pages; p++)
    {
        uint16_t addr = (uint16_t)(p * EEPROM_PAGE_SIZE);

        int ret = handle->write(handle->ctx,
                                handle->dev_addr,
                                addr,
                                EEPROM_ADDR_SIZE,
                                page_buf,
                                EEPROM_PAGE_SIZE);
        if (ret != 0)
            return EEPROM_ERROR;

        handle->delay(EEPROM_WRITE_CYCLE_MS);
    }

    return EEPROM_OK;
}

