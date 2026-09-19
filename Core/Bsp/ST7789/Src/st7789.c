/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-07-27
 * Modified: 2026-08-03
 */

#include "st7789.h"

/* Conjunto de bits de erro acumulados por HAL_SPI_Transmit(). Nenhuma das
 * funcoes abaixo checava o retorno antes — se a transmissao estivesse
 * falhando (timeout, erro de barramento), nunca saberiamos. */
static volatile uint32_t s_spi_error_count = 0u;

static void spi_transmit_checked(uint8_t *data, uint16_t size) {
    if (HAL_SPI_Transmit(&hspi1, data, size, 100) != HAL_OK) {
        s_spi_error_count++;
    }
}

uint32_t ST7789_GetSpiErrorCount(void) {
    return s_spi_error_count;
}

void ST7789_SendCmd(uint8_t cmd) {
    ST7789_DC_CMD();
    ST7789_CS_LOW();
    spi_transmit_checked(&cmd, 1);
    ST7789_CS_HIGH();
}

void ST7789_SendData(uint8_t data) {
    ST7789_DC_DATA();
    ST7789_CS_LOW();
    spi_transmit_checked(&data, 1);
    ST7789_CS_HIGH();
}

void ST7789_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    ST7789_SendCmd(0x2A); // Column Address Set
    ST7789_SendData(x0 >> 8);
    ST7789_SendData(x0 & 0xFF);
    ST7789_SendData(x1 >> 8);
    ST7789_SendData(x1 & 0xFF);

    ST7789_SendCmd(0x2B); // Row Address Set
    ST7789_SendData(y0 >> 8);
    ST7789_SendData(y0 & 0xFF);
    ST7789_SendData(y1 >> 8);
    ST7789_SendData(y1 & 0xFF);

    ST7789_SendCmd(0x2C); // Memory Write
}

void ST7789_Init(void) {
    // Reset por hardware
    ST7789_RST_LOW();
    HAL_Delay(50);
    ST7789_RST_HIGH();
    HAL_Delay(50);

    ST7789_SendCmd(0x01); // Software Reset
    HAL_Delay(150);

    ST7789_SendCmd(0x11); // Sleep Out
    HAL_Delay(120); // datasheet exige >=120ms antes do proximo comando

    ST7789_SendCmd(0x3A); // Color Mode
    ST7789_SendData(0x55); // 16-bit color (RGB565)

    ST7789_SendCmd(0x36); // MADCTL (Memory Data Access Control)
    ST7789_SendData(0x60); // Landscape 320x240

    ST7789_SendCmd(0x20); // Display Inversion ON

    ST7789_SendCmd(0x29); // Display ON
    HAL_Delay(10);
}

void ST7789_FillRect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color) {
    static uint8_t line_buf[320 * 2];
    uint16_t width = (uint16_t)(x1 - x0 + 1);
    uint16_t height = (uint16_t)(y1 - y0 + 1);
    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)(color & 0xFF);
    uint16_t row;
    uint16_t i;

    if (width > 320u) {
        width = 320u;
    }

    for (i = 0; i < width; i++) {
        line_buf[(2u * i)]      = hi;
        line_buf[(2u * i) + 1u] = lo;
    }

    ST7789_SetWindow(x0, y0, x1, y1);
    ST7789_DC_DATA();
    ST7789_CS_LOW();
    for (row = 0; row < height; row++) {
        spi_transmit_checked(line_buf, (uint16_t)(width * 2u));
    }
    ST7789_CS_HIGH();
}