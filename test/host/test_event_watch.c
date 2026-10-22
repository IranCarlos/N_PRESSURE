/* Teste de host de event_watch (Fase 7.4, sem HAL). Ver test/host/run.sh. */
#include "event_watch.h"
#include <stdio.h>
#include <string.h>

static int s_fail;
#define CHECK(c) do { if (!(c)) { printf("FALHA %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)

static EventWatch_Snapshot_t snap(SystemState_t st, uint8_t err, uint8_t sd, uint8_t fin)
{
    EventWatch_Snapshot_t s;
    s.state = st;
    s.error = err;
    s.sd_mounted = sd;
    s.cycle_finalized = fin;
    return s;
}

static void test_first_poll_only_primes(void)
{
    EventWatch_t w;
    EventWatch_Snapshot_t now = snap(SYSTEM_STATE_FAULT, 2U, 1U, 1U);
    EventWatch_Snapshot_t prev;

    EventWatch_Init(&w);
    CHECK(EventWatch_Poll(&w, &now, &prev) == 0U); /* sem evento falso ao conectar/bootar */
    CHECK(EventWatch_Poll(&w, &now, &prev) == 0U); /* nada mudou */
}

static void test_full_cycle_sequence(void)
{
    EventWatch_t w;
    EventWatch_Snapshot_t s;
    EventWatch_Snapshot_t prev;
    uint32_t m;

    EventWatch_Init(&w);
    s = snap(SYSTEM_STATE_IDLE, 0U, 1U, 1U); /* finalized=1 do ciclo anterior */
    (void)EventWatch_Poll(&w, &s, &prev);

    s = snap(SYSTEM_STATE_INFLATING, 0U, 1U, 0U);
    m = EventWatch_Poll(&w, &s, &prev);
    CHECK(m == EVENT_WATCH_STATE_CHANGED);
    CHECK(prev.state == SYSTEM_STATE_IDLE);

    s = snap(SYSTEM_STATE_DEFLATING_FAST, 3U, 1U, 0U); /* E003 */
    m = EventWatch_Poll(&w, &s, &prev);
    CHECK(m == (EVENT_WATCH_STATE_CHANGED | EVENT_WATCH_ERROR_RAISED));

    s = snap(SYSTEM_STATE_IDLE, 3U, 1U, 1U); /* mesmo erro: nao repete */
    m = EventWatch_Poll(&w, &s, &prev);
    CHECK(m == (EVENT_WATCH_STATE_CHANGED | EVENT_WATCH_CYCLE_FINISHED));

    s = snap(SYSTEM_STATE_IDLE, 0U, 1U, 1U);
    m = EventWatch_Poll(&w, &s, &prev);
    CHECK(m == EVENT_WATCH_ERROR_CLEARED);
    CHECK(prev.error == 3U);
}

static void test_error_code_change_is_new_raise(void)
{
    EventWatch_t w;
    EventWatch_Snapshot_t s;

    EventWatch_Init(&w);
    s = snap(SYSTEM_STATE_IDLE, 3U, 0U, 0U);
    (void)EventWatch_Poll(&w, &s, NULL);
    s = snap(SYSTEM_STATE_IDLE, 4U, 0U, 0U);
    CHECK(EventWatch_Poll(&w, &s, NULL) == EVENT_WATCH_ERROR_RAISED);
}

static void test_sd_edges(void)
{
    EventWatch_t w;
    EventWatch_Snapshot_t s;

    EventWatch_Init(&w);
    s = snap(SYSTEM_STATE_IDLE, 0U, 0U, 0U);
    (void)EventWatch_Poll(&w, &s, NULL);
    s.sd_mounted = 1U;
    CHECK(EventWatch_Poll(&w, &s, NULL) == EVENT_WATCH_SD_INSERTED);
    s.sd_mounted = 0U;
    CHECK(EventWatch_Poll(&w, &s, NULL) == EVENT_WATCH_SD_REMOVED);
}

static void test_state_names_fit_log_field(void)
{
    int st;
    for (st = (int)SYSTEM_STATE_BOOT; st <= (int)SYSTEM_STATE_FAULT; st++) {
        const char *n = EventWatch_StateName((SystemState_t)st);
        CHECK(strlen(n) <= 15U);          /* state_name[16] do StorageTask */
        CHECK(strchr(n, ';') == NULL);    /* ';' e' o delimitador do log */
        CHECK(strcmp(n, "?") != 0);
    }
    CHECK(strcmp(EventWatch_StateName(SYSTEM_STATE_IDLE), "STANDBY") == 0);
}

int main(void)
{
    test_first_poll_only_primes();
    test_full_cycle_sequence();
    test_error_code_change_is_new_raise();
    test_sd_edges();
    test_state_names_fit_log_field();
    printf(s_fail ? "event_watch: %d FALHA(S)\n" : "event_watch: OK\n", s_fail);
    return s_fail != 0;
}
