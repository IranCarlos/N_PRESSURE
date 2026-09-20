/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-09-20
 *
 * Bancada de atuadores (bomba + valvulas) comandada por variaveis no debugger,
 * SEM a malha de controle. Logica pura (sem HAL): decide as saidas e aplica
 * os intertravamentos; quem aplica no hardware e' o chamador (main.c).
 *
 * Seguranca (todas latcham ate' desarmar e rearmar):
 *  - desarmado (arm == 0): bomba 0 e V1+V2+V3 abertas;
 *  - potencia limitada a ACTBENCH_PUMP_MAX;
 *  - bomba ligada de forma continua por mais de ACTBENCH_PUMP_TIMEOUT_MS = trip;
 *  - pressao acima de ACTBENCH_OVERPRESSURE_KPA ou sensor invalido = trip.
 */
#ifndef ACTUATOR_BENCH_H
#define ACTUATOR_BENCH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ACTBENCH_PUMP_MAX            (0.30f)
#define ACTBENCH_PUMP_TIMEOUT_MS     (5000u)
#define ACTBENCH_OVERPRESSURE_KPA    (15.0f)   /* ~112 mmHg */

#define ACTBENCH_V1   (1u << 0)
#define ACTBENCH_V2   (1u << 1)
#define ACTBENCH_V3   (1u << 2)
#define ACTBENCH_ALL_OPEN (ACTBENCH_V1 | ACTBENCH_V2 | ACTBENCH_V3)

typedef enum {
    ACTBENCH_TRIP_NONE = 0,
    ACTBENCH_TRIP_OVERPRESSURE,
    ACTBENCH_TRIP_SENSOR,
    ACTBENCH_TRIP_PUMP_TIMEOUT
} ActuatorBench_Trip_t;

typedef struct {
    uint8_t arm;               /* 0 = seguro; 1 = obedece os comandos */
    float   pump_cmd;          /* 0..1, sera' limitada */
    uint8_t valve_open_mask;   /* bit=1 -> valvula aberta */
} ActuatorBench_Cmd_t;

typedef struct {
    float   pump_power;
    uint8_t valve_open_mask;
} ActuatorBench_Out_t;

typedef struct {
    uint32_t             pump_on_ms;
    ActuatorBench_Trip_t trip;
} ActuatorBench_t;

void ActuatorBench_Init(ActuatorBench_t *b);

void ActuatorBench_Step(ActuatorBench_t *b, const ActuatorBench_Cmd_t *cmd,
                        float pressure_kpa, uint8_t sensor_ok, uint32_t dt_ms,
                        ActuatorBench_Out_t *out);

#ifdef __cplusplus
}
#endif

#endif /* ACTUATOR_BENCH_H */
