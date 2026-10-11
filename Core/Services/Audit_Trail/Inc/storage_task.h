/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-11
 *
 * Tarefa de armazenamento (Apendice C/Parte C do prompt do projeto):
 * recebe registros por fila e grava no SD — unico modulo que toca FatFS.
 * Nada de acesso ao SD no contexto do loop de controle: as funcoes
 * StorageTask_Log*()/SubmitTestRecord() so' enfileiram (copiam os dados e
 * devolvem na hora; quem efetivamente abre/escreve arquivo e' a tarefa,
 * de forma assincrona) — ainda assim NAO devem ser chamadas de dentro de
 * ControlLoop_Update()/controlTask (prioridade mais alta do sistema); use
 * appTask/safetyTask/uma fila intermediaria.
 *
 * NUNCA testado em hardware — primeira vez que este projeto escreve no
 * cartao SD. Ver checklist de bancada em 00-auditoria.md antes de habilitar
 * BRINGUP_ENABLE_STORAGE.
 */
#ifndef STORAGE_TASK_H
#define STORAGE_TASK_H

#include <stdint.h>
#include "storage_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Chamar uma vez no boot, antes de StartStorageTask()/StorageTask_Run()
 * come,ar a rodar (cria a fila estatica). */
void StorageTask_InitQueue(void);

/* Enfileira um registro (copia os campos — seguro usar buffers locais do
 * chamador). Retorna 0 se enfileirou, -1 se a fila estava cheia (perdido;
 * contabilizado em StorageTask_GetStats().messages_dropped). Nao bloqueia
 * (timeout 0). */
int StorageTask_LogEvent(const StorageFormat_LogEntry_t *entry);
int StorageTask_LogError(const StorageFormat_LogEntry_t *entry);
int StorageTask_LogAudit(const StorageFormat_LogEntry_t *entry);
int StorageTask_SubmitTestRecord(const StorageFormat_RecInfo_t *info);

/* Corpo da tarefa (chamar de dentro de StartStorageTask() em main.c, dentro
 * do for(;;) — esta funcao JA e' o loop infinito, nunca retorna). Tenta
 * montar o cartao; se falhar, tenta de novo periodicamente (E005 — nunca
 * trava, so' descarta mensagens enquanto o cartao nao estiver pronto). */
void StorageTask_Run(void);

typedef struct {
    uint8_t  sd_mounted;
    uint32_t remount_attempts;
    uint32_t messages_processed;
    uint32_t messages_dropped;  /* fila cheia no momento de enfileirar */
    uint32_t messages_discarded_no_sd; /* fila ok, mas sem cartao montado */
    uint32_t write_errors;      /* f_write/f_open falhou (ex.: cartao removido) */
} StorageTask_Stats_t;

/* Copia atomica (os campos sao uint32_t individuais; leitura de um campo
 * por vez do Live Watch e' segura mesmo sem lock). */
StorageTask_Stats_t StorageTask_GetStats(void);

#ifdef __cplusplus
}
#endif

#endif /* STORAGE_TASK_H */
