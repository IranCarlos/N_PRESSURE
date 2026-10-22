/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-22
 */
#include "event_watch.h"
#include <stddef.h>

void EventWatch_Init(EventWatch_t *w)
{
    if (w == NULL) {
        return;
    }
    w->prev.state = SYSTEM_STATE_BOOT;
    w->prev.error = 0u;
    w->prev.sd_mounted = 0u;
    w->prev.cycle_finalized = 0u;
    w->primed = 0u;
}

uint32_t EventWatch_Poll(EventWatch_t *w, const EventWatch_Snapshot_t *now,
                         EventWatch_Snapshot_t *prev_out)
{
    uint32_t mask = 0u;

    if ((w == NULL) || (now == NULL)) {
        return 0u;
    }
    if (prev_out != NULL) {
        *prev_out = w->prev;
    }
    if (w->primed == 0u) {
        w->prev = *now;
        w->primed = 1u;
        if (prev_out != NULL) {
            *prev_out = *now;
        }
        return 0u;
    }

    if (now->state != w->prev.state) {
        mask |= EVENT_WATCH_STATE_CHANGED;
    }
    if (now->error != w->prev.error) {
        mask |= (now->error != 0u) ? EVENT_WATCH_ERROR_RAISED : EVENT_WATCH_ERROR_CLEARED;
    }
    if ((now->sd_mounted != 0u) && (w->prev.sd_mounted == 0u)) {
        mask |= EVENT_WATCH_SD_INSERTED;
    } else if ((now->sd_mounted == 0u) && (w->prev.sd_mounted != 0u)) {
        mask |= EVENT_WATCH_SD_REMOVED;
    } else {
        /* sem mudanca */
    }
    if ((now->cycle_finalized != 0u) && (w->prev.cycle_finalized == 0u)) {
        mask |= EVENT_WATCH_CYCLE_FINISHED;
    }

    w->prev = *now;
    return mask;
}

const char *EventWatch_StateName(SystemState_t state)
{
    switch (state) {
        case SYSTEM_STATE_BOOT:           return "BOOT";
        case SYSTEM_STATE_IDLE:           return "STANDBY";
        case SYSTEM_STATE_INFLATING:      return "INFLANDO";
        case SYSTEM_STATE_STABILIZING:    return "ESTABILIZANDO";
        case SYSTEM_STATE_STABILIZED:     return "MANUTENCAO";
        case SYSTEM_STATE_DEFLATING_FAST: return "DESINFLANDO";
        case SYSTEM_STATE_LEAK_TEST:      return "TESTE VAZAMENTO";
        case SYSTEM_STATE_CONFIG:         return "AJUSTES";
        case SYSTEM_STATE_MANUAL:         return "MANUAL PC";
        case SYSTEM_STATE_FAULT:          return "ERRO";
        default:                          return "?";
    }
}
