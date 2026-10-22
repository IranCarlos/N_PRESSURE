/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-01
 *
 * Binding HAL de rtc_validity: le hora/data/estado do LSE e avalia a
 * confianca do calendario. Services nao inclui main.h — RTC_HandleTypeDef e'
 * injetado pelo chamador (mesmo criterio de ADC_Acq/Control_Loop).
 */
#ifndef RTC_SERVICE_H
#define RTC_SERVICE_H

#include "stm32h7xx_hal.h"
#include "rtc_validity.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Chamar uma vez no boot, apos MX_RTC_Init(). backup_domain_was_reset deve
 * vir de quem sabe se o dominio de backup foi resetado neste boot (ver
 * RTC_CALENDAR_INITIALIZED_BKP_REG em main.c). Retorna os flags avaliados
 * (ver RtcValidityFlags_t); 0 = calendario confiavel.
 */
uint32_t RtcService_CheckAtBoot(RTC_HandleTypeDef *hrtc, uint8_t backup_domain_was_reset);

/*
 * Chamar logo apos o operador ajustar data/hora (HAL_RTC_SetDate/SetTime OK):
 * reavalia os flags com a hora nova. Sem isso o resultado do boot
 * (NEVER_SET/BACKUP_DOMAIN_RESET/DATE_IMPLAUSIBLE) ficava valendo ate' o
 * proximo reset, e IHM/USB continuavam acusando "calendario nao
 * inicializado" mesmo com a hora ja' ajustada. BACKUP_DOMAIN_RESET e'
 * descartado: a hora atual e' a que o operador acabou de informar.
 * LSE_NOT_READY continua valendo (RTC em LSI nao conta durante VBAT).
 * Retorna os novos flags (0 = calendario confiavel).
 */
uint32_t RtcService_ReevaluateAfterSet(RTC_HandleTypeDef *hrtc);

/*
 * Ajuste de data/hora pelo operador (IHM ou USB SET_DATETIME): grava o
 * calendario (RTC_FORMAT_BIN, year = 2000..2099) e chama
 * RtcService_ReevaluateAfterSet(). NAO valida os campos — o chamador ja'
 * validou (dia do mes, faixas). Retorna 1 se o HAL aceitou as duas escritas.
 */
uint8_t RtcService_SetDateTime(RTC_HandleTypeDef *hrtc, const RtcValidity_DateTime_t *dt);

/* Ultimo resultado de RtcService_CheckAtBoot()/ReevaluateAfterSet() (Live Watch / diagnostico). */
uint32_t RtcService_GetFlags(void);

/*
 * Selecao segura do clock do RTC (2026-10, bug de boot com LSE): chamar no
 * lugar do HAL_RCCEx_PeriphCLKConfig(RTC) gerado em HAL_RTC_MspInit().
 *
 * O caminho gerado tem um defeito quando a fonte do RTC muda (LSI->LSE, ou
 * dominio de backup sem RTCSEL): a HAL reseta o dominio de backup e
 * restaura o RCC_BDCR inteiro de uma vez — inclusive LSECSSON, que o
 * SystemClock_Config() ja' tinha ligado — religando o LSE COM o CSS armado
 * durante a partida do cristal (o RM0468 exige LSECSSON so' depois de
 * LSERDY). O CSS acusa falha (LSECSSD), que fica travada ate' outro reset do
 * dominio de backup: o RTC fica sem clock, HAL_RTC_Init() estoura o timeout,
 * Error_Handler(), IWDG, e o mesmo a cada boot.
 *
 * Esta funcao:
 *   - nao mexe em nada se o RTC ja' esta' em LSE, pronto e sem falha;
 *   - recupera LSECSSD travado / troca de fonte com um reset do dominio de
 *     backup, preservando os BKPxR indicados em `preserve_bkp_mask`
 *     (bit n = RTC->BKPnR, ex.: o registro de HardFault do Fault_Manager);
 *   - parte o LSE (drive HIGH) SEM o CSS, espera ate' `lse_timeout_ms`
 *     alimentando o IWDG, seleciona LSE e so' entao religa o CSS (se estava
 *     ligado antes);
 *   - se o LSE nao partir, cai para LSI: o boot segue (RTC sem hora
 *     confiavel — RtcService_CheckAtBoot() acusa LSE_NOT_READY).
 *
 * Usa HAL_GetTick() (timebase da HAL ja' rodando; antes do scheduler).
 */
typedef struct {
    uint8_t  lse_ok;              /* 1 = RTC em LSE; 0 = caiu para LSI */
    uint8_t  backup_domain_reset; /* 1 = foi preciso resetar o dominio de backup neste boot */
    uint8_t  lse_css_failure_seen;/* 1 = LSECSSD estava travado ao entrar */
    uint32_t lse_startup_ms;      /* tempo ate' LSERDY (0 se ja' estava pronto) */
    uint32_t bdcr_at_entry;       /* RCC->BDCR ao entrar (diagnostico) */
} RtcService_ClockInfo_t;

void RtcService_ConfigureClock(uint32_t lse_timeout_ms, uint32_t preserve_bkp_mask);

/*
 * Chamar ANTES de SystemClock_Config(): se o CSS do LSE ficou travado em
 * falha (LSECSSD) num boot anterior, reseta o dominio de backup (preservando
 * `preserve_bkp_mask`) para o LSE poder partir limpo. Sem isso, com o LSE
 * ligado no CubeMX, SystemClock_Config() pode esperar um LSE "falho" e cair
 * em Error_Handler() antes ate' do IWDG — placa morta, sem reset.
 * Retorna 1 se precisou resetar.
 */
uint8_t RtcService_RecoverLseFailureEarly(uint32_t preserve_bkp_mask);

/*
 * Se RtcService_ConfigureClock() precisou resetar o dominio de backup, a
 * data/hora de antes do reset foi guardada: chamar logo apos HAL_RTC_Init()
 * (USER CODE Check_RTC_BKUP em MX_RTC_Init) para regrava-la. Devolve 1 se
 * restaurou (o chamador deve marcar o calendario como inicializado e NAO
 * gravar a data padrao).
 */
uint8_t RtcService_RestoreSavedCalendar(RTC_HandleTypeDef *hrtc);

/* Resultado da ultima RtcService_ConfigureClock() (Live Watch). */
const RtcService_ClockInfo_t *RtcService_GetClockInfo(void);

#ifdef __cplusplus
}
#endif

#endif /* RTC_SERVICE_H */
