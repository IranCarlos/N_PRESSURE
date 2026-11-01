/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-01
 */

#include "control_core.h"
#include <string.h>
#include <math.h>

/* Em regime a bomba ja' sobe a 15% (bancada 2026-10-31), mas as rajadas no
 * piso rendem melhor a 45% (manguito pequeno, ate' 150 mmHg). */
#define CONTROL_CORE_PUMP_MIN_EFFECTIVE     (0.45f)

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
/* Curva de bancada 2026-10-31 (manguito grande): V2 tira ~0,03 mmHg/ms depois
 * de ~25 ms de atraso; o menor pulso da V1 ja' tira ~8 mmHg, mais que a banda.
 * Por isso a regulacao fina e' na V2 e a V1 fica para excesso grande. */
#define CONTROL_CORE_RELIEF_MS_PER_MMHG     (30.0f)
#define CONTROL_CORE_RELIEF_COARSE_MMHG     (10.0f)
#define CONTROL_CORE_RELIEF_DEAD_MS         (25u)
#define CONTROL_CORE_RELIEF_PULSE_MIN_MS    (20u)
#define CONTROL_CORE_RELIEF_PULSE_MAX_MS    (300u)
#define CONTROL_CORE_RELIEF_SETTLE_MS       (400u)

/* STABILIZING so' volta para INFLATING se ficar fora de 2x a banda por
 * este tempo seguido (mesmo criterio de histerese de STABILIZED). */
#define CONTROL_CORE_STABILIZING_EXIT_MS    (50u)

/*
 * Regulacao por velocidade (2026-10-31, manguito pequeno instavel na bancada).
 * Em vez de calcular a potencia direto do erro de pressao:
 *     velocidade pedida = erro / RATE_TIME (limitada a RATE_MAX), e a
 *     potencia e' ajustada ate' a subida medida igualar a pedida.
 * O ganho e' normalizado pela subida medida a 100% (pump_authority), que ja'
 * embute o volume do manguito: nao ha' configuracao por manguito. Abaixo do
 * piso da bomba a potencia vira rajadas no piso (BURST_MIN_ON_MS e' a menor
 * rajada). Valores iniciais, a confirmar com as curvas reais da bancada.
 */
#define CONTROL_CORE_RATE_CTRL_ENABLE       (1u)
#define CONTROL_CORE_RATE_TIME_S            (1.0f)
#define CONTROL_CORE_RATE_MAX_MMHG_S        (20.0f)
#define CONTROL_CORE_RATE_TI_S              (0.5f)
#define CONTROL_CORE_RATE_DEADBAND_FRACTION (0.5f)
/* Bomba de embolo: rajada curta pode nao completar o curso e nao render nada
 * (bancada: 120 ms falha as vezes, 250 ms nao); no manguito pequeno 250 ms
 * seria demais. A duracao aprende entre estes limites, pelo ganho medido. */
#define CONTROL_CORE_BURST_MIN_ON_MS        (60u)
#define CONTROL_CORE_BURST_MAX_ON_MS        (300u)
#define CONTROL_CORE_BURST_GAIN_MIN_MMHG    (0.5f)
#define CONTROL_CORE_BURST_GAIN_TARGET_MMHG (1.5f)
#define CONTROL_CORE_BURST_GROW             (2.0f)
#define CONTROL_CORE_BURST_SHRINK_MIN       (0.3f)
/* Antes da 1a medida da subida a 100% (ex.: alvo logo acima da pressao). */
#define CONTROL_CORE_AUTHORITY_DEFAULT_MMHG_S (50.0f)
/* Aprendizado do pulso de alivio: cada pulso compara a queda real com a
 * pedida. Limites contra uma medida ruim (ex.: vazamento na acomodacao). */
#define CONTROL_CORE_RELIEF_LEARN_GAIN      (0.5f)
#define CONTROL_CORE_RELIEF_LEARN_MIN_DROP_MMHG (0.5f)
#define CONTROL_CORE_RELIEF_LEARN_MIN_FACTOR (0.1f)
#define CONTROL_CORE_RELIEF_LEARN_MAX_FACTOR (5.0f)
/* valv.ms_por_mmhg vale para o manguito grande (~11 mmHg/s a 100%). Num manguito
 * rapido a mesma abertura tira muito mais: 225 ms na V2 derrubou 40 mmHg no
 * pequeno (bancada 2026-10-31). A partida escala pela subida medida; errar para
 * menos so' pede outro pulso. Autoridade fora de 1/3..3x da anterior = outro
 * manguito (ou faixa da curva): recomeca o aprendizado. */
#define CONTROL_CORE_RELIEF_REF_AUTHORITY_MMHG_S (11.0f)
#define CONTROL_CORE_CUFF_CHANGE_RATIO      (3.0f)
/* Manutencao: repoe o vazamento natural (bancada: ~0,6 mmHg/s) com rajadas,
 * esperando a leitura assentar. As rajadas escondem a queda da pressao, entao
 * o E013 soma so' o que cai com a bomba parada entre elas e compara com
 * dp_vazamento na janela. HOLD_MAX_LOW_EXITS saidas por baixo da banda no
 * mesmo ciclo tambem sao E013: a reposicao nao esta' dando conta. */
#define CONTROL_CORE_HOLD_SETTLE_MS         (400u)
/* A referencia da queda espera o ar se redistribuir apos a rajada (bancada:
 * ~1 s); medir antes conta a acomodacao como vazamento. Vazamento forte nao
 * deixa assentar: rajadas demais na janela tambem sao E013. */
#define CONTROL_CORE_HOLD_REF_DELAY_MS      (1000u)
#define CONTROL_CORE_HOLD_MAX_BURSTS        (7u)
#define CONTROL_CORE_HOLD_MAX_LOW_EXITS     (4u)
/* A leitura salta com o fluxo da bomba; a banda de conformidade e' +-2% do alvo.
 * A reposicao usa so' a forca que a pressao pede: bomba.minimo vale em
 * HOLD_POWER_REF e cai ate' HOLD_POWER_MIN nos alvos baixos. */
#define CONTROL_CORE_HOLD_POWER_REF_MMHG    (150.0f)
#define CONTROL_CORE_HOLD_POWER_MIN         (0.20f)
/* Banda de controle: 1,5% do alvo (margem dentro dos +-2% de conformidade),
 * limitada por estab.banda. */
#define CONTROL_CORE_BAND_RATIO             (0.015f)
/* Conformidade do plato (decisao do usuario, 2026-10-31): o limite vale para a
 * pressao no manguito. A estatistica ignora este tempo apos cada rajada, em que
 * o sensor ve o salto da tubulacao. A confirmar com manometro no manguito. */
#define CONTROL_CORE_PLATEAU_SETTLE_MS      (350u)
/* Decisoes do plato usam esta media: com o manguito num membro o pulso arterial
 * dispararia a reposicao em cada vale e, sem valvula aberta, a pressao so' sobe.
 * Atenua ~5x um pulso de 75 bpm. P_MAX_ABS continua na leitura bruta. */
#define CONTROL_CORE_HOLD_MEAN_TAU_S        (0.6f)

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

uint8_t ControlCore_OverPressure(const ControlCore_SafetyConfig_t *safety, float pressure_kpa)
{
    return ((pressure_kpa * CONTROL_CORE_MMHG_PER_KPA) > safety->p_max_abs_mmhg) ? 1u : 0u;
}

ControlCore_ManualVerdict_t ControlCore_CheckManual(const ControlCore_SafetyConfig_t *safety,
                                                    float pressure_kpa, uint8_t sensor_ok,
                                                    uint32_t ms_since_heartbeat,
                                                    uint8_t local_stop)
{
    if (ControlCore_OverPressure(safety, pressure_kpa) != 0u) {
        return CONTROL_MANUAL_OVERPRESSURE;
    }
    if (local_stop != 0u) {
        return CONTROL_MANUAL_LOCAL_STOP;
    }
    if (ms_since_heartbeat > CONTROL_CORE_MANUAL_HEARTBEAT_TIMEOUT_MS) {
        return CONTROL_MANUAL_HEARTBEAT_LOST;
    }
    if (sensor_ok == 0u) {
        return CONTROL_MANUAL_SENSOR_INVALID;
    }
    return CONTROL_MANUAL_OK;
}

void ControlCore_DefaultTuning(ControlCore_TuningConfig_t *t)
{
    t->pump_min_effective  = CONTROL_CORE_PUMP_MIN_EFFECTIVE;
    t->approach_lookahead_s = CONTROL_CORE_APPROACH_LOOKAHEAD_S;
    t->approach_zone_mmhg  = CONTROL_CORE_APPROACH_ZONE_MMHG;
    t->approach_rearm_mmhg = CONTROL_CORE_APPROACH_REARM_MMHG;
    t->approach_rearm_ms   = CONTROL_CORE_APPROACH_REARM_MS;
    t->rate_p_tau_s        = CONTROL_CORE_RATE_P_TAU_S;
    t->rate_tau_s          = CONTROL_CORE_RATE_TAU_S;
    t->relief_on_fraction  = CONTROL_CORE_RELIEF_ON_FRACTION;
    t->relief_ms_per_mmhg  = CONTROL_CORE_RELIEF_MS_PER_MMHG;
    t->relief_pulse_min_ms = CONTROL_CORE_RELIEF_PULSE_MIN_MS;
    t->relief_pulse_max_ms = CONTROL_CORE_RELIEF_PULSE_MAX_MS;
    t->relief_settle_ms    = CONTROL_CORE_RELIEF_SETTLE_MS;
    t->stabilizing_exit_ms = CONTROL_CORE_STABILIZING_EXIT_MS;
    t->rate_ctrl_enable    = CONTROL_CORE_RATE_CTRL_ENABLE;
    t->rate_time_s         = CONTROL_CORE_RATE_TIME_S;
    t->rate_max_mmhg_s     = CONTROL_CORE_RATE_MAX_MMHG_S;
    t->rate_ti_s           = CONTROL_CORE_RATE_TI_S;
    t->rate_deadband_fraction = CONTROL_CORE_RATE_DEADBAND_FRACTION;
    t->burst_min_on_ms     = CONTROL_CORE_BURST_MIN_ON_MS;
    t->burst_max_on_ms     = CONTROL_CORE_BURST_MAX_ON_MS;
    t->relief_learn_gain   = CONTROL_CORE_RELIEF_LEARN_GAIN;
    t->relief_coarse_mmhg  = CONTROL_CORE_RELIEF_COARSE_MMHG;
    t->relief_dead_ms      = CONTROL_CORE_RELIEF_DEAD_MS;
    t->hold_settle_ms      = CONTROL_CORE_HOLD_SETTLE_MS;
}

void ControlCore_Init(ControlCore_t *cc, SystemConfig_t *cfg, SystemRuntime_t *runtime,
                      const ControlCore_SafetyConfig_t *safety)
{
    memset(cc, 0, sizeof(*cc));
    cc->cfg = cfg;
    cc->runtime = runtime;
    cc->safety = *safety;
    ControlCore_DefaultTuning(&cc->tuning);
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
        cc->relief_off_ms = cc->tuning.relief_settle_ms; /* 1a abertura liberada */
        cc->relief_pulse_ms = 0u;
        cc->relief_measure_pending = 0u;

        cc->rate_integral = 0.0f;
        cc->pump_authority_mmhg_s = 0.0f;
        cc->burst_credit_ms = 0.0f;
        cc->burst_on_ms = 0u;
        cc->stabilized_out_of_band_ms = 0u;
        cc->hold_low_exits = 0u;
        cc->hold_recover_ms = 0u;
        cc->reg_pump_off_ms = 0u;
        cc->cancel_pending = 0u;

        cc->plateau_bin_sum = 0.0f;
        cc->plateau_bin_ms = 0u;
        cc->plateau_win_sum = 0.0f;
        cc->plateau_win_idx = 0u;
        cc->plateau_win_count = 0u;

        memset(&cc->cycle, 0, sizeof(cc->cycle));
        cc->cycle.plateau_min_mmhg = 1.0e9f;   /* sentinela: nenhuma amostra ainda */
        cc->cycle.plateau_max_mmhg = -1.0e9f;
    }
}

/* Minimo/maximo do plato sobre a media movel de 1 batimento, nao sobre a
 * amostra bruta: com o manguito num membro a leitura oscila com a pressao
 * arterial (+-1 mmHg medido na perna) e isso nao e' instabilidade do ensaio.
 * Uma queda real continua aparecendo: ela desloca a media. */
static void plateau_track_band(ControlCore_t *cc, float pressure_mmhg, uint32_t dt_ms)
{
    float mean_mmhg;

    cc->plateau_bin_sum += pressure_mmhg * (float)dt_ms;
    cc->plateau_bin_ms += dt_ms;
    if (cc->plateau_bin_ms < CONTROL_CORE_PLATEAU_BIN_MS) {
        return;
    }

    if (cc->plateau_win_count >= CONTROL_CORE_PLATEAU_WIN_BINS) {
        cc->plateau_win_sum -= cc->plateau_win[cc->plateau_win_idx];
    } else {
        cc->plateau_win_count++;
    }
    cc->plateau_win[cc->plateau_win_idx] = cc->plateau_bin_sum / (float)cc->plateau_bin_ms;
    cc->plateau_win_sum += cc->plateau_win[cc->plateau_win_idx];
    cc->plateau_win_idx = (uint8_t)((cc->plateau_win_idx + 1u) % CONTROL_CORE_PLATEAU_WIN_BINS);
    cc->plateau_bin_sum = 0.0f;
    cc->plateau_bin_ms = 0u;

    if (cc->plateau_win_count >= CONTROL_CORE_PLATEAU_WIN_BINS) {
        mean_mmhg = cc->plateau_win_sum / (float)CONTROL_CORE_PLATEAU_WIN_BINS;
        if (mean_mmhg < cc->cycle.plateau_min_mmhg) {
            cc->cycle.plateau_min_mmhg = mean_mmhg;
        }
        if (mean_mmhg > cc->cycle.plateau_max_mmhg) {
            cc->cycle.plateau_max_mmhg = mean_mmhg;
        }
        if (mean_mmhg > cc->cycle.peak_mmhg) {
            cc->cycle.peak_mmhg = mean_mmhg;
        }
    }
}

void ControlCore_RequestCancel(ControlCore_t *cc)
{
    cc->cancel_pending = 1u;
}

const ControlCore_CycleStats_t *ControlCore_GetCycleStats(const ControlCore_t *cc)
{
    return &cc->cycle;
}

const ControlCore_PidDiag_t *ControlCore_GetPidDiag(const ControlCore_t *cc)
{
    return &cc->pid_diag;
}

static float control_band(const ControlCore_t *cc)
{
    float band = cc->cfg->stabilization.band_mmhg;

    if (cc->tuning.rate_ctrl_enable != 0u) {
        float scaled = cc->runtime->setpoint_mmhg * CONTROL_CORE_BAND_RATIO;

        if (scaled < band) { band = scaled; }
    }
    return band;
}

/* Partida do aprendizado para o manguito medido neste ciclo. */
static void relief_seed_for_cuff(ControlCore_t *cc)
{
    float seed = cc->tuning.relief_ms_per_mmhg;

    if (cc->pump_authority_mmhg_s > CONTROL_CORE_RELIEF_REF_AUTHORITY_MMHG_S) {
        seed *= CONTROL_CORE_RELIEF_REF_AUTHORITY_MMHG_S / cc->pump_authority_mmhg_s;
    }
    cc->relief_learned_ms_per_mmhg = seed;
    cc->relief_learn_seed = cc->tuning.relief_ms_per_mmhg;
    cc->cuff_authority_mmhg_s = cc->pump_authority_mmhg_s;
}

/* Na troca para a regulacao: o manguito mudou? */
static void cuff_check_change(ControlCore_t *cc)
{
    float known = cc->cuff_authority_mmhg_s;
    float now = cc->pump_authority_mmhg_s;

    if ((now > 0.0f) && ((known <= 0.0f) ||
                         (now > (known * CONTROL_CORE_CUFF_CHANGE_RATIO)) ||
                         (known > (now * CONTROL_CORE_CUFF_CHANGE_RATIO)))) {
        relief_seed_for_cuff(cc);
    }
}

/* Volta a' partida na 1a vez ou se o parametro mudou pelo app DEV. */
static void relief_sync_learned(ControlCore_t *cc)
{
    if ((cc->relief_learned_ms_per_mmhg <= 0.0f) ||
        (cc->relief_learn_seed != cc->tuning.relief_ms_per_mmhg)) {
        relief_seed_for_cuff(cc);
    }
    cc->pid_diag.relief_ms_per_mmhg = cc->relief_learned_ms_per_mmhg;
}

/* Ao fim da acomodacao: quantos ms de pulso custou cada mmHg neste manguito. */
static void relief_learn(ControlCore_t *cc)
{
    float drop = cc->relief_p_before_mmhg - cc->p_filt_mmhg;

    cc->relief_measure_pending = 0u;
    if ((cc->relief_coarse == 0u) && (drop >= CONTROL_CORE_RELIEF_LEARN_MIN_DROP_MMHG) &&
        (cc->relief_pulse_ms > cc->tuning.relief_dead_ms)) {
        float seen = (float)(cc->relief_pulse_ms - cc->tuning.relief_dead_ms) / drop;
        float low = cc->tuning.relief_ms_per_mmhg * CONTROL_CORE_RELIEF_LEARN_MIN_FACTOR;
        float high = cc->tuning.relief_ms_per_mmhg * CONTROL_CORE_RELIEF_LEARN_MAX_FACTOR;

        cc->relief_learned_ms_per_mmhg +=
            (seen - cc->relief_learned_ms_per_mmhg) * cc->tuning.relief_learn_gain;
        if (cc->relief_learned_ms_per_mmhg < low) { cc->relief_learned_ms_per_mmhg = low; }
        if (cc->relief_learned_ms_per_mmhg > high) { cc->relief_learned_ms_per_mmhg = high; }
    }
}

/** @brief Valvula de alivio por pulso + acomodacao (ver constantes
 *         CONTROL_CORE_RELIEF_*). `over_mmhg` = pressao filtrada - setpoint.
 *         Devolve 1 se a valvula deve ficar aberta neste passo. */
static uint8_t update_relief_valve(ControlCore_t *cc, float over_mmhg, uint32_t dt_ms)
{
    float on_threshold = cc->tuning.relief_on_fraction * control_band(cc);

    relief_sync_learned(cc);

    if (cc->relief_open != 0u) {
        cc->relief_on_ms += dt_ms;
        if (cc->relief_on_ms >= cc->relief_pulse_ms) {
            cc->relief_open = 0u;
            cc->relief_off_ms = 0u;
        }
    } else if (cc->relief_off_ms < cc->tuning.relief_settle_ms) {
        cc->relief_off_ms += dt_ms; /* acomodando: leitura nao confiavel */
    } else if (cc->relief_measure_pending != 0u) {
        relief_learn(cc); /* acomodou: reavalia no proximo passo com o novo valor */
    } else if (over_mmhg >= on_threshold) {
        float pulse = (float)cc->tuning.relief_dead_ms +
                      (over_mmhg * cc->relief_learned_ms_per_mmhg);

        if (pulse < (float)cc->tuning.relief_pulse_min_ms) {
            pulse = (float)cc->tuning.relief_pulse_min_ms;
        }
        if (pulse > (float)cc->tuning.relief_pulse_max_ms) {
            pulse = (float)cc->tuning.relief_pulse_max_ms;
        }
        cc->relief_coarse = (over_mmhg >= cc->tuning.relief_coarse_mmhg) ? 1u : 0u;
        if (cc->relief_coarse != 0u) {
            pulse = (float)cc->tuning.relief_pulse_min_ms; /* V1: o minimo ja' tira muito */
        }
        cc->relief_pulse_ms = (uint32_t)pulse;
        cc->relief_open = 1u;
        cc->relief_on_ms = 0u;
        cc->relief_p_before_mmhg = cc->p_filt_mmhg;
        cc->relief_measure_pending = 1u;
    } else {
        /* dentro da faixa: nada a fazer */
    }

    return cc->relief_open;
}

/* 1 = pulso de alivio em andamento ou pressao ainda acomodando depois dele
 * (leitura distorcida pela reequalizacao da tubulacao). */
static uint8_t relief_window_active(const ControlCore_t *cc)
{
    return ((cc->relief_open != 0u) || (cc->relief_off_ms < cc->tuning.relief_settle_ms)) ? 1u : 0u;
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

/* Pressao filtrada e taxa de subida (tambem em STABILIZED, para a saida do
 * estado nao reagir a' ondulacao do embolo). */
static void update_rate_filter(ControlCore_t *cc, uint32_t dt_ms)
{
    SystemRuntime_t *rt = cc->runtime;
    float dt_s = (float)dt_ms / 1000.0f;

    if (cc->rate_valid == 0u) {
        cc->p_filt_mmhg = rt->pressure_display;
        cc->rise_rate_mmhg_s = 0.0f;
        cc->rate_valid = 1u;
    } else if (dt_s > 0.0f) {
        float a_p = dt_s / (cc->tuning.rate_p_tau_s + dt_s);
        float a_r = dt_s / (cc->tuning.rate_tau_s + dt_s);
        float p_prev = cc->p_filt_mmhg;
        float raw_rate;

        cc->p_filt_mmhg += (rt->pressure_display - cc->p_filt_mmhg) * a_p;
        raw_rate = (cc->p_filt_mmhg - p_prev) / dt_s;
        cc->rise_rate_mmhg_s += (raw_rate - cc->rise_rate_mmhg_s) * a_r;
    } else {
        /* dt = 0: nada a integrar */
    }
}

/** @brief Atualiza a taxa de subida filtrada e decide se a aproximacao a
 *         100% continua (ver CONTROL_CORE_APPROACH_*). Chamado a cada passo
 *         de INFLATING/STABILIZING. */
static void update_fast_approach(ControlCore_t *cc, uint32_t dt_ms)
{
    SystemRuntime_t *rt = cc->runtime;
    float error_mmhg = rt->setpoint_mmhg - rt->pressure_display;
    float predicted;

    update_rate_filter(cc, dt_ms);

    /* A 100%, a taxa ATUAL e' a autoridade da bomba neste manguito. Nao o
     * pico: na bancada a subida cai de ~60 para ~11 mmHg/s perto de 70 mmHg. */
    if ((cc->fast_approach != 0u) && (cc->rise_rate_mmhg_s > 0.0f)) {
        cc->pump_authority_mmhg_s = cc->rise_rate_mmhg_s;
    }
    cc->pid_diag.pump_authority_mmhg_s = cc->pump_authority_mmhg_s;

    predicted = rt->pressure_display;
    if (cc->rise_rate_mmhg_s > 0.0f) {
        predicted += cc->rise_rate_mmhg_s * cc->tuning.approach_lookahead_s;
    }
    cc->pid_diag.rise_rate_mmhg_s = cc->rise_rate_mmhg_s;
    cc->pid_diag.predicted_mmhg = predicted;

    if (cc->fast_approach != 0u) {
        if (predicted >= (rt->setpoint_mmhg - cc->tuning.approach_zone_mmhg)) {
            /* Entra na regulacao: PID parte sem integral acumulada durante
             * a subida e sem "chute" de derivada. */
            cc->fast_approach = 0u;
            cc->rearm_elapsed_ms = 0u;
            cc->pid_integral = 0.0f;
            cc->pid_prev_error = error_mmhg / CONTROL_CORE_MMHG_PER_KPA;
            cuff_check_change(cc);
            cc->rate_integral = 1.0f; /* continua de onde a bomba estava */
            cc->burst_credit_ms = 0.0f;
            cc->burst_on_ms = 0u;
        }
    } else {
        if (error_mmhg > cc->tuning.approach_rearm_mmhg) {
            cc->rearm_elapsed_ms += dt_ms;
            if (cc->rearm_elapsed_ms >= cc->tuning.approach_rearm_ms) {
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

static uint32_t burst_length_ms(ControlCore_t *cc)
{
    float low = (float)cc->tuning.burst_min_on_ms;
    float high = (float)cc->tuning.burst_max_on_ms;

    if (high < low) { high = low; }
    if (cc->burst_learned_ms < low) { cc->burst_learned_ms = low; }
    if (cc->burst_learned_ms > high) { cc->burst_learned_ms = high; }
    return (uint32_t)cc->burst_learned_ms;
}

static float hold_power(const ControlCore_t *cc);

static float burst_tick(ControlCore_t *cc, float floor_power, uint32_t dt_ms)
{
    uint32_t used = (dt_ms < cc->burst_on_ms) ? dt_ms : cc->burst_on_ms;

    cc->burst_on_ms -= used;
    cc->burst_credit_ms -= (float)used;
    if (cc->burst_credit_ms < 0.0f) { cc->burst_credit_ms = 0.0f; }
    return floor_power;
}

/* Potencia abaixo do piso da bomba vira rajadas no piso: a media no tempo e'
 * a pedida e cada rajada dura ao menos burst_min_on_ms. */
static float burst_modulate(ControlCore_t *cc, float power, uint32_t dt_ms)
{
    float floor_power = cc->tuning.pump_min_effective;
    /* Perto do alvo a rajada usa a potencia do plato: no piso, a 60 mmHg, cada uma
     * saltava ~7 mmHg na leitura e estourava o pico (bancada 2026-10-31). */
    float burst_power = (cc->runtime->state == SYSTEM_STATE_STABILIZING) ? hold_power(cc) : floor_power;

    if (power >= floor_power) {
        cc->burst_credit_ms = 0.0f;
        cc->burst_on_ms = 0u;
        return power;
    }
    if ((power <= 0.0f) || (floor_power <= 0.0f)) {
        cc->burst_credit_ms = 0.0f;
        return (cc->burst_on_ms > 0u) ? burst_tick(cc, burst_power, dt_ms) : 0.0f;
    }

    cc->burst_credit_ms += (power / floor_power) * (float)dt_ms;
    if ((cc->burst_on_ms == 0u) && (cc->burst_credit_ms >= (float)burst_length_ms(cc))) {
        cc->burst_on_ms = burst_length_ms(cc);
    }
    return (cc->burst_on_ms > 0u) ? burst_tick(cc, burst_power, dt_ms) : 0.0f;
}

/** @brief Regulacao por velocidade + alivio (ver CONTROL_CORE_RATE_*). */
static void run_rate_regulation(ControlCore_t *cc, const ControlCore_Inputs_t *in,
                                ControlCore_Actuators_t *out)
{
    const ControlCore_TuningConfig_t *t = &cc->tuning;
    float dt_s = (float)in->dt_ms / 1000.0f;
    float setpoint = cc->runtime->setpoint_mmhg;
    float error_mmhg = setpoint - cc->p_filt_mmhg;
    float deadband = t->rate_deadband_fraction * control_band(cc);
    float authority = (cc->pump_authority_mmhg_s > 1.0f)
        ? cc->pump_authority_mmhg_s : CONTROL_CORE_AUTHORITY_DEFAULT_MMHG_S;
    float rate_ref = (t->rate_time_s > 0.0f) ? (error_mmhg / t->rate_time_s) : 0.0f;
    float rate_error;
    float power;
    uint8_t relief_on;

    if (rate_ref < 0.0f) { rate_ref = 0.0f; }
    if (rate_ref > t->rate_max_mmhg_s) { rate_ref = t->rate_max_mmhg_s; }

    rate_error = (rate_ref - cc->rise_rate_mmhg_s) / authority;
    relief_on = update_relief_valve(cc, cc->p_filt_mmhg - setpoint, in->dt_ms);

    if ((relief_window_active(cc) != 0u) || (error_mmhg <= deadband)) {
        /* Chegou (ou esta' aliviando): sem bomba e sem acumular integral. */
        cc->rate_integral = 0.0f;
        power = 0.0f;
        if (relief_on != 0u) {
            cc->burst_on_ms = 0u; /* a rajada nao briga com a valvula aberta */
        }
    } else {
        if (t->rate_ti_s > 0.0f) {
            cc->rate_integral += rate_error * dt_s / t->rate_ti_s;
        }
        if (cc->rate_integral > 1.0f) { cc->rate_integral = 1.0f; }
        if (cc->rate_integral < 0.0f) { cc->rate_integral = 0.0f; }
        power = cc->rate_integral + rate_error;
        if (power > 1.0f) { power = 1.0f; }
        if (power < 0.0f) { power = 0.0f; }
    }

    cc->pid_diag.error_mmhg = error_mmhg;
    cc->pid_diag.rate_ref_mmhg_s = rate_ref;
    cc->pid_diag.pump_power_pid = power;
    power = burst_modulate(cc, power, in->dt_ms);
    cc->pid_diag.pump_power_final = power;
    cc->pid_diag.floor_applied = 0u;
    cc->pid_diag.relief_active = relief_on;

    if (relief_on != 0u) {
        safe_actuators(out, power, cc->relief_coarse, (cc->relief_coarse == 0u) ? 1u : 0u, 0u);
    } else {
        safe_actuators(out, power, 0u, 0u, 0u);
    }
}

static float hold_power(const ControlCore_t *cc)
{
    float power = cc->tuning.pump_min_effective *
                  (cc->runtime->setpoint_mmhg / CONTROL_CORE_HOLD_POWER_REF_MMHG);

    if (power < CONTROL_CORE_HOLD_POWER_MIN) { power = CONTROL_CORE_HOLD_POWER_MIN; }
    if (power > cc->tuning.pump_min_effective) { power = cc->tuning.pump_min_effective; }
    return power;
}

/* Rajada minima de reposicao na manutencao, quando a pressao assentada cai
 * abaixo da zona morta. Devolve a potencia do passo. */
static float run_hold_topup(ControlCore_t *cc, float error_mmhg, uint32_t dt_ms)
{
    float deadband = cc->tuning.rate_deadband_fraction * control_band(cc);

    if (cc->burst_on_ms > 0u) {
        cc->hold_since_burst_ms = 0u;
        return burst_tick(cc, hold_power(cc), dt_ms);
    }
    cc->hold_since_burst_ms += dt_ms;
    if ((cc->hold_ref_valid == 0u) && (cc->hold_since_burst_ms >= CONTROL_CORE_HOLD_REF_DELAY_MS)) {
        cc->hold_ref_mmhg = cc->hold_mean_mmhg;
        cc->hold_ref_valid = 1u;
    }
    if (cc->hold_wait_ms > dt_ms) {
        cc->hold_wait_ms -= dt_ms;
        return 0.0f;
    }
    cc->hold_wait_ms = 0u;
    if (cc->hold_gain_pending != 0u) {
        float gain = cc->p_filt_mmhg - cc->hold_p_before_mmhg;

        cc->hold_gain_pending = 0u;
        if (gain < CONTROL_CORE_BURST_GAIN_MIN_MMHG) {
            cc->burst_learned_ms *= CONTROL_CORE_BURST_GROW;
        } else if (gain > control_band(cc)) {
            float shrink = CONTROL_CORE_BURST_GAIN_TARGET_MMHG / gain;

            if (shrink < CONTROL_CORE_BURST_SHRINK_MIN) { shrink = CONTROL_CORE_BURST_SHRINK_MIN; }
            cc->burst_learned_ms *= shrink;
        } else {
            /* rajada na medida */
        }
    }
    if (error_mmhg > deadband) {
        if ((cc->hold_ref_valid != 0u) && (cc->hold_ref_mmhg > cc->hold_mean_mmhg)) {
            cc->hold_drop_mmhg += cc->hold_ref_mmhg - cc->hold_mean_mmhg;
        }
        cc->hold_ref_valid = 0u;
        cc->hold_bursts++;
        cc->hold_gain_pending = 1u;
        cc->hold_p_before_mmhg = cc->p_filt_mmhg;
        cc->burst_on_ms = burst_length_ms(cc);
        cc->hold_wait_ms = cc->burst_on_ms + cc->tuning.hold_settle_ms;
    }
    return 0.0f;
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
    if ((pump_power > 0.0f) && (pump_power < cc->tuning.pump_min_effective) &&
        (error_mmhg > cc->cfg->stabilization.band_mmhg)) {
        pump_power = cc->tuning.pump_min_effective;
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

static void run_regulation(ControlCore_t *cc, const ControlCore_Inputs_t *in,
                           ControlCore_Actuators_t *out)
{
    if (cc->tuning.rate_ctrl_enable != 0u) {
        run_rate_regulation(cc, in, out);
        cc->reg_pump_off_ms = (out->pump_power > 0.0f) ? 0u : (cc->reg_pump_off_ms + in->dt_ms);
    } else {
        run_pid_iteration(cc, in, out);
    }
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
        run_regulation(cc, in, out);
    }

    error_mmhg = rt->setpoint_mmhg - rt->pressure_display;
    if (fabsf(error_mmhg) <= control_band(cc)) {
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
    run_regulation(cc, in, out);

    /* Histerese: entra com |erro| <= banda (handle_inflating), mas so' sai
     * com |erro| > 2x banda por CONTROL_CORE_STABILIZING_EXIT_MS seguidos —
     * a ondulacao do embolo cruzava a banda a cada curso e reiniciava a
     * contagem de estabilizacao sem parar. */
    error_mmhg = rt->setpoint_mmhg - rt->pressure_display;
    if (relief_window_active(cc) != 0u) {
        /* transitorio do pulso de alivio: leitura nao representa o manguito */
        cc->stabilizing_out_of_band_ms = 0u;
    } else if (fabsf(error_mmhg) > (2.0f * control_band(cc))) {
        cc->stabilizing_out_of_band_ms += in->dt_ms;
        if (cc->stabilizing_out_of_band_ms >= cc->tuning.stabilizing_exit_ms) {
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
    /* A leitura chega ao alvo antes do manguito (bancada 2026-10-31: caia ~2 mmHg
     * depois de parar a bomba e o plato comecava abaixo da banda). So' estabiliza
     * com a pressao na banda e a bomba parada ha' PLATEAU_SETTLE_MS. */
    if ((cc->state_elapsed_ms >= cc->cfg->stabilization.time_ms) &&
        (fabsf(error_mmhg) <= control_band(cc)) &&
        (relief_window_active(cc) == 0u) &&
        ((cc->tuning.rate_ctrl_enable == 0u) ||
         (cc->reg_pump_off_ms >= CONTROL_CORE_PLATEAU_SETTLE_MS))) {
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
    float hold_power = 0.0f;

    safe_actuators(out, 0.0f, 0u, 0u, 0u);
    close_relief_valve(cc);

    if (in->abort_request != 0u) {
        rt->state = SYSTEM_STATE_DEFLATING_FAST;
        return;
    }

    /* Sem pedal, botao ou PC ninguem encerraria o plato. Nao e' erro tecnico:
     * o ciclo termina e fica registrado como NC04 (plato acima do limite). */
    if (cc->cycle.plateau_time_ms >= cc->safety.t_plato_max_ms) {
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
    update_rate_filter(cc, in->dt_ms);
    cc->hold_mean_mmhg += (rt->pressure_display - cc->hold_mean_mmhg) *
        ((float)in->dt_ms / ((1000.0f * CONTROL_CORE_HOLD_MEAN_TAU_S) + (float)in->dt_ms));
    error_mmhg = rt->setpoint_mmhg - cc->hold_mean_mmhg;
    if (fabsf(error_mmhg) > (2.0f * control_band(cc))) {
        cc->stabilized_out_of_band_ms += in->dt_ms;
        if (cc->stabilized_out_of_band_ms >= cc->tuning.stabilizing_exit_ms) {
            cc->stabilized_out_of_band_ms = 0u;
            if ((cc->tuning.rate_ctrl_enable != 0u) && (error_mmhg > 0.0f)) {
                cc->hold_low_exits++;
            }
            if (cc->hold_low_exits >= CONTROL_CORE_HOLD_MAX_LOW_EXITS) {
                cc->last_error = CONTROL_ERR_E013_LEAK;
                rt->state = SYSTEM_STATE_DEFLATING_FAST;
            } else {
                rt->state = SYSTEM_STATE_INFLATING;
            }
            return;
        }
    } else {
        cc->stabilized_out_of_band_ms = 0u;
    }

    if (cc->tuning.rate_ctrl_enable != 0u) {
        hold_power = run_hold_topup(cc, error_mmhg, in->dt_ms);
        out->pump_power = hold_power;
    }

    cc->leak_window_elapsed_ms += in->dt_ms;
    if (cc->leak_window_elapsed_ms <= in->dt_ms) {
        cc->leak_window_start_mmhg = rt->pressure_display; /* 1a chamada da janela */
    }
    if (cc->leak_window_elapsed_ms >= cc->safety.t_janela_ms) {
        drop_mmhg = cc->leak_window_start_mmhg - rt->pressure_display;
        if (cc->tuning.rate_ctrl_enable != 0u) {
            drop_mmhg = cc->hold_drop_mmhg;
            if ((cc->hold_ref_valid != 0u) && (cc->hold_ref_mmhg > cc->hold_mean_mmhg)) {
                drop_mmhg += cc->hold_ref_mmhg - cc->hold_mean_mmhg;
            }
            if (cc->hold_ref_valid != 0u) {
                cc->hold_ref_mmhg = cc->hold_mean_mmhg; /* nova janela parte daqui */
            }
            if (cc->hold_bursts >= CONTROL_CORE_HOLD_MAX_BURSTS) {
                drop_mmhg = cc->safety.dp_vazamento_mmhg;
            }
            cc->hold_bursts = 0u;
        }
        cc->leak_window_elapsed_ms = 0u;
        cc->hold_drop_mmhg = 0.0f;
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

    /* Saiu do plato por baixo e nao voltou em uma janela de vazamento: a
     * regulacao esta' repondo sozinha um vazamento real. */
    if (rt->state == SYSTEM_STATE_STABILIZED) {
        cc->hold_recover_ms = 0u;
    } else if ((cc->hold_low_exits > 0u) &&
               ((rt->state == SYSTEM_STATE_INFLATING) || (rt->state == SYSTEM_STATE_STABILIZING))) {
        cc->hold_recover_ms += in->dt_ms;
        if (cc->hold_recover_ms >= cc->safety.t_janela_ms) {
            cc->last_error = CONTROL_ERR_E013_LEAK;
            rt->state = SYSTEM_STATE_DEFLATING_FAST;
            safe_actuators(out, 0.0f, 1u, 1u, 1u);
        }
    } else {
        /* fora do ensaio: nada a vigiar */
    }

    if ((rt->state != state_before) && (rt->state == SYSTEM_STATE_STABILIZED)) {
        /* janela de vazamento sempre comeca do zero numa nova entrada em
         * STABILIZED — evita comparar com um baseline de uma visita antiga
         * (ex.: INFLATING<->STABILIZED oscilando perto da banda). */
        if ((cc->tuning.rate_ctrl_enable == 0u) || (cc->cycle.plateau_time_ms == 0u)) {
            /* Reentrada no mesmo ciclo nao zera: sair e voltar a cada 2 s faria a
             * janela nunca fechar e esconderia um vazamento continuo. */
            cc->leak_window_elapsed_ms = 0u;
            cc->hold_drop_mmhg = 0.0f;
        }
        cc->hold_mean_mmhg = cc->p_filt_mmhg;
        cc->hold_ref_valid = 0u;
        cc->hold_since_burst_ms = 0u;
        cc->hold_bursts = 0u;
        cc->hold_gain_pending = 0u;
        cc->hold_wait_ms = 0u; /* a leitura cai logo apos a bomba parar: repoe ja' */
        cc->burst_on_ms = 0u;
        cc->stabilized_out_of_band_ms = 0u;
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

            /* No plato o pico vem da media (plateau_stats_feed): o pulso arterial nao
             * reprova o ciclo, decisao do usuario. P_MAX_ABS segue na leitura bruta. */
            if (((rt->state != SYSTEM_STATE_STABILIZED) || (cc->tuning.rate_ctrl_enable == 0u)) &&
                (rt->pressure_display > cc->cycle.peak_mmhg)) {
                cc->cycle.peak_mmhg = rt->pressure_display;
            }

            if ((cc->cycle.reached_setpoint == 0u) &&
                ((rt->state == SYSTEM_STATE_STABILIZING) || (rt->state == SYSTEM_STATE_STABILIZED))) {
                cc->cycle.reached_setpoint = 1u;
                cc->cycle.inflate_time_ms = cc->cycle.total_elapsed_ms;
            }

            if (rt->state == SYSTEM_STATE_STABILIZED) {
                cc->cycle.plateau_time_ms += in->dt_ms;
                if ((cc->tuning.rate_ctrl_enable == 0u) ||
                    ((cc->burst_on_ms == 0u) &&
                     (cc->hold_since_burst_ms >= CONTROL_CORE_PLATEAU_SETTLE_MS))) {
                    cc->cycle.plateau_sum_mmhg += rt->pressure_display;
                    cc->cycle.plateau_sample_count++;
                    plateau_track_band(cc, rt->pressure_display, in->dt_ms);
                }
            }

            if ((state_before != SYSTEM_STATE_DEFLATING_FAST) && (rt->state == SYSTEM_STATE_DEFLATING_FAST)) {
                cc->cycle.deflate_start_ms = cc->cycle.total_elapsed_ms;
                /* NC07 so' se o pedal cortou o ensaio ANTES do plato: em
                 * STABILIZED ("pronto p/ desinflar") o pedal e' o fim
                 * NORMAL do ensaio, nao uma interrupcao (achado de bancada
                 * 2026-10-17: todo ensaio saia NC07). Plato curto demais e'
                 * avaliado a parte pelo NC do tempo de plato. Cancelar pelo
                 * app e' interrupcao mesmo no plato. */
                if (((in->abort_request != 0u) &&
                     ((state_before == SYSTEM_STATE_INFLATING) ||
                      (state_before == SYSTEM_STATE_STABILIZING))) ||
                    (cc->cancel_pending != 0u)) {
                    cc->cycle.aborted_by_operator = 1u;
                }
            }
        }

        if ((state_before != SYSTEM_STATE_IDLE) && (rt->state == SYSTEM_STATE_IDLE)) {
            cc->cycle.deflate_time_ms = cc->cycle.total_elapsed_ms - cc->cycle.deflate_start_ms;
            cc->cycle.residual_mmhg = rt->pressure_display;
            if ((cc->cycle.plateau_sample_count > 0u) && (cc->cycle.plateau_min_mmhg > cc->cycle.plateau_max_mmhg)) {
                /* plato mais curto que a janela da media: fica so' a media do trecho */
                cc->cycle.plateau_min_mmhg = cc->cycle.plateau_sum_mmhg / (float)cc->cycle.plateau_sample_count;
                cc->cycle.plateau_max_mmhg = cc->cycle.plateau_min_mmhg;
            }
            cc->cycle.technical_fault = (cc->last_error != CONTROL_ERR_NONE) ? 1u : 0u;
            cc->cycle.finalized = 1u;
        }
    }
}
