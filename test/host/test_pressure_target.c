/* Rotina de ir a' pressao, contra um modelo de tubo + manguito como o da bancada. */
#include "pressure_target.h"
#include <stdio.h>

static int s_fail;
#define CHECK(c) do { if (!(c)) { printf("FALHA %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)

typedef struct {
    float tube;
    float cuff;
    float pump_mmhg_s;  /* subida do tubo a 100% */
    float flow;         /* 1/s: troca entre tubo e manguito */
    float cuff_ratio;   /* volume do manguito / volume do tubo */
    float leak_mmhg_s;
    uint32_t ms;
} Plant_t;

static void plant_step(Plant_t *p, const PressureTarget_Output_t *o)
{
    const float dt = 0.001f;
    float q = (p->tube - p->cuff) * p->flow;

    p->tube += ((o->pump_power * p->pump_mmhg_s) - q) * dt;
    p->cuff += (q / p->cuff_ratio) * dt;
    if (o->vent_open != 0u) { p->tube -= p->tube * 8.0f * dt; }
    p->cuff -= p->leak_mmhg_s * dt;
    if (p->cuff < 0.0f) { p->cuff = 0.0f; }
    if (p->tube < 0.0f) { p->tube = 0.0f; }
    p->ms++;
}

static PressureTarget_State_t run(Plant_t *p, PressureTarget_t *pt, float target, uint32_t max_ms)
{
    PressureTarget_Output_t o = { 0.0f, 0u };
    uint32_t i;

    PressureTarget_Start(pt, target);
    for (i = 0u; i < max_ms; i++) {
        PressureTarget_Step(pt, p->tube, 1u, &o);
        if ((pt->state == PRESSURE_TARGET_STABLE) || (pt->state == PRESSURE_TARGET_FAILED)) {
            break;
        }
        plant_step(p, &o);
    }
    return pt->state;
}

static void test_settles_after_slow_fill(void)
{
    Plant_t p = { 0.0f, 0.0f, 90.0f, 3.0f, 2.0f, 0.1f, 0u };
    PressureTarget_t pt;
    PressureTarget_Output_t off = { 0.0f, 0u };
    float at_stable;
    uint32_t i;

    CHECK(run(&p, &pt, 100.0f, 100000u) == PRESSURE_TARGET_STABLE);
    at_stable = p.tube;
    printf("  [lento] %.1f s, tubo %.1f manguito %.1f\n", (double)p.ms / 1000.0, (double)p.tube, (double)p.cuff);
    CHECK((at_stable > 94.0f) && (at_stable < 106.0f));
    for (i = 0u; i < 10000u; i++) { plant_step(&p, &off); }
    CHECK((at_stable - p.tube) < 8.0f); /* assentado de verdade: nao desaba depois */
}

static void test_big_steady_leak_can_be_measured(void)
{
    Plant_t p = { 0.0f, 0.0f, 90.0f, 8.0f, 0.5f, 1.0f, 0u };
    PressureTarget_t pt;

    /* Vazamento constante nao pode virar reabastecimento sem fim. */
    CHECK(run(&p, &pt, 60.0f, 100000u) == PRESSURE_TARGET_STABLE);
}

static void test_cuff_that_cannot_hold_fails_unstable(void)
{
    Plant_t p = { 0.0f, 0.0f, 90.0f, 8.0f, 0.5f, 25.0f, 0u };
    PressureTarget_t pt;

    CHECK(run(&p, &pt, 100.0f, 120000u) == PRESSURE_TARGET_FAILED);
    CHECK((pt.fail == PRESSURE_TARGET_FAIL_UNSTABLE) || (pt.fail == PRESSURE_TARGET_FAIL_NO_RISE));
}

static void test_open_circuit_stops_by_timeout(void)
{
    Plant_t p = { 0.0f, 0.0f, 90.0f, 8.0f, 0.5f, 300.0f, 0u };
    PressureTarget_t pt;
    PressureTarget_Output_t o = { 0.0f, 0u };

    CHECK(run(&p, &pt, 100.0f, 120000u) == PRESSURE_TARGET_FAILED);
    CHECK(pt.fail == PRESSURE_TARGET_FAIL_NO_RISE);
    PressureTarget_Step(&pt, 0.0f, 1u, &o);
    CHECK(o.pump_power == 0.0f);
}

static void test_overpressure_stops_pump_at_once(void)
{
    PressureTarget_t pt;
    PressureTarget_Output_t o = { 1.0f, 1u };

    PressureTarget_Start(&pt, 100.0f);
    PressureTarget_Step(&pt, 121.0f, 1u, &o);
    CHECK(pt.state == PRESSURE_TARGET_FAILED);
    CHECK(pt.fail == PRESSURE_TARGET_FAIL_OVER);
    CHECK(o.pump_power == 0.0f);
}

static void test_above_target_vents(void)
{
    PressureTarget_t pt;
    PressureTarget_Output_t o = { 0.0f, 0u };
    uint32_t i;
    uint32_t vents = 0u;

    PressureTarget_Start(&pt, 60.0f);
    for (i = 0u; (i < 7000u) && (vents == 0u); i++) {
        PressureTarget_Step(&pt, 75.0f, 1u, &o);
        vents += o.vent_open;
    }
    CHECK(vents > 0u);
}

static void test_stop_silences_outputs(void)
{
    PressureTarget_t pt;
    PressureTarget_Output_t o = { 0.0f, 0u };

    PressureTarget_Start(&pt, 100.0f);
    PressureTarget_Step(&pt, 0.0f, 1u, &o);
    CHECK(o.pump_power == 1.0f);
    PressureTarget_Stop(&pt);
    PressureTarget_Step(&pt, 0.0f, 1u, &o);
    CHECK(o.pump_power == 0.0f);
}

int main(void)
{
    test_settles_after_slow_fill();
    test_big_steady_leak_can_be_measured();
    test_cuff_that_cannot_hold_fails_unstable();
    test_open_circuit_stops_by_timeout();
    test_overpressure_stops_pump_at_once();
    test_above_target_vents();
    test_stop_silences_outputs();
    printf(s_fail ? "pressure_target: %d FALHA(S)\n" : "pressure_target: OK\n", s_fail);
    return s_fail != 0;
}
