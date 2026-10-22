/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-22
 */

/**
 * @file app_event_log.h
 * @brief Fase 7.4: registro automatico no log do SD (Apendice C) do que
 *        acontece no equipamento - mudancas de estado, erros do controle
 *        (Apendice D), cartao SD inserido/removido e fim de ensaio.
 *
 * Antes disto nada gravava os erros E00x nem o estado no log: so' os
 * comandos USB e a autenticacao geravam linhas. Roda na appTask
 * (AppEventLog_Poll a cada volta), independente de USB e da IHM; a deteccao
 * de mudancas e' o Event_Watch (testado no host). As linhas tambem ficam na
 * cauda em RAM do StorageTask, entao aparecem no GET_LOG_TAIL mesmo sem SD.
 */

#ifndef APP_EVENT_LOG_H
#define APP_EVENT_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "system_types.h"

/* Mesmo runtime passado ao Control_Loop. */
void AppEventLog_Init(const SystemRuntime_t * runtime);

/* Chamar periodicamente (appTask). Nao bloqueia (so' enfileira no StorageTask). */
void AppEventLog_Poll(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_EVENT_LOG_H */
