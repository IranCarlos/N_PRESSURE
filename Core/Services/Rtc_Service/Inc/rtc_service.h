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

/* Ultimo resultado de RtcService_CheckAtBoot() (Live Watch / diagnostico). */
uint32_t RtcService_GetFlags(void);

#ifdef __cplusplus
}
#endif

#endif /* RTC_SERVICE_H */
