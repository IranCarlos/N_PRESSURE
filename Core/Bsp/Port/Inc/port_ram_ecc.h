/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-17
 */

#ifndef PORT_RAM_ECC_H
#define PORT_RAM_ECC_H

#include <stdint.h>
#include "stm32h7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file port_ram_ecc.h
 * @brief Inicializacao do ECC da AXI SRAM (RAM_D1) no boot.
 *
 * A AXI SRAM do STM32H7 tem ECC por palavra de 64 bits. No power-on o
 * conteudo — e os bits de ECC — sao aleatorios. Uma escrita MENOR que 64
 * bits (ex.: pixel RGB565 de 16 bits do LVGL) vira leitura-modifica-escrita
 * no controlador; a leitura acha ECC invalido (erro duplo) e devolve erro de
 * barramento — BusFault "imprecise" (CFSR = 0x400), acusado depois, na
 * proxima barreira. A startup so' zera .bss (DTCM); .dma_buffer e
 * .fatfs_dma_buffer (RAM_D1) sao NOLOAD e nunca eram escritos inteiros.
 *
 * Sintomas antes da correcao (2026-10): resets repetidos ao energizar
 * (HardFault -> Fault_Manager -> NVIC_SystemReset; o reset por software
 * preserva a RAM, entao cada tentativa deixava mais palavras com ECC valido
 * ate' a aplicacao "pegar"), e o IMPRECISERR intermitente no desenho do
 * LVGL que nunca aparecia com o depurador (a RAM ja' estava inicializada
 * pelas sessoes anteriores).
 */

/**
 * @brief Escreve zero em toda a AXI SRAM com escritas de 64 bits alinhadas,
 *        deixando o ECC valido em todas as palavras.
 *
 * Chamar como PRIMEIRA coisa em main(), antes de qualquer uso de RAM_D1
 * (nada e' colocado em RAM_D1 pela startup — .data/.bss ficam em DTCM).
 * O tamanho vem do option byte TCM_AXI_SHARED (a AXI divide 192 KB com o
 * ITCM). Leva ~1 ms com o clock de reset (HSI 64 MHz).
 */
void PortRamEcc_InitAxiSram(void);

/**
 * @brief Escreve zero na parte da DTCM que a startup nao inicializa: de
 *        `_end` (fim de .bss) ate' o ponteiro de pilha atual (MSP).
 *
 * A DTCM tambem tem ECC (por palavra de 32 bits). .data/.bss sao escritos
 * inteiros pela startup, mas o resto — heap do newlib e, principalmente, a
 * pilha principal (MSP), usada por TODAS as interrupcoes depois que o
 * FreeRTOS sobe — fica com ECC aleatorio no power-on. Uma ISR gravando um
 * byte/meia-palavra ali gera erro de barramento imprecise, atribuido ao
 * codigo interrompido (achado 2026-10-17: com AXI SRAM e perifericos em
 * strongly-ordered o fault continuou IMPRECISERR — logo a escrita ruim nao
 * ia para la').
 *
 * Chamar no inicio de main(), com interrupcoes ainda desligadas: so' escreve
 * ABAIXO do SP atual, nunca no quadro de quem chamou.
 */
void PortRamEcc_InitDtcmFree(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_RAM_ECC_H */
