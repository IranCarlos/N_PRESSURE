/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-09-20
 */

#include "valve.h"
#include <stddef.h>

static Valve_Hw_t    s_hw;
static uint8_t       s_ready;
static Valve_State_t s_state[VALVE_COUNT];

static void drive(Valve_Id_t id, Valve_State_t state)
{
    uint8_t level = (state == VALVE_OPEN) ? s_hw.open_level
                                          : (uint8_t)((s_hw.open_level == 0u) ? 1u : 0u);
    s_hw.write(s_hw.ctx, id, level);
    s_state[id] = state;
}

void Valve_Init(const Valve_Hw_t *hw)
{
    s_ready = 0u;
    if ((hw == NULL) || (hw->write == NULL)) {
        return;
    }
    s_hw = *hw;
    s_ready = 1u;
    Valve_OpenAll();
}

int Valve_Set(Valve_Id_t id, Valve_State_t state)
{
    if ((s_ready == 0u) || ((unsigned)id >= (unsigned)VALVE_COUNT)) {
        return -1;
    }
    drive(id, (state == VALVE_OPEN) ? VALVE_OPEN : VALVE_CLOSED);
    return 0;
}

Valve_State_t Valve_Get(Valve_Id_t id)
{
    if ((s_ready == 0u) || ((unsigned)id >= (unsigned)VALVE_COUNT)) {
        return VALVE_OPEN; /* na duvida, reporta o estado seguro */
    }
    return s_state[id];
}

void Valve_SetAll(uint8_t v1_open, uint8_t v2_open, uint8_t v3_open)
{
    (void)Valve_Set(VALVE_1, (v1_open != 0u) ? VALVE_OPEN : VALVE_CLOSED);
    (void)Valve_Set(VALVE_2, (v2_open != 0u) ? VALVE_OPEN : VALVE_CLOSED);
    (void)Valve_Set(VALVE_3, (v3_open != 0u) ? VALVE_OPEN : VALVE_CLOSED);
}

void Valve_OpenAll(void)
{
    Valve_SetAll(1u, 1u, 1u);
}
