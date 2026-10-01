/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-01
 */

#include "rtc_validity.h"

static uint8_t is_leap_year(uint32_t year4)
{
    return (uint8_t)(((year4 % 4u) == 0u) && (((year4 % 100u) != 0u) || ((year4 % 400u) == 0u)));
}

static uint8_t days_in_month(uint8_t month, uint32_t year4)
{
    static const uint8_t dim[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

    if ((month < 1u) || (month > 12u)) {
        return 0u;
    }
    if ((month == 2u) && (is_leap_year(year4) != 0u)) {
        return 29u;
    }
    return dim[month - 1u];
}

uint32_t RtcValidity_Evaluate(const RtcValidity_DateTime_t *dt, uint8_t lse_ready,
                              uint8_t backup_domain_was_reset)
{
    uint32_t flags = (uint32_t)RTC_STATUS_OK;
    uint8_t fields_ok;
    uint8_t is_default_datetime;

    if (lse_ready == 0u) {
        flags |= (uint32_t)RTC_FLAG_LSE_NOT_READY;
    }
    if (backup_domain_was_reset != 0u) {
        flags |= (uint32_t)RTC_FLAG_BACKUP_DOMAIN_RESET;
    }

    fields_ok = (uint8_t)((dt->month >= 1u) && (dt->month <= 12u) &&
                          (dt->day >= 1u) &&
                          (dt->day <= days_in_month(dt->month, 2000u + dt->year_2d)) &&
                          (dt->hour <= 23u) && (dt->minute <= 59u) && (dt->second <= 59u));

    if (fields_ok == 0u) {
        flags |= (uint32_t)RTC_FLAG_DATE_IMPLAUSIBLE;
        return flags;
    }

    if (dt->year_2d >= RTC_VALIDITY_MIN_YEAR_2D) {
        return flags; /* ano plausivel: nao avalia NEVER_SET/IMPLAUSIBLE abaixo */
    }

    /* Ano anterior ao inicio do projeto: so' nao e' "implausivel" se for
     * EXATAMENTE o default gravado por MX_RTC_Init() (1o boot de sempre) —
     * nesse caso e' "nunca ajustado", um diagnostico mais especifico. */
    is_default_datetime = (uint8_t)((dt->year_2d == 0u) && (dt->month == 1u) &&
                                    (dt->day == 1u) && (dt->hour == 0u) &&
                                    (dt->minute == 0u) && (dt->second == 0u));
    flags |= is_default_datetime ? (uint32_t)RTC_FLAG_NEVER_SET
                                 : (uint32_t)RTC_FLAG_DATE_IMPLAUSIBLE;
    return flags;
}
