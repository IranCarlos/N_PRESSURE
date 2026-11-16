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

/* Testes genericos usam a curva do sensor selecionado
 * (pressure_sensor_model.h); run.sh roda este arquivo com os dois modelos. */
#define V_PER_KPA ((PRESSURE_SENSOR_MAX_V - PRESSURE_SENSOR_MIN_V) /                    (PRESSURE_SENSOR_MAX_KPA - PRESSURE_SENSOR_MIN_KPA))
/* Tensao do "zero atmosferico" nos testes de baseline: um ponto qualquer
 * dentro da curva (o modulo so' ve diferencas em relacao a ele). */
#define V_ATM (2.50f)
/* Tensao no sensor para `kpa` acima de V_ATM. */
static float v_above_atm(float kpa) { return V_ATM + (kpa * V_PER_KPA); }

static float kpa_at(float v)
{
    return PRESSURE_SENSOR_MIN_KPA + ((v - PRESSURE_SENSOR_MIN_V) / V_PER_KPA);
}

static void test_curve_endpoints(void)
{
    PressureCore_t pc; uint16_t b[16];
    PressureCore_Init(&pc, NULL);
    fill(b, 16, counts_for_sensor_v(PRESSURE_SENSOR_MIN_V));
    PressureCore_ProcessBlock(&pc, b, 16, 0);
    NEAR(pc.raw_kpa, PRESSURE_SENSOR_MIN_KPA, 0.01f);
    fill(b, 16, counts_for_sensor_v(PRESSURE_SENSOR_MAX_V));
    PressureCore_ProcessBlock(&pc, b, 16, 0);
    NEAR(pc.raw_kpa, PRESSURE_SENSOR_MAX_KPA, 0.01f);
    fill(b, 16, counts_for_sensor_v(4.9f));    /* acima do fundo: satura */
    PressureCore_ProcessBlock(&pc, b, 16, 0);
    NEAR(pc.raw_kpa, PRESSURE_SENSOR_MAX_KPA, 0.01f);
#if PRESSURE_SENSOR_MODEL == PRESSURE_SENSOR_MPXV7025DP
    fill(b, 16, counts_for_sensor_v(2.45f));   /* meio da escala = 0 kPa */
    PressureCore_ProcessBlock(&pc, b, 16, 0);
    NEAR(pc.raw_kpa, 0.0f, 0.01f);
#elif PRESSURE_SENSOR_MODEL == PRESSURE_SENSOR_MPXV5100GP
    fill(b, 16, counts_for_sensor_v(2.0f));    /* 40 kPa = 300 mmHg */
    PressureCore_ProcessBlock(&pc, b, 16, 0);
    NEAR(pc.raw_kpa, 40.0f, 0.01f);
#endif
}

#if PRESSURE_SENSOR_MODEL == PRESSURE_SENSOR_MPXV5100GP
/* Manometrico: o zero atmosferico fica no pe' da curva e o offset real varia
 * por unidade (0.088..0.313 V). Abaixo de 0.2 V a curva tem que continuar
 * (extrapolada), sem acusar sensor invalido; senao a pressao saindo do zero
 * ficaria "achatada" ate' o sinal passar de 0.2 V. */
static void test_gauge_offset_below_nominal(void)
{
    PressureCore_t pc; uint16_t b[16];
    const float v_off = 0.15f;                 /* unidade com offset baixo */
    PressureCore_Init(&pc, NULL);
    fill(b, 16, counts_for_sensor_v(v_off));
    PressureCore_ProcessBlock(&pc, b, 16, 1);  /* zero */
    CHECK((pc.flags & PRESSURE_FLAG_OUT_OF_RANGE) == 0u);
    NEAR(pc.raw_kpa, -1.111f, 0.01f);          /* extrapolado, nao 0 */
    NEAR(pc.pressure_kpa, 0.0f, 1e-4f);
    /* +5 mmHg (0.6667 kPa) a partir do zero: ainda abaixo de 0.2 V */
    fill(b, 16, counts_for_sensor_v(v_off + (0.6667f * V_PER_KPA)));
    PressureCore_ProcessBlock(&pc, b, 16, 0);
    NEAR(pc.pressure_kpa, 0.6667f, 0.01f);
    /* 300 mmHg acima do zero */
    fill(b, 16, counts_for_sensor_v(v_off + (40.0f * V_PER_KPA)));
    PressureCore_ProcessBlock(&pc, b, 16, 0);
    NEAR(pc.pressure_kpa, 40.0f, 0.02f);
    /* menor offset de datasheet com Vs no minimo (0.084 V): ainda valido */
    fill(b, 16, counts_for_sensor_v(0.084f));
    PressureCore_ProcessBlock(&pc, b, 16, 1);
    CHECK((pc.flags & PRESSURE_FLAG_OUT_OF_RANGE) == 0u);
    /* rastreio do zero desce ate' a leitura (precisa ver abaixo do zero) */
    for (int i = 0; i < 1000; i++) { PressureCore_ProcessBlock(&pc, b, 16, 1); }
    NEAR(pc.pressure_kpa, 0.0f, 0.02f);
}
#endif

static void test_baseline_and_diff(void)
{
    PressureCore_t pc; uint16_t b[16];
    PressureCore_Init(&pc, NULL);
    fill(b, 16, counts_for_sensor_v(V_ATM));   /* offset do sensor */
    PressureCore_ProcessBlock(&pc, b, 16, 1);  /* 1o bloco fixa o baseline */
    NEAR(pc.pressure_kpa, 0.0f, 1e-4f);
    /* sem tracking: baseline congela, pressao sobe */
    fill(b, 16, counts_for_sensor_v(v_above_atm(1.0f))); /* +1 kPa */
    PressureCore_ProcessBlock(&pc, b, 16, 0);
    NEAR(pc.pressure_kpa, 1.0f, 0.01f);
}

static void test_baseline_ignores_applied_pressure(void)
{
    PressureCore_t pc; uint16_t b[16];
    PressureCore_Init(&pc, NULL);
    fill(b, 16, counts_for_sensor_v(V_ATM));
    PressureCore_ProcessBlock(&pc, b, 16, 1);
    /* +2 kPa aplicados durante 10 s COM tracking ligado (estado IDLE):
     * fora da janela, o zero nao pode acompanhar. */
    fill(b, 16, counts_for_sensor_v(v_above_atm(2.0f)));
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
    fill(b, 16, counts_for_sensor_v(v_above_atm(5.0f)));  /* +5 kPa no boot */
    PressureCore_ProcessBlock(&pc, b, 16, 1);
    fill(b, 16, counts_for_sensor_v(V_ATM));          /* escoou: atmosfera */
    for (int i = 0; i < 1000; i++) { PressureCore_ProcessBlock(&pc, b, 16, 1); }
    NEAR(pc.pressure_kpa, 0.0f, 0.02f);
}

static void test_baseline_follows_slow_drift(void)
{
    PressureCore_t pc; uint16_t b[16];
    PressureCore_Init(&pc, NULL);
    fill(b, 16, counts_for_sensor_v(V_ATM));
    PressureCore_ProcessBlock(&pc, b, 16, 1);
    /* deriva de +0.2 kPa (dentro da janela): apos ~30 s (6 constantes) some. */
    fill(b, 16, counts_for_sensor_v(v_above_atm(0.2f)));
    for (int i = 0; i < 30000; i++) { PressureCore_ProcessBlock(&pc, b, 16, 1); }
    NEAR(pc.pressure_kpa, 0.0f, 0.01f);
}

static void test_calibration_applied(void)
{
    PressureCore_t pc; uint16_t b[16];
    PressureCal_t cal = { .offset_kpa = 0.5f, .scale = 2.0f };
    PressureCore_Init(&pc, &cal);
    fill(b, 16, counts_for_sensor_v(V_ATM));
    PressureCore_ProcessBlock(&pc, b, 16, 1);
    fill(b, 16, counts_for_sensor_v(v_above_atm(1.0f)));
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
    NEAR(pc.raw_kpa, kpa_at(PRESSURE_SENSOR_CLAMP_MIN_V), 0.01f); /* clamp preservado */
    fill(b, 16, counts_for_sensor_v(V_ATM));
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

/* Fase 7.8: media de captura - converge para o diferencial e o desvio
 * mostra quando a pressao esta' variando. */
static void test_cal_average_and_deviation(void)
{
    PressureCore_t pc; uint16_t b[16]; int i;
    PressureCore_Init(&pc, NULL);
    fill(b, 16, counts_for_sensor_v(V_ATM));
    PressureCore_ProcessBlock(&pc, b, 16, 1);           /* zero */
    for (i = 0; i < 6000; i++) {                         /* 6 s a +1 kPa estavel */
        fill(b, 16, counts_for_sensor_v(v_above_atm(1.0f)));
        PressureCore_ProcessBlock(&pc, b, 16, 0);
    }
    NEAR(pc.cal_avg_kpa, 1.0f, 0.01f);
    CHECK(pc.cal_dev_kpa < 0.005f);
    for (i = 0; i < 3000; i++) {                         /* ondulacao +-0,5 kPa (bomba) */
        fill(b, 16, counts_for_sensor_v(v_above_atm(1.0f + (((i / 20) % 2) ? 0.5f : -0.5f))));
        PressureCore_ProcessBlock(&pc, b, 16, 0);
    }
    NEAR(pc.cal_avg_kpa, 1.0f, 0.05f);                  /* media continua no centro */
    CHECK(pc.cal_dev_kpa > 0.3f);                        /* mas acusa a variacao */
}

int main(void)
{
    printf("pressure_core: sensor %s\n", PRESSURE_SENSOR_NAME);
    test_curve_endpoints();
#if PRESSURE_SENSOR_MODEL == PRESSURE_SENSOR_MPXV5100GP
    test_gauge_offset_below_nominal();
#endif
    test_baseline_and_diff();
    test_baseline_ignores_applied_pressure();
    test_baseline_follows_slow_drift();
    test_baseline_recovers_from_prepressurized_boot();
    test_calibration_applied();
    test_two_point();
    test_out_of_range();
    test_stuck();
    test_noise_stats_and_guards();
    test_cal_average_and_deviation();
    printf(s_fail ? "pressure_core: %d FALHA(S)\n" : "pressure_core: OK\n", s_fail);
    return s_fail != 0;
}
