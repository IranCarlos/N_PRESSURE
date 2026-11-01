/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-12
 * Modified: 2026-10-17
 */

#include "ui_session.h"
#include "ui_backend.h"

static UiSession_t s_session;

void UiSession_Init(void)
{
    s_session.operator_id = 0u;
    s_session.participant_id = 0u;
    s_session.cycle_number = 0u;
    s_session.measure = 1u;
    s_session.series_done = 0u;
    s_session.buzzer_enabled = 1u;
    s_session.click_enabled = 1u;
    UiBackend_SetClickSound(true);
}

UiSession_t * UiSession_Get(void)
{
    return &s_session;
}

void UiSession_SetOperator(uint16_t operator_id)
{
    s_session.operator_id = operator_id;
}

void UiSession_SetParticipant(uint16_t participant_id)
{
    if (participant_id != s_session.participant_id) {
        UiSession_SeriesRestart();
    }
    s_session.participant_id = participant_id;
}

UiSeriesStep_t UiSession_SeriesOnResult(bool counted)
{
    if (!counted) {
        return UI_SERIES_REPEAT;
    }
    if (s_session.measure >= UI_SERIES_LENGTH) {
        s_session.series_done = 1u;
        return UI_SERIES_COMPLETE;
    }
    s_session.measure++;
    return UI_SERIES_NEXT;
}

void UiSession_SeriesRestart(void)
{
    s_session.measure = 1u;
    s_session.series_done = 0u;
}

void UiSession_SetClickEnabled(uint8_t enabled)
{
    s_session.click_enabled = (enabled != 0u) ? 1u : 0u;
    UiBackend_SetClickSound(enabled != 0u);
}

void UiSession_SetBuzzerEnabled(uint8_t enabled)
{
    s_session.buzzer_enabled = (enabled != 0u) ? 1u : 0u;
}

void UiSession_NextCycle(void)
{
    s_session.cycle_number++;
    if (s_session.series_done != 0u) {
        UiSession_SeriesRestart(); /* pedal com a pergunta aberta: refaz a bateria */
    }
}

void UiSession_Beep(uint16_t freq_hz, uint16_t duration_ms)
{
    if (s_session.buzzer_enabled != 0u) {
        UiBackend_BuzzerBeep(freq_hz, duration_ms);
    }
}
