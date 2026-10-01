/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-01
 *
 * Avaliacao pura (sem HAL) da confiabilidade do calendario do RTC. Decide se
 * a hora/data atual e' digna de confianca para timestamps de log/ensaio —
 * NAO mantem estado nem le hardware; isso fica em rtc_service.c (binding).
 */
#ifndef RTC_VALIDITY_H
#define RTC_VALIDITY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Ano minimo plausivel (RTC_DateTypeDef.Year = 0..99, representa 2000+Year).
 * O projeto comecou em 2026; qualquer ano anterior indica que o calendario
 * nunca foi ajustado de verdade ou que a hora voltou (bateria/LSE falhou). */
#define RTC_VALIDITY_MIN_YEAR_2D   (26u)

typedef enum {
    RTC_STATUS_OK                = 0u,
    RTC_FLAG_LSE_NOT_READY        = (1u << 0), /* RTC sem a referencia de cristal (fallback/LSI) */
    RTC_FLAG_BACKUP_DOMAIN_RESET  = (1u << 1), /* dominio de backup foi resetado neste boot */
    RTC_FLAG_NEVER_SET            = (1u << 2), /* calendario no default do MX_RTC_Init (nunca ajustado) */
    RTC_FLAG_DATE_IMPLAUSIBLE     = (1u << 3)  /* campos fora de faixa, ou ano < RTC_VALIDITY_MIN_YEAR_2D */
} RtcValidityFlags_t;

typedef struct {
    uint8_t year_2d; /* 0..99, representa 2000+ano */
    uint8_t month;   /* 1..12 */
    uint8_t day;     /* 1..31 */
    uint8_t hour;    /* 0..23 */
    uint8_t minute;  /* 0..59 */
    uint8_t second;  /* 0..59 */
} RtcValidity_DateTime_t;

/*
 * lse_ready: estado atual do bit LSERDY (RCC).
 * backup_domain_was_reset: setado pelo chamador quando sabe que o dominio de
 * backup acabou de ser resetado neste boot (ver RTC_CALENDAR_INITIALIZED_BKP_REG
 * em main.c). Retorna OR dos RtcValidityFlags_t aplicaveis (0 = confiavel).
 */
uint32_t RtcValidity_Evaluate(const RtcValidity_DateTime_t *dt, uint8_t lse_ready,
                              uint8_t backup_domain_was_reset);

#ifdef __cplusplus
}
#endif

#endif /* RTC_VALIDITY_H */
