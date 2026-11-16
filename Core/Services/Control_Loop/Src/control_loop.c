/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-07-24
 * Modified: 2026-10-01
 */

#include "control_loop.h"
#include "adc_acq.h"
#include "valve_gpio.h"
#include "pwm_motor_tim.h"
#include "control_core.h"
#include "pressure_sensor_model.h"
#include "port_dwt.h"
#include <string.h>
#include <math.h> // Para fabsf

/* Definido em main.c (gerado pelo CubeMX). Declarado aqui em vez de
 * incluir main.h para nao acoplar a camada Services ao arquivo top-level
 * da aplicacao — mesmo tratamento de falha que todo MX_*_Init() ja usa. */
extern void Error_Handler(void);

#define MMHG_PER_KPA (7.50062f)
#define LEAK_TEST_TARGET_HYST_MMHG   (10.0f) // Histerese para considerar que a pressão atingiu o alvo durante o teste de vazamento (em mmHg)

/* Periodo real do loop de controle (TASK_CONTROL_PERIOD_MS em main.c).
 * Repetido aqui como constante numerica (nao um #include de main.h — ver
 * nota acima) porque e' o dt que ControlCore_Step() assume a cada chamada;
 * se a tarefa que chama ControlLoop_Update() mudar de periodo, atualizar os
 * dois lugares. */
#define CONTROL_LOOP_PERIOD_MS (1u)

/*
 * Parametros de seguranca do Apendice E (Fase 3/4) usados pelo Control_Core
 * no caminho automatico do ensaio. Ainda NAO vivem na EEPROM/SystemConfig_t
 * (isso e' trabalho de IHM/parametros, Fase 6) — por ora sao constantes,
 * com os MESMOS valores simulados e documentados em Docs/dev/sim-report.md.
 * Overshoot_max permanece o do Apendice E (<= 2 mmHg); o simulador mostrou
 * um residuo de ~3 mmHg em manguitos pequenos/rapidos com a banda de alivio
 * atual — ajuste fino fica para quando o hardware estiver rodando de
 * verdade (decisao do usuario, 00-auditoria.md).
 */
static const ControlCore_SafetyConfig_t s_control_safety = {
    /* Depende do sensor (pressure_sensor_model.h): sempre ABAIXO da
     * saturacao e ACIMA do setpoint maximo. MPXV7025DP: 185 (satura em
     * ~187,5 - com 320 o E001 nunca disparava); MPXV5100GP: 320. */
    .p_max_abs_mmhg = PRESSURE_P_MAX_ABS_DEFAULT_MMHG,
    /* 300 (nao mais 50): achado de bancada (2026-10) — 50 ms e' menor que o
     * transiente normal de start-up do ADC (ADC_Acq_Init zera o buffer do
     * DMA antes de armar; os primeiros blocos processados por
     * ADC_Acq_Update() podem misturar amostras zeradas com reais ate' o
     * buffer circular ser todo renovado), o que gerava OUT_OF_RANGE falso
     * logo no boot e travava em FAULT/E002 — sem saida (SYSTEM_STATE_FAULT
     * so' sai por intervencao explicita, ainda nao implementada) a bomba
     * nunca chegava a ligar mesmo pedindo INFLATING. */
    .sensor_fault_debounce_ms = 300u,
    /* 180000 (nao mais 60000): achado de bancada (2026-10) — a bomba e'
     * de embolo (pulsos discretos, nao fluxo continuo), entao a pressao
     * oscila a cada curso do embolo; isso faz o erro cruzar a banda de
     * estabilizacao repetidamente (INFLATING<->STABILIZING, cada
     * transicao reinicia state_elapsed_ms mas NAO inflate_elapsed_ms, que
     * soma em ambos os estados), demorando bem mais que 60 s pra'
     * acumular uma janela continua de STABILIZED. Ainda e' so' um
     * placeholder generoso pra nao abortar um ensaio que esta'
     * progredindo de verdade — nao substitui tunar o controle para essa
     * bomba (banda/histerese maiores, ou aceitar a ondulacao como
     * caracteristica fisica do embolo). */
    .t_inflar_max_ms = 180000u,
    .t_desinflar_max_ms = 15000u,
    /* 8 (antes 1): o manguito grande perde ~0,6 mmHg/s parado (bancada
     * 2026-10-31, no copo), ~3 mmHg na janela. Com 1 todo ensaio dava E013. */
    .dp_vazamento_mmhg = 8.0f,
    .t_janela_ms = 5000u,
    /* Acima dos 10 s da especificacao de proposito: quem passa do limite vira
     * NC04 no registro, em vez de um ciclo que parece conforme. */
    .t_plato_max_ms = 15000u,
    .p_segura_mmhg = 10.0f,
    .t_seguro_ms = 200u,
};

#define LEAK_TARGET_MAX_MMHG (170.0f)
#define LEAK_MEAN_TAU_S      (0.6f)
#define SERVICE_ZERO_VENT_MS (5000U)

/**
 * @brief Estrutura interna do estado do módulo de controle.
 */
static struct {
    // Ponteiros para dados externos
    SystemConfig_t* sys_cfg;
    SystemRuntime_t* runtime_data;

    // Nucleo do ensaio automatico (IDLE/INFLATING/STABILIZING/STABILIZED/
    // DEFLATING_FAST/FAULT) — logica pura, testada no host (Fase 3/
    // sim-report.md). CONFIG e LEAK_TEST continuam tratados aqui embaixo,
    // sem passar pelo Control_Core (nao fazem parte do ciclo normal de
    // ensaio — ver sim-report.md, item 10).
    ControlCore_t cc;

    // Rotina de ir a' pressao: servico da IHM e subida do teste de vazamento.
    PressureTarget_t service;
    uint8_t  service_active;
    uint8_t  service_zero;     // 0 mmHg: abre tudo, sem bomba
    uint32_t service_zero_ms;
    ControlLoop_LeakStatus_t leak;
    float leak_mean_mmhg;
    float leak_prev_setpoint_mmhg;
    SystemState_t leak_prev_state;

    uint8_t manual_test_enabled;
    uint8_t manual_v1_on;
    uint8_t manual_v2_on;
    uint8_t manual_v3_on;
    float manual_pump_power;

    // Fase 7.7: modo manual remoto (app DEV). Escrito pela usbProtoTask,
    // lido aqui (controlTask) - uint32/uint8 alinhados: leitura atomica.
    volatile uint32_t remote_hb_tick;        // HAL_GetTick() do ultimo sinal de vida do PC
    volatile uint8_t  remote_exit_reason;    // ControlLoop_RemoteExit_t, para o log (lido e zerado)

    // Jitter do loop de controle (Fase 3/4: "registrar jitter maximo e
    // expor via diagnostico"). Cycles do DWT->CYCCNT entre chamadas
    // consecutivas de ControlLoop_Update(); o alvo e' CONTROL_LOOP_PERIOD_MS.
    uint32_t last_update_cycles;
    uint32_t max_jitter_cycles; /**< maior |delta - periodo esperado| ja' visto */
    uint32_t v3_linger_ms;      /**< ver ControlCore_V3Follow() */
    uint8_t  v3_follow_off;     /**< 1 = V3 nao acompanha o ensaio */
    uint32_t max_jitter_tick;   /**< HAL_GetTick() do pior caso */
    uint32_t max_jitter_state;  /**< SystemState_t no pior caso */
    uint32_t max_exec_cycles;   /**< maior duracao de ControlLoop_Update() */
    uint8_t  jitter_has_sample;

} s_control;

/* --- Funções Privadas --- */

/** @brief Controla o estado das válvulas. Valvula 3 (calibracao) so e aberta
 *         explicitamente pelo modo manual — todo chamador automatico passa 0. */
static void set_valves(uint8_t v1_open, uint8_t v2_open, uint8_t v3_open) {
    Valve_SetAll(v1_open, v2_open, v3_open); // polaridade NA/LOW=aberta vive em valve_gpio.c
}

/** @brief Controla a bomba de ar via PWM. Escreve direto no CCR do timer
 *         (sem DMA) — simples e suficiente, ja que o duty so muda no ritmo
 *         do loop de controle (TASK_CONTROL_PERIOD_MS), nao a cada periodo
 *         de PWM. */
static void set_pump_power(float power) {
    (void)PwmMotor_SetPower(power); // satura em [0, limite]; CCR = potencia*(ARR+1)
}

/* --- Handlers fora do ensaio automatico (CONFIG e LEAK_TEST) --- */

static float current_mmhg(void) {
    return s_control.runtime_data->pressure_kpa * MMHG_PER_KPA;
}

/* Servico (IHM): V3 aberta para o manometro; so' a rotina decide bomba e V2. */
static void handle_service(void) {
    PressureTarget_Output_t out;

    if (s_control.service_zero != 0U) {
        s_control.service_zero_ms += CONTROL_LOOP_PERIOD_MS;
        set_pump_power(0.0f);
        set_valves(1, 1, 1);
        return;
    }

    PressureTarget_Step(&s_control.service, current_mmhg(), CONTROL_LOOP_PERIOD_MS, &out);
    set_pump_power(out.pump_power);
    if (s_control.service.state == PRESSURE_TARGET_FAILED) {
        set_valves(1, 1, 1);
    } else {
        set_valves(0, out.vent_open, 1);
    }
}

static void handle_state_config(void) {
    if (s_control.manual_test_enabled != 0U) {
        set_pump_power(s_control.manual_pump_power);
        set_valves(s_control.manual_v1_on, s_control.manual_v2_on, s_control.manual_v3_on);
    } else if (s_control.service_active != 0U) {
        handle_service();
    } else {
        uint8_t v3_open = (s_control.v3_linger_ms > 0U) ? 1U : 0U;

        if (s_control.v3_linger_ms > CONTROL_LOOP_PERIOD_MS) {
            s_control.v3_linger_ms -= CONTROL_LOOP_PERIOD_MS;
        } else {
            s_control.v3_linger_ms = 0U;
        }
        set_pump_power(0.0f);
        set_valves(1, 1, v3_open);
    }
}

/* Fase 7.7 - modo manual remoto: aplica o comando do PC so' enquanto
 * ControlCore_CheckManual() deixar (supervisao a cada 1 ms, aqui na tarefa
 * de maior prioridade - nao depende da usbProtoTask estar viva). */
static void leave_remote_manual(ControlLoop_RemoteExit_t reason) {
    s_control.manual_test_enabled = 0U;
    set_pump_power(0.0f);
    set_valves(1, 1, 1); // estado seguro: tudo aberto; o IDLE assume no proximo passo
    s_control.v3_linger_ms = CONTROL_CORE_V3_LINGER_MS; // esvazia a linha do manometro
    s_control.runtime_data->state = SYSTEM_STATE_IDLE;
    s_control.remote_exit_reason = (uint8_t)reason;
}

static void handle_state_manual(void) {
    const PressureCore_t* diag = ADC_Acq_GetDiagnostics();
    uint8_t local_stop = s_control.runtime_data->bt_user_pressed;
    ControlCore_ManualVerdict_t v;

    s_control.runtime_data->bt_user_pressed = 0u; // consumido: nao pode sobrar para o proximo ensaio
    v = ControlCore_CheckManual(&s_control.cc.safety,
                                s_control.runtime_data->pressure_kpa,
                                (diag->flags == 0u) ? 1u : 0u,
                                HAL_GetTick() - s_control.remote_hb_tick,
                                local_stop);
    switch (v) {
        case CONTROL_MANUAL_OVERPRESSURE:
            s_control.cc.last_error = CONTROL_ERR_E001_OVERPRESSURE;
            s_control.remote_exit_reason = (uint8_t)CONTROL_REMOTE_EXIT_OVERPRESSURE;
            ControlLoop_EnterFaultState();
            break;
        case CONTROL_MANUAL_LOCAL_STOP:
            leave_remote_manual(CONTROL_REMOTE_EXIT_LOCAL_STOP);
            break;
        case CONTROL_MANUAL_HEARTBEAT_LOST:
            leave_remote_manual(CONTROL_REMOTE_EXIT_HEARTBEAT);
            break;
        case CONTROL_MANUAL_SENSOR_INVALID:
            // Sem leitura confiavel nao ha' como vigiar a pressao: bomba off,
            // valvulas como o PC pediu (o PC ve o erro do sensor no stream).
            set_pump_power(0.0f);
            set_valves(s_control.manual_v1_on, s_control.manual_v2_on, s_control.manual_v3_on);
            break;
        case CONTROL_MANUAL_OK:
        default:
            set_pump_power(s_control.manual_test_enabled ? s_control.manual_pump_power : 0.0f);
            set_valves(s_control.manual_v1_on, s_control.manual_v2_on, s_control.manual_v3_on);
            break;
    }
}

static void leak_finish(ControlLoop_LeakPhase_t phase) {
    s_control.leak.phase = phase;
    s_control.leak.remaining_ms = 0U;
    set_pump_power(0.0f);
    s_control.runtime_data->setpoint_mmhg = s_control.leak_prev_setpoint_mmhg;
    s_control.runtime_data->state = s_control.leak_prev_state;
}

/* Sobe, espera assentar, para a bomba e compara a queda com o limite. So' um teste
 * que chegou ao fim tem veredito; pedal e falha terminam em ABORTED. */
static void handle_state_leak_test(void) {
    float pressure = current_mmhg();
    const uint32_t dt_ms = CONTROL_LOOP_PERIOD_MS;
    const SystemConfig_t* cfg = s_control.sys_cfg;

    /* Media de ~1 batimento: no membro o pulso arterial nao entra na queda. */
    s_control.leak_mean_mmhg += (pressure - s_control.leak_mean_mmhg) *
        ((float)dt_ms / ((1000.0f * LEAK_MEAN_TAU_S) + (float)dt_ms));

    if (s_control.runtime_data->bt_user_pressed != 0U) {
        s_control.runtime_data->bt_user_pressed = 0U;
        leak_finish(CONTROL_LEAK_ABORTED);
        return;
    }

    if (s_control.leak.phase == CONTROL_LEAK_RISING) {
        PressureTarget_Output_t out;

        PressureTarget_Step(&s_control.service, pressure, dt_ms, &out);
        set_pump_power(out.pump_power);
        set_valves(0, out.vent_open, 0);
        if (s_control.service.state == PRESSURE_TARGET_STABLE) {
            s_control.leak.phase = CONTROL_LEAK_OBSERVING;
            s_control.leak.start_mmhg = s_control.leak_mean_mmhg;
            s_control.leak.end_mmhg = s_control.leak_mean_mmhg;
            s_control.leak.remaining_ms = cfg->leak_test.hold_time_ms;
        } else if (s_control.service.state == PRESSURE_TARGET_FAILED) {
            if (s_control.service.fail == PRESSURE_TARGET_FAIL_OVER) {
                leak_finish(CONTROL_LEAK_ABORTED);
            } else { /* nao manter a pressao e' reprovacao, nao falta de resultado */
                s_control.leak.held = 0U;
                s_control.leak.start_mmhg = s_control.service.target_mmhg;
                s_control.leak.end_mmhg = s_control.leak_mean_mmhg;
                leak_finish(CONTROL_LEAK_FAILED);
            }
        }
        return;
    }

    set_pump_power(0.0f);
    set_valves(0, 0, 0);
    s_control.leak.end_mmhg = s_control.leak_mean_mmhg;
    if (s_control.leak.remaining_ms > dt_ms) {
        s_control.leak.remaining_ms -= dt_ms;
    } else {
        leak_finish(((s_control.leak.start_mmhg - s_control.leak.end_mmhg) <= cfg->leak_test.max_drop_mmhg)
                    ? CONTROL_LEAK_PASSED : CONTROL_LEAK_FAILED);
    }
}

/* --- Funções Públicas --- */

void ControlLoop_Init(SystemConfig_t* p_sys_cfg, TIM_HandleTypeDef* p_htim_pwm,
                       SystemRuntime_t* p_runtime_data, const ControlLoop_ValvePins_t* p_valve_pins) {
    memset(&s_control, 0, sizeof(s_control));
    s_control.v3_follow_off = 1U; // decisao do usuario: V3 so' acompanha se pedido nos Ajustes

    s_control.sys_cfg = p_sys_cfg;
    s_control.runtime_data = p_runtime_data;

    s_control.runtime_data->setpoint_mmhg = p_sys_cfg->current_setpoint_mmhg;
    if (s_control.runtime_data->setpoint_mmhg < 0.0f) {
        s_control.runtime_data->setpoint_mmhg = 0.0f;
    }
    if (s_control.runtime_data->setpoint_mmhg > p_sys_cfg->max_setpoint_mmhg) {
        s_control.runtime_data->setpoint_mmhg = p_sys_cfg->max_setpoint_mmhg;
    }

    // Nucleo do ensaio automatico — deixa runtime_data->state em IDLE.
    ControlCore_Init(&s_control.cc, p_sys_cfg, p_runtime_data, &s_control_safety);

    // Drivers dos atuadores: valvulas (todas abertas ao iniciar) e PWM da
    // bomba (CCR = 0 antes de habilitar a saida). O canal (TIM_CHANNEL_1) deve
    // corresponder ao configurado no CubeMX.
    {
        Valve_GpioMap_t map = { .valve = {
            { p_valve_pins->valve1.port, p_valve_pins->valve1.pin },
            { p_valve_pins->valve2.port, p_valve_pins->valve2.pin },
            { p_valve_pins->valve3.port, p_valve_pins->valve3.pin },
        } };
        Valve_InitGpio(&map);
    }
    PwmMotor_InitTim(p_htim_pwm, TIM_CHANNEL_1);

    // Estado de repouso: bomba off, V1+V2 abertas, V3 (manometro) fechada.
    set_pump_power(0.0f);
    set_valves(1, 1, 0);
}

/** @brief Atualiza g_control_jitter_dbg-equivalente (ver ControlLoop_GetMaxJitterCycles):
 *         mede o intervalo real entre chamadas via DWT->CYCCNT e guarda o
 *         maior desvio ja' observado frente ao periodo nominal de 1 ms.
 *         Fase 3/4 do prompt: "Registrar jitter maximo... e expor via
 *         diagnostico". So' mede — nao decide nada a partir disso ainda. */
static void update_jitter_measurement(void) {
    uint32_t now_cycles = PortDWT_GetCycles();

    if (s_control.jitter_has_sample != 0u) {
        uint32_t delta_cycles = now_cycles - s_control.last_update_cycles; /* wrap-safe (uint32) */
        uint32_t expected_cycles = (SystemCoreClock / 1000u) * CONTROL_LOOP_PERIOD_MS;
        uint32_t jitter = (delta_cycles > expected_cycles)
            ? (delta_cycles - expected_cycles) : (expected_cycles - delta_cycles);

        if (jitter > s_control.max_jitter_cycles) {
            s_control.max_jitter_cycles = jitter;
            s_control.max_jitter_tick = HAL_GetTick();
            s_control.max_jitter_state = (uint32_t)s_control.runtime_data->state;
        }
    }
    s_control.last_update_cycles = now_cycles;
    s_control.jitter_has_sample = 1u;
}

#define CONTROL_LOOP_CURVE_PERIOD_MS (100u)
#define CONTROL_LOOP_CURVE_MAX       (2400u)

static int16_t  s_curve[CONTROL_LOOP_CURVE_MAX];
static uint16_t s_curve_count;
static uint32_t s_curve_elapsed_ms;
static uint8_t  s_curve_active;

/* Acumula a curva do ensaio. Fora de um ensaio nao faz nada. */
static void update_curve(void) {
    SystemState_t state = s_control.runtime_data->state;

    if ((state != SYSTEM_STATE_INFLATING) && (state != SYSTEM_STATE_STABILIZING) &&
        (state != SYSTEM_STATE_STABILIZED) && (state != SYSTEM_STATE_DEFLATING_FAST)) {
        s_curve_active = 0u;
        return;
    }
    if (s_curve_active == 0u) {
        s_curve_active = 1u;
        s_curve_count = 0u;
        s_curve_elapsed_ms = 0u;
    }
    if (((s_curve_elapsed_ms % CONTROL_LOOP_CURVE_PERIOD_MS) == 0u) &&
        (s_curve_count < CONTROL_LOOP_CURVE_MAX)) {
        s_curve[s_curve_count] = (int16_t)lroundf(s_control.runtime_data->pressure_display * 10.0f);
        s_curve_count++;
    }
    s_curve_elapsed_ms += CONTROL_LOOP_PERIOD_MS;
}

const int16_t *ControlLoop_GetCurve(uint16_t *count) {
    *count = s_curve_count;
    return s_curve;
}

static void control_loop_step(void);

void ControlLoop_Update(void) {
    uint32_t start_cycles;
    uint32_t exec_cycles;

    update_jitter_measurement();
    start_cycles = PortDWT_GetCycles();
    control_loop_step();
    update_curve();
    exec_cycles = PortDWT_GetCycles() - start_cycles;
    if (exec_cycles > s_control.max_exec_cycles) {
        s_control.max_exec_cycles = exec_cycles;
    }
}

static void control_loop_step(void) {
    // Atualiza as leituras de pressão do módulo ADC
    ADC_Acq_Update();
    s_control.runtime_data->pressure_kpa = ADC_Acq_GetPressureKpa();
    s_control.runtime_data->pressure_display = ADC_Acq_ConvertPressure(
        s_control.runtime_data->pressure_kpa,
        s_control.sys_cfg->unit
    );

    // P_MAX_ABS em TODO estado (Parte A, seguranca 2: "independente de
    // setpoint e do estado da maquina"). O Control_Core so' cobre o ensaio
    // automatico; CONFIG (painel de atuadores da IHM) e LEAK_TEST nao tinham
    // protecao nenhuma - achado 2026-10-22, Fase 7.7. MANUAL checa dentro de
    // handle_state_manual() (mesma regra, via ControlCore_CheckManual).
    if (((s_control.runtime_data->state == SYSTEM_STATE_CONFIG) ||
         (s_control.runtime_data->state == SYSTEM_STATE_LEAK_TEST)) &&
        (ControlCore_OverPressure(&s_control.cc.safety, s_control.runtime_data->pressure_kpa) != 0u)) {
        s_control.cc.last_error = CONTROL_ERR_E001_OVERPRESSURE;
        ControlLoop_EnterFaultState();
        return;
    }

    if (s_control.runtime_data->state == SYSTEM_STATE_MANUAL) {
        handle_state_manual();
        return;
    }

    // CONFIG e LEAK_TEST ficam fora do Control_Core (ver sim-report.md item 10).
    if (s_control.runtime_data->state == SYSTEM_STATE_CONFIG) {
        handle_state_config();
        return;
    }
    if (s_control.runtime_data->state == SYSTEM_STATE_LEAK_TEST) {
        handle_state_leak_test();
        return;
    }

    // Ensaio automatico (IDLE/INFLATING/STABILIZING/STABILIZED/
    // DEFLATING_FAST/FAULT): delega ao Control_Core — ver sim-report.md.
    {
        const PressureCore_t* diag = ADC_Acq_GetDiagnostics();
        ControlCore_Inputs_t in;
        ControlCore_Actuators_t out;

        in.pressure_kpa = s_control.runtime_data->pressure_kpa;
        in.sensor_ok = (diag->flags == 0u) ? 1u : 0u;
        in.abort_request = s_control.runtime_data->bt_user_pressed;
        s_control.runtime_data->bt_user_pressed = 0u; // consumido aqui (so chega != 0 nestes estados)
        in.dt_ms = CONTROL_LOOP_PERIOD_MS;

        ControlCore_Step(&s_control.cc, &in, &out);

        set_pump_power(out.pump_power);
#if CONTROL_CORE_V3_FOLLOWS_TEST
        if (s_control.v3_follow_off != 0U) {
            out.valve3_open = 0u; /* opcao dos Ajustes: fechada no ensaio e na descida */
        } else if (ControlCore_V3Follow(s_control.runtime_data->state, in.dt_ms, &s_control.v3_linger_ms) != 0u) {
            out.valve3_open = 1u;
        }
#endif
        set_valves(out.valve1_open, out.valve2_open, out.valve3_open);
    }
}

uint32_t ControlLoop_GetMaxJitterCycles(void) {
    return s_control.max_jitter_cycles;
}

uint32_t ControlLoop_GetMaxJitterTick(void) {
    return s_control.max_jitter_tick;
}

uint32_t ControlLoop_GetMaxJitterState(void) {
    return s_control.max_jitter_state;
}

uint32_t ControlLoop_GetMaxExecCycles(void) {
    return s_control.max_exec_cycles;
}

ControlCore_Error_t ControlLoop_GetLastError(void) {
    return s_control.cc.last_error;
}

const ControlCore_CycleStats_t* ControlLoop_GetCycleStats(void) {
    return ControlCore_GetCycleStats(&s_control.cc);
}

const ControlCore_PidDiag_t* ControlLoop_GetPidDiag(void) {
    return ControlCore_GetPidDiag(&s_control.cc);
}

ControlCore_SafetyConfig_t* ControlLoop_GetSafetyConfig(void) {
    return &s_control.cc.safety;
}

ControlCore_TuningConfig_t* ControlLoop_GetTuningConfig(void) {
    return &s_control.cc.tuning;
}

void ControlLoop_SetSetpointMmHg(float setpoint_mmhg) {
    if (setpoint_mmhg > s_control.sys_cfg->max_setpoint_mmhg) {
        s_control.runtime_data->setpoint_mmhg = s_control.sys_cfg->max_setpoint_mmhg;
    } else if (setpoint_mmhg < 0.0f) {
        s_control.runtime_data->setpoint_mmhg = 0.0f;
    } else {
        s_control.runtime_data->setpoint_mmhg = setpoint_mmhg;
    }

    s_control.sys_cfg->current_setpoint_mmhg = s_control.runtime_data->setpoint_mmhg;

    // Alterar setpoint nao inicia inflacao automaticamente.
}

void ControlLoop_RequestCancel(void) {
    ControlCore_RequestCancel(&s_control.cc);
}

void ControlLoop_StartInflation(void) {
    if (s_control.runtime_data->state == SYSTEM_STATE_IDLE) {
        s_control.manual_test_enabled = 0U;
        ControlCore_StartInflation(&s_control.cc);
        set_valves(0, 0, 0); // Fecha no inicio da inflacao, nao espera o proximo Update()
    }
}

void ControlLoop_EnterConfigMode(void) {
    s_control.manual_test_enabled = 0U;
    s_control.runtime_data->state = SYSTEM_STATE_CONFIG;
    set_pump_power(0.0f);
    set_valves(1, 1, 0);
}

void ControlLoop_ExitConfigMode(void) {
    s_control.manual_test_enabled = 0U;
    set_pump_power(0.0f);
    set_valves(1, 1, 0);
    s_control.runtime_data->state = SYSTEM_STATE_IDLE;
}

void ControlLoop_EnterFaultState(void) {
    ControlCore_Actuators_t out;

    if ((s_control.leak.phase == CONTROL_LEAK_RISING) || (s_control.leak.phase == CONTROL_LEAK_OBSERVING)) {
        s_control.leak.phase = CONTROL_LEAK_ABORTED;
    }
    s_control.service_active = 0U;
    s_control.manual_test_enabled = 0U;
    // Aplica o estado seguro imediatamente, sem esperar o proximo
    // ControlLoop_Update().
    ControlCore_EnterFaultState(&s_control.cc, &out);
    set_pump_power(out.pump_power);
    set_valves(out.valve1_open, out.valve2_open, out.valve3_open);
}

void ControlLoop_StartLeakTest(void) {
    SystemState_t state = s_control.runtime_data->state;
    float target = s_control.sys_cfg->leak_test.target_mmhg;

    if ((state == SYSTEM_STATE_CONFIG) || (state == SYSTEM_STATE_IDLE)) {
        s_control.manual_test_enabled = 0U;
        s_control.service_active = 0U;
        set_pump_power(0.0f);
        set_valves(0, 0, 0);
        if (target > LEAK_TARGET_MAX_MMHG) {
            target = LEAK_TARGET_MAX_MMHG; /* longe de P_MAX_ABS, mesmo com inercia */
        }
        s_control.leak_prev_state = state;
        s_control.leak_prev_setpoint_mmhg = s_control.runtime_data->setpoint_mmhg;
        s_control.runtime_data->setpoint_mmhg = target;
        s_control.leak_mean_mmhg = current_mmhg();
        memset(&s_control.leak, 0, sizeof(s_control.leak));
        s_control.leak.phase = CONTROL_LEAK_RISING;
        s_control.leak.held = 1U;
        PressureTarget_Start(&s_control.service, target);
        s_control.runtime_data->state = SYSTEM_STATE_LEAK_TEST;
    }
}

void ControlLoop_SetV3Follow(uint8_t enabled) {
    s_control.v3_follow_off = (uint8_t)((enabled != 0U) ? 0U : 1U);
    if (enabled == 0U) {
        s_control.v3_linger_ms = 0U;
    }
}

uint8_t ControlLoop_GetV3Follow(void) {
    return (uint8_t)((s_control.v3_follow_off != 0U) ? 0U : 1U);
}

void ControlLoop_GetLeakStatus(ControlLoop_LeakStatus_t *out) {
    *out = s_control.leak;
}

void ControlLoop_ServiceGoTo(float target_mmhg) {
    if (s_control.runtime_data->state != SYSTEM_STATE_CONFIG) {
        return;
    }
    s_control.manual_test_enabled = 0U;
    s_control.service_zero = (target_mmhg < 1.0f) ? 1U : 0U;
    s_control.service_zero_ms = 0U;
    if (s_control.service_zero == 0U) {
        if (target_mmhg > LEAK_TARGET_MAX_MMHG) {
            target_mmhg = LEAK_TARGET_MAX_MMHG;
        }
        PressureTarget_Start(&s_control.service, target_mmhg);
    }
    s_control.service_active = 1U;
}

void ControlLoop_ServiceStop(void) {
    if (s_control.service_active != 0U) {
        s_control.service_active = 0U;
        s_control.v3_linger_ms = CONTROL_CORE_V3_LINGER_MS; // esvazia a linha do manometro
    }
}

PressureTarget_State_t ControlLoop_ServiceState(void) {
    if (s_control.service_active == 0U) {
        return PRESSURE_TARGET_IDLE;
    }
    if (s_control.service_zero != 0U) {
        return (s_control.service_zero_ms >= SERVICE_ZERO_VENT_MS) ? PRESSURE_TARGET_STABLE
                                                                  : PRESSURE_TARGET_SETTLING;
    }
    return s_control.service.state;
}

void ControlLoop_SetManualActuators(float pump_power, uint8_t valve1_on, uint8_t valve2_on, uint8_t valve3_on) {
    if (pump_power < 0.0f) {
        pump_power = 0.0f;
    }
    if (pump_power > 1.0f) {
        pump_power = 1.0f;
    }

    s_control.manual_test_enabled = 1U;
    s_control.manual_pump_power = pump_power;
    s_control.manual_v1_on = (valve1_on != 0U) ? 1U : 0U;
    s_control.manual_v2_on = (valve2_on != 0U) ? 1U : 0U;
    s_control.manual_v3_on = (valve3_on != 0U) ? 1U : 0U;

    if (s_control.runtime_data->state == SYSTEM_STATE_CONFIG) {
        set_pump_power(s_control.manual_pump_power);
        set_valves(s_control.manual_v1_on, s_control.manual_v2_on, s_control.manual_v3_on);
    }
    // MANUAL: aplicado no proximo passo de 1 ms, ja' passando pela
    // supervisao (handle_state_manual).
}

uint8_t ControlLoop_EnterRemoteManual(void) {
    if (s_control.runtime_data->state != SYSTEM_STATE_IDLE) {
        return 0u;
    }
    // Comeca seguro: bomba off, V1+V2 abertas (igual ao IDLE), V3 fechada.
    s_control.manual_test_enabled = 1U;
    s_control.manual_pump_power = 0.0f;
    s_control.manual_v1_on = 1U;
    s_control.manual_v2_on = 1U;
    s_control.manual_v3_on = 0U;
    s_control.remote_hb_tick = HAL_GetTick();
    s_control.runtime_data->bt_user_pressed = 0u; // pedal de antes nao conta como parada
    s_control.runtime_data->state = SYSTEM_STATE_MANUAL;
    return 1u;
}

void ControlLoop_ExitRemoteManual(void) {
    if (s_control.runtime_data->state == SYSTEM_STATE_MANUAL) {
        leave_remote_manual(CONTROL_REMOTE_EXIT_COMMAND);
    }
}

void ControlLoop_RemoteManualHeartbeat(void) {
    s_control.remote_hb_tick = HAL_GetTick();
}

ControlLoop_RemoteExit_t ControlLoop_TakeRemoteExitReason(void) {
    ControlLoop_RemoteExit_t r = (ControlLoop_RemoteExit_t)s_control.remote_exit_reason;
    s_control.remote_exit_reason = (uint8_t)CONTROL_REMOTE_EXIT_NONE;
    return r;
}

void ControlLoop_DisableManualActuators(void) {
    s_control.manual_test_enabled = 0U;
    s_control.manual_pump_power = 0.0f;
    s_control.manual_v1_on = 0U;
    s_control.manual_v2_on = 0U;
    s_control.manual_v3_on = 0U;

    set_pump_power(0.0f);
    set_valves(1, 1, 0);
}
