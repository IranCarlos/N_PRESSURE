/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-17
 */

/**
 * @file ui_backend.h
 * @brief Contrato entre a IHM (Core/App/HMI_UI) e o resto do sistema.
 *
 * Nenhum arquivo de Core/App/HMI_UI inclui HAL, main.h, system_types.h ou
 * servicos — tudo que a UI le ou comanda passa por aqui. Isso permite
 * compilar EXATAMENTE os mesmos .c da UI em dois lugares:
 *   - firmware: Core/App/HMI_Port/Src/ui_backend_stm32.c (Control_Loop,
 *     ADC_Acq, RTC, StorageTask, EEPROM, Buzzer);
 *   - simulador de PC: pc_sim/ui_backend_mock.c (maquina de estados fake).
 *
 * Todas as funcoes sao chamadas SO' da tarefa da UI (a mesma que chama
 * lv_timer_handler()). Nenhuma bloqueia, exceto UiBackend_BuzzerBeep()
 * (tom curto sincrono, mesmo comportamento de Buzzer_Beep()).
 */

#ifndef UI_BACKEND_H
#define UI_BACKEND_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Espelho de SystemState_t (system_types.h) — mesma ordem; o backend do
 * firmware converte explicitamente, nao por cast. */
typedef enum {
    UI_STATE_BOOT = 0,
    UI_STATE_IDLE,
    UI_STATE_INFLATING,
    UI_STATE_STABILIZING,
    UI_STATE_STABILIZED,
    UI_STATE_DEFLATING_FAST,
    UI_STATE_LEAK_TEST,
    UI_STATE_CONFIG,
    UI_STATE_FAULT
} UiState_t;

/* Indices do roller de unidade — mesma ordem de PressureUnit_t. */
typedef enum {
    UI_UNIT_KPA = 0,
    UI_UNIT_HPA,
    UI_UNIT_MMHG,
    UI_UNIT_PSI,
    UI_UNIT_CMH2O,
    UI_UNIT_BAR,
    UI_UNIT_COUNT
} UiUnit_t;

typedef struct {
    uint16_t year;   /* 2000..2099 */
    uint8_t  month;  /* 1..12 */
    uint8_t  day;    /* 1..31 */
    uint8_t  hour;   /* 0..23 */
    uint8_t  minute; /* 0..59 */
    uint8_t  second; /* 0..59 */
} UiDateTime_t;

/* Resultado de um ensaio finalizado, ja' avaliado (conforme / NCxx). */
typedef struct {
    int32_t  setpoint_mmhg;
    int32_t  peak_mmhg;
    int32_t  overshoot_mmhg;  /* max(0, pico - setpoint) */
    int32_t  inflate_time_s;  /* tempo ate' o setpoint */
    bool     conforme;
    char     nc_code[8];      /* "NC00", "NC03", ... */
} UiCycleResult_t;

/* Codigos de erro (Apendice D) — mesmos valores de ControlCore_Error_t. */
#define UI_ERR_NONE  (0u)

/* --- Leitura de estado ------------------------------------------------- */
UiState_t UiBackend_GetState(void);
float     UiBackend_GetPressureMmHg(void);
float     UiBackend_GetSetpointMmHg(void);
uint16_t  UiBackend_GetMaxSetpointMmHg(void);
uint32_t  UiBackend_GetTickMs(void);
bool      UiBackend_GetDateTime(UiDateTime_t * out);
bool      UiBackend_IsSdMounted(void);
bool      UiBackend_IsRtcValid(void);
uint16_t  UiBackend_GetLastError(void);      /* UI_ERR_NONE = nenhum */
UiUnit_t  UiBackend_GetUnit(void);
float     UiBackend_GetRawPressureMmHg(void); /* calibracao: (bruto - atm) */
uint32_t  UiBackend_GetLeakHoldTimeMs(void);
float     UiBackend_GetLeakMaxDropMmHg(void);
const char * UiBackend_GetDeviceId(void);
const char * UiBackend_GetFirmwareVersion(void);

/* Botao fisico do usuario/pedal (bt_user_pressed): devolve true uma vez
 * por clique e consome o evento. A UI so' chama na tela de boot (pular) e
 * nos Ajustes com o estado em CONFIG (sair) — fora disso o evento e' do
 * Control_Loop (abortar/desinflar), ver system_types.h. */
bool UiBackend_ConsumeUserButton(void);

/* Borda de "ensaio finalizado": devolve true UMA vez por ciclo concluido e
 * preenche *out. O chamador decide se mostra/grava (a IHM ignora durante
 * os Ajustes, onde o fim do teste de vazamento tambem gera essa borda). */
bool UiBackend_PollCycleResult(UiCycleResult_t * out);

/* Operador/participante definidos REMOTAMENTE (USB SET_OPERATOR, Fase 7.3):
 * devolve true uma vez por pedido novo e preenche os IDs. A IHM aplica na
 * propria sessao (UiSession_t e' dado da UI, so' a uiTask escreve nela). */
bool UiBackend_PollRemoteSession(uint16_t * operator_id, uint16_t * participant_id);

/* Ensaio iniciado REMOTAMENTE (USB START_TEST, Fase 7.3): devolve true uma
 * vez por inicio remoto, para a IHM avisar quem esta' perto do equipamento. */
bool UiBackend_PollRemoteStart(void);

/* --- Acoes -------------------------------------------------------------- */
void UiBackend_SetSetpoint(float mmhg);
void UiBackend_StartInflation(void);
void UiBackend_EnterConfigMode(void);
void UiBackend_ExitConfigMode(void);  /* tambem persiste a configuracao */
void UiBackend_StartLeakTest(void);
void UiBackend_SetUnit(UiUnit_t unit);
bool UiBackend_SetDateTime(const UiDateTime_t * dt);
void UiBackend_BuzzerBeep(uint16_t freq_hz, uint16_t duration_ms);

/* Grava o registro do ultimo ciclo devolvido por UiBackend_PollCycleResult()
 * (cartao SD / trilha de auditoria) com a identificacao da sessao. */
void UiBackend_SubmitCycleRecord(uint16_t operator_id, uint16_t participant_id,
                                 uint16_t cycle_number);

/* Calibracao de 2 pontos com manometro externo na V3. */
void UiBackend_CalZero(void);
void UiBackend_CalSetPumpPct(uint8_t pct);     /* abre V1+V2+V3 + bomba em pct */
void UiBackend_CalAbort(void);                 /* bomba off, valvulas ao controle */
void UiBackend_CalCapture(uint8_t point, float ref_mmhg); /* point = 1 ou 2 */
bool UiBackend_CalComputeAndSave(char * msg, size_t msg_len);

#ifdef __cplusplus
}
#endif

#endif /* UI_BACKEND_H */
