/* Testes de host: valve, pwm_motor e actuator_bench (portas mockadas). */
#include "valve.h"
#include "pwm_motor.h"
#include "actuator_bench.h"
#include <stdio.h>
#include <math.h>

static int s_fail;
#define CHECK(c) do { if (!(c)) { printf("FALHA %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)

/* ---------------- valve ---------------- */
static uint8_t s_pin[VALVE_COUNT];
static int     s_pin_writes;

static void mock_write(void *ctx, Valve_Id_t id, uint8_t level)
{
    (void)ctx;
    s_pin[id] = level;
    s_pin_writes++;
}

static void test_valve_na_low_is_open(void)
{
    Valve_Hw_t hw = { .write = mock_write, .ctx = NULL, .open_level = 0u };
    Valve_Init(&hw);
    /* Init deixa tudo ABERTO = pino LOW (valvula NA de-energizada). */
    CHECK(s_pin[0] == 0u && s_pin[1] == 0u && s_pin[2] == 0u);
    CHECK(Valve_Get(VALVE_1) == VALVE_OPEN);

    CHECK(Valve_Set(VALVE_2, VALVE_CLOSED) == 0);
    CHECK(s_pin[1] == 1u);
    CHECK(s_pin[0] == 0u && s_pin[2] == 0u);
    CHECK(Valve_Get(VALVE_2) == VALVE_CLOSED);

    Valve_SetAll(0, 1, 0);
    CHECK(s_pin[0] == 1u && s_pin[1] == 0u && s_pin[2] == 1u);

    Valve_OpenAll();
    CHECK(s_pin[0] == 0u && s_pin[1] == 0u && s_pin[2] == 0u);
}

static void test_valve_polarity_inverted(void)
{
    Valve_Hw_t hw = { .write = mock_write, .ctx = NULL, .open_level = 1u };
    Valve_Init(&hw);
    CHECK(s_pin[0] == 1u);                     /* aberta = HIGH nesta polaridade */
    (void)Valve_Set(VALVE_1, VALVE_CLOSED);
    CHECK(s_pin[0] == 0u);
}

static void test_valve_guards(void)
{
    Valve_Hw_t hw = { .write = mock_write, .ctx = NULL, .open_level = 0u };
    int before;
    Valve_Init(&hw);
    before = s_pin_writes;
    CHECK(Valve_Set((Valve_Id_t)7, VALVE_CLOSED) == -1);
    CHECK(s_pin_writes == before);
    CHECK(Valve_Get((Valve_Id_t)7) == VALVE_OPEN);   /* invalido -> seguro */

    Valve_Init(NULL);                                /* sem HW: nada escreve */
    before = s_pin_writes;
    CHECK(Valve_Set(VALVE_1, VALVE_CLOSED) == -1);
    CHECK(s_pin_writes == before);
}

/* ---------------- pwm_motor ---------------- */
static uint32_t s_ccr;

static void mock_ccr(void *ctx, uint32_t v) { (void)ctx; s_ccr = v; }

static void test_pwm_mapping(void)
{
    PwmMotor_Hw_t hw = { .set_compare = mock_ccr, .ctx = NULL, .period_counts = 13750u };
    s_ccr = 999u;
    PwmMotor_Init(&hw);
    CHECK(s_ccr == 0u);                        /* Init garante potencia 0 */

    CHECK(PwmMotor_SetPower(0.5f) == 0.5f);
    CHECK(s_ccr == 6875u);
    (void)PwmMotor_SetPower(1.0f);
    CHECK(s_ccr == 13750u);                    /* 100 % real, nao ARR */
    (void)PwmMotor_SetPower(2.5f);
    CHECK(s_ccr == 13750u);                    /* satura */
    (void)PwmMotor_SetPower(-1.0f);
    CHECK(s_ccr == 0u);
    (void)PwmMotor_SetPower(NAN);
    CHECK(s_ccr == 0u && PwmMotor_GetPower() == 0.0f);
}

static void test_pwm_limit_and_stop(void)
{
    PwmMotor_Hw_t hw = { .set_compare = mock_ccr, .ctx = NULL, .period_counts = 1000u };
    PwmMotor_Init(&hw);
    PwmMotor_SetLimit(0.3f);
    CHECK(fabsf(PwmMotor_SetPower(1.0f) - 0.3f) < 1e-6f);
    CHECK(s_ccr == 300u);
    PwmMotor_Stop();
    CHECK(s_ccr == 0u);
    PwmMotor_SetLimit(NAN);                    /* limite invalido = 0: nunca liga */
    (void)PwmMotor_SetPower(1.0f);
    CHECK(s_ccr == 0u);
}

static void test_pwm_guards(void)
{
    PwmMotor_Hw_t bad = { .set_compare = NULL, .ctx = NULL, .period_counts = 100u };
    PwmMotor_Init(&bad);
    CHECK(PwmMotor_SetPower(1.0f) == 0.0f);    /* sem HW nao aplica */
}

/* ---------------- actuator_bench ---------------- */
static ActuatorBench_Out_t step(ActuatorBench_t *b, uint8_t arm, float pump,
                                uint8_t mask, float kpa, uint8_t ok, uint32_t dt)
{
    ActuatorBench_Cmd_t c = { .arm = arm, .pump_cmd = pump, .valve_open_mask = mask };
    ActuatorBench_Out_t o;
    ActuatorBench_Step(b, &c, kpa, ok, dt, &o);
    return o;
}

static void test_bench_disarmed_is_safe(void)
{
    ActuatorBench_t b; ActuatorBench_Out_t o;
    ActuatorBench_Init(&b);
    o = step(&b, 0, 1.0f, 0, 0.0f, 1, 1);
    CHECK(o.pump_power == 0.0f && o.valve_open_mask == ACTBENCH_ALL_OPEN);
}

static void test_bench_armed_obeys_and_caps(void)
{
    ActuatorBench_t b; ActuatorBench_Out_t o;
    ActuatorBench_Init(&b);
    o = step(&b, 1, 0.2f, ACTBENCH_V3, 0.0f, 1, 1);
    CHECK(fabsf(o.pump_power - 0.2f) < 1e-6f && o.valve_open_mask == ACTBENCH_V3);
    o = step(&b, 1, 1.0f, ACTBENCH_V3, 0.0f, 1, 1);
    CHECK(fabsf(o.pump_power - ACTBENCH_PUMP_MAX) < 1e-6f);
    o = step(&b, 1, NAN, 0, 0.0f, 1, 1);
    CHECK(o.pump_power == 0.0f);
}

static void test_bench_overpressure_latches(void)
{
    ActuatorBench_t b; ActuatorBench_Out_t o;
    ActuatorBench_Init(&b);
    o = step(&b, 1, 0.3f, 0, ACTBENCH_OVERPRESSURE_KPA + 0.1f, 1, 1);
    CHECK(o.pump_power == 0.0f && o.valve_open_mask == ACTBENCH_ALL_OPEN);
    CHECK(b.trip == ACTBENCH_TRIP_OVERPRESSURE);
    o = step(&b, 1, 0.3f, 0, 0.0f, 1, 1);      /* pressao voltou: continua travado */
    CHECK(o.pump_power == 0.0f && o.valve_open_mask == ACTBENCH_ALL_OPEN);
    (void)step(&b, 0, 0.0f, 0, 0.0f, 1, 1);    /* desarma = rearme */
    o = step(&b, 1, 0.3f, 0, 0.0f, 1, 1);
    CHECK(o.pump_power > 0.0f);
}

static void test_bench_sensor_fault_trips(void)
{
    ActuatorBench_t b; ActuatorBench_Out_t o;
    ActuatorBench_Init(&b);
    o = step(&b, 1, 0.3f, 0, 0.0f, 0, 1);
    CHECK(o.pump_power == 0.0f && b.trip == ACTBENCH_TRIP_SENSOR);
    ActuatorBench_Init(&b);
    o = step(&b, 1, 0.3f, 0, NAN, 1, 1);
    CHECK(o.pump_power == 0.0f && b.trip == ACTBENCH_TRIP_SENSOR);
}

static void test_bench_pump_timeout(void)
{
    ActuatorBench_t b; ActuatorBench_Out_t o = {0};
    ActuatorBench_Init(&b);
    for (uint32_t t = 0; t < ACTBENCH_PUMP_TIMEOUT_MS; t += 10u) {
        o = step(&b, 1, 0.3f, 0, 0.0f, 1, 10);
    }
    CHECK(o.pump_power > 0.0f);
    o = step(&b, 1, 0.3f, 0, 0.0f, 1, 10);
    CHECK(o.pump_power == 0.0f && b.trip == ACTBENCH_TRIP_PUMP_TIMEOUT);
    CHECK(o.valve_open_mask == ACTBENCH_ALL_OPEN);
}

static void test_bench_pump_pause_resets_timer(void)
{
    ActuatorBench_t b; ActuatorBench_Out_t o;
    ActuatorBench_Init(&b);
    for (int i = 0; i < 3; i++) {
        for (uint32_t t = 0; t < 4000u; t += 10u) { (void)step(&b, 1, 0.3f, 0, 0.0f, 1, 10); }
        (void)step(&b, 1, 0.0f, 0, 0.0f, 1, 10);   /* pausa zera */
    }
    o = step(&b, 1, 0.3f, 0, 0.0f, 1, 10);
    CHECK(o.pump_power > 0.0f && b.trip == ACTBENCH_TRIP_NONE);
}

int main(void)
{
    test_valve_na_low_is_open();
    test_valve_polarity_inverted();
    test_valve_guards();
    test_pwm_mapping();
    test_pwm_limit_and_stop();
    test_pwm_guards();
    test_bench_disarmed_is_safe();
    test_bench_armed_obeys_and_caps();
    test_bench_overpressure_latches();
    test_bench_sensor_fault_trips();
    test_bench_pump_timeout();
    test_bench_pump_pause_resets_timer();
    printf(s_fail ? "actuators: %d FALHA(S)\n" : "actuators: OK\n", s_fail);
    return s_fail != 0;
}
