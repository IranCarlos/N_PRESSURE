/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-01
 */

#include "rtc_service.h"

static uint32_t s_flags;

uint32_t RtcService_CheckAtBoot(RTC_HandleTypeDef *hrtc, uint8_t backup_domain_was_reset)
{
    RTC_TimeTypeDef t = { 0 };
    RTC_DateTypeDef d = { 0 };
    RtcValidity_DateTime_t dt;
    uint8_t lse_ready;

    /* GetTime trava os registradores sombra do calendario ate' o GetDate
     * seguinte — mesma exigencia do HAL para qualquer leitura consistente. */
    (void)HAL_RTC_GetTime(hrtc, &t, RTC_FORMAT_BIN);
    (void)HAL_RTC_GetDate(hrtc, &d, RTC_FORMAT_BIN);

    lse_ready = (__HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY) != RESET) ? 1u : 0u;

    dt.year_2d = d.Year;
    dt.month   = d.Month;
    dt.day     = d.Date;
    dt.hour    = t.Hours;
    dt.minute  = t.Minutes;
    dt.second  = t.Seconds;

    s_flags = RtcValidity_Evaluate(&dt, lse_ready, backup_domain_was_reset);
    return s_flags;
}

uint32_t RtcService_GetFlags(void)
{
    return s_flags;
}
