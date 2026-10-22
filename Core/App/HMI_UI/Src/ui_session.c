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
    s_session.buzzer_enabled = 1u;
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
    s_session.participant_id = participant_id;
}

void UiSession_SetBuzzerEnabled(uint8_t enabled)
{
    s_session.buzzer_enabled = (enabled != 0u) ? 1u : 0u;
}

void UiSession_NextCycle(void)
{
    s_session.cycle_number++;
}

void UiSession_Beep(uint16_t freq_hz, uint16_t duration_ms)
{
    if (s_session.buzzer_enabled != 0u) {
        UiBackend_BuzzerBeep(freq_hz, duration_ms);
    }
}
