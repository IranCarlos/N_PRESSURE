/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-07-27
 * Modified: 2026-08-03
 */

#ifndef ST7789_H
#define ST7789_H

#include "main.h"

// Modifique conforme a sua SPI configurada no CubeMX
extern SPI_HandleTypeDef hspi1;

// Macros para os pinos (Ajuste as portas e pinos conforme seu CubeMX)
#define ST7789_CS_PORT    TFT_CS_GPIO_Port
#define ST7789_CS_PIN     TFT_CS_Pin
#define ST7789_DC_PORT    TFT_DC_GPIO_Port
#define ST7789_DC_PIN     TFT_DC_Pin
#define ST7789_RES_PORT   TFT_RES_GPIO_Port
#define ST7789_RES_PIN    TFT_RES_Pin

#define ST7789_CS_LOW()   HAL_GPIO_WritePin(ST7789_CS_PORT, ST7789_CS_PIN, GPIO_PIN_RESET)
#define ST7789_CS_HIGH()  HAL_GPIO_WritePin(ST7789_CS_PORT, ST7789_CS_PIN, GPIO_PIN_SET)
#define ST7789_DC_CMD()   HAL_GPIO_WritePin(ST7789_DC_PORT, ST7789_DC_PIN, GPIO_PIN_RESET)
#define ST7789_DC_DATA()  HAL_GPIO_WritePin(ST7789_DC_PORT, ST7789_DC_PIN, GPIO_PIN_SET)
#define ST7789_RST_LOW()  HAL_GPIO_WritePin(ST7789_RES_PORT, ST7789_RES_PIN, GPIO_PIN_RESET)
#define ST7789_RST_HIGH() HAL_GPIO_WritePin(ST7789_RES_PORT, ST7789_RES_PIN, GPIO_PIN_SET)

/* Inicializa o painel com a RAM de video preenchida de PRETO antes do
 * Display ON — o backlight continua APAGADO; ligar com
 * ST7789_SetBacklight(1) quando quiser mostrar a tela. */
void ST7789_Init(void);
void ST7789_SetBacklight(uint8_t on);
void ST7789_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void ST7789_SendCmd(uint8_t cmd);
void ST7789_SendData(uint8_t data);

/**
 * @brief Preenche um retangulo com uma cor solida (RGB565), via SPI polling.
 *        Usado apenas para bring-up/depuracao sem LVGL (sem DMA, sem cache).
 * @param color RGB565 em ordem "de tela" (ja no formato que o painel espera).
 */
void ST7789_FillRect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);

/**
 * @brief Numero de vezes que HAL_SPI_Transmit() retornou erro/timeout desde
 *        o boot. Use para diagnosticar se o problema esta na transmissao
 *        SPI em si (contador > 0) ou no painel/fiacao (contador == 0 mas
 *        nada aparece na tela).
 */
uint32_t ST7789_GetSpiErrorCount(void);

#endif