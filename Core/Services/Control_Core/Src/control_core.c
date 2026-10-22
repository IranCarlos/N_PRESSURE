/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-01
 */

#include "control_core.h"
#include <string.h>
#include <math.h>

#define CONTROL_CORE_PUMP_MIN_EFFECTIVE     (0.30f)

/*
 * Aproximacao rapida (2026-10, pedido do usuario na bancada): longe do
 * setpoint a bomba roda a 100% com as valvulas fechadas; o PID so' assume
 * dentro da zona de regulacao. A troca e' PREDITIVA: corta quando
 *     pressao + taxa_de_subida * ANTECIPACAO >= setpoint - ZONA
 * — a taxa medida ja' embute o tamanho do manguito (manguito pequeno sobe
 * rapido -> troca mais cedo), e ANTECIPACAO cobre a inercia da bomba/
 * pneumatica depois do corte (T_lag do Apendice E). Valores iniciais, NAO
 * medidos: ajustar na bancada observando pid_diag.predicted_mmhg e o
 * overshoot real.
 */
#define CONTROL_CORE_APPROACH_LOOKAHEAD_S   (1.0f)
#define CONTROL_CORE_APPROACH_ZONE_MMHG     (10.0f)
/* So' volta a 100% se cair MUITO abaixo do alvo e ficar la' (vazamento
 * grande) — histerese larga para nao alternar 100%/PID perto do alvo. */
#define CONTROL_CORE_APPROACH_REARM_MMHG    (30.0f)
#define CONTROL_CORE_APPROACH_REARM_MS      (500u)
/* Filtros da taxa de subida: a bomba e' de embolo (ondulacao a cada curso),
 * entao a derivada crua e' inutil. Pressao filtrada (tau curto) e depois a
 * taxa filtrada (tau maior). */
#define CONTROL_CORE_RATE_P_TAU_S           (0.05f)
#define CONTROL_CORE_RATE_TAU_S             (0.08f)

/*
 * Valvula de alivio (V1) na regulacao: PULSO + ACOMODACAO (2026-10-17).
 *
 * Achado de bancada: ao abrir a valvula, o sensor ve uma queda LOCAL de
 * ~20 mmHg (a tubulacao perto da valvula esvazia rapido; o manguito, maior,
 * ainda esta' cheio). Reagir a essa leitura era o problema: a valvula
 * fechava cedo pela queda falsa, o PID via "20 mmHg abaixo" e ligava a
 * bomba, e na reequalizacao a pressao passava do setpoint de novo — ciclo.
 * Versoes anteriores: pulsos de 30 ms a cada 40 ms com 0,5 mmHg de banda
 * (metralhava) e depois histerese pura (abria ate' voltar ao setpoint).
 *
 * Agora:
 *   - so' avalia com a pressao ACOMODADA (filtrada, fora da janela abaixo);
 *   - se sobrepressao >= RELIEF_ON_FRACTION * banda: abre UM pulso de
 *     duracao proporcional ao excesso (RELIEF_MS_PER_MMHG, limitado a
 *     [RELIEF_PULSE_MIN_MS, RELIEF_PULSE_MAX_MS]) — leitura ignorada
 *     durante o pulso;
 *   - fecha e espera RELIEF_SETTLE_MS (reequalizacao da tubulacao) antes de
 *     reavaliar. Durante pulso + acomodacao: bomba desligada, integrador do
 *     PID congelado e STABILIZING nao volta para INFLATING.
 * Valores iniciais (nao medidos) — ajustar na bancada: se ainda cair abaixo
 * do alvo, diminuir RELIEF_MS_PER_MMHG/RELIEF_PULSE_MAX_MS; se a pressao
 * demorar a reequalizar, aumentar RELIEF_SETTLE_MS.
 */
#define CONTROL_CORE_RELIEF_ON_FRACTION     (0.5f)
#define CONTROL_CORE_RELIEF_MS_PER_MMHG     (10.0f)
#define CONTROL_CORE_RELIEF_PULSE_MIN_MS    (20u)
#define CONTROL_CORE_RELIEF_PULSE_MAX_MS    (150u)
#define CONTROL_CORE_RELIEF_SETTLE_MS       (400u)

/* STABILIZING so' volta para INFLATING se ficar fora de 2x a banda por
 * este tempo seguido (mesmo criterio de histerese de STABILIZED). */
#define CONTROL_CORE_STABILIZING_EXIT_MS    (50u)

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
        cc->stabilizing_out_of_band_ms = 0u;

        cc->fast_approach = 1u;
        cc->rearm_elapsed_ms = 0u;
        cc->rate_valid = 0u;
        cc->rise_rate_mmhg_s = 0.0f;

        cc->relief_open = 0u;
        cc->relief_on_ms = 0u;
        cc->relief_off_ms = CONTROL_CORE_RELIEF_SETTLE_MS; /* 1a abertura liberada */
        cc->relief_pulse_ms = 0u;

        memset(&cc->cycle, 0, sizeof(cc->cycle));
        cc->cycle.plateau_min_mmhg = 1.0e9f;   /* sentinela: nenhuma amostra ainda */
        cc->cycle.plateau_max_mmhg = -1.0e9f;
    }
}

const ControlCore_CycleStats_t *ControlCore_GetCycleStats(const ControlCore_t *cc)
{
    return &cc->cycle;
}

const ControlCore_PidDiag_t *ControlCore_GetPidDiag(const ControlCore_t *cc)
{
    return &cc->pid_diag;
}

/** @brief Valvula de alivio por pulso + acomodacao (ver constantes
 *         CONTROL_CORE_RELIEF_*). `over_mmhg` = pressao filtrada - setpoint.
 *         Devolve 1 se a valvula deve ficar aberta neste passo. */
static uint8_t update_relief_valve(ControlCore_t *cc, float over_mmhg, uint32_t dt_ms)
{
    float on_threshold = CONTROL_CORE_RELIEF_ON_FRACTION * cc->cfg->stabilization.band_mmhg;

    if (cc->relief_open != 0u) {
        cc->relief_on_ms += dt_ms;
        if (cc->relief_on_ms >= cc->relief_pulse_ms) {
            cc->relief_open = 0u;
            cc->relief_off_ms = 0u;
        }
    } else if (cc->relief_off_ms < CONTROL_CORE_RELIEF_SETTLE_MS) {
        cc->relief_off_ms += dt_ms; /* acomodando: leitura nao confiavel */
    } else if (over_mmhg >= on_threshold) {
        float pulse = over_mmhg * CONTROL_CORE_RELIEF_MS_PER_MMHG;

        if (pulse < (float)CONTROL_CORE_RELIEF_PULSE_MIN_MS) {
            pulse = (float)CONTROL_CORE_RELIEF_PULSE_MIN_MS;
        }
        if (pulse > (float)CONTROL_CORE_RELIEF_PULSE_MAX_MS) {
            pulse = (float)CONTROL_CORE_RELIEF_PULSE_MAX_MS;
        }
        cc->relief_pulse_ms = (uint32_t)pulse;
        cc->relief_open = 1u;
        cc->relief_on_ms = 0u;
    } else {
        /* dentro da faixa: nada a fazer */
    }

    return cc->relief_open;
}

/* 1 = pulso de alivio em andamento ou pressao ainda acomodando depois dele
 * (leitura distorcida pela reequalizacao da tubulacao). */
static uint8_t relief_window_active(const ControlCore_t *cc)
{
    return ((cc->relief_open != 0u) || (cc->relief_off_ms < CONTROL_CORE_RELIEF_SETTLE_MS)) ? 1u : 0u;
}

/* Fecha a valvula de alivio fora da regulacao; a proxima abertura ainda
 * espera CONTROL_CORE_RELIEF_SETTLE_MS. */
static void close_relief_valve(ControlCore_t *cc)
{
    if (cc->relief_open != 0u) {
        cc->relief_open = 0u;
        cc->relief_off_ms = 0u;
    }
}

/** @brief Atualiza a taxa de subida filtrada e decide se a aproximacao a
 *         100% continua (ver CONTROL_CORE_APPROACH_*). Chamado a cada passo
 *         de INFLATING/STABILIZING. */
static void update_fast_approach(ControlCore_t *cc, uint32_t dt_ms)
{
    SystemRuntime_t *rt = cc->runtime;
    float dt_s = (float)dt_ms / 1000.0f;
    float error_mmhg = rt->setpoint_mmhg - rt->pressure_display;
    float predicted;

    if (cc->rate_valid == 0u) {
        cc->p_filt_mmhg = rt->pressure_display;
        cc->rise_rate_mmhg_s = 0.0f;
        cc->rate_valid = 1u;
    } else if (dt_s > 0.0f) {
        float a_p = dt_s / (CONTROL_CORE_RATE_P_TAU_S + dt_s);
        float a_r = dt_s / (CONTROL_CORE_RATE_TAU_S + dt_s);
        float p_prev = cc->p_filt_mmhg;
        float raw_rate;

        cc->p_filt_mmhg += (rt->pressure_display - cc->p_filt_mmhg) * a_p;
        raw_rate = (cc->p_filt_mmhg - p_prev) / dt_s;
        cc->rise_rate_mmhg_s += (raw_rate - cc->rise_rate_mmhg_s) * a_r;
    } else {
        /* dt = 0: nada a integrar */
    }

    predicted = rt->pressure_display;
    if (cc->rise_rate_mmhg_s > 0.0f) {
        predicted += cc->rise_rate_mmhg_s * CONTROL_CORE_APPROACH_LOOKAHEAD_S;
    }
    cc->pid_diag.rise_rate_mmhg_s = cc->rise_rate_mmhg_s;
    cc->pid_diag.predicted_mmhg = predicted;

    if (cc->fast_approach != 0u) {
        if (predicted >= (rt->setpoint_mmhg - CONTROL_CORE_APPROACH_ZONE_MMHG)) {
            /* Entra na regulacao: PID parte sem integral acumulada durante
             * a subida e sem "chute" de derivada. */
            cc->fast_approach = 0u;
            cc->rearm_elapsed_ms = 0u;
            cc->pid_integral = 0.0f;
            cc->pid_prev_error = error_mmhg / CONTROL_CORE_MMHG_PER_KPA;
        }
    } else {
        if (error_mmhg > CONTROL_CORE_APPROACH_REARM_MMHG) {
            cc->rearm_elapsed_ms += dt_ms;
            if (cc->rearm_elapsed_ms >= CONTROL_CORE_APPROACH_REARM_MS) {
                cc->fast_approach = 1u;
                cc->rearm_elapsed_ms = 0u;
            }
        } else {
            cc->rearm_elapsed_ms = 0u;
        }
    }

    cc->pid_diag.fast_approach = cc->fast_approach;
}

/** @brief Bomba a 100%, valvulas fechadas (aproximacao rapida). */
static void run_fast_approach(ControlCore_t *cc, ControlCore_Actuators_t *out)
{
    close_relief_valve(cc);
    cc->pid_diag.pump_power_final = 1.0f;
    cc->pid_diag.relief_active = 0u;
    cc->pid_diag.floor_applied = 0u;
    safe_actuators(out, 1.0f, 0u, 0u, 0u);
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

    /* Integrador congelado durante pulso/acomodacao do alivio: a queda
     * falsa vista pelo sensor nessa janela enchia o integral e a bomba
     * empurrava a pressao acima do setpoint na reequalizacao. */
    if (relief_window_active(cc) == 0u) {
        cc->pid_integral += pid_cfg->ki * error * dt_s;
        if (cc->pid_integral > pid_cfg->i_max) { cc->pid_integral = pid_cfg->i_max; }
        if (cc->pid_integral < pid_cfg->i_min) { cc->pid_integral = pid_cfg->i_min; }
    }

    derivative = (dt_s > 0.0f) ? ((error - cc->pid_prev_error) / dt_s) : 0.0f;
    d_term = pid_cfg->kd * derivative / 10.0f;
    cc->pid_prev_error = error;

    ff_term = pid_cfg->ff * setpoint_kpa;

    output = ff_term + p_term + cc->pid_integral + d_term;
    pump_power = (pid_cfg->out_max > 0.0f) ? (output / pid_cfg->out_max) : output;
    if (pump_power < 0.0f) { pump_power = 0.0f; }
    if (pump_power > 1.0f) { pump_power = 1.0f; }

    /* Diagnostico (ver ControlCore_PidDiag_t) — capturado AQUI, antes do
     * piso de partida/alivio mexerem em pump_power, pra' mostrar o que o
     * PID "pediu" de verdade antes de qualquer correcao. */
    cc->pid_diag.error_mmhg = error * CONTROL_CORE_MMHG_PER_KPA;
    cc->pid_diag.p_term_kpa = p_term;
    cc->pid_diag.i_term_kpa = cc->pid_integral;
    cc->pid_diag.d_term_kpa = d_term;
    cc->pid_diag.ff_term_kpa = ff_term;
    cc->pid_diag.output_kpa = output;
    cc->pid_diag.pump_power_pid = pump_power;
    cc->pid_diag.floor_applied = 0u;

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
        cc->pid_diag.floor_applied = 1u;
    }

    /* Acima do alvo a bomba nao ajuda em nada: corta. Quem tira o excesso
     * e' a valvula de alivio (com histerese, abaixo) ou o proprio
     * vazamento natural do manguito, se o excesso for pequeno. */
    {
        float over_filt_mmhg = cc->p_filt_mmhg - cc->runtime->setpoint_mmhg;

        if (over_filt_mmhg > 0.0f) {
            pump_power = 0.0f;
        }
        relief_on = update_relief_valve(cc, over_filt_mmhg, in->dt_ms);
    }
    if (relief_window_active(cc) != 0u) {
        pump_power = 0.0f; /* pulso ou acomodacao: bomba fora */
    }

    cc->pid_diag.pump_power_final = pump_power;
    cc->pid_diag.relief_active = relief_on;

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
        safe_actuators(out, 0.0f, 1u, 1u, 1u); // V3 tambem — ver handle_deflating
        return;
    }

    update_fast_approach(cc, in->dt_ms);
    if (cc->fast_approach != 0u) {
        run_fast_approach(cc, out);
    } else {
        run_pid_iteration(cc, in, out);
    }

    error_mmhg = rt->setpoint_mmhg - rt->pressure_display;
    if (fabsf(error_mmhg) <= cc->cfg->stabilization.band_mmhg) {
        rt->state = SYSTEM_STATE_STABILIZING;
        cc->stabilizing_out_of_band_ms = 0u;
    }

    if (cc->inflate_elapsed_ms >= cc->safety.t_inflar_max_ms) {
        cc->last_error = CONTROL_ERR_E003_INFLATE_TIMEOUT;
        rt->state = SYSTEM_STATE_DEFLATING_FAST;
        safe_actuators(out, 0.0f, 1u, 1u, 1u); // V3 tambem — ver handle_deflating
    }
}

static void handle_stabilizing(ControlCore_t *cc, const ControlCore_Inputs_t *in,
                               ControlCore_Actuators_t *out)
{
    SystemRuntime_t *rt = cc->runtime;
    float error_mmhg;

    if (in->abort_request != 0u) {
        rt->state = SYSTEM_STATE_DEFLATING_FAST;
        safe_actuators(out, 0.0f, 1u, 1u, 1u); // V3 tambem — ver handle_deflating
        return;
    }

    update_fast_approach(cc, in->dt_ms);
    run_pid_iteration(cc, in, out);

    /* Histerese: entra com |erro| <= banda (handle_inflating), mas so' sai
     * com |erro| > 2x banda por CONTROL_CORE_STABILIZING_EXIT_MS seguidos —
     * a ondulacao do embolo cruzava a banda a cada curso e reiniciava a
     * contagem de estabilizacao sem parar. */
    error_mmhg = rt->setpoint_mmhg - rt->pressure_display;
    if (relief_window_active(cc) != 0u) {
        /* transitorio do pulso de alivio: leitura nao representa o manguito */
        cc->stabilizing_out_of_band_ms = 0u;
    } else if (fabsf(error_mmhg) > (2.0f * cc->cfg->stabilization.band_mmhg)) {
        cc->stabilizing_out_of_band_ms += in->dt_ms;
        if (cc->stabilizing_out_of_band_ms >= CONTROL_CORE_STABILIZING_EXIT_MS) {
            cc->stabilizing_out_of_band_ms = 0u;
            rt->state = SYSTEM_STATE_INFLATING;
            return;
        }
    } else {
        cc->stabilizing_out_of_band_ms = 0u;
    }

    /* So' estabiliza com a pressao DENTRO da banda e fora da janela do
     * alivio: a saida acima e' suspensa durante pulso/acomodacao, entao sem
     * esta condicao pulsos seguidos deixavam o tempo correr e o estado
     * virava STABILIZED muito acima do alvo. */
    if ((cc->state_elapsed_ms >= cc->cfg->stabilization.time_ms) &&
        (fabsf(error_mmhg) <= cc->cfg->stabilization.band_mmhg) &&
        (relief_window_active(cc) == 0u)) {
        rt->state = SYSTEM_STATE_STABILIZED;
    }

    if (cc->inflate_elapsed_ms >= cc->safety.t_inflar_max_ms) {
        cc->last_error = CONTROL_ERR_E003_INFLATE_TIMEOUT;
        rt->state = SYSTEM_STATE_DEFLATING_FAST;
        safe_actuators(out, 0.0f, 1u, 1u, 1u); // V3 tambem — ver handle_deflating
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
    close_relief_valve(cc);

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

    /* V3 agora tambem abre aqui (2026-10, pedido do usuario na bancada):
     * o prompt do projeto ja' previa isso ("V3 e' usada para desinflar o
     * manguito e para calibracao", secao 2.3) — a implementacao anterior
     * so' abria V1+V2, deixando a via extra de descarga de fora. */
    safe_actuators(out, 0.0f, 1u, 1u, 1u);

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

    /* Fase 5: estatisticas do ciclo (ver ControlCore_CycleStats_t). So'
     * acumula enquanto o ciclo automatico esta' ativo (saiu de IDLE, ainda
     * nao finalizou); congela ao voltar para IDLE.
     *
     * A checagem de finalizacao fica FORA do "if (rt->state != IDLE)" de
     * proposito: e' exatamente no tick em que rt->state VIRA IDLE que ela
     * precisa disparar — gate-la pelo estado atual (!= IDLE) faria o bloco
     * inteiro ser pulado bem no tick que importa, e o ciclo nunca
     * finalizaria (achado ao escrever o teste de host). */
    if (cc->cycle.finalized == 0u) {
        if (rt->state != SYSTEM_STATE_IDLE) {
            cc->cycle.total_elapsed_ms += in->dt_ms;

            if (rt->pressure_display > cc->cycle.peak_mmhg) {
                cc->cycle.peak_mmhg = rt->pressure_display;
            }

            if ((cc->cycle.reached_setpoint == 0u) &&
                ((rt->state == SYSTEM_STATE_STABILIZING) || (rt->state == SYSTEM_STATE_STABILIZED))) {
                cc->cycle.reached_setpoint = 1u;
                cc->cycle.inflate_time_ms = cc->cycle.total_elapsed_ms;
            }

            if (rt->state == SYSTEM_STATE_STABILIZED) {
                cc->cycle.plateau_time_ms += in->dt_ms;
                cc->cycle.plateau_sum_mmhg += rt->pressure_display;
                cc->cycle.plateau_sample_count++;
                if (rt->pressure_display < cc->cycle.plateau_min_mmhg) {
                    cc->cycle.plateau_min_mmhg = rt->pressure_display;
                }
                if (rt->pressure_display > cc->cycle.plateau_max_mmhg) {
                    cc->cycle.plateau_max_mmhg = rt->pressure_display;
                }
            }

            if ((state_before != SYSTEM_STATE_DEFLATING_FAST) && (rt->state == SYSTEM_STATE_DEFLATING_FAST)) {
                cc->cycle.deflate_start_ms = cc->cycle.total_elapsed_ms;
                /* NC07 so' se o pedal cortou o ensaio ANTES do plato: em
                 * STABILIZED ("pronto p/ desinflar") o pedal e' o fim
                 * NORMAL do ensaio, nao uma interrupcao (achado de bancada
                 * 2026-10-17: todo ensaio saia NC07). Plato curto demais e'
                 * avaliado a parte pelo NC do tempo de plato. */
                if ((in->abort_request != 0u) &&
                    ((state_before == SYSTEM_STATE_INFLATING) ||
                     (state_before == SYSTEM_STATE_STABILIZING))) {
                    cc->cycle.aborted_by_operator = 1u;
                }
            }
        }

        if ((state_before != SYSTEM_STATE_IDLE) && (rt->state == SYSTEM_STATE_IDLE)) {
            cc->cycle.deflate_time_ms = cc->cycle.total_elapsed_ms - cc->cycle.deflate_start_ms;
            cc->cycle.residual_mmhg = rt->pressure_display;
            cc->cycle.technical_fault = (cc->last_error != CONTROL_ERR_NONE) ? 1u : 0u;
            cc->cycle.finalized = 1u;
        }
    }
}
