/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-07-24
 * Modified: 2026-08-03
 */

#include "control_loop.h"
#include "adc_acq.h"
#include <string.h>
#include <math.h> // Para fabsf

/* Definido em main.c (gerado pelo CubeMX). Declarado aqui em vez de
 * incluir main.h para nao acoplar a camada Services ao arquivo top-level
 * da aplicacao — mesmo tratamento de falha que todo MX_*_Init() ja usa. */
extern void Error_Handler(void);

#define MMHG_PER_KPA (7.50062f)
#define CONTROL_BAND_MMHG            (10.0f) // Banda de controle para ativar alívio de pressão (em mmHg)
#define OVERPRESS_MILD_MMHG          (10.0f) // Sobrepressão leve para ativar alívio de pressão (em mmHg)
#define OVERPRESS_STRONG_MMHG        (50.0f) // Sobrepressão forte para ativar alívio de pressão (em mmHg)
#define PUMP_MIN_COMPENSATION_POWER  (0.2f) // Potência mínima da bomba para compensar vazamentos (0..1)
#define RELIEF_PULSE_FAST_MS         (1U)   // Duração do pulso de alívio rápido (em ms)
#define RELIEF_PULSE_STRONG_MS       (1U)   // Duração do pulso de alívio forte (em ms)
#define RELIEF_SETTLE_MS             (10U) // Tempo de espera após alívio para estabilização (em ms)
#define LEAK_TEST_TARGET_HYST_MMHG   (10.0f) // Histerese para considerar que a pressão atingiu o alvo durante o teste de vazamento (em mmHg)

typedef enum {
    LEAK_PHASE_INFLATE = 0,
    LEAK_PHASE_HOLD
} LeakPhase_t;

/**
 * @brief Estrutura interna do estado do PID.
 */
typedef struct {
    float integral;
    float prev_error;
    uint32_t last_update_tick;
} PID_State_t;

/**
 * @brief Estrutura interna do estado do módulo de controle.
 */
static struct {
    // Ponteiros para dados externos
    SystemConfig_t* sys_cfg;
    SystemRuntime_t* runtime_data;
    TIM_HandleTypeDef* htim_pwm;
    ControlLoop_ValvePins_t valve_pins;

    // Estado interno
    PID_State_t pid_state;
    uint32_t stabilization_start_tick;
    float leak_test_start_pressure;
    uint32_t leak_test_start_tick;
    uint32_t leak_test_hold_start_tick;
    LeakPhase_t leak_phase;
    float leak_prev_setpoint_mmhg;
    uint32_t relief_pulse_until_tick;
    uint32_t relief_recheck_tick;
    uint32_t deflate_settle_start_tick; /**< 0 = ainda nao atingiu o alvo de desinflar; !=0 = aguardando assentar antes de IDLE. */

    uint8_t manual_test_enabled;
    uint8_t manual_v1_on;
    uint8_t manual_v2_on;
    uint8_t manual_v3_on;
    float manual_pump_power;

} s_control;

/* --- Funções Privadas --- */

/** @brief Controla o estado das válvulas. Valvula 3 (calibracao) so e aberta
 *         explicitamente pelo modo manual — todo chamador automatico passa 0. */
static void set_valves(uint8_t v1_open, uint8_t v2_open, uint8_t v3_open) {
    HAL_GPIO_WritePin(s_control.valve_pins.valve1.port, s_control.valve_pins.valve1.pin, !v1_open);
    HAL_GPIO_WritePin(s_control.valve_pins.valve2.port, s_control.valve_pins.valve2.pin, !v2_open);
    HAL_GPIO_WritePin(s_control.valve_pins.valve3.port, s_control.valve_pins.valve3.pin, !v3_open);
}

/** @brief Controla a bomba de ar via PWM. Escreve direto no CCR do timer
 *         (sem DMA) — simples e suficiente, ja que o duty so muda no ritmo
 *         do loop de controle (TASK_CONTROL_PERIOD_MS), nao a cada periodo
 *         de PWM. */
static void set_pump_power(float power) {
    uint32_t period;
    uint32_t pulse;

    // Garante faixa valida para duty cycle de PWM.
    if (power < 0.0f) {
        power = 0.0f;
    }
    if (power > 1.0f) {
        power = 1.0f;
    }

    // Converte a potência (0.0 a 1.0) para o valor do registrador de pulso do timer
    period = s_control.htim_pwm->Init.Period;
    pulse = (uint32_t)(power * (float)period);
    __HAL_TIM_SET_COMPARE(s_control.htim_pwm, TIM_CHANNEL_1, pulse);
}

/** @brief Executa uma iteração do algoritmo PID. */
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

    // Converte setpoint de mmHg para kPa para consistencia interna.
    float setpoint_kpa = s_control.runtime_data->setpoint_mmhg / MMHG_PER_KPA;
    float current_kpa = s_control.runtime_data->pressure_kpa;

    float error = setpoint_kpa - current_kpa;

    // --- Termo Proporcional ---
    float p_term = pid_cfg->kp * error;

    // --- Termo Integral (com anti-windup) ---
    s_control.pid_state.integral += pid_cfg->ki * error * (pid_cfg->sample_time_ms / 1000.0f);
    if (s_control.pid_state.integral > pid_cfg->i_max) s_control.pid_state.integral = pid_cfg->i_max;
    if (s_control.pid_state.integral < pid_cfg->i_min) s_control.pid_state.integral = pid_cfg->i_min;

    // --- Termo Derivativo ---
    float derivative = (error - s_control.pid_state.prev_error) / (pid_cfg->sample_time_ms / 1000.0f);
    float d_term = pid_cfg->kd * derivative / 10.f;
    s_control.pid_state.prev_error = error;

    // --- Termo Feedforward ---
    // Estima a potência necessária para manter o setpoint (relação linear)
    float ff_term = pid_cfg->ff * setpoint_kpa;

    // --- Saída Final ---
    output = ff_term + p_term + s_control.pid_state.integral + d_term;

    // Converte a saida do PID para potencia normalizada de PWM (0..1).
    // out_max da EEPROM representa a escala de comando do PID.
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

    // Mantem bomba em baixa potencia proximo do alvo para compensar pequenos vazamentos.
    error_mmhg = (setpoint_kpa - current_kpa) * MMHG_PER_KPA;
    if ((fabsf(error_mmhg) <= 5.0f) && (s_control.runtime_data->setpoint_mmhg > 10.0f) &&
        (pump_power < PUMP_MIN_COMPENSATION_POWER)) {
        pump_power = PUMP_MIN_COMPENSATION_POWER;
    }

    relief_on = 0U;
    valve2_on = 0U;
    now_tick = HAL_GetTick();

    if ((s_control.relief_pulse_until_tick != 0U) && (now_tick < s_control.relief_pulse_until_tick)) {
        relief_on = 1U;
        pump_power *= 0.1f;
    }

    if (error_mmhg < -CONTROL_BAND_MMHG) {
        over_mmhg = -error_mmhg;

        if (over_mmhg >= OVERPRESS_STRONG_MMHG) {
            // Sobrepressao alta: pulso mais largo + queda forte da bomba.
            if (now_tick >= s_control.relief_recheck_tick) {
                pulse_ms = RELIEF_PULSE_STRONG_MS;
                s_control.relief_pulse_until_tick = now_tick + pulse_ms;
                s_control.relief_recheck_tick = now_tick + pulse_ms + RELIEF_SETTLE_MS;
                relief_on = 1U;
            }
            pump_power *= 0.8f;
        } else {
            // Sobrepressao moderada: pulso curto e aguarda acomodar antes do proximo.
            if (now_tick >= s_control.relief_recheck_tick) {
                pulse_ms = RELIEF_PULSE_FAST_MS;
                s_control.relief_pulse_until_tick = now_tick + pulse_ms;
                s_control.relief_recheck_tick = now_tick + pulse_ms + RELIEF_SETTLE_MS;
                relief_on = 1U;
            }

            if (pump_power < PUMP_MIN_COMPENSATION_POWER) {
                pump_power = PUMP_MIN_COMPENSATION_POWER;
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

/* --- Máquina de Estados (Handlers) --- */

static void handle_state_idle(void) {
    set_pump_power(0.0f);
    set_valves(1, 1, 0); // Mantem valvulas abertas fora do ciclo de inflacao
}

static void handle_state_inflating(void) {
    uint32_t now = HAL_GetTick();

    if (s_control.runtime_data->bt_user_pressed) {
        s_control.runtime_data->bt_user_pressed = 0;
        s_control.runtime_data->state = SYSTEM_STATE_DEFLATING_FAST;
        return;
    }

    if (now - s_control.pid_state.last_update_tick >= s_control.sys_cfg->pid.sample_time_ms) {
        s_control.pid_state.last_update_tick = now;
        run_pid_iteration();
    }

    // Condição de transição para estabilização
    float error_mmhg = s_control.runtime_data->setpoint_mmhg - s_control.runtime_data->pressure_display;
    if (fabsf(error_mmhg) <= s_control.sys_cfg->stabilization.band_mmhg) {
        s_control.runtime_data->state = SYSTEM_STATE_STABILIZING;
        s_control.stabilization_start_tick = now;
    }
}

static void handle_state_stabilizing(void) {
    uint32_t now = HAL_GetTick();

    if (s_control.runtime_data->bt_user_pressed) {
        s_control.runtime_data->bt_user_pressed = 0;
        s_control.runtime_data->state = SYSTEM_STATE_DEFLATING_FAST;
        return;
    }

    // Continua executando o PID para manter a pressão
    if (now - s_control.pid_state.last_update_tick >= s_control.sys_cfg->pid.sample_time_ms) {
        s_control.pid_state.last_update_tick = now;
        run_pid_iteration();
    }

    float error_mmhg = s_control.runtime_data->setpoint_mmhg - s_control.runtime_data->pressure_display;
    if (fabsf(error_mmhg) > s_control.sys_cfg->stabilization.band_mmhg) {
        // Saiu da banda de estabilização, volta a inflar
        s_control.runtime_data->state = SYSTEM_STATE_INFLATING;
        return;
    }

    if (now - s_control.stabilization_start_tick >= s_control.sys_cfg->stabilization.time_ms) {
        // Tempo de estabilização atingido
        s_control.runtime_data->state = SYSTEM_STATE_STABILIZED;
    }
}

static void handle_state_stabilized(void) {
    static uint32_t wait_error = 0U;
    set_pump_power(0.0f); // Desliga a bomba
    set_valves(0, 0, 0);     // Fecha as válvulas para manter a pressão

    // Aguarda o botão do usuário para desinflar
    if (s_control.runtime_data->bt_user_pressed) {
        s_control.runtime_data->state = SYSTEM_STATE_DEFLATING_FAST;
        s_control.runtime_data->bt_user_pressed = 0; // Consome o evento
    }
    float error_mmhg = s_control.runtime_data->setpoint_mmhg - s_control.runtime_data->pressure_display;
    if (fabsf(error_mmhg) > s_control.sys_cfg->stabilization.band_mmhg) {
        wait_error += 1; // Incrementa o tempo de espera (ajuste conforme necessário)
        if (wait_error >= 2U) {
            // Saiu da banda de estabilização, volta a inflar
            s_control.runtime_data->state = SYSTEM_STATE_INFLATING;
            return;
        }
    } else {
        wait_error = 0U; // Reseta o tempo de espera se o erro estiver dentro da banda
    }
}

/** @brief Tempo de assentamento apos a pressao cair abaixo do limiar de
 *         "desinflado", antes de ir para IDLE (ver handle_state_deflating). */
#define DEFLATE_SETTLE_MS (100U)

static void handle_state_deflating(void) {
    set_pump_power(0.0f);
    set_valves(1, 1, 0); // Abre ambas as válvulas para desinsuflação rápida

    // Condição de transição para IDLE: pressão abaixo de 2 mmHg por
    // DEFLATE_SETTLE_MS seguidos. Não-bloqueante (baseado em tick) — o
    // HAL_Delay(100) que havia aqui congelava o super loop inteiro (LVGL,
    // encoder, watchdog) por 100ms a cada ciclo de desinflação.
    if (s_control.runtime_data->pressure_display < 2.0f) {
        if (s_control.deflate_settle_start_tick == 0U) {
            s_control.deflate_settle_start_tick = HAL_GetTick();
        } else if ((HAL_GetTick() - s_control.deflate_settle_start_tick) >= DEFLATE_SETTLE_MS) {
            s_control.deflate_settle_start_tick = 0U;
            s_control.runtime_data->state = SYSTEM_STATE_IDLE;
        }
    } else {
        s_control.deflate_settle_start_tick = 0U; // pressão voltou a subir antes de assentar; reinicia a espera
    }
}

/** @brief Estado seguro (Parte A, item 1 do CLAUDE.md/prompt de projeto):
 *         motor OFF e V1+V2+V3 abertas. Unico handler que abre a valvula 3
 *         automaticamente — todo caminho de falha (sobrepressao, sensor
 *         invalido, timeout, watchdog, perda de heartbeat em modo manual)
 *         deve levar o sistema a este estado via ControlLoop_EnterFaultState(),
 *         em vez de abrir valvulas individualmente em cada ponto de deteccao
 *         de falha. */
static void handle_state_fault(void) {
    set_pump_power(0.0f);
    set_valves(1, 1, 1);
}

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
    s_control.htim_pwm = p_htim_pwm;
    s_control.valve_pins = *p_valve_pins;

    // Estado inicial
    s_control.runtime_data->state = SYSTEM_STATE_IDLE;
    s_control.runtime_data->setpoint_mmhg = p_sys_cfg->current_setpoint_mmhg;
    if (s_control.runtime_data->setpoint_mmhg < 0.0f) {
        s_control.runtime_data->setpoint_mmhg = 0.0f;
    }
    if (s_control.runtime_data->setpoint_mmhg > p_sys_cfg->max_setpoint_mmhg) {
        s_control.runtime_data->setpoint_mmhg = p_sys_cfg->max_setpoint_mmhg;
    }

    // Desliga todos os atuadores
    set_pump_power(0.0f);
    set_valves(1, 1, 0);

    // Inicia o PWM (escrita direta do CCR via __HAL_TIM_SET_COMPARE em
    // set_pump_power(), sem DMA). O canal (TIM_CHANNEL_1) deve corresponder
    // ao configurado no CubeMX.
    if (HAL_TIM_PWM_Start(s_control.htim_pwm, TIM_CHANNEL_1) != HAL_OK) {
        Error_Handler();
    }
}

void ControlLoop_Update(void) {
    // Atualiza as leituras de pressão do módulo ADC
    ADC_Acq_Update();
    s_control.runtime_data->pressure_kpa = ADC_Acq_GetPressureKpa();
    s_control.runtime_data->pressure_display = ADC_Acq_ConvertPressure(
        s_control.runtime_data->pressure_kpa,
        s_control.sys_cfg->unit
    );

    // Máquina de estados principal
    switch (s_control.runtime_data->state) {
        case SYSTEM_STATE_IDLE:
            handle_state_idle();
            break;

        case SYSTEM_STATE_INFLATING:
            handle_state_inflating();
            break;

        case SYSTEM_STATE_STABILIZING:
            handle_state_stabilizing();
            break;

        case SYSTEM_STATE_STABILIZED:
            handle_state_stabilized();
            break;

        case SYSTEM_STATE_DEFLATING_FAST:
            handle_state_deflating();
            break;

        case SYSTEM_STATE_LEAK_TEST:
            handle_state_leak_test();
            break;

        case SYSTEM_STATE_CONFIG:
            handle_state_config();
            break;

        case SYSTEM_STATE_FAULT:
            handle_state_fault();
            break;

        case SYSTEM_STATE_BOOT:
            // Nenhuma ação de controle neste estado
            break;
    }
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
        s_control.runtime_data->state = SYSTEM_STATE_INFLATING;
        // Reseta o estado do PID para um novo ciclo
        memset(&s_control.pid_state, 0, sizeof(s_control.pid_state));
        set_valves(0, 0, 0); // Fecha no inicio da inflacao
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
    s_control.manual_test_enabled = 0U;
    s_control.runtime_data->state = SYSTEM_STATE_FAULT;
    // Aplica o estado seguro imediatamente, sem esperar o proximo
    // ControlLoop_Update() — mesma logica de handle_state_fault().
    set_pump_power(0.0f);
    set_valves(1, 1, 1);
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
