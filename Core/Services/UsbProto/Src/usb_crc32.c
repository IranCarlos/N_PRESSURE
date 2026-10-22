/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-14
 */
#include "usb_crc32.h"

#define USB_CRC32_POLY (0x04C11DB7UL)
#define USB_CRC32_INIT (0xFFFFFFFFUL)

uint32_t UsbCrc32_Compute(const uint8_t *data, size_t len)
{
    return UsbCrc32_Update(UsbCrc32_Init(), data, len);
}

uint32_t UsbCrc32_Init(void)
{
    return (uint32_t)USB_CRC32_INIT;
}

uint32_t UsbCrc32_Update(uint32_t crc_in, const uint8_t *data, size_t len)
{
    uint32_t crc = crc_in;
    size_t i;

    for (i = 0U; i < len; i++) {
        uint8_t bit;
        crc ^= ((uint32_t)data[i]) << 24;
        for (bit = 0U; bit < 8U; bit++) {
            if ((crc & 0x80000000UL) != 0UL) {
                crc = (crc << 1) ^ (uint32_t)USB_CRC32_POLY;
            } else {
                crc = crc << 1;
            }
        }
    }
    return crc;
}
