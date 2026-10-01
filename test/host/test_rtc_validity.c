/* Teste de host de rtc_validity (sem HAL). Ver test/host/run.sh. */
#include "rtc_validity.h"
#include <stdio.h>

static int s_fail;
#define CHECK(c) do { if (!(c)) { printf("FALHA %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)

static RtcValidity_DateTime_t dt(uint8_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t mi, uint8_t s)
{
    RtcValidity_DateTime_t v = { y, mo, d, h, mi, s };
    return v;
}

static void test_ok_date(void)
{
    RtcValidity_DateTime_t v = dt(26, 9, 30, 12, 0, 0);
    CHECK(RtcValidity_Evaluate(&v, 1, 0) == RTC_STATUS_OK);
}

static void test_lse_not_ready_flagged_even_with_good_date(void)
{
    RtcValidity_DateTime_t v = dt(26, 9, 30, 12, 0, 0);
    CHECK(RtcValidity_Evaluate(&v, 0, 0) == RTC_FLAG_LSE_NOT_READY);
}

static void test_backup_domain_reset_flagged_independently(void)
{
    RtcValidity_DateTime_t v = dt(26, 9, 30, 12, 0, 0);
    CHECK(RtcValidity_Evaluate(&v, 1, 1) == RTC_FLAG_BACKUP_DOMAIN_RESET);
    CHECK(RtcValidity_Evaluate(&v, 0, 1) == (RTC_FLAG_LSE_NOT_READY | RTC_FLAG_BACKUP_DOMAIN_RESET));
}

static void test_default_datetime_is_never_set(void)
{
    RtcValidity_DateTime_t v = dt(0, 1, 1, 0, 0, 0); /* default gravado por MX_RTC_Init() */
    CHECK(RtcValidity_Evaluate(&v, 1, 0) == RTC_FLAG_NEVER_SET);
}

static void test_old_but_not_default_year_is_implausible(void)
{
    RtcValidity_DateTime_t v = dt(10, 5, 15, 8, 30, 0); /* 2010, campos validos, mas nao e' o default */
    CHECK(RtcValidity_Evaluate(&v, 1, 0) == RTC_FLAG_DATE_IMPLAUSIBLE);
}

static void test_out_of_range_fields_are_implausible(void)
{
    CHECK(RtcValidity_Evaluate(&(RtcValidity_DateTime_t){ 26, 13, 1, 0, 0, 0 }, 1, 0) == RTC_FLAG_DATE_IMPLAUSIBLE);
    CHECK(RtcValidity_Evaluate(&(RtcValidity_DateTime_t){ 26, 4, 31, 0, 0, 0 }, 1, 0) == RTC_FLAG_DATE_IMPLAUSIBLE); /* abril tem 30 */
    CHECK(RtcValidity_Evaluate(&(RtcValidity_DateTime_t){ 26, 2, 30, 0, 0, 0 }, 1, 0) == RTC_FLAG_DATE_IMPLAUSIBLE);
    CHECK(RtcValidity_Evaluate(&(RtcValidity_DateTime_t){ 26, 1, 1, 24, 0, 0 }, 1, 0) == RTC_FLAG_DATE_IMPLAUSIBLE);
    CHECK(RtcValidity_Evaluate(&(RtcValidity_DateTime_t){ 26, 1, 1, 0, 60, 0 }, 1, 0) == RTC_FLAG_DATE_IMPLAUSIBLE);
}

static void test_leap_year_feb29(void)
{
    CHECK(RtcValidity_Evaluate(&(RtcValidity_DateTime_t){ 28, 2, 29, 0, 0, 0 }, 1, 0) == RTC_STATUS_OK); /* 2028 bissexto */
    CHECK(RtcValidity_Evaluate(&(RtcValidity_DateTime_t){ 27, 2, 29, 0, 0, 0 }, 1, 0) == RTC_FLAG_DATE_IMPLAUSIBLE); /* 2027 nao */
}

int main(void)
{
    test_ok_date();
    test_lse_not_ready_flagged_even_with_good_date();
    test_backup_domain_reset_flagged_independently();
    test_default_datetime_is_never_set();
    test_old_but_not_default_year_is_implausible();
    test_out_of_range_fields_are_implausible();
    test_leap_year_feb29();
    printf(s_fail ? "rtc_validity: %d FALHA(S)\n" : "rtc_validity: OK\n", s_fail);
    return s_fail != 0;
}
