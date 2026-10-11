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

    /* Diagnostico de bancada (1a vez com cartao SD de verdade): separa onde
     * a montagem esta' falhando. card_detected espelha BSP_SD_IsDetected()
     * (1 = PD0 leu "presente") direto, fora do f_mount — se ficar 0 com o
     * cartao fisicamente inserido, e' deteccao (polaridade do switch/fiacao,
     * nao falta de pull-up). last_mount_fresult e' o FRESULT (ff.h) da
     * ultima tentativa de f_mount — 13 = FR_NO_FILESYSTEM (cartao detectado,
     * mas sem FAT valida: conferir exFAT vs FAT32, este build NAO suporta
     * exFAT, _FS_EXFAT=0 em ffconf.h). */
    uint8_t  card_detected;
    uint8_t  last_mount_fresult;

    /* Diagnostico de escrita (2026-10, write_errors subindo sem remocao do
     * cartao): write_errors sozinho nao diz ONDE na sequencia f_mkdir/
     * f_open/f_write/f_write("\n") falhou nem qual FRESULT (ff.h) exato
     * veio — last_write_step: 0=f_mkdir (ensure_dir_path, qualquer nivel),
     * 1=f_open, 2=f_write(linha), 3=f_write('\n'), 4=written!=len (escreveu
     * menos bytes que pedido, sem erro de FRESULT — last_write_fresult
     * nao e' valido nesse caso), 5=f_close/f_sync (ver comentario em
     * append_line() — se f_sync() falhar aqui, o lock do arquivo fica preso
     * e a PROXIMA tentativa deste mesmo arquivo falha em f_open com
     * FR_LOCKED=16, nao em f_close). Atencao: f_mkdir() retornando
     * FR_EXIST (diretorio ja' existe) e' esperado e NAO conta como falha
     * aqui. */
    uint8_t  last_write_step;
    uint8_t  last_write_fresult;

    /* Folga minima de pilha da storageTask (bytes), via
     * osThreadGetStackSpace() — storageTask ja' teve estouro real de pilha
     * nesta sessao (512->1024 palavras). _USE_LFN=2 (buffer dinamico de
     * 512B por chamada) + buffers locais de storage_format.c/storage_task.c
     * (varios char[16..256] por nivel de chamada) podem consumir bastante
     * em f_open/f_mkdir aninhados. Atualizado a cada volta do loop de
     * StorageTask_Run(); valor baixo (poucas centenas de bytes) indica
     * pilha T apertada mesmo sem o hook de overflow ter disparado. */
    uint32_t storage_task_stack_free_bytes;
} StorageTask_Stats_t;

/* Copia atomica (os campos sao uint32_t individuais; leitura de um campo
 * por vez do Live Watch e' segura mesmo sem lock). */
StorageTask_Stats_t StorageTask_GetStats(void);

#ifdef __cplusplus
}
#endif

#endif /* STORAGE_TASK_H */
