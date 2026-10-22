/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-09-19
 */

#ifndef FAULT_MANAGER_H
#define FAULT_MANAGER_H

#include <stdint.h>
#include "stm32h7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file fault_manager.h
 * @brief Captura de HardFault/reset e persistencia do diagnostico entre boots.
 *
 * O H723 nao tem uma regiao de "backup SRAM" dedicada (isso existe em outras
 * familias STM32); o equivalente aqui sao os 32 registradores de 32 bits do
 * dominio de backup do RTC (`RTC->BKPxR`), alimentados pela mesma bateria/VBAT
 * do cristal do RTC (ver Docs/dev/hardware.md, H8) — sobrevivem a reset e a
 * queda de energia principal. FaultManager_Init() deve ser chamado logo apos
 * MX_RTC_Init() (precisa do dominio de backup ja desbloqueado pelo RTC).
 *
 * Fluxo: HardFault_Handler() (stm32h7xx_it.c) captura o quadro de excecao e
 * chama FaultManager_CaptureHardFault(), que grava tudo nos BKPxR e reseta a
 * MCU imediatamente (nao tenta continuar rodando em estado corrompido). No
 * boot seguinte, FaultManager_Init() le os BKPxR: se o magic bater, havia um
 * fault pendente — o chamador recupera o registro via
 * FaultManager_GetLastFault() e decide o que fazer (hoje: nenhum consumidor
 * ainda existe — a Fase 5/6, quando storage_task/IHM existirem, deve logar
 * isso em /logs/errors/ e exibir no popup de erro, conforme Apendice D
 * (E009) do prompt de projeto).
 */

/** @brief BKPxR usados pelo registro de fault (bit n = RTC->BKPnR: BKP0..BKP12).
 *         Quem precisar resetar o dominio de backup deve preserva-los
 *         (ver RtcService_ConfigureClock()). */
#define FAULT_MANAGER_BKP_MASK (0x00001FFFu)

/** @brief Snapshot de uma excecao de hard fault, tal como persistido nos BKPxR. */
typedef struct {
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uint32_t lr;
    uint32_t pc;
    uint32_t psr;
    uint32_t cfsr;  /**< SCB->CFSR no momento do fault. */
    uint32_t hfsr;  /**< SCB->HFSR no momento do fault. */
    uint32_t mmfar; /**< SCB->MMFAR (valido só se CFSR indicar MMARVALID). */
    uint32_t bfar;  /**< SCB->BFAR (valido só se CFSR indicar BFARVALID). */
} FaultManager_Record_t;

/**
 * @brief Inicializa o modulo e consome um registro de fault pendente, se houver.
 *
 * Chamar uma unica vez no boot, depois de MX_RTC_Init(). Le a causa de reset
 * (IWDG/WWDG/software/power-on, via __HAL_RCC_GET_FLAG) e os BKPxR; se havia
 * um fault registrado, guarda uma copia interna (ver FaultManager_GetLastFault)
 * e limpa o magic (para nao ser reportado de novo no proximo boot). Sempre
 * limpa as flags de causa de reset da RCC ao final (proximo reset comeca "zerado").
 *
 * @param p_hrtc Handle do RTC ja inicializado (MX_RTC_Init concluido).
 */
void FaultManager_Init(RTC_HandleTypeDef *p_hrtc);

/** @brief Retorna 1 se havia um HardFault pendente no boot anterior (consumido em Init). */
uint8_t FaultManager_HasPendingFault(void);

/** @brief Retorna 1 se o boot anterior terminou por reset do IWDG. */
uint8_t FaultManager_WasIwdgReset(void);

/**
 * @brief Copia o ultimo registro de fault capturado (valido só se
 *        FaultManager_HasPendingFault() retornar 1).
 */
void FaultManager_GetLastFault(FaultManager_Record_t *p_out);

/**
 * @brief Chamada pelo trampolim em assembly de HardFault_Handler
 *        (stm32h7xx_it.c), nunca diretamente pela aplicacao.
 *
 * @param p_stacked_regs Ponteiro para o quadro de excecao empilhado pelo
 *        hardware (r0,r1,r2,r3,r12,lr,pc,psr, nesta ordem) — MSP ou PSP,
 *        conforme bit 2 do EXC_RETURN em LR no momento do fault.
 */
void FaultManager_CaptureHardFault(uint32_t *p_stacked_regs) __attribute__((noreturn));

/**
 * @brief Diagnostico: forca um HardFault deliberado (dereferencia de ponteiro
 *        invalido) para validar em bancada que a captura sobrevive ao reset
 *        (criterio de aceitacao da Fase 1). NAO chamar em operacao normal —
 *        so a partir de um comando de diagnostico explicito (bring-up/DEV).
 */
void FaultManager_ForceHardFault(void) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

#endif /* FAULT_MANAGER_H */
