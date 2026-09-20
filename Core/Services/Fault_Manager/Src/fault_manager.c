/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-09-19
 */

#include "fault_manager.h"

/** @brief Marca a presenca de um registro de fault valido em RTC_BKP_DR0. */
#define FAULT_MANAGER_MAGIC (0xFA17FA17U)

/* Layout dos BKPxR (1 palavra de 32 bits cada): */
#define BKP_MAGIC   RTC_BKP_DR0
#define BKP_R0      RTC_BKP_DR1
#define BKP_R1      RTC_BKP_DR2
#define BKP_R2      RTC_BKP_DR3
#define BKP_R3      RTC_BKP_DR4
#define BKP_R12     RTC_BKP_DR5
#define BKP_LR      RTC_BKP_DR6
#define BKP_PC      RTC_BKP_DR7
#define BKP_PSR     RTC_BKP_DR8
#define BKP_CFSR    RTC_BKP_DR9
#define BKP_HFSR    RTC_BKP_DR10
#define BKP_MMFAR   RTC_BKP_DR11
#define BKP_BFAR    RTC_BKP_DR12

/* Handle do RTC guardado em static, nao em .bss zerado por seguranca (o
 * HardFault pode acontecer antes de qualquer outra inicializacao rodar de
 * novo, mas depois de FaultManager_Init ja ter guardado o ponteiro). */
static RTC_HandleTypeDef *s_hrtc;
static uint8_t s_has_pending_fault;
static uint8_t s_was_iwdg_reset;
static FaultManager_Record_t s_last_fault;

void FaultManager_Init(RTC_HandleTypeDef *p_hrtc) {
    s_hrtc = p_hrtc;
    s_has_pending_fault = 0u;
    s_was_iwdg_reset = (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDG1RST) != RESET) ? 1u : 0u;

    if (HAL_RTCEx_BKUPRead(s_hrtc, BKP_MAGIC) == FAULT_MANAGER_MAGIC) {
        s_last_fault.r0    = HAL_RTCEx_BKUPRead(s_hrtc, BKP_R0);
        s_last_fault.r1    = HAL_RTCEx_BKUPRead(s_hrtc, BKP_R1);
        s_last_fault.r2    = HAL_RTCEx_BKUPRead(s_hrtc, BKP_R2);
        s_last_fault.r3    = HAL_RTCEx_BKUPRead(s_hrtc, BKP_R3);
        s_last_fault.r12   = HAL_RTCEx_BKUPRead(s_hrtc, BKP_R12);
        s_last_fault.lr    = HAL_RTCEx_BKUPRead(s_hrtc, BKP_LR);
        s_last_fault.pc    = HAL_RTCEx_BKUPRead(s_hrtc, BKP_PC);
        s_last_fault.psr   = HAL_RTCEx_BKUPRead(s_hrtc, BKP_PSR);
        s_last_fault.cfsr  = HAL_RTCEx_BKUPRead(s_hrtc, BKP_CFSR);
        s_last_fault.hfsr  = HAL_RTCEx_BKUPRead(s_hrtc, BKP_HFSR);
        s_last_fault.mmfar = HAL_RTCEx_BKUPRead(s_hrtc, BKP_MMFAR);
        s_last_fault.bfar  = HAL_RTCEx_BKUPRead(s_hrtc, BKP_BFAR);
        s_has_pending_fault = 1u;

        /* Consome o registro — nao reportar de novo no proximo boot. */
        HAL_RTCEx_BKUPWrite(s_hrtc, BKP_MAGIC, 0u);
    }

    __HAL_RCC_CLEAR_RESET_FLAGS();
}

uint8_t FaultManager_HasPendingFault(void) {
    return s_has_pending_fault;
}

uint8_t FaultManager_WasIwdgReset(void) {
    return s_was_iwdg_reset;
}

void FaultManager_GetLastFault(FaultManager_Record_t *p_out) {
    if (p_out != NULL) {
        *p_out = s_last_fault;
    }
}

void FaultManager_CaptureHardFault(uint32_t *p_stacked_regs) {
    /* Sem HAL_RTCEx_BKUPWrite aqui: estamos dentro do proprio HardFault
     * (contexto corrompido, prioridade mais alta que tudo) — evitar
     * qualquer chamada que dependa de estado de mutex/lock da HAL. Os BKPxR
     * sao registradores simples mapeados em memoria; escrever direto neles
     * e' seguro mesmo aqui. O dominio de backup ja foi desbloqueado por
     * MX_RTC_Init() antes do boot chegar ao super loop, entao nao precisa
     * (nem deve, em contexto de fault) reconfigurar PWR/RTC agora. */
    RTC->BKP0R  = FAULT_MANAGER_MAGIC;
    RTC->BKP1R  = p_stacked_regs[0]; /* r0  */
    RTC->BKP2R  = p_stacked_regs[1]; /* r1  */
    RTC->BKP3R  = p_stacked_regs[2]; /* r2  */
    RTC->BKP4R  = p_stacked_regs[3]; /* r3  */
    RTC->BKP5R  = p_stacked_regs[4]; /* r12 */
    RTC->BKP6R  = p_stacked_regs[5]; /* lr  */
    RTC->BKP7R  = p_stacked_regs[6]; /* pc  */
    RTC->BKP8R  = p_stacked_regs[7]; /* psr */
    RTC->BKP9R  = SCB->CFSR;
    RTC->BKP10R = SCB->HFSR;
    RTC->BKP11R = SCB->MMFAR;
    RTC->BKP12R = SCB->BFAR;

    NVIC_SystemReset();
    for (;;) {
        /* Nunca alcancado — NVIC_SystemReset() nao retorna. */
    }
}

void FaultManager_ForceHardFault(void) {
    volatile uint32_t *p_bad = (volatile uint32_t *)0xFFFFFFFFU;
    *p_bad = 0u; /* Escrita em endereco invalido -> BusFault escalado a HardFault. */
    /* Nao deve chegar aqui (a escrita acima falha). Se chegar, reseta em vez de
     * ficar num laco infinito — que o analisador estatico (-fanalyzer)
     * corretamente aponta como CWE-835 — e cumpre o "noreturn". */
    NVIC_SystemReset();
}
