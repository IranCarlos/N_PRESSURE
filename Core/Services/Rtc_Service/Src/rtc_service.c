/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-01
 */

#include "rtc_service.h"

#define RTC_SERVICE_BKP_COUNT (32u)

static uint32_t s_flags;
static RtcService_ClockInfo_t s_clock_info;
static uint8_t s_early_reset; /* RtcService_RecoverLseFailureEarly() resetou o dominio */

/* Reload do IWDG por registrador (KR = 0xAAAA): nao precisa do handle (que
 * mora em main.c) e nao tem efeito se o IWDG ainda nao foi iniciado. */
static void rtc_service_kick_iwdg(void)
{
    IWDG1->KR = 0x0000AAAAu;
}

/* Calendario salvo antes de um reset do dominio de backup, restaurado por
 * RtcService_RestoreSavedCalendar() depois de HAL_RTC_Init(). */
static uint8_t s_cal_saved;
static uint32_t s_cal_tr; /* RTC->TR (BCD) */
static uint32_t s_cal_dr; /* RTC->DR (BCD) */

/* Reset do dominio de backup preservando os BKPxR de preserve_mask e a
 * data/hora (o reset apaga RTC, calendario, BKPxR, RTCSEL, LSE e LSECSSD). */
static void rtc_service_backup_domain_reset(uint32_t preserve_mask)
{
    volatile uint32_t *bkp = &RTC->BKP0R;
    uint32_t saved[RTC_SERVICE_BKP_COUNT];
    uint32_t i;

    /* BKPxR/TR/DR ficam no barramento APB4 do RTC: precisa do clock de
     * interface (RTCAPBEN), independente do clock do contador (RTCEN). */
    __HAL_RCC_RTC_CLK_ENABLE();

    for (i = 0u; i < RTC_SERVICE_BKP_COUNT; i++) {
        saved[i] = ((preserve_mask & (1uL << i)) != 0u) ? bkp[i] : 0u;
    }

    /* Calendario: so' se ja' tinha sido inicializado (INITS).
     *
     * BUG corrigido (2026-10-21): lia TR/DR com BYPSHAD=0, ou seja, os
     * registradores-SOMBRA. Depois de um reset de sistema eles voltam ao
     * valor de reset (TR=0, DR=0x2101 = 01/01/2000) e so' sao recarregados
     * pelo RTCCLK (RSF). No caminho tipico deste reset (LSECSSD: o LSE
     * parou), nao ha' RTCCLK — o "calendario salvo" era sempre o default e
     * a hora voltava a 01/01/2000 a cada power-cycle, mesmo com VBAT bom.
     * Com BYPSHAD=1 (RTC_CR bit 5, RM0468) a leitura vai direto aos
     * contadores do calendario, que mantem a ultima hora contada.
     * RTC_CR e' protegido: WPR recebe a chave 0xCA, 0x53 para destravar e
     * qualquer outro valor (0xFF) para travar de novo. */
    if ((RTC->ISR & RTC_ISR_INITS) != 0u) {
        uint32_t tr;
        uint32_t dr;

        RTC->WPR = 0xCAu;
        RTC->WPR = 0x53u;
        RTC->CR |= RTC_CR_BYPSHAD;   /* set do bit 5: le os contadores direto */

        /* Sem sombra, TR e DR nao sao travados juntos: com o contador ainda
         * rodando, a virada de meia-noite entre as duas leituras daria uma
         * hora incoerente. Le ate' duas leituras seguidas baterem. */
        do {
            tr = RTC->TR;
            dr = RTC->DR;
        } while ((tr != RTC->TR) || (dr != RTC->DR));

        RTC->CR &= ~RTC_CR_BYPSHAD;  /* clear do bit 5 (o reset abaixo zera o CR de qualquer forma) */
        RTC->WPR = 0xFFu;

        s_cal_tr = tr;
        s_cal_dr = dr;
        s_cal_saved = 1u;
    }

    __HAL_RCC_BACKUPRESET_FORCE();
    __HAL_RCC_BACKUPRESET_RELEASE();

    for (i = 0u; i < RTC_SERVICE_BKP_COUNT; i++) {
        if ((preserve_mask & (1uL << i)) != 0u) {
            bkp[i] = saved[i];
        }
    }

    s_clock_info.backup_domain_reset = 1u;
}

/* Liga o LSE (sem CSS) se ainda nao estiver ligado e espera LSERDY ate'
 * timeout_ms, alimentando o IWDG. Devolve 1 se ficou pronto. */
static uint8_t rtc_service_start_lse(uint32_t timeout_ms)
{
    uint32_t tickstart;

    /* LSEDRV so' pode mudar com o LSE desligado. */
    if ((RCC->BDCR & RCC_BDCR_LSEON) == 0u) {
        __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_HIGH);
        SET_BIT(RCC->BDCR, RCC_BDCR_LSEON);
    }

    tickstart = HAL_GetTick();
    while ((RCC->BDCR & RCC_BDCR_LSERDY) == 0u) {
        rtc_service_kick_iwdg();
        if ((HAL_GetTick() - tickstart) > timeout_ms) {
            break;
        }
    }
    s_clock_info.lse_startup_ms += HAL_GetTick() - tickstart;

    return ((RCC->BDCR & RCC_BDCR_LSERDY) != 0u) ? 1u : 0u;
}

void RtcService_ConfigureClock(uint32_t lse_timeout_ms, uint32_t preserve_bkp_mask)
{
    uint32_t bdcr;
    uint32_t rtcsel;
    uint8_t css_requested;

    s_clock_info.lse_ok = 0u;
    s_clock_info.backup_domain_reset = s_early_reset;
    s_clock_info.lse_startup_ms = 0u;

    HAL_PWR_EnableBkUpAccess(); /* PWR_CR1.DBP: libera escrita no RCC_BDCR */

    bdcr = RCC->BDCR;
    rtcsel = bdcr & RCC_BDCR_RTCSEL;
    css_requested = ((bdcr & RCC_BDCR_LSECSSON) != 0u) ? 1u : 0u;
    s_clock_info.bdcr_at_entry = bdcr;
    s_clock_info.lse_css_failure_seen = (((bdcr & RCC_BDCR_LSECSSD) != 0u) || (s_early_reset != 0u)) ? 1u : 0u;

    /* Falha travada do CSS do LSE: so' sai com reset do dominio (calendario
     * preservado). Depois disso RTCSEL fica 0 e cai no caminho abaixo. */
    if ((bdcr & RCC_BDCR_LSECSSD) != 0u) {
        rtc_service_backup_domain_reset(preserve_bkp_mask);
        rtcsel = 0u;
    }

    /* 1) Testa o LSE SEM resetar nada (LSEON pode ser ligado com qualquer
     *    RTCSEL). Com bateria no VBAT ele normalmente ja' esta' rodando. */
    if (rtc_service_start_lse(lse_timeout_ms) != 0u) {
        if (rtcsel == RCC_RTCCLKSOURCE_LSE) {
            /* caminho normal: nada a mudar */
        } else if (rtcsel == 0u) {
            __HAL_RCC_RTC_CONFIG(RCC_RTCCLKSOURCE_LSE);
        } else {
            /* Estava em LSI (fallback de um boot anterior) e o LSE agora
             * funciona: a troca exige reset do dominio (que para o LSE) —
             * calendario preservado — e esperar o LSE de novo. */
            rtc_service_backup_domain_reset(preserve_bkp_mask);
            if (rtc_service_start_lse(lse_timeout_ms) != 0u) {
                __HAL_RCC_RTC_CONFIG(RCC_RTCCLKSOURCE_LSE);
            } else {
                CLEAR_BIT(RCC->BDCR, RCC_BDCR_LSEON);
                __HAL_RCC_RTC_CONFIG(RCC_RTCCLKSOURCE_LSI);
                return;
            }
        }
        if (css_requested != 0u) {
            SET_BIT(RCC->BDCR, RCC_BDCR_LSECSSON); /* so' com LSERDY=1 */
        }
        s_clock_info.lse_ok = 1u;
        return;
    }

    /* 2) LSE nao partiu: fica em LSI. Se ja' estava em LSI, NAO reseta nada
     *    (antes resetava a cada boot tentando o LSE e apagava a hora — bug
     *    2026-10-17). So' reseta se RTCSEL estiver preso em outra fonte. */
    CLEAR_BIT(RCC->BDCR, RCC_BDCR_LSEON);
    rtcsel = RCC->BDCR & RCC_BDCR_RTCSEL;
    if ((rtcsel != 0u) && (rtcsel != RCC_RTCCLKSOURCE_LSI)) {
        rtc_service_backup_domain_reset(preserve_bkp_mask);
        rtcsel = 0u;
    }
    if (rtcsel == 0u) {
        __HAL_RCC_RTC_CONFIG(RCC_RTCCLKSOURCE_LSI);
    }
}

uint8_t RtcService_RestoreSavedCalendar(RTC_HandleTypeDef *hrtc)
{
    RTC_TimeTypeDef t = { 0 };
    RTC_DateTypeDef d = { 0 };

    if ((s_cal_saved == 0u) || (hrtc == NULL)) {
        return 0u;
    }
    s_cal_saved = 0u;

    /* Campos BCD de RTC_TR / RTC_DR (RM0468). */
    t.Hours   = (uint8_t)((s_cal_tr >> 16) & 0x3Fu);
    t.Minutes = (uint8_t)((s_cal_tr >> 8) & 0x7Fu);
    t.Seconds = (uint8_t)(s_cal_tr & 0x7Fu);
    t.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    t.StoreOperation = RTC_STOREOPERATION_RESET;
    d.Year    = (uint8_t)((s_cal_dr >> 16) & 0xFFu);
    d.WeekDay = (uint8_t)((s_cal_dr >> 13) & 0x07u);
    d.Month   = (uint8_t)((s_cal_dr >> 8) & 0x1Fu);
    d.Date    = (uint8_t)(s_cal_dr & 0x3Fu);
    if (d.WeekDay == 0u) {
        d.WeekDay = RTC_WEEKDAY_MONDAY; /* 0 e' invalido para a HAL */
    }

    if ((HAL_RTC_SetTime(hrtc, &t, RTC_FORMAT_BCD) != HAL_OK) ||
        (HAL_RTC_SetDate(hrtc, &d, RTC_FORMAT_BCD) != HAL_OK)) {
        return 0u;
    }
    return 1u;
}

uint8_t RtcService_RecoverLseFailureEarly(uint32_t preserve_bkp_mask)
{
    HAL_PWR_EnableBkUpAccess();

    if ((RCC->BDCR & RCC_BDCR_LSECSSD) == 0u) {
        return 0u;
    }
    rtc_service_backup_domain_reset(preserve_bkp_mask);
    s_early_reset = 1u;
    return 1u;
}

const RtcService_ClockInfo_t *RtcService_GetClockInfo(void)
{
    return &s_clock_info;
}

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

uint32_t RtcService_ReevaluateAfterSet(RTC_HandleTypeDef *hrtc)
{
    return RtcService_CheckAtBoot(hrtc, 0u);
}

uint32_t RtcService_GetFlags(void)
{
    return s_flags;
}
