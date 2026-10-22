/* Teste de host de pressure_core (sem HAL). Ver test/host/run.sh. */
#include "pressure_core.h"
#include <stdio.h>
#include <math.h>

static int s_fail;
#define CHECK(c) do { if (!(c)) { printf("FALHA %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)
#define NEAR(a, b, t) CHECK(fabsf((a) - (b)) <= (t))

/* counts que o ADC produz para uma tensao no SENSOR */
static uint16_t counts_for_sensor_v(float v)
{
    return (uint16_t)(v / PRESSURE_SENSOR_VSS_V * PRESSURE_ADC_MAX_COUNTS + 0.5f);
}

static void fill(uint16_t *b, size_t n, uint16_t v) { for (size_t i = 0; i < n; i++) { b[i] = v; } }

static void test_curve_endpoints(void)
{
    PressureCore_t pc; uint16_t b[16];
    PressureCore_Init(&pc, NULL);
    fill(b, 16, counts_for_sensor_v(0.2f));
    PressureCore_ProcessBlock(&pc, b, 16, 0);
    NEAR(pc.raw_kpa, -25.0f, 0.01f);
    fill(b, 16, counts_for_sensor_v(4.7f));
    PressureCore_ProcessBlock(&pc, b, 16, 0);
    NEAR(pc.raw_kpa, 25.0f, 0.01f);
    fill(b, 16, counts_for_sensor_v(2.45f));   /* meio da escala = 0 kPa */
    PressureCore_ProcessBlock(&pc, b, 16, 0);
    NEAR(pc.raw_kpa, 0.0f, 0.01f);
}

static void test_baseline_and_diff(void)
{
    PressureCore_t pc; uint16_t b[16];
    PressureCore_Init(&pc, NULL);
    fill(b, 16, counts_for_sensor_v(2.50f));   /* offset do sensor */
    PressureCore_ProcessBlock(&pc, b, 16, 1);  /* 1o bloco fixa o baseline */
    NEAR(pc.pressure_kpa, 0.0f, 1e-4f);
    /* sem tracking: baseline congela, pressao sobe */
    fill(b, 16, counts_for_sensor_v(2.50f + 0.09f)); /* +0.09 V = +1 kPa */
    PressureCore_ProcessBlock(&pc, b, 16, 0);
    NEAR(pc.pressure_kpa, 1.0f, 0.01f);
}

static void test_baseline_ignores_applied_pressure(void)
{
    PressureCore_t pc; uint16_t b[16];
    PressureCore_Init(&pc, NULL);
    fill(b, 16, counts_for_sensor_v(2.50f));
    PressureCore_ProcessBlock(&pc, b, 16, 1);
    /* +2 kPa aplicados durante 10 s COM tracking ligado (estado IDLE):
     * fora da janela, o zero nao pode acompanhar. */
    fill(b, 16, counts_for_sensor_v(2.50f + 0.18f));
    for (int i = 0; i < 10000; i++) { PressureCore_ProcessBlock(&pc, b, 16, 1); }
    NEAR(pc.pressure_kpa, 2.0f, 0.02f);
}

/* Boot com o tubo ja' pressurizado (bomba ligou durante o reset): a 1a
 * amostra fixa o zero alto; quando a pressao escoa ate' a atmosfera, o zero
 * tem que descer junto (antes ficava preso fora da janela para sempre). */
static void test_baseline_recovers_from_prepressurized_boot(void)
{
    PressureCore_t pc; uint16_t b[16];
    PressureCore_Init(&pc, NULL);
    fill(b, 16, counts_for_sensor_v(2.50f + 0.45f));  /* +5 kPa no boot */
    PressureCore_ProcessBlock(&pc, b, 16, 1);
    fill(b, 16, counts_for_sensor_v(2.50f));          /* escoou: atmosfera */
    for (int i = 0; i < 1000; i++) { PressureCore_ProcessBlock(&pc, b, 16, 1); }
    NEAR(pc.pressure_kpa, 0.0f, 0.02f);
}

static void test_baseline_follows_slow_drift(void)
{
    PressureCore_t pc; uint16_t b[16];
    PressureCore_Init(&pc, NULL);
    fill(b, 16, counts_for_sensor_v(2.50f));
    PressureCore_ProcessBlock(&pc, b, 16, 1);
    /* deriva de +0.2 kPa (dentro da janela): apos ~30 s (6 constantes) some. */
    fill(b, 16, counts_for_sensor_v(2.50f + 0.018f));
    for (int i = 0; i < 30000; i++) { PressureCore_ProcessBlock(&pc, b, 16, 1); }
    NEAR(pc.pressure_kpa, 0.0f, 0.01f);
}

static void test_calibration_applied(void)
{
    PressureCore_t pc; uint16_t b[16];
    PressureCal_t cal = { .offset_kpa = 0.5f, .scale = 2.0f };
    PressureCore_Init(&pc, &cal);
    fill(b, 16, counts_for_sensor_v(2.50f));
    PressureCore_ProcessBlock(&pc, b, 16, 1);
    fill(b, 16, counts_for_sensor_v(2.50f + 0.09f));
    PressureCore_ProcessBlock(&pc, b, 16, 0);
    NEAR(pc.pressure_kpa, 1.0f * 2.0f + 0.5f, 0.02f);
}

static void test_two_point(void)
{
    PressureCal_t c;
    /* sensor le 1.9 kPa quando manometro marca 2.0; le 9.6 quando marca 10 */
    CHECK(PressureCore_CalibrateTwoPoint(2.0f, 1.9f, 10.0f, 9.6f, &c) == 0);
    NEAR(1.9f * c.scale + c.offset_kpa, 2.0f, 1e-4f);
    NEAR(9.6f * c.scale + c.offset_kpa, 10.0f, 1e-4f);
    CHECK(PressureCore_CalibrateTwoPoint(2.0f, 5.0f, 10.0f, 5.0f, &c) == -1);
}

static void test_out_of_range(void)
{
    PressureCore_t pc; uint16_t b[16];
    PressureCore_Init(&pc, NULL);
    fill(b, 16, 0);                            /* cabo solto / GND */
    PressureCore_ProcessBlock(&pc, b, 16, 0);
    CHECK((pc.flags & PRESSURE_FLAG_OUT_OF_RANGE) != 0u);
    NEAR(pc.raw_kpa, -25.0f, 0.01f);           /* clamp preservado */
    fill(b, 16, counts_for_sensor_v(2.5f));
    PressureCore_ProcessBlock(&pc, b, 16, 0);
    CHECK((pc.flags & PRESSURE_FLAG_OUT_OF_RANGE) == 0u);
}

static void test_stuck(void)
{
    PressureCore_t pc; uint16_t b[16];
    PressureCore_Init(&pc, NULL);
    fill(b, 16, 30000);
    for (unsigned i = 0; i < PRESSURE_STUCK_BLOCKS - 1u; i++) { PressureCore_ProcessBlock(&pc, b, 16, 0); }
    CHECK((pc.flags & PRESSURE_FLAG_STUCK) == 0u);
    PressureCore_ProcessBlock(&pc, b, 16, 0);
    CHECK((pc.flags & PRESSURE_FLAG_STUCK) != 0u);
    b[3] = 30001;                              /* qualquer ruido limpa */
    PressureCore_ProcessBlock(&pc, b, 16, 0);
    CHECK((pc.flags & PRESSURE_FLAG_STUCK) == 0u);
    CHECK(pc.last_p2p_counts == 1u);
}

static void test_noise_stats_and_guards(void)
{
    PressureCore_t pc; uint16_t b[4] = { 100, 110, 90, 100 };
    PressureCore_Init(&pc, NULL);
    PressureCore_ProcessBlock(&pc, b, 4, 0);
    CHECK(pc.last_mean_counts == 100u);
    CHECK(pc.last_min_counts == 90u && pc.last_max_counts == 110u);
    CHECK(pc.last_p2p_counts == 20u);
    PressureCore_ProcessBlock(&pc, b, 0, 0);   /* n=0 nao deve quebrar */
    PressureCore_ProcessBlock(&pc, NULL, 4, 0);
}

int main(void)
{
    test_curve_endpoints();
    test_baseline_and_diff();
    test_baseline_ignores_applied_pressure();
    test_baseline_follows_slow_drift();
    test_baseline_recovers_from_prepressurized_boot();
    test_calibration_applied();
    test_two_point();
    test_out_of_range();
    test_stuck();
    test_noise_stats_and_guards();
    printf(s_fail ? "pressure_core: %d FALHA(S)\n" : "pressure_core: OK\n", s_fail);
    return s_fail != 0;
}
