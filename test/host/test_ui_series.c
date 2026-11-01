/* Serie de 3 medidas por paciente (ui_session). */
#include "ui_session.h"
#include "ui_backend.h"
#include <stdio.h>

static int s_fail;
static int s_beeps;
static int s_click;
#define CHECK(c) do { if (!(c)) { printf("FALHA %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)

void UiBackend_BuzzerBeep(uint16_t freq_hz, uint16_t duration_ms)
{
    (void)freq_hz;
    (void)duration_ms;
    s_beeps++;
}

void UiBackend_SetClickSound(bool enabled)
{
    s_click = enabled ? 1 : 0;
}

static void test_three_counted_measures_complete(void)
{
    UiSession_t *s = UiSession_Get();

    UiSession_Init();
    UiSession_SetParticipant(5u);
    CHECK(s->measure == 1u);
    CHECK(UiSession_SeriesOnResult(true) == UI_SERIES_NEXT);
    CHECK(s->measure == 2u);
    CHECK(UiSession_SeriesOnResult(true) == UI_SERIES_NEXT);
    CHECK(s->measure == 3u);
    CHECK(UiSession_SeriesOnResult(true) == UI_SERIES_COMPLETE);
    CHECK(s->measure == 3u);
}

static void test_aborted_repeats_same_measure(void)
{
    UiSession_t *s = UiSession_Get();

    UiSession_Init();
    CHECK(UiSession_SeriesOnResult(true) == UI_SERIES_NEXT);
    CHECK(UiSession_SeriesOnResult(false) == UI_SERIES_REPEAT);
    CHECK(s->measure == 2u);
}

static void test_redo_and_new_patient_restart(void)
{
    UiSession_t *s = UiSession_Get();

    UiSession_Init();
    UiSession_SetParticipant(5u);
    (void)UiSession_SeriesOnResult(true);
    UiSession_SetParticipant(5u); /* confirmar o mesmo paciente nao zera a serie */
    CHECK(s->measure == 2u);
    UiSession_SetParticipant(6u);
    CHECK(s->measure == 1u);
    (void)UiSession_SeriesOnResult(true);
    UiSession_SeriesRestart();
    CHECK(s->measure == 1u);
    CHECK(s->participant_id == 6u);
}

static void test_cycle_started_with_pending_choice_redoes_series(void)
{
    UiSession_t *s = UiSession_Get();

    UiSession_Init();
    (void)UiSession_SeriesOnResult(true);
    (void)UiSession_SeriesOnResult(true);
    (void)UiSession_SeriesOnResult(true);
    UiSession_NextCycle(); /* pedal com a pergunta aberta */
    CHECK(s->measure == 1u);
}

static void test_sound_switches_are_independent(void)
{
    UiSession_Init();
    s_beeps = 0;
    UiSession_SetClickEnabled(0u);
    CHECK(s_click == 0);
    UiSession_Beep(1000u, 10u);
    CHECK(s_beeps == 1); /* som do ensaio segue ligado */
    UiSession_SetClickEnabled(1u);
    UiSession_SetBuzzerEnabled(0u);
    CHECK(s_click == 1);
    UiSession_Beep(1000u, 10u);
    CHECK(s_beeps == 1);
}

int main(void)
{
    test_three_counted_measures_complete();
    test_aborted_repeats_same_measure();
    test_redo_and_new_patient_restart();
    test_cycle_started_with_pending_choice_redoes_series();
    test_sound_switches_are_independent();
    printf(s_fail ? "ui_series: %d FALHA(S)\n" : "ui_series: OK\n", s_fail);
    return s_fail != 0;
}
