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

typedef struct {
    SystemConfig_t             *cfg;     /* pid/stabilization/leak_test/setpoint (EEPROM) */
    SystemRuntime_t            *runtime; /* state/pressure_display/setpoint_mmhg/bt_user_pressed */
    ControlCore_SafetyConfig_t  safety;

    /* --- estado interno (equivalente ao s_control do Control_Loop atual) --- */
    float    pid_integral;
    float    pid_prev_error;
    uint32_t state_elapsed_ms;        /* tempo no estado atual (reseta a cada transicao) */
    uint32_t inflate_elapsed_ms;      /* tempo acumulado em INFLATING+STABILIZING (p/ E003) */
    uint32_t sensor_fault_elapsed_ms; /* tempo consecutivo com sensor_ok=0 (p/ E002) */
    uint32_t leak_window_elapsed_ms;
    float    leak_window_start_mmhg;
    uint32_t stabilized_out_of_band_calls; /* chamadas CONSECUTIVAS fora da banda, em STABILIZED */
    uint32_t deflate_settle_elapsed_ms;    /* 0 = ainda acima de P_SEGURA */
    uint32_t relief_pulse_remaining_ms;
    uint32_t relief_recheck_remaining_ms;

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

#ifdef __cplusplus
}
#endif

#endif /* CONTROL_CORE_H */
