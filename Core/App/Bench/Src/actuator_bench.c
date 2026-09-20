/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-09-20
 */

#include "actuator_bench.h"
#include <stddef.h>

void ActuatorBench_Init(ActuatorBench_t *b)
{
    b->pump_on_ms = 0u;
    b->trip = ACTBENCH_TRIP_NONE;
}

void ActuatorBench_Step(ActuatorBench_t *b, const ActuatorBench_Cmd_t *cmd,
                        float pressure_kpa, uint8_t sensor_ok, uint32_t dt_ms,
                        ActuatorBench_Out_t *out)
{
    float pump = cmd->pump_cmd;

    out->pump_power = 0.0f;
    out->valve_open_mask = ACTBENCH_ALL_OPEN;

    /* Desarmar limpa o trip: e' o "rearme" explicito. */
    if (cmd->arm == 0u) {
        b->trip = ACTBENCH_TRIP_NONE;
        b->pump_on_ms = 0u;
        return;
    }

    /* Pressao/sensor sao verificados mesmo com trip ja ativo e sempre vencem. */
    if ((sensor_ok == 0u) || !(pressure_kpa == pressure_kpa)) {  /* NaN */
        if (b->trip == ACTBENCH_TRIP_NONE) { b->trip = ACTBENCH_TRIP_SENSOR; }
    } else if (pressure_kpa > ACTBENCH_OVERPRESSURE_KPA) {
        if (b->trip == ACTBENCH_TRIP_NONE) { b->trip = ACTBENCH_TRIP_OVERPRESSURE; }
    }

    if (b->trip != ACTBENCH_TRIP_NONE) {
        return;                       /* saida segura ja preenchida */
    }

    if (!(pump > 0.0f)) {             /* <= 0 ou NaN */
        pump = 0.0f;
    }
    if (pump > ACTBENCH_PUMP_MAX) {
        pump = ACTBENCH_PUMP_MAX;
    }

    if (pump > 0.0f) {
        b->pump_on_ms += dt_ms;
        if (b->pump_on_ms > ACTBENCH_PUMP_TIMEOUT_MS) {
            b->trip = ACTBENCH_TRIP_PUMP_TIMEOUT;
            return;
        }
    } else {
        b->pump_on_ms = 0u;
    }

    out->pump_power = pump;
    out->valve_open_mask = (uint8_t)(cmd->valve_open_mask & ACTBENCH_ALL_OPEN);
}
