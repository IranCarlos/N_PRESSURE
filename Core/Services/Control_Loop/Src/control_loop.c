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
    .p_max_abs_mmhg = 320.0f,
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
    .dp_vazamento_mmhg = 1.0f,
    .t_janela_ms = 5000u,
    .p_segura_mmhg = 10.0f,
    .t_seguro_ms = 200u,
};

typedef enum {
    LEAK_PHASE_INFLATE = 0,
    LEAK_PHASE_HOLD
} LeakPhase_t;

/**
 * @brief Estrutura interna do estado do PID (usada SO' pelo teste de
 *        vazamento manual/calibracao — ver handle_state_leak_test). O
 *        caminho automatico do ensaio usa o Control_Core (ControlCore_t).
 */
typedef struct {
    float integral;
    float prev_error;
} PID_State_t;

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

    // Estado do PID "manual", usado so' pelo teste de vazamento (DEV/cal).
    PID_State_t pid_state;
    float leak_test_start_pressure;
    uint32_t leak_test_start_tick;
    uint32_t leak_test_hold_start_tick;
    LeakPhase_t leak_phase;
    float leak_prev_setpoint_mmhg;
    uint32_t relief_pulse_until_tick;
    uint32_t relief_recheck_tick;

    uint8_t manual_test_enabled;
    uint8_t manual_v1_on;
    uint8_t manual_v2_on;
    uint8_t manual_v3_on;
    float manual_pump_power;

    // Jitter do loop de controle (Fase 3/4: "registrar jitter maximo e
    // expor via diagnostico"). Cycles do DWT->CYCCNT entre chamadas
    // consecutivas de ControlLoop_Update(); o alvo e' CONTROL_LOOP_PERIOD_MS.
    uint32_t last_update_cycles;
    uint32_t max_jitter_cycles; /**< maior |delta - periodo esperado| ja' visto */
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

/** @brief Executa uma iteração do algoritmo PID "manual" — usado SO' pela
 *         fase de inflar do teste de vazamento (handle_state_leak_test).
 *         Mesma formula historica deste modulo; o caminho automatico do
 *         ensaio usa o PID do Control_Core (ver control_core.c), que
 *         corrigiu dois bugs aqui presentes (ver sim-report.md itens 2-3) —
 *         nao replicados aqui porque o teste de vazamento e' uma rotina de
 *         bancada/calibracao, nao o ensaio em si; revisar se for reaproveitar
 *         tambem. */
static void run_pid_iteration(void) {
    const PID_Config_t* pid_cfg = &s_control.sys_cfg->pid;
    float error_mmhg;
    float output;
    float pump_power;
    float over_mmhg;
    uint8_t relief_on;
    uint8_t valve2_on;
    uint32_t now_tick;
    uint32_t pulse_ms;

    float setpoint_kpa = s_control.runtime_data->setpoint_mmhg / MMHG_PER_KPA;
    float current_kpa = s_control.runtime_data->pressure_kpa;

    float error = setpoint_kpa - current_kpa;

    float p_term = pid_cfg->kp * error;

    s_control.pid_state.integral += pid_cfg->ki * error * (pid_cfg->sample_time_ms / 1000.0f);
    if (s_control.pid_state.integral > pid_cfg->i_max) s_control.pid_state.integral = pid_cfg->i_max;
    if (s_control.pid_state.integral < pid_cfg->i_min) s_control.pid_state.integral = pid_cfg->i_min;

    float derivative = (error - s_control.pid_state.prev_error) / (pid_cfg->sample_time_ms / 1000.0f);
    float d_term = pid_cfg->kd * derivative / 10.f;
    s_control.pid_state.prev_error = error;

    float ff_term = pid_cfg->ff * setpoint_kpa;

    output = ff_term + p_term + s_control.pid_state.integral + d_term;

    if (pid_cfg->out_max > 0.0f) {
        pump_power = output / pid_cfg->out_max;
    } else {
        pump_power = output;
    }

    if (pump_power < 0.0f) {
        pump_power = 0.0f;
    }
    if (pump_power > 1.0f) {
        pump_power = 1.0f;
    }

    error_mmhg = (setpoint_kpa - current_kpa) * MMHG_PER_KPA;
    if ((fabsf(error_mmhg) <= 5.0f) && (s_control.runtime_data->setpoint_mmhg > 10.0f) &&
        (pump_power < 0.2f)) {
        pump_power = 0.2f;
    }

    relief_on = 0U;
    valve2_on = 0U;
    now_tick = HAL_GetTick();

    if ((s_control.relief_pulse_until_tick != 0U) && (now_tick < s_control.relief_pulse_until_tick)) {
        relief_on = 1U;
        pump_power *= 0.1f;
    }

    if (error_mmhg < -10.0f) {
        over_mmhg = -error_mmhg;

        if (over_mmhg >= 50.0f) {
            if (now_tick >= s_control.relief_recheck_tick) {
                pulse_ms = 1u;
                s_control.relief_pulse_until_tick = now_tick + pulse_ms;
                s_control.relief_recheck_tick = now_tick + pulse_ms + 10u;
                relief_on = 1U;
            }
            pump_power *= 0.8f;
        } else {
            if (now_tick >= s_control.relief_recheck_tick) {
                pulse_ms = 1u;
                s_control.relief_pulse_until_tick = now_tick + pulse_ms;
                s_control.relief_recheck_tick = now_tick + pulse_ms + 10u;
                relief_on = 1U;
            }

            if (pump_power < 0.2f) {
                pump_power = 0.2f;
            }
            pump_power *= 0.6f;
        }
    } else {
        s_control.relief_pulse_until_tick = 0U;
        s_control.relief_recheck_tick = now_tick;
    }

    set_pump_power(pump_power);
    set_valves(relief_on, valve2_on, 0);
}

/* --- Handlers fora do ensaio automatico (CONFIG e LEAK_TEST) --- */

static void handle_state_config(void) {
    if (s_control.manual_test_enabled != 0U) {
        set_pump_power(s_control.manual_pump_power);
        set_valves(s_control.manual_v1_on, s_control.manual_v2_on, s_control.manual_v3_on);
    } else {
        set_pump_power(0.0f);
        set_valves(1, 1, 0);
    }
}

static void handle_state_leak_test(void) {
    uint32_t now = HAL_GetTick();
    float current_mmhg = s_control.runtime_data->pressure_kpa * MMHG_PER_KPA;
    float target_mmhg = s_control.sys_cfg->leak_test.target_mmhg;

    if (s_control.leak_phase == LEAK_PHASE_INFLATE) {
        float err_mmhg = target_mmhg - current_mmhg;

        if (err_mmhg <= LEAK_TEST_TARGET_HYST_MMHG) {
            s_control.leak_phase = LEAK_PHASE_HOLD;
            s_control.leak_test_hold_start_tick = now;
            s_control.leak_test_start_pressure = current_mmhg;
            set_pump_power(0.0f);
            set_valves(0, 0, 0);
            return;
        }

        if ((now - s_control.leak_test_start_tick) >= s_control.sys_cfg->leak_test.reach_timeout_ms) {
            set_pump_power(0.0f);
            set_valves(0, 0, 0);
            s_control.runtime_data->setpoint_mmhg = s_control.leak_prev_setpoint_mmhg;
            s_control.runtime_data->state = SYSTEM_STATE_IDLE;
            return;
        }

        run_pid_iteration();
        return;
    }

    set_pump_power(0.0f);
    set_valves(0, 0, 0);

    if ((now - s_control.leak_test_hold_start_tick) >= s_control.sys_cfg->leak_test.hold_time_ms) {
        s_control.runtime_data->setpoint_mmhg = s_control.leak_prev_setpoint_mmhg;
        s_control.runtime_data->state = SYSTEM_STATE_IDLE;
    }
}

/* --- Funções Públicas --- */

void ControlLoop_Init(SystemConfig_t* p_sys_cfg, TIM_HandleTypeDef* p_htim_pwm,
                       SystemRuntime_t* p_runtime_data, const ControlLoop_ValvePins_t* p_valve_pins) {
    memset(&s_control, 0, sizeof(s_control));

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
        }
    }
    s_control.last_update_cycles = now_cycles;
    s_control.jitter_has_sample = 1u;
}

void ControlLoop_Update(void) {
    update_jitter_measurement();

    // Atualiza as leituras de pressão do módulo ADC
    ADC_Acq_Update();
    s_control.runtime_data->pressure_kpa = ADC_Acq_GetPressureKpa();
    s_control.runtime_data->pressure_display = ADC_Acq_ConvertPressure(
        s_control.runtime_data->pressure_kpa,
        s_control.sys_cfg->unit
    );

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
        set_valves(out.valve1_open, out.valve2_open, out.valve3_open);
    }
}

uint32_t ControlLoop_GetMaxJitterCycles(void) {
    return s_control.max_jitter_cycles;
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

    s_control.manual_test_enabled = 0U;
    // Aplica o estado seguro imediatamente, sem esperar o proximo
    // ControlLoop_Update().
    ControlCore_EnterFaultState(&s_control.cc, &out);
    set_pump_power(out.pump_power);
    set_valves(out.valve1_open, out.valve2_open, out.valve3_open);
}

void ControlLoop_StartLeakTest(void) {
    if (s_control.runtime_data->state == SYSTEM_STATE_CONFIG || s_control.runtime_data->state == SYSTEM_STATE_IDLE) {
        s_control.manual_test_enabled = 0U;
        set_pump_power(0.0f);
        set_valves(0, 0, 0);
        s_control.leak_prev_setpoint_mmhg = s_control.runtime_data->setpoint_mmhg;
        s_control.runtime_data->setpoint_mmhg = s_control.sys_cfg->leak_test.target_mmhg;
        s_control.runtime_data->state = SYSTEM_STATE_LEAK_TEST;
        s_control.leak_test_start_tick = HAL_GetTick();
        s_control.leak_test_hold_start_tick = s_control.leak_test_start_tick;
        s_control.leak_phase = LEAK_PHASE_INFLATE;
        s_control.leak_test_start_pressure = s_control.runtime_data->pressure_kpa * MMHG_PER_KPA;
    }
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
