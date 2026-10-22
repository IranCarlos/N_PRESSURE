/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-22
 *
 * Fase 7.4: detector de mudancas do estado do equipamento (C puro, testado
 * no host). Cada consumidor tem a sua instancia e chama EventWatch_Poll()
 * no proprio ritmo com uma foto do sistema; recebe uma mascara do que mudou
 * desde a chamada anterior. Hoje sao dois consumidores independentes:
 *   - registrador do SD (Core/App/App_Log, appTask): grava estado/erro no log;
 *   - usbProtoTask: manda eventos assincronos (EVT) ao PC.
 * Um nao depende do outro (o log nao pode depender do USB nem vice-versa).
 *
 * A 1a chamada so' memoriza a foto e nao reporta nada (sem "mudanca" falsa
 * no boot ou ao (re)conectar o PC).
 */
#ifndef EVENT_WATCH_H
#define EVENT_WATCH_H

#include <stdint.h>
#include "system_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Foto do sistema numa chamada. */
typedef struct {
    SystemState_t state;
    uint8_t  error;      /* valor de ControlCore_Error_t (= numero do Apendice D, 0 = nenhum) */
    uint8_t  sd_mounted;
    uint8_t  cycle_finalized; /* ControlCore_CycleStats_t.finalized (nivel, nao borda) */
} EventWatch_Snapshot_t;

/* Mascara devolvida por EventWatch_Poll(). */
#define EVENT_WATCH_STATE_CHANGED   (1u << 0)
#define EVENT_WATCH_ERROR_RAISED    (1u << 1) /* erro novo != 0 (borda; mesmo codigo repetido nao conta) */
#define EVENT_WATCH_ERROR_CLEARED   (1u << 2) /* voltou a 0 */
#define EVENT_WATCH_SD_INSERTED     (1u << 3)
#define EVENT_WATCH_SD_REMOVED      (1u << 4)
#define EVENT_WATCH_CYCLE_FINISHED  (1u << 5) /* borda de subida de cycle_finalized */

typedef struct {
    EventWatch_Snapshot_t prev;
    uint8_t primed; /* 0 = ainda nao recebeu a 1a foto */
} EventWatch_t;

void EventWatch_Init(EventWatch_t *w);

/* Compara `now` com a foto anterior, guarda `now` e devolve a mascara.
 * *prev_out (opcional) recebe a foto anterior (ex.: estado de origem). */
uint32_t EventWatch_Poll(EventWatch_t *w, const EventWatch_Snapshot_t *now,
                         EventWatch_Snapshot_t *prev_out);

/* Nome curto pt-BR do estado para log/IHM (Parte B 2.1.2), sem ';'
 * (delimitador do formato de log) e com no maximo 15 caracteres. */
const char *EventWatch_StateName(SystemState_t state);

#ifdef __cplusplus
}
#endif

#endif /* EVENT_WATCH_H */
