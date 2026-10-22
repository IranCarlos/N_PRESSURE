/*
 * Fase 3 — Control_Core contra o simulador de planta (sim_plant). Cobre o
 * criterio de aceitacao do prompt: overshoot <= overshoot_max, sem
 * chattering, falhas injetadas (sensor travado/vazamento/sobrepressao)
 * levam ao estado seguro e ao erro correto. Resultados tambem alimentam
 * Docs/sim-report.md.
 */
#include "control_core.h"
#include "sim_plant.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

#define MMHG_PER_KPA (7.50062f)

static int s_fail;
#define CHECK(c) do { if (!(c)) { printf("FALHA %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)

static SystemConfig_t default_cfg(void)
{
    SystemConfig_t c;
    memset(&c, 0, sizeof(c));
    /* Mesmos defaults de EEPROM_Storage_LoadDefaults() — ver 00-auditoria.md D4. */
    c.pid.kp = 0.8f; c.pid.ki = 0.015f; c.pid.kd = 0.15f; c.pid.ff = 1.5f;
    c.pid.out_min = 0.0f; c.pid.out_max = 1000.0f;
    c.pid.i_min = -8.0f; c.pid.i_max = 8.0f;
    c.pid.sample_time_ms = 1u;
    c.unit = SYS_PRESSURE_UNIT_MMHG;
    c.max_setpoint_mmhg = 300.0f;
    c.current_setpoint_mmhg = 120.0f;
    c.stabilization.band_mmhg = 3.0f;
    c.stabilization.time_ms = 1000u;
    return c;
}

/* Placeholders do Apendice E — "a definir/medir" na bancada (Fase 4). */
static ControlCore_SafetyConfig_t default_safety(void)
{
    ControlCore_SafetyConfig_t s;
    s.p_max_abs_mmhg = 320.0f;
    s.sensor_fault_debounce_ms = 50u;
    s.t_inflar_max_ms = 60000u;
    s.t_desinflar_max_ms = 15000u;
    s.dp_vazamento_mmhg = 1.0f;
    s.t_janela_ms = 5000u;
    s.p_segura_mmhg = 5.0f;
    s.t_seguro_ms = 200u;
    return s;
}

static SimPlant_Config_t plant_small_cuff(void)
{
    SimPlant_Config_t p;
    memset(&p, 0, sizeof(p));
    p.C_manguito = 1.0f;
    p.k_pump = 50.0f;
    p.tau_lag_s = 0.3f;
    p.k_valve = 0.3f;
    p.k_leak = 0.0f;
    p.leak_enabled = 0u;
    return p;
}

static SimPlant_Config_t plant_large_cuff(void)
{
    SimPlant_Config_t p = plant_small_cuff();
    p.C_manguito = 4.0f;
    return p;
}

typedef struct {
    float    peak_true_mmhg;
    float    overshoot_mmhg;
    uint32_t time_to_setpoint_ms;  /* 0 = nao definido ainda */
    uint8_t  reached_setpoint;
    uint32_t stabilized_ms;        /* 0 = nunca chegou a STABILIZED */
    uint32_t valve_transitions;    /* so' contado quando sample_chatter != 0 */
    uint32_t final_error;
    SystemState_t final_state;
} ScenarioResult_t;

/*
 * Roda ate' `max_ticks` passos de 1 ms. `leak_from_tick` (0 = nunca) liga o
 * vazamento do plant a partir daquele tick. `sensor_fail_from_tick` (0 =
 * nunca) zera sensor_ok a partir daquele tick. `abort_at_tick` (0 = nunca)
 * injeta abort_request por 1 tick. `force_overpressure_at_tick` (0 = nunca)
 * soma um salto artificial na pressao "verdadeira" (simula sensor saltando
 * ou vazamento catastrofico subito) para testar o teto absoluto. `count_chatter_from_tick`
 * (0 = nao conta) comeca a contar transicoes de mascara de valvula a partir
 * dali, para o criterio "sem chattering".
 */
static ScenarioResult_t run_scenario(const SimPlant_Config_t *plant_cfg, uint32_t rng_seed,
                                     float setpoint_mmhg, uint32_t max_ticks,
                                     uint32_t leak_from_tick, uint32_t sensor_fail_from_tick,
                                     uint32_t abort_at_tick, uint32_t force_overpressure_at_tick,
                                     uint32_t count_chatter_from_tick)
{
    SystemConfig_t cfg = default_cfg();
    SystemRuntime_t rt;
    ControlCore_SafetyConfig_t safety = default_safety();
    ControlCore_t cc;
    SimPlant_t plant;
    ScenarioResult_t res;
    float measured_kpa;
    uint8_t last_valve_mask = 0xFFu;

    memset(&rt, 0, sizeof(rt));
    memset(&res, 0, sizeof(res));
    cfg.current_setpoint_mmhg = setpoint_mmhg;
    rt.setpoint_mmhg = setpoint_mmhg;

    ControlCore_Init(&cc, &cfg, &rt, &safety);
    SimPlant_Init(&plant, plant_cfg, rng_seed);
    ControlCore_StartInflation(&cc);

    SimPlant_Step(&plant, 0.0f, 0u, 0u, &measured_kpa); /* amostra inicial (dt=0) */

    for (uint32_t tick = 1; tick <= max_ticks; tick++) {
        ControlCore_Inputs_t in;
        ControlCore_Actuators_t out;
        float true_mmhg;
        uint8_t mask;
        SystemState_t state_before_step = rt.state;

        if ((leak_from_tick != 0u) && (tick == leak_from_tick)) {
            plant.cfg.leak_enabled = 1u;
            plant.cfg.k_leak = 0.01f;
        }
        if ((force_overpressure_at_tick != 0u) && (tick == force_overpressure_at_tick)) {
            plant.true_pressure_kpa += 50.0f; /* salto subito (ex.: falha grave) */
        }

        in.pressure_kpa = measured_kpa;
        in.sensor_ok = (uint8_t)(((sensor_fail_from_tick != 0u) && (tick >= sensor_fail_from_tick)) ? 0u : 1u);
        in.abort_request = (tick == abort_at_tick) ? 1u : 0u;
        in.dt_ms = 1u;

        ControlCore_Step(&cc, &in, &out);
        SimPlant_Step(&plant, out.pump_power,
                     (uint8_t)(out.valve1_open + out.valve2_open + out.valve3_open),
                     1u, &measured_kpa);

        true_mmhg = plant.true_pressure_kpa * MMHG_PER_KPA;
        if (true_mmhg > res.peak_true_mmhg) { res.peak_true_mmhg = true_mmhg; }

        if ((res.reached_setpoint == 0u) && (fabsf(true_mmhg - setpoint_mmhg) <= cfg.stabilization.band_mmhg)) {
            res.reached_setpoint = 1u;
            res.time_to_setpoint_ms = tick;
        }
        if ((state_before_step != SYSTEM_STATE_STABILIZED) && (rt.state == SYSTEM_STATE_STABILIZED)) {
            /* (Re)entrada em STABILIZED: registra a mais RECENTE (nao so' a
             * primeira — a inercia da bomba pode empurrar a pressao um
             * pouco mais e forcar uma volta rapida a INFLATING logo depois
             * de uma entrada prematura) e reinicia a contagem de chattering
             * a partir daqui, para medir se a entrada atual realmente ficou
             * parada. */
            res.stabilized_ms = tick;
            last_valve_mask = 0xFFu;
            res.valve_transitions = 0u;
        }

        /* So' conta chattering DEPOIS de SYSTEM_STATE_STABILIZED de verdade
         * (handle_stabilized() e' estatico — bomba e valvulas fechadas o
         * tempo todo — entao qualquer transicao aqui so' pode vir de sair
         * e voltar a STABILIZED, ex.: vazamento). O chaveamento durante a
         * aproximacao/regulacao e' coberto por
         * test_relief_valve_hysteresis_with_ripple(). */
        if ((count_chatter_from_tick != 0u) && (res.stabilized_ms != 0u)) {
            mask = (uint8_t)((out.valve1_open << 0) | (out.valve2_open << 1) | (out.valve3_open << 2));
            if (last_valve_mask == 0xFFu) { last_valve_mask = mask; }
            else if (mask != last_valve_mask) { res.valve_transitions++; last_valve_mask = mask; }
        }

        res.final_error = (uint32_t)cc.last_error;
        res.final_state = rt.state;
    }

    res.overshoot_mmhg = res.peak_true_mmhg - setpoint_mmhg;
    if (res.overshoot_mmhg < 0.0f) { res.overshoot_mmhg = 0.0f; }
    return res;
}

static void test_small_cuff_reaches_setpoint(void)
{
    SimPlant_Config_t plant1 = plant_small_cuff();
    ScenarioResult_t r = run_scenario(&plant1, 1u, 120.0f,
                                      60000u, 0u, 0u, 0u, 0u, 0u);
    printf("  [small_cuff] peak=%.2f mmHg overshoot=%.2f mmHg t_setpoint=%u ms stabilized=%u ms state=%d err=%u\n",
          r.peak_true_mmhg, r.overshoot_mmhg, r.time_to_setpoint_ms, r.stabilized_ms,
          (int)r.final_state, r.final_error);
    CHECK(r.reached_setpoint != 0u);
    CHECK(r.stabilized_ms != 0u);
    /* overshoot_max placeholder do Apendice E e' "ex.: <= 2 mmHg". Manguito
     * pequeno/rapido (C baixo) piorou de ~3 para ~11 mmHg depois do piso de
     * potencia de partida (CONTROL_CORE_PUMP_MIN_EFFECTIVE, achado de
     * bancada 2026-10: a bomba real so' gira a partir de ~30%) — o mesmo
     * piso que torna o ensaio funcional no hardware da' mais "punch" ao
     * controlador, e a banda de alivio atual nao segura essa punch a tempo
     * num manguito rapido. Documentado como achado aberto em sim-report.md;
     * usa 12 mmHg de limite aqui so' para nao mascarar uma regressao maior
     * ainda, nao como meta — o alvo de verdade (<=2 mmHg) exige retunar
     * com T_lag/vazao medidos na bancada (Fase 4).
     * 2026-10-17: alivio virou pulso+acomodacao por achado de BANCADA
     * (valvula aberta derrubava ~20 mmHg na tubulacao e a pressao voltava
     * acima do setpoint). A valvula deste modelo sangra devagar (k_valve
     * generico), entao pulsos limitados seguram menos o excesso aqui:
     * ~26 mmHg. Limite afrouxado por decisao do usuario (priorizar a
     * bancada); so' pega regressao grosseira. */
    CHECK(r.overshoot_mmhg <= 30.0f);
    CHECK(r.final_error == (uint32_t)CONTROL_ERR_NONE);
}

static void test_large_cuff_reaches_setpoint(void)
{
    SimPlant_Config_t plant2 = plant_large_cuff();
    ScenarioResult_t r = run_scenario(&plant2, 2u, 120.0f,
                                      90000u, 0u, 0u, 0u, 0u, 0u);
    printf("  [large_cuff] peak=%.2f mmHg overshoot=%.2f mmHg t_setpoint=%u ms stabilized=%u ms state=%d err=%u\n",
          r.peak_true_mmhg, r.overshoot_mmhg, r.time_to_setpoint_ms, r.stabilized_ms,
          (int)r.final_state, r.final_error);
    CHECK(r.reached_setpoint != 0u);
    CHECK(r.stabilized_ms != 0u);
    /* Placeholder do Apendice E era 2 mmHg; com o alivio por pulso (ver
     * test_small_cuff_reaches_setpoint) este modelo da' ~5 mmHg. Afrouxado
     * por decisao do usuario — o valor real vem da bancada. */
    CHECK(r.overshoot_mmhg <= 6.0f);
}

/*
 * "Sem chattering" testado onde a maquina de estados GARANTE isso por
 * construcao: uma vez em SYSTEM_STATE_STABILIZED (MANUTENCAO), bomba e
 * valvulas ficam estaticas (handle_stabilized nao chama o PID/alivio) ate'
 * um vazamento ou abort tirar o sistema de la'. Esse teste cobre essa
 * garantia (0 transicoes depois de estabilizar, neste cenario sem
 * vazamento). A fase de regulacao (STABILIZING) tem teste proprio:
 * test_relief_valve_hysteresis_with_ripple().
 */
static void test_no_chattering_once_stabilized(void)
{
    SimPlant_Config_t plant = plant_small_cuff();
    ScenarioResult_t r = run_scenario(&plant, 3u, 120.0f, 60000u, 0u, 0u, 0u, 0u, 1u);
    CHECK(r.stabilized_ms != 0u);
    printf("  [no_chatter] valve_transitions(apos STABILIZED)=%u\n", r.valve_transitions);
    /* <=1: a ultima amostra de handle_stabilizing (ainda com o alivio do
     * PID, calculada antes do estado virar STABILIZED neste mesmo tick)
     * conta como a referencia inicial; o 1o tick de handle_stabilized de
     * verdade (saida sempre zerada) aparece como UMA transicao "artificial"
     * contra essa referencia. Qualquer coisa alem disso seria chattering
     * de verdade durante a espera. */
    CHECK(r.valve_transitions <= 1u);
}

static void test_leak_in_maintenance_sets_e013(void)
{
    /* Liga o vazamento bem depois do ensaio estabilizar (tick ~ metade do
     * teto de ticks), para garantir que a malha ja' esta' em STABILIZED. */
    SimPlant_Config_t plant1 = plant_small_cuff();
    ScenarioResult_t r = run_scenario(&plant1, 4u, 120.0f,
                                      40000u, 15000u, 0u, 0u, 0u, 0u);
    printf("  [leak] final_state=%d err=%u\n", (int)r.final_state, r.final_error);
    CHECK(r.final_error == (uint32_t)CONTROL_ERR_E013_LEAK);
    CHECK((r.final_state == SYSTEM_STATE_DEFLATING_FAST) || (r.final_state == SYSTEM_STATE_IDLE));
}

static void test_sensor_fault_triggers_e002_safe_state(void)
{
    SimPlant_Config_t plant1 = plant_small_cuff();
    ScenarioResult_t r = run_scenario(&plant1, 5u, 120.0f,
                                      5000u, 0u, 3000u, 0u, 0u, 0u);
    printf("  [sensor_fault] final_state=%d err=%u\n", (int)r.final_state, r.final_error);
    CHECK(r.final_error == (uint32_t)CONTROL_ERR_E002_SENSOR_FAULT);
    CHECK(r.final_state == SYSTEM_STATE_FAULT);
}

static void test_overpressure_triggers_e001_immediately(void)
{
    SystemConfig_t cfg = default_cfg();
    SystemRuntime_t rt;
    ControlCore_SafetyConfig_t safety = default_safety();
    ControlCore_t cc;
    SimPlant_t plant;
    SimPlant_Config_t pcfg = plant_small_cuff();
    float measured_kpa;
    ControlCore_Inputs_t in;
    ControlCore_Actuators_t out;

    memset(&rt, 0, sizeof(rt));
    rt.setpoint_mmhg = 120.0f;
    ControlCore_Init(&cc, &cfg, &rt, &safety);
    SimPlant_Init(&plant, &pcfg, 6u);
    ControlCore_StartInflation(&cc);
    SimPlant_Step(&plant, 0.0f, 0u, 0u, &measured_kpa);

    /* forca a pressao "real" acima de P_MAX_ABS direto, como se o sensor
     * saltasse/uma falha grave tivesse ocorrido entre dois ticks. */
    plant.true_pressure_kpa = (safety.p_max_abs_mmhg + 10.0f) / MMHG_PER_KPA;
    measured_kpa = plant.true_pressure_kpa;

    in.pressure_kpa = measured_kpa;
    in.sensor_ok = 1u;
    in.abort_request = 0u;
    in.dt_ms = 1u;
    ControlCore_Step(&cc, &in, &out);

    printf("  [overpressure] state=%d err=%d pump=%.3f v1=%u v2=%u v3=%u\n",
          (int)rt.state, (int)cc.last_error, out.pump_power, out.valve1_open, out.valve2_open, out.valve3_open);
    CHECK(rt.state == SYSTEM_STATE_FAULT);
    CHECK(cc.last_error == CONTROL_ERR_E001_OVERPRESSURE);
    CHECK(out.pump_power == 0.0f);
    CHECK((out.valve1_open != 0u) && (out.valve2_open != 0u) && (out.valve3_open != 0u));
}

static void test_abort_request_deflates_to_idle(void)
{
    SimPlant_Config_t plant1 = plant_small_cuff();
    ScenarioResult_t r = run_scenario(&plant1, 7u, 120.0f,
                                      40000u, 0u, 0u, 15000u, 0u, 0u);
    printf("  [abort] final_state=%d err=%u peak=%.2f\n", (int)r.final_state, r.final_error, r.peak_true_mmhg);
    CHECK(r.final_state == SYSTEM_STATE_IDLE);
}

/* Fase 5: ControlCore_GetCycleStats() — alimenta CycleRecord_Evaluate(). */
static void test_cycle_stats_full_run(void)
{
    SystemConfig_t cfg = default_cfg();
    SystemRuntime_t rt;
    ControlCore_SafetyConfig_t safety = default_safety();
    ControlCore_t cc;
    SimPlant_t plant;
    SimPlant_Config_t pcfg = plant_small_cuff();
    float measured_kpa;
    const ControlCore_CycleStats_t *stats;
    uint32_t tick;
    uint32_t stabilized_at = 0u;

    memset(&rt, 0, sizeof(rt));
    rt.setpoint_mmhg = 40.0f;
    ControlCore_Init(&cc, &cfg, &rt, &safety);
    SimPlant_Init(&plant, &pcfg, 42u);
    ControlCore_StartInflation(&cc);
    SimPlant_Step(&plant, 0.0f, 0u, 0u, &measured_kpa);

    stats = ControlCore_GetCycleStats(&cc);
    CHECK(stats->finalized == 0u); /* mal comecou */

    for (tick = 1; tick <= 60000u; tick++) {
        ControlCore_Inputs_t in;
        ControlCore_Actuators_t out;

        in.pressure_kpa = measured_kpa;
        in.sensor_ok = 1u;
        /* pedal 500 ms depois de chegar em STABILIZED = fim normal */
        if ((rt.state == SYSTEM_STATE_STABILIZED) && (stabilized_at == 0u)) { stabilized_at = tick; }
        in.abort_request = ((stabilized_at != 0u) && (tick == stabilized_at + 500u)) ? 1u : 0u;
        in.dt_ms = 1u;

        ControlCore_Step(&cc, &in, &out);
        SimPlant_Step(&plant, out.pump_power,
                     (uint8_t)(out.valve1_open + out.valve2_open + out.valve3_open),
                     1u, &measured_kpa);

        if (stats->finalized != 0u) { break; }
    }

    printf("  [cycle_stats] peak=%.2f plato_n=%u plato_mean=%.2f inflate_ms=%u plato_ms=%u"
          " deflate_ms=%u residual=%.2f abort=%u fault=%u finalized=%u\n",
          stats->peak_mmhg, stats->plateau_sample_count,
          (stats->plateau_sample_count > 0u) ? (stats->plateau_sum_mmhg / (float)stats->plateau_sample_count) : 0.0f,
          stats->inflate_time_ms, stats->plateau_time_ms, stats->deflate_time_ms,
          stats->residual_mmhg, stats->aborted_by_operator, stats->technical_fault, stats->finalized);

    CHECK(stats->finalized != 0u);
    CHECK(stats->reached_setpoint != 0u);
    CHECK(stats->plateau_sample_count > 0u);
    CHECK(stats->peak_mmhg >= 35.0f); /* chegou perto do alvo de 40 */
    CHECK(stats->inflate_time_ms > 0u);
    CHECK(stats->plateau_time_ms > 0u);
    CHECK(stats->deflate_time_ms > 0u);
    CHECK(stats->aborted_by_operator == 0u); /* fim normal, nao NC07 */
    CHECK(stats->technical_fault == 0u);
    CHECK(stats->residual_mmhg < safety.p_segura_mmhg);
}

/* Pedal durante a INFLACAO (antes do plato) = interrupcao pelo operador. */
static void test_abort_during_inflation_sets_nc07_flag(void)
{
    SystemConfig_t cfg = default_cfg();
    SystemRuntime_t rt;
    ControlCore_SafetyConfig_t safety = default_safety();
    ControlCore_t cc;
    SimPlant_t plant;
    SimPlant_Config_t pcfg = plant_large_cuff();
    float measured_kpa;
    const ControlCore_CycleStats_t *stats;
    uint32_t tick;

    memset(&rt, 0, sizeof(rt));
    rt.setpoint_mmhg = 120.0f;
    ControlCore_Init(&cc, &cfg, &rt, &safety);
    SimPlant_Init(&plant, &pcfg, 7u);
    ControlCore_StartInflation(&cc);
    SimPlant_Step(&plant, 0.0f, 0u, 0u, &measured_kpa);
    stats = ControlCore_GetCycleStats(&cc);

    for (tick = 1; tick <= 30000u; tick++) {
        ControlCore_Inputs_t in;
        ControlCore_Actuators_t out;

        in.pressure_kpa = measured_kpa;
        in.sensor_ok = 1u;
        in.abort_request = (tick == 300u) ? 1u : 0u; /* ainda subindo */
        in.dt_ms = 1u;
        if (tick == 300u) { CHECK(rt.state == SYSTEM_STATE_INFLATING); }

        ControlCore_Step(&cc, &in, &out);
        SimPlant_Step(&plant, out.pump_power,
                     (uint8_t)(out.valve1_open + out.valve2_open + out.valve3_open),
                     1u, &measured_kpa);
        if (stats->finalized != 0u) { break; }
    }

    printf("  [abort_inflating] finalized=%u abort=%u plato_n=%u\n",
           stats->finalized, stats->aborted_by_operator, stats->plateau_sample_count);
    CHECK(stats->finalized != 0u);
    CHECK(stats->aborted_by_operator != 0u);
}

/*
 * Achado de bancada (2026-10): "na estabilizacao as valvulas ficam loucas".
 * Bomba de embolo = ondulacao a cada curso; aqui aproximada por ruido de
 * +-2 mmHg na leitura. Conta TODAS as trocas de valvula do ciclo inteiro
 * (aproximacao + regulacao + espera), nao so' depois de STABILIZED. A
 * logica anterior (pulso de 30 ms a cada 40 ms com 0,5 mmHg de banda)
 * dava 300-1150 trocas em 8 s neste cenario; com histerese + tempo minimo
 * fechada (RELIEF_MIN_OFF_MS) fica em poucas unidades.
 */
static uint32_t count_valve_transitions_with_ripple(float c_manguito, uint32_t ms)
{
    SystemConfig_t cfg = default_cfg();
    SystemRuntime_t rt;
    ControlCore_SafetyConfig_t safety = default_safety();
    ControlCore_t cc;
    SimPlant_Config_t pc = plant_small_cuff();
    SimPlant_t plant;
    float measured_kpa;
    uint8_t last_mask = 0xFFu;
    uint32_t transitions = 0u;

    pc.C_manguito = c_manguito;
    pc.sensor_noise_amplitude_kpa = 2.0f / MMHG_PER_KPA;
    memset(&rt, 0, sizeof(rt));
    rt.setpoint_mmhg = 120.0f;
    ControlCore_Init(&cc, &cfg, &rt, &safety);
    SimPlant_Init(&plant, &pc, 11u);
    ControlCore_StartInflation(&cc);
    SimPlant_Step(&plant, 0.0f, 0u, 0u, &measured_kpa);

    for (uint32_t tick = 1; tick <= ms; tick++) {
        ControlCore_Inputs_t in = { measured_kpa, 1u, 0u, 1u };
        ControlCore_Actuators_t out;
        uint8_t mask;

        ControlCore_Step(&cc, &in, &out);
        mask = (uint8_t)((out.valve1_open << 0) | (out.valve2_open << 1) | (out.valve3_open << 2));
        if ((last_mask != 0xFFu) && (mask != last_mask)) { transitions++; }
        last_mask = mask;
        SimPlant_Step(&plant, out.pump_power,
                     (uint8_t)(out.valve1_open + out.valve2_open + out.valve3_open), 1u, &measured_kpa);
    }
    return transitions;
}

static void test_relief_valve_hysteresis_with_ripple(void)
{
    uint32_t small = count_valve_transitions_with_ripple(1.0f, 8000u);
    uint32_t large = count_valve_transitions_with_ripple(4.0f, 8000u);

    printf("  [ripple] valve_transitions em 8 s: small_cuff=%u large_cuff=%u\n", small, large);
    /* Teto de RELIEF_MIN_OFF_MS (300 ms): no maximo ~27 aberturas em 8 s
     * mesmo no pior caso; o limite aqui pega uma volta do chaveamento. */
    CHECK(small <= 20u);
    CHECK(large <= 20u);
}

/* Longe do setpoint a bomba roda a 100% com as valvulas fechadas. */
static void test_fast_approach_full_power_far_from_setpoint(void)
{
    SystemConfig_t cfg = default_cfg();
    SystemRuntime_t rt;
    ControlCore_SafetyConfig_t safety = default_safety();
    ControlCore_t cc;
    ControlCore_Inputs_t in = { 0.0f, 1u, 0u, 1u };
    ControlCore_Actuators_t out;

    memset(&rt, 0, sizeof(rt));
    rt.setpoint_mmhg = 120.0f;
    ControlCore_Init(&cc, &cfg, &rt, &safety);
    ControlCore_StartInflation(&cc);

    ControlCore_Step(&cc, &in, &out);
    printf("  [fast_approach] pump=%.2f v=%u%u%u fast=%u\n", (double)out.pump_power,
           out.valve1_open, out.valve2_open, out.valve3_open, cc.fast_approach);
    CHECK(out.pump_power == 1.0f);
    CHECK((out.valve1_open | out.valve2_open | out.valve3_open) == 0u);
    CHECK(cc.fast_approach != 0u);

    /* Ja' dentro da zona de regulacao: PID assume (sai da potencia maxima). */
    in.pressure_kpa = 115.0f / MMHG_PER_KPA;
    ControlCore_Step(&cc, &in, &out);
    CHECK(cc.fast_approach == 0u);
    CHECK(out.pump_power < 1.0f);
}

int main(void)
{
    printf("Control_Core x sim_plant (Fase 3) — parametros de seguranca e PID sao placeholders\n");
    printf("do Apendice E ('a definir/medir'); ver Docs/sim-report.md.\n");
    test_small_cuff_reaches_setpoint();
    test_large_cuff_reaches_setpoint();
    test_no_chattering_once_stabilized();
    test_leak_in_maintenance_sets_e013();
    test_sensor_fault_triggers_e002_safe_state();
    test_overpressure_triggers_e001_immediately();
    test_abort_request_deflates_to_idle();
    test_cycle_stats_full_run();
    test_abort_during_inflation_sets_nc07_flag();
    test_relief_valve_hysteresis_with_ripple();
    test_fast_approach_full_power_far_from_setpoint();
    printf(s_fail ? "control_core: %d FALHA(S)\n" : "control_core: OK\n", s_fail);
    return s_fail != 0;
}
