/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-01
 */

#include "control_core.h"
#include <string.h>
#include <math.h>

/* Constantes do estagio de alivio de sobrepressao (PID ativo), portadas
 * sem alteracao de Core/Services/Control_Loop — ver 00-auditoria.md D4.
 * NOTA: RELIEF_PULSE_*_MS = 1 ms e' menor ou igual ao proprio periodo do
 * loop de controle (1 ms): na pratica o pulso raramente fica visivel por
 * mais de uma chamada. Isso ja' existia no Control_Loop atual (nunca
 * verificado em bancada); registrado aqui, nao "corrigido" por conta
 * propria — `t_min_valvula` no Apendice E ainda esta' como "medir". */
#define CONTROL_CORE_CONTROL_BAND_MMHG     (0.5f)
#define CONTROL_CORE_OVERPRESS_STRONG_MMHG (50.0f)
#define CONTROL_CORE_PUMP_MIN_EFFECTIVE     (0.30f)
#define CONTROL_CORE_RELIEF_PULSE_FAST_MS   (30u)
#define CONTROL_CORE_RELIEF_PULSE_STRONG_MS (80u)
#define CONTROL_CORE_RELIEF_SETTLE_MS       (10u)

static void safe_actuators(ControlCore_Actuators_t *out, float pump,
                           uint8_t v1, uint8_t v2, uint8_t v3)
{
    out->pump_power = pump;
    out->valve1_open = v1;
    out->valve2_open = v2;
    out->valve3_open = v3;
}

void ControlCore_EnterFaultState(ControlCore_t *cc, ControlCore_Actuators_t *out)
{
    cc->runtime->state = SYSTEM_STATE_FAULT;
    safe_actuators(out, 0.0f, 1u, 1u, 1u);
}

void ControlCore_Init(ControlCore_t *cc, SystemConfig_t *cfg, SystemRuntime_t *runtime,
                      const ControlCore_SafetyConfig_t *safety)
{
    memset(cc, 0, sizeof(*cc));
    cc->cfg = cfg;
    cc->runtime = runtime;
    cc->safety = *safety;
    runtime->state = SYSTEM_STATE_IDLE;
}

void ControlCore_StartInflation(ControlCore_t *cc)
{
    if (cc->runtime->state == SYSTEM_STATE_IDLE) {
        cc->runtime->state = SYSTEM_STATE_INFLATING;
        cc->pid_integral = 0.0f;
        cc->pid_prev_error = 0.0f;
        cc->last_error = CONTROL_ERR_NONE;
        cc->inflate_elapsed_ms = 0u;
        cc->state_elapsed_ms = 0u;
    }
}

/** @brief PID + feedforward + alivio de sobrepressao. Usado por INFLATING e
 *         STABILIZING — igual ao `run_pid_iteration()` do Control_Loop
 *         atual, trocando `HAL_GetTick()` por contadores de `dt_ms`. */
static void run_pid_iteration(ControlCore_t *cc, const ControlCore_Inputs_t *in,
                              ControlCore_Actuators_t *out)
{
    const PID_Config_t *pid_cfg = &cc->cfg->pid;
    float setpoint_kpa = cc->runtime->setpoint_mmhg / CONTROL_CORE_MMHG_PER_KPA;
    float current_kpa = in->pressure_kpa;
    float error = setpoint_kpa - current_kpa;
    float dt_s = (float)in->dt_ms / 1000.0f;
    float p_term;
    float derivative;
    float d_term;
    float ff_term;
    float output;
    float pump_power;
    float error_mmhg;
    uint8_t relief_on = 0u;

    p_term = pid_cfg->kp * error;

    cc->pid_integral += pid_cfg->ki * error * dt_s;
    if (cc->pid_integral > pid_cfg->i_max) { cc->pid_integral = pid_cfg->i_max; }
    if (cc->pid_integral < pid_cfg->i_min) { cc->pid_integral = pid_cfg->i_min; }

    derivative = (dt_s > 0.0f) ? ((error - cc->pid_prev_error) / dt_s) : 0.0f;
    d_term = pid_cfg->kd * derivative / 10.0f;
    cc->pid_prev_error = error;

    ff_term = pid_cfg->ff * setpoint_kpa;

    output = ff_term + p_term + cc->pid_integral + d_term;
    pump_power = (pid_cfg->out_max > 0.0f) ? (output / pid_cfg->out_max) : output;
    if (pump_power < 0.0f) { pump_power = 0.0f; }
    if (pump_power > 1.0f) { pump_power = 1.0f; }

    /* NAO porta o piso de PUMP_MIN_COMPENSATION_POWER do Control_Loop atual
     * aqui. No codigo original ele e' aplicado dentro de run_pid_iteration()
     * sempre que |erro| <= 5 mmHg — inclusive enquanto a pressao ainda esta'
     * SUBINDO pela primeira vez rumo ao setpoint (INFLATING/STABILIZING).
     * No simulador isso forca ~20% de potencia bem na hora de cruzar o
     * alvo e produz overshoot de dezenas de mmHg (achado desta fase — ver
     * sim-report.md e 00-auditoria.md). A compensacao de vazamento faz
     * sentido na fase de MANUTENCAO (STABILIZED), que nem chama esta
     * funcao (desliga a bomba e so' observa — ver handle_state_stabilized);
     * reintroduzir um piso so' ali, se a bancada mostrar necessidade, fica
     * para a Fase 4. */
    error_mmhg = error * CONTROL_CORE_MMHG_PER_KPA;

    /* Piso de partida (achado de bancada, 2026-10): a bomba real so' gira de
     * fato a partir de ~30% de duty (medido na Fase 2 pela bancada de
     * atuadores) — bem acima do que o PID calcula para erros pequenos/
     * moderados com os ganhos simulados atuais (ex.: ~1-4% em 40 mmHg). Sem
     * isto a bomba nunca sai da inercia e o ensaio nunca infla de verdade.
     * So' se aplica enquanto falta mais que a banda de estabilizacao para o
     * alvo — nunca perto o bastante do setpoint pra reintroduzir o
     * overshoot do piso removido acima (aquele agia ate' 5 mmHg do alvo e
     * causava ~60 mmHg de overshoot no simulador). 0.30 ainda nao foi
     * confirmado como o minimo exato — e' o que a bancada mostrou. */
    if ((pump_power > 0.0f) && (pump_power < CONTROL_CORE_PUMP_MIN_EFFECTIVE) &&
        (error_mmhg > cc->cfg->stabilization.band_mmhg)) {
        pump_power = CONTROL_CORE_PUMP_MIN_EFFECTIVE;
    }

    /* Contadores de pulso/recheck de alivio: decrementam a cada chamada. */
    cc->relief_pulse_remaining_ms = (cc->relief_pulse_remaining_ms > in->dt_ms)
        ? (cc->relief_pulse_remaining_ms - in->dt_ms) : 0u;
    cc->relief_recheck_remaining_ms = (cc->relief_recheck_remaining_ms > in->dt_ms)
        ? (cc->relief_recheck_remaining_ms - in->dt_ms) : 0u;

    if (cc->relief_pulse_remaining_ms > 0u) {
        relief_on = 1u;
        pump_power *= 0.1f;
    }

    if (error_mmhg < -CONTROL_CORE_CONTROL_BAND_MMHG) {
        float over_mmhg = -error_mmhg;

        /* Achado desta fase (presente tambem no Control_Loop atual, linhas
         * 161-183): o piso de PUMP_MIN_COMPENSATION_POWER era reaplicado
         * AQUI, dentro do proprio ramo de sobrepressao leve — forcando a
         * bomba de volta a pelo menos 20% (12% apos o fator 0.6) bem
         * quando ela deveria estar sendo cortada para permitir a sangria
         * vencer. Em vez de so' atenuar, corta a bomba (a sangria e' quem
         * deve reduzir a pressao); sem isso o simulador mostra "corrida"
         * de sobrepressao em manguitos pequenos/rapidos (C baixo), onde o
         * fluxo residual da bomba supera o que a valvula consegue sangrar
         * nos pulsos curtos. Ver sim-report.md e 00-auditoria.md. */
        if (over_mmhg >= CONTROL_CORE_OVERPRESS_STRONG_MMHG) {
            if (cc->relief_recheck_remaining_ms == 0u) {
                cc->relief_pulse_remaining_ms = CONTROL_CORE_RELIEF_PULSE_STRONG_MS;
                cc->relief_recheck_remaining_ms =
                    CONTROL_CORE_RELIEF_PULSE_STRONG_MS + CONTROL_CORE_RELIEF_SETTLE_MS;
                relief_on = 1u;
            }
            pump_power = 0.0f;
        } else {
            if (cc->relief_recheck_remaining_ms == 0u) {
                cc->relief_pulse_remaining_ms = CONTROL_CORE_RELIEF_PULSE_FAST_MS;
                cc->relief_recheck_remaining_ms =
                    CONTROL_CORE_RELIEF_PULSE_FAST_MS + CONTROL_CORE_RELIEF_SETTLE_MS;
                relief_on = 1u;
            }
            pump_power *= 0.3f;
        }
    } else {
        cc->relief_pulse_remaining_ms = 0u;
        cc->relief_recheck_remaining_ms = 0u;
    }

    safe_actuators(out, pump_power, relief_on, 0u, 0u);
}

static void handle_idle(ControlCore_Actuators_t *out)
{
    safe_actuators(out, 0.0f, 1u, 1u, 0u);
}

static void handle_inflating(ControlCore_t *cc, const ControlCore_Inputs_t *in,
                             ControlCore_Actuators_t *out)
{
    SystemRuntime_t *rt = cc->runtime;
    float error_mmhg;

    if (in->abort_request != 0u) {
        rt->state = SYSTEM_STATE_DEFLATING_FAST;
        safe_actuators(out, 0.0f, 1u, 1u, 0u);
        return;
    }

    run_pid_iteration(cc, in, out);

    error_mmhg = rt->setpoint_mmhg - rt->pressure_display;
    if (fabsf(error_mmhg) <= cc->cfg->stabilization.band_mmhg) {
        rt->state = SYSTEM_STATE_STABILIZING;
    }

    if (cc->inflate_elapsed_ms >= cc->safety.t_inflar_max_ms) {
        cc->last_error = CONTROL_ERR_E003_INFLATE_TIMEOUT;
        rt->state = SYSTEM_STATE_DEFLATING_FAST;
        safe_actuators(out, 0.0f, 1u, 1u, 0u);
    }
}

static void handle_stabilizing(ControlCore_t *cc, const ControlCore_Inputs_t *in,
                               ControlCore_Actuators_t *out)
{
    SystemRuntime_t *rt = cc->runtime;
    float error_mmhg;

    if (in->abort_request != 0u) {
        rt->state = SYSTEM_STATE_DEFLATING_FAST;
        safe_actuators(out, 0.0f, 1u, 1u, 0u);
        return;
    }

    run_pid_iteration(cc, in, out);

    error_mmhg = rt->setpoint_mmhg - rt->pressure_display;
    if (fabsf(error_mmhg) > cc->cfg->stabilization.band_mmhg) {
        rt->state = SYSTEM_STATE_INFLATING;
        return;
    }

    if (cc->state_elapsed_ms >= cc->cfg->stabilization.time_ms) {
        rt->state = SYSTEM_STATE_STABILIZED;
    }

    if (cc->inflate_elapsed_ms >= cc->safety.t_inflar_max_ms) {
        cc->last_error = CONTROL_ERR_E003_INFLATE_TIMEOUT;
        rt->state = SYSTEM_STATE_DEFLATING_FAST;
        safe_actuators(out, 0.0f, 1u, 1u, 0u);
    }
}

/** @brief Fase MANUTENCAO: bomba desligada, valvulas fechadas, so' observa.
 *         Novo nesta fase: deteccao de vazamento por janela (E013) — o
 *         Control_Loop atual nao tinha isso (so' o teste de vazamento
 *         manual/calibracao, state SYSTEM_STATE_LEAK_TEST, que e' outra
 *         coisa e nao foi portado ainda). */
static void handle_stabilized(ControlCore_t *cc, const ControlCore_Inputs_t *in,
                              ControlCore_Actuators_t *out)
{
    SystemRuntime_t *rt = cc->runtime;
    float error_mmhg;
    float drop_mmhg;

    safe_actuators(out, 0.0f, 0u, 0u, 0u);

    if (in->abort_request != 0u) {
        rt->state = SYSTEM_STATE_DEFLATING_FAST;
        return;
    }

    /* Histerese (achado desta fase, nao existia no Control_Loop atual, que
     * usava a MESMA banda para entrar e sair de STABILIZED): ao entrar em
     * STABILIZED a bomba e' cortada para 0 de uma vez, mas o fluxo real
     * (inercia do motor+pneumatica, T_lag) leva uma fracao de segundo para
     * cair a zero — a pressao pode continuar subindo um pouco por inercia
     * logo apos a transicao. Sem histerese isso derrubava de volta para
     * INFLATING e gerava um ciclo limite (bate-e-volta) que, no simulador,
     * levou dezenas de segundos para amortecer no manguito pequeno/rapido.
     * Sair exige uma banda MAIOR (2x) que a usada para entrar. */
    error_mmhg = rt->setpoint_mmhg - rt->pressure_display;
    if (fabsf(error_mmhg) > (2.0f * cc->cfg->stabilization.band_mmhg)) {
        cc->stabilized_out_of_band_calls++;
        if (cc->stabilized_out_of_band_calls >= 2u) {
            cc->stabilized_out_of_band_calls = 0u;
            rt->state = SYSTEM_STATE_INFLATING;
            return;
        }
    } else {
        cc->stabilized_out_of_band_calls = 0u;
    }

    cc->leak_window_elapsed_ms += in->dt_ms;
    if (cc->leak_window_elapsed_ms <= in->dt_ms) {
        cc->leak_window_start_mmhg = rt->pressure_display; /* 1a chamada da janela */
    }
    if (cc->leak_window_elapsed_ms >= cc->safety.t_janela_ms) {
        drop_mmhg = cc->leak_window_start_mmhg - rt->pressure_display;
        cc->leak_window_elapsed_ms = 0u;
        if (drop_mmhg >= cc->safety.dp_vazamento_mmhg) {
            cc->last_error = CONTROL_ERR_E013_LEAK;
            rt->state = SYSTEM_STATE_DEFLATING_FAST;
        }
    }
}

static void handle_deflating(ControlCore_t *cc, const ControlCore_Inputs_t *in,
                             ControlCore_Actuators_t *out)
{
    SystemRuntime_t *rt = cc->runtime;

    safe_actuators(out, 0.0f, 1u, 1u, 0u);

    if (rt->pressure_display < cc->safety.p_segura_mmhg) {
        cc->deflate_settle_elapsed_ms += in->dt_ms;
        if (cc->deflate_settle_elapsed_ms >= cc->safety.t_seguro_ms) {
            cc->deflate_settle_elapsed_ms = 0u;
            rt->state = SYSTEM_STATE_IDLE;
            return;
        }
    } else {
        cc->deflate_settle_elapsed_ms = 0u;
    }

    if (cc->state_elapsed_ms >= cc->safety.t_desinflar_max_ms) {
        /* "Mantem valvulas abertas, alarme" (Apendice D/E004): ja' esta'
         * seguro (bomba off, V1+V2 abertas); so' sinaliza e permanece —
         * nao ha' recuperacao automatica. */
        cc->last_error = CONTROL_ERR_E004_DEFLATE_TIMEOUT;
    }
}

/** @brief SYSTEM_STATE_LEAK_TEST (calibracao/teste manual de vazamento) e'
 *         fora do escopo desta fase — mantem a saida segura (fechada) sem
 *         executar a logica do ensaio especial. Portar junto do restante
 *         do modo DEV/calibracao. */
static void handle_leak_test_stub(ControlCore_Actuators_t *out)
{
    safe_actuators(out, 0.0f, 0u, 0u, 0u);
}

void ControlCore_Step(ControlCore_t *cc, const ControlCore_Inputs_t *in,
                      ControlCore_Actuators_t *out)
{
    SystemRuntime_t *rt = cc->runtime;
    SystemState_t state_before = rt->state;

    rt->pressure_kpa = in->pressure_kpa;
    rt->pressure_display = in->pressure_kpa * CONTROL_CORE_MMHG_PER_KPA;

    safe_actuators(out, 0.0f, 0u, 0u, 0u);

    /* --- seguranca, preemptiva a qualquer estado (exceto se ja' em FAULT) --- */
    cc->sensor_fault_elapsed_ms = (in->sensor_ok == 0u)
        ? (cc->sensor_fault_elapsed_ms + in->dt_ms) : 0u;

    if (rt->state != SYSTEM_STATE_FAULT) {
        if (cc->sensor_fault_elapsed_ms >= cc->safety.sensor_fault_debounce_ms) {
            cc->last_error = CONTROL_ERR_E002_SENSOR_FAULT;
            ControlCore_EnterFaultState(cc, out);
            return;
        }
        if (rt->pressure_display > cc->safety.p_max_abs_mmhg) {
            cc->last_error = CONTROL_ERR_E001_OVERPRESSURE;
            ControlCore_EnterFaultState(cc, out);
            return;
        }
    }

    switch (rt->state) {
        case SYSTEM_STATE_IDLE:
            handle_idle(out);
            break;
        case SYSTEM_STATE_INFLATING:
            handle_inflating(cc, in, out);
            break;
        case SYSTEM_STATE_STABILIZING:
            handle_stabilizing(cc, in, out);
            break;
        case SYSTEM_STATE_STABILIZED:
            handle_stabilized(cc, in, out);
            break;
        case SYSTEM_STATE_DEFLATING_FAST:
            handle_deflating(cc, in, out);
            break;
        case SYSTEM_STATE_LEAK_TEST:
            handle_leak_test_stub(out);
            break;
        case SYSTEM_STATE_CONFIG:
            safe_actuators(out, 0.0f, 1u, 1u, 0u);
            break;
        case SYSTEM_STATE_FAULT:
            safe_actuators(out, 0.0f, 1u, 1u, 1u);
            break;
        case SYSTEM_STATE_BOOT:
        default:
            break;
    }

    if ((rt->state != state_before) && (rt->state == SYSTEM_STATE_STABILIZED)) {
        /* janela de vazamento sempre comeca do zero numa nova entrada em
         * STABILIZED — evita comparar com um baseline de uma visita antiga
         * (ex.: INFLATING<->STABILIZED oscilando perto da banda). */
        cc->leak_window_elapsed_ms = 0u;
    }

    cc->state_elapsed_ms = (rt->state == state_before) ? (cc->state_elapsed_ms + in->dt_ms) : 0u;

    cc->inflate_elapsed_ms = ((rt->state == SYSTEM_STATE_INFLATING) ||
                             (rt->state == SYSTEM_STATE_STABILIZING))
        ? (cc->inflate_elapsed_ms + in->dt_ms) : 0u;
}
