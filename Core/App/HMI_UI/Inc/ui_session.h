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
 * cycle_number conta ensaios desde o boot (reinicia ao resetar) — numeracao
 * persistente dependeria de ler de volta /tests/index.csv no SD.
 */

#ifndef UI_SESSION_H
#define UI_SESSION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct {
    uint16_t operator_id;    /* 0 = nenhum selecionado ainda */
    uint16_t participant_id; /* 0 = nenhum selecionado ainda */
    uint16_t cycle_number;   /* incrementado em UiSession_NextCycle() */

    /* Buzzer: so' RAM, de proposito — volta a "ligado" a cada boot (um
     * alarme mudo por engano nao fica mudo para sempre). */
    uint8_t buzzer_enabled;  /* 1 = ligado (default) */
} UiSession_t;

void UiSession_Init(void);
UiSession_t * UiSession_Get(void);
void UiSession_SetOperator(uint16_t operator_id);
void UiSession_SetParticipant(uint16_t participant_id);
void UiSession_SetBuzzerEnabled(uint8_t enabled);

/* Chamar ao INICIAR um ensaio (transicao IDLE -> ativo), nao ao terminar. */
void UiSession_NextCycle(void);

/* Bipe respeitando o liga/desliga do buzzer da sessao. */
void UiSession_Beep(uint16_t freq_hz, uint16_t duration_ms);

#ifdef __cplusplus
}
#endif

#endif /* UI_SESSION_H */
