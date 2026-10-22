/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-01
 *
 * Nucleo da malha de controle (maquina de estados + PID), em C puro — SEM
 * HAL, SEM `HAL_GetTick()`, SEM chamada direta a driver nenhum. Mesma logica
 * de `Core/Services/Control_Loop` (reaproveitada por decisao do usuario —
 * ver 00-auditoria.md D4), reorganizada para:
 *   - receber o tempo decorrido (`dt_ms`) em vez de ler o relogio;
 *   - devolver os comandos de atuador numa struct em vez de chamar
 *     Valve_SetAll()/PwmMotor_SetPower() diretamente;
 *   - adicionar as protecoes de seguranca que a Fase 3 exige testar no
 *     simulador: limite absoluto de pressao (E001), sensor invalido (E002),
 *     vazamento em manutencao (E013) e timeouts de inflar/desinflar
 *     (E003/E004) — ver Apendice D do prompt do projeto.
 *
 * `Core/Services/Control_Loop` (ligado ao hardware) NAO foi alterado nesta
 * fase — a integracao e verificacao em bancada e' Fase 4 (prompt do
 * projeto, Parte C), que e' quando este nucleo passa a comandar o
 * hardware de verdade.
 */
#ifndef CONTROL_CORE_H
#define CONTROL_CORE_H

#include "system_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CONTROL_CORE_MMHG_PER_KPA (7.50062f)

/* Subconjunto do Apendice D relevante a esta malha. Os demais codigos (SD,
 * RTC, EEPROM, USB, watchdog, hard fault) pertencem a outros modulos. */
typedef enum {
    CONTROL_ERR_NONE                 = 0,
    CONTROL_ERR_E001_OVERPRESSURE    = 1,  /* P_MAX_ABS — estado seguro imediato */
    CONTROL_ERR_E002_SENSOR_FAULT    = 2,  /* sensor invalido — estado seguro imediato */
    CONTROL_ERR_E003_INFLATE_TIMEOUT = 3,  /* nao atinge o setpoint a tempo — aborta e desinfla */
    CONTROL_ERR_E004_DEFLATE_TIMEOUT = 4,  /* nao desinflou a tempo — mantem valvulas abertas, alarme */
    CONTROL_ERR_E013_LEAK            = 13  /* vazamento em manutencao — encerra o ensaio com aviso */
} ControlCore_Error_t;

/* Parametros de seguranca do Apendice E ainda nao representados em
 * SystemConfig_t (que ja' traz pid/stabilization/leak_test/setpoint). */
typedef struct {
    float    p_max_abs_mmhg;       /* P_MAX_ABS: teto absoluto, qualquer estado */
    uint32_t sensor_fault_debounce_ms; /* sensor_ok=0 por mais que isto -> E002 */
    uint32_t t_inflar_max_ms;      /* tempo total em INFLATING+STABILIZING -> E003 */
    uint32_t t_desinflar_max_ms;   /* tempo em DEFLATING_FAST -> E004 */
    float    dp_vazamento_mmhg;    /* queda de pressao... */
    uint32_t t_janela_ms;          /* ...nesta janela, durante STABILIZED -> E013 */
    float    p_segura_mmhg;        /* P_SEGURA: abaixo disto por t_seguro = desinflado */
    uint32_t t_seguro_ms;
} ControlCore_SafetyConfig_t;

typedef struct {
    float   pressure_kpa;   /* leitura ja calibrada (Pressure_Core/ADC_Acq) */
    uint8_t sensor_ok;      /* 0 = sensor invalido (travado/fora de faixa/salto) */
    uint8_t abort_request;  /* pedal/bt_user: pedido de desinflar (pulso; esta malha consome) */
    uint32_t dt_ms;         /* tempo decorrido desde a chamada anterior (1 ms no hardware) */
} ControlCore_Inputs_t;

typedef struct {
    float   pump_power;    /* 0..1 */
    uint8_t valve1_open;
    uint8_t valve2_open;
    uint8_t valve3_open;
} ControlCore_Actuators_t;

/*
 * Metricas de um ciclo completo (Fase 5: alimenta CycleRecord_Evaluate() em
 * Audit_Trail/cycle_record.h). Zeradas em ControlCore_StartInflation();
 * acumulam enquanto o estado != IDLE; `finalized` vira 1 ao voltar para
 * IDLE (congela ali ate' o proximo StartInflation). Um ciclo que termina em
 * SYSTEM_STATE_FAULT nunca finaliza por este mecanismo — FAULT hoje nao
 * tem saida automatica (ver ControlCore_EnterFaultState); a camada de
 * storage deve tratar esse caso lendo o estado diretamente.
 */
typedef struct {
    float    peak_mmhg;
    float    plateau_sum_mmhg;
    uint32_t plateau_sample_count;  /* 0 = nunca chegou a STABILIZED */
    float    plateau_min_mmhg;
    float    plateau_max_mmhg;
    uint32_t total_elapsed_ms;      /* desde StartInflation */
    uint32_t inflate_time_ms;       /* ate' a 1a vez em STABILIZING/STABILIZED */
    uint32_t plateau_time_ms;       /* tempo total acumulado em STABILIZED */
    uint32_t deflate_start_ms;      /* uso interno — snapshot ao entrar em DEFLATING_FAST */
    uint32_t deflate_time_ms;       /* DEFLATING_FAST ate' IDLE */
    float    residual_mmhg;         /* pressao no instante de voltar a IDLE */
    uint8_t  reached_setpoint;      /* chegou a STABILIZING/STABILIZED ao menos 1x */
    uint8_t  aborted_by_operator;   /* abort_request iniciou o desinflar (nao timeout/vazamento) */
    uint8_t  technical_fault;       /* last_error != NONE ao finalizar */
    uint8_t  finalized;             /* 1 = ciclo completo, campos acima prontos para ler */
} ControlCore_CycleStats_t;

/*
 * Diagnostico de bancada (2026-10, achado do usuario: "a bomba enche muito
 * devagar e quase nunca chega ao setpoint, sempre fica abaixo"). Antes
 * disto, nenhum termo interno do PID (P/I/D/FF, saida antes/depois do
 * piso de partida CONTROL_CORE_PUMP_MIN_EFFECTIVE) era visivel fora de
 * run_pid_iteration() — so' dava pra' ver o RESULTADO (pressure_display
 * nunca alcan,ando o setpoint), nao a CAUSA (qual termo esta' segurando a
 * saida baixa: Ki/i_max pequenos demais para vencer o "vazamento"/atrito
 * do sistema real? out_max desproporcional ao fluxo real da bomba? o piso
 * de 30% nao estava disparando por algum motivo?). Atualizado a cada
 * chamada de run_pid_iteration() (INFLATING/STABILIZING); exposto via
 * ControlCore_GetPidDiag()/ControlLoop_GetPidDiag() pro Live Watch —
 * puramente informativo, NAO influencia a saida (sem isso, so' dava pra'
 * tunar por tentativa cega: mudar ganho, reflashar, testar nehum resto).
 */
typedef struct {
    float   error_mmhg;
    float   p_term_kpa;
    float   i_term_kpa;       /* cc->pid_integral, ja' com o clamp i_min/i_max aplicado */
    float   d_term_kpa;
    float   ff_term_kpa;
    float   output_kpa;       /* soma P+I+D+FF, antes de dividir por out_max */
    float   pump_power_pid;   /* output/out_max, ja' saturado em [0,1] — o que o PID "pediu" */
    float   pump_power_final; /* depois do piso de partida e do alivio — o que REALMENTE foi pra' PWM */
    uint8_t floor_applied;    /* 1 = CONTROL_CORE_PUMP_MIN_EFFECTIVE substituiu o valor do PID */
    uint8_t relief_active;    /* 1 = valvula de alivio (V1) aberta neste passo */
    uint8_t fast_approach;    /* 1 = aproximacao a potencia maxima (PID ainda nao assumiu) */
    float   rise_rate_mmhg_s; /* taxa de subida filtrada (usada na previsao de corte) */
    float   predicted_mmhg;   /* pressao + taxa * antecipacao — dispara a troca p/ o PID */
} ControlCore_PidDiag_t;

typedef struct {
    SystemConfig_t             *cfg;     /* pid/stabilization/leak_test/setpoint (EEPROM) */
    SystemRuntime_t            *runtime; /* state/pressure_display/setpoint_mmhg/bt_user_pressed */
    ControlCore_SafetyConfig_t  safety;

    ControlCore_PidDiag_t pid_diag;   /* ver comentario acima */

    /* --- estado interno (equivalente ao s_control do Control_Loop atual) --- */
    float    pid_integral;
    float    pid_prev_error;
    uint32_t state_elapsed_ms;        /* tempo no estado atual (reseta a cada transicao) */
    uint32_t inflate_elapsed_ms;      /* tempo acumulado em INFLATING+STABILIZING (p/ E003) */
    uint32_t sensor_fault_elapsed_ms; /* tempo consecutivo com sensor_ok=0 (p/ E002) */
    uint32_t leak_window_elapsed_ms;
    float    leak_window_start_mmhg;
    uint32_t stabilized_out_of_band_calls; /* chamadas CONSECUTIVAS fora da banda, em STABILIZED */
    uint32_t stabilizing_out_of_band_ms;   /* tempo CONSECUTIVO fora da banda larga, em STABILIZING */
    uint32_t deflate_settle_elapsed_ms;    /* 0 = ainda acima de P_SEGURA */

    /* Aproximacao rapida (bomba a 100% longe do setpoint) — ver
     * update_fast_approach() em control_core.c. */
    uint8_t  fast_approach;           /* 1 = ainda na aproximacao a potencia maxima */
    uint32_t rearm_elapsed_ms;        /* tempo consecutivo muito abaixo do alvo (re-arma a aproximacao) */
    float    p_filt_mmhg;             /* pressao filtrada (base da taxa de subida) */
    float    rise_rate_mmhg_s;        /* taxa de subida filtrada */
    uint8_t  rate_valid;              /* 0 = filtro ainda nao inicializado */

    /* Valvula de alivio com histerese + tempos minimos — ver
     * update_relief_valve() em control_core.c. */
    uint8_t  relief_open;
    uint32_t relief_on_ms;            /* ha' quanto tempo esta' aberta */
    uint32_t relief_off_ms;           /* ha' quanto tempo esta' fechada */
    uint32_t relief_pulse_ms;         /* duracao do pulso atual */

    ControlCore_Error_t last_error;   /* ultimo erro latched; Init/novo ensaio zera */

    ControlCore_CycleStats_t cycle;   /* Fase 5 — ver ControlCore_CycleStats_t */
} ControlCore_t;

void ControlCore_Init(ControlCore_t *cc, SystemConfig_t *cfg, SystemRuntime_t *runtime,
                      const ControlCore_SafetyConfig_t *safety);

/* Um passo da malha. Chamar a cada `in->dt_ms` decorridos (1 ms no
 * hardware, qualquer valor no simulador). Preenche `out` sempre. */
void ControlCore_Step(ControlCore_t *cc, const ControlCore_Inputs_t *in,
                      ControlCore_Actuators_t *out);

/* Inicia um ciclo de insuflacao a partir de IDLE (equivalente a
 * ControlLoop_StartInflation). Sem efeito em outros estados. */
void ControlCore_StartInflation(ControlCore_t *cc);

/* Forca o estado seguro imediatamente (equivalente a
 * ControlLoop_EnterFaultState) — usar para falhas detectadas fora desta
 * malha (ex.: watchdog, heartbeat USB perdido). */
void ControlCore_EnterFaultState(ControlCore_t *cc, ControlCore_Actuators_t *out);

/* Metricas do ciclo em andamento ou do ultimo ciclo completo (ver
 * ControlCore_CycleStats_t.finalized). Ponteiro para dentro de cc — valido
 * ate' o proximo ControlCore_StartInflation(). */
const ControlCore_CycleStats_t *ControlCore_GetCycleStats(const ControlCore_t *cc);

/* Termos internos do PID no ultimo passo de run_pid_iteration() — ver
 * ControlCore_PidDiag_t. So' atualizado durante INFLATING/STABILIZING;
 * fora disso mantem o ultimo valor calculado (nao e' zerado). */
const ControlCore_PidDiag_t *ControlCore_GetPidDiag(const ControlCore_t *cc);

#ifdef __cplusplus
}
#endif

#endif /* CONTROL_CORE_H */
