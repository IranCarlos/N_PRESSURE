/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-12
 * Modified: 2026-10-17
 */

/**
 * @file ui_session.h
 * @brief Identificacao do ensaio atual (operador/participante/ciclo) —
 *        dado da UI, nao de controle (nao entra em SystemRuntime_t/
 *        SystemConfig_t).
 *
 * Operador e participante sao IDs numericos ("OP03"/"P037"): a IHM so' tem
 * encoder (spinbox/roller), sem teclado de texto.
 *
 * cycle_number conta ensaios desde o boot (reinicia ao resetar). `measure` e' a
 * posicao na serie do paciente e vira o ciclo C1..C3 do registro; o ID unico do
 * ensaio vem da storageTask.
 */

#ifndef UI_SESSION_H
#define UI_SESSION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* Cada paciente faz 3 medidas (pedido do pesquisador, 2026-10-31). */
#define UI_SERIES_LENGTH (3u)

typedef enum {
    UI_SERIES_REPEAT = 0, /* nao contou: repetir a mesma medida */
    UI_SERIES_NEXT,       /* contou: proxima medida do paciente */
    UI_SERIES_COMPLETE    /* contou a ultima: perguntar o que fazer */
} UiSeriesStep_t;

typedef struct {
    uint16_t operator_id;    /* 0 = nenhum selecionado ainda */
    uint16_t participant_id; /* 0 = nenhum selecionado ainda */
    uint16_t cycle_number;   /* incrementado em UiSession_NextCycle() */

    uint8_t  measure;        /* medida atual do paciente, 1..UI_SERIES_LENGTH */
    uint8_t  series_done;    /* 1 = terminou as medidas e aguarda a escolha */

    /* Buzzer: so' RAM, de proposito — volta a "ligado" a cada boot (um
     * alarme mudo por engano nao fica mudo para sempre). Erro tecnico e aviso
     * de ensaio iniciado pelo PC tocam sempre. */
    uint8_t buzzer_enabled;  /* 1 = sons de status do ensaio ligados (default) */
    uint8_t click_enabled;   /* 1 = clique do encoder ligado (default) */
} UiSession_t;

void UiSession_Init(void);
UiSession_t * UiSession_Get(void);
void UiSession_SetOperator(uint16_t operator_id);
void UiSession_SetParticipant(uint16_t participant_id);
void UiSession_SetBuzzerEnabled(uint8_t enabled);
void UiSession_SetClickEnabled(uint8_t enabled);

/* Avanca a serie com o resultado do ensaio (`counted` = medida vale). */
UiSeriesStep_t UiSession_SeriesOnResult(bool counted);
/* Recomeca as medidas do paciente atual. */
void UiSession_SeriesRestart(void);

/* Chamar ao INICIAR um ensaio (transicao IDLE -> ativo), nao ao terminar. */
void UiSession_NextCycle(void);

/* Bipe de status do ensaio, respeitando o liga/desliga da sessao. */
void UiSession_Beep(uint16_t freq_hz, uint16_t duration_ms);

#ifdef __cplusplus
}
#endif

#endif /* UI_SESSION_H */
