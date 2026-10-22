/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-11
 */

#include "storage_task.h"
#include "fatfs.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"   /* taskENTER_CRITICAL: cauda do log em RAM */
#include "cmsis_os2.h"
#include "system_types.h"
#include <string.h>

/* Fila estatica (DTCM ja' esta' ~85% usada — ver 00-auditoria.md; manter a
 * profundidade modesta). Cada item carrega COPIAS (arrays de char fixos,
 * nao ponteiros) de tudo que precisa para montar a linha depois — o
 * chamador pode usar buffers de pilha, o item fica valido ate' ser
 * processado independente do tempo de vida de quem enfileirou. */
#define STORAGE_QUEUE_DEPTH    (8u)
#define STORAGE_TASK_POLL_MS   (200u)
#define STORAGE_REMOUNT_PERIOD_MS (2000u)

typedef enum {
    STORAGE_MSG_LOG_EVENT = 0,
    STORAGE_MSG_LOG_ERROR,
    STORAGE_MSG_LOG_AUDIT,
    STORAGE_MSG_TEST_REC
} StorageTask_MsgType_t;

typedef struct {
    StorageTask_MsgType_t type;

    /* --- log (EVENT/ERROR/AUDIT) --- */
    StorageFormat_DateTime_t datetime;
    int8_t   tz_offset_hours;
    uint32_t seq;
    char     code[8];
    uint32_t test_id;       /* 0 = sem ensaio associado */
    char     state_name[16];
    float    pressure_mmhg;
    char     message[64];

    /* --- TEST_REC (reaproveita datetime/test_id acima) --- */
    char     participant_id[8];
    char     operator_id[8];
    uint16_t cycle_number;
    CycleRecord_Metrics_t metrics;
    char     device_id[24];
    char     firmware_version[16];
    StorageFormat_DateTime_t last_calibration;
} StorageTask_QueueMsg_t;

static StaticQueue_t             s_queue_cb;
static StorageTask_QueueMsg_t    s_queue_buf[STORAGE_QUEUE_DEPTH];
static osMessageQueueId_t        s_queue;

static StorageTask_Stats_t       s_stats;

/* FIL._FS_TINY==0 (ffconf.h) da' a cada arquivo aberto um buffer de setor
 * PROPRIO (fp->buf[512]), usado por f_write() pra' escritas parciais e
 * mandado ao cartao via DMA dentro de f_sync()/f_close() (disk_write(...,
 * fp->buf, ...), ff.c). Se este FIL fosse uma variavel local de pilha de
 * append_line(), fp->buf cairia na pilha da storageTask — DTCMRAM,
 * inalcancavel pela IDMA do SDMMC1 (mesmo motivo de SDFatFS/SDFile em
 * RAM_D1, ver STM32H723xG_flash.ld). Achado em bancada (2026-10):
 * f_write() sempre "funcionava" (so' copia pro buffer, sem DMA) mas
 * f_close()/f_sync() sempre falhava com FR_DISK_ERR (o 1o DMA de verdade
 * deste FIL). static + DMA_BUFFER resolve; storageTask so' abre um arquivo
 * de cada vez (serial, sincrono), entao reaproveitar uma unica instancia
 * entre chamadas e' seguro. */
static FIL s_append_file DMA_BUFFER;

/* Uma corrente de hash POR ARQUIVO de log do mes (events/errors/audit) —
 * cada arquivo e' encadeado independentemente (Apendice C: "o 1o registro
 * do mes referencia o ultimo hash do mes anterior", por arquivo). Carregar
 * o ultimo hash gravado ao trocar de mes/no boot ainda nao esta'
 * implementado (hoje sempre reinicia do genesis) — ver nota no fim do
 * arquivo. */
static AuditChain_t s_chain_events;
static AuditChain_t s_chain_errors;
static AuditChain_t s_chain_audit;
static uint8_t      s_chains_initialized;

void StorageTask_InitQueue(void)
{
    osMessageQueueAttr_t attr;

    memset(&s_queue_cb, 0, sizeof(s_queue_cb));
    memset(&s_queue_buf, 0, sizeof(s_queue_buf));
    memset(&s_stats, 0, sizeof(s_stats));
    s_stats.last_write_step = 0xFFu;   /* 0xFF = nenhuma escrita tentada ainda */
    s_stats.last_write_fresult = 0xFFu;

    memset(&attr, 0, sizeof(attr));
    attr.name = "storageQueue";
    attr.cb_mem = &s_queue_cb;
    attr.cb_size = sizeof(s_queue_cb);
    attr.mq_mem = &s_queue_buf[0];
    attr.mq_size = sizeof(s_queue_buf);

    s_queue = osMessageQueueNew(STORAGE_QUEUE_DEPTH, sizeof(StorageTask_QueueMsg_t), &attr);

    if (s_chains_initialized == 0u) {
        AuditChain_Init(&s_chain_events, NULL);
        AuditChain_Init(&s_chain_errors, NULL);
        AuditChain_Init(&s_chain_audit, NULL);
        s_chains_initialized = 1u;
    }
}

static void copy_str(char *dst, size_t dst_size, const char *src)
{
    if (src == NULL) { dst[0] = '\0'; return; }
    strncpy(dst, src, dst_size - 1u);
    dst[dst_size - 1u] = '\0';
}

static int enqueue(const StorageTask_QueueMsg_t *msg)
{
    if (s_queue == NULL) { return -1; }
    if (osMessageQueuePut(s_queue, msg, 0u, 0u) != osOK) {
        s_stats.messages_dropped++;
        return -1;
    }
    return 0;
}

/* Cauda do log em RAM (ver storage_task.h). s_tail_count = total ja'
 * gravado (corre livre); a posicao da linha k e' k % STORAGE_LOG_TAIL_LEN. */
static StorageTask_LogTailEntry_t s_tail[STORAGE_LOG_TAIL_LEN];
static uint32_t s_tail_count;
static uint32_t s_log_seq;

static int submit_log(StorageTask_MsgType_t type, const StorageFormat_LogEntry_t *entry)
{
    StorageTask_QueueMsg_t msg;
    StorageTask_LogTailEntry_t *slot;

    memset(&msg, 0, sizeof(msg));
    msg.type = type;
    msg.datetime = entry->datetime;
    msg.tz_offset_hours = entry->tz_offset_hours;
    copy_str(msg.code, sizeof(msg.code), entry->code);
    msg.test_id = entry->test_id;
    copy_str(msg.state_name, sizeof(msg.state_name), entry->state_name);
    msg.pressure_mmhg = entry->pressure_mmhg;
    copy_str(msg.message, sizeof(msg.message), entry->message);

    /* Numeracao unica + copia para a cauda: varias tarefas chamam isto
     * (usbProtoTask, appTask, uiTask), entao a secao critica cobre o
     * incremento e a copia (~90 bytes, poucos microssegundos). */
    taskENTER_CRITICAL();
    msg.seq = ++s_log_seq;
    slot = &s_tail[s_tail_count % STORAGE_LOG_TAIL_LEN];
    slot->datetime = msg.datetime;
    slot->seq = msg.seq;
    slot->level = (type == STORAGE_MSG_LOG_ERROR) ? (uint8_t)STORAGE_LOG_LEVEL_ERR
                : (type == STORAGE_MSG_LOG_AUDIT) ? (uint8_t)STORAGE_LOG_LEVEL_AUDIT
                                                  : (uint8_t)STORAGE_LOG_LEVEL_EVT;
    memcpy(slot->code, msg.code, sizeof(slot->code));
    memcpy(slot->message, msg.message, sizeof(slot->message));
    s_tail_count++;
    taskEXIT_CRITICAL();

    return enqueue(&msg);
}

uint8_t StorageTask_GetLogTail(uint32_t newest_offset, StorageTask_LogTailEntry_t *out)
{
    uint8_t found = 0u;

    if (out == NULL) {
        return 0u;
    }
    taskENTER_CRITICAL();
    if ((newest_offset < s_tail_count) && (newest_offset < STORAGE_LOG_TAIL_LEN)) {
        *out = s_tail[(s_tail_count - 1u - newest_offset) % STORAGE_LOG_TAIL_LEN];
        found = 1u;
    }
    taskEXIT_CRITICAL();
    return found;
}

int StorageTask_LogEvent(const StorageFormat_LogEntry_t *entry)
{
    return submit_log(STORAGE_MSG_LOG_EVENT, entry);
}

int StorageTask_LogError(const StorageFormat_LogEntry_t *entry)
{
    return submit_log(STORAGE_MSG_LOG_ERROR, entry);
}

int StorageTask_LogAudit(const StorageFormat_LogEntry_t *entry)
{
    return submit_log(STORAGE_MSG_LOG_AUDIT, entry);
}

int StorageTask_SubmitTestRecord(const StorageFormat_RecInfo_t *info)
{
    StorageTask_QueueMsg_t msg;

    memset(&msg, 0, sizeof(msg));
    msg.type = STORAGE_MSG_TEST_REC;
    msg.test_id = info->test_id;
    msg.datetime = info->datetime;
    copy_str(msg.participant_id, sizeof(msg.participant_id), info->participant_id);
    copy_str(msg.operator_id, sizeof(msg.operator_id), info->operator_id);
    msg.cycle_number = info->cycle_number;
    msg.metrics = info->metrics;
    copy_str(msg.device_id, sizeof(msg.device_id), info->device_id);
    copy_str(msg.firmware_version, sizeof(msg.firmware_version), info->firmware_version);
    msg.last_calibration = info->last_calibration;

    return enqueue(&msg);
}

StorageTask_Stats_t StorageTask_GetStats(void)
{
    return s_stats;
}

/* Cria cada diretorio do caminho (menos o nome do arquivo em si). FatFS
 * nao cria subdiretorios recursivamente — precisa ir nivel por nivel.
 * f_mkdir() devolvendo FR_EXIST e' esperado e ignorado; qualquer outro erro
 * fica registrado em s_stats (ver storage_task.h) — antes era descartado
 * (void), escondendo uma falha aqui atras de um FR_INT_ERR aparentemente
 * vindo so' do f_open() seguinte. */
static void ensure_dir_path(const char *file_path)
{
    char buf[STORAGE_FORMAT_PATH_MAX];
    size_t len = strlen(file_path);
    size_t i;
    FRESULT fr;

    if (len >= sizeof(buf)) { return; }
    strcpy(buf, file_path);

    for (i = 1u; i < len; i++) {
        if (buf[i] == '/') {
            buf[i] = '\0';
            fr = f_mkdir(buf);
            if ((fr != FR_OK) && (fr != FR_EXIST)) {
                s_stats.last_write_step = 0u;
                s_stats.last_write_fresult = (uint8_t)fr;
            }
            buf[i] = '/';
        }
    }
}

/* Abre em modo "append" (cria se nao existir, posiciona no fim), escreve
 * a linha + '\n', fecha.
 *
 * Achado em bancada (2026-10): f_close() (ff.c) faz f_sync() ANTES de
 * liberar o lock do arquivo (dec_lock so' roda se f_sync()==FR_OK) — se o
 * sync (gravacao da entrada de diretorio/tamanho do arquivo) falhar,
 * f_close() retorna erro E o lock fica preso, travando a PROXIMA abertura
 * do mesmo arquivo com FR_LOCKED. O retorno de f_close() estava sendo
 * descartado aqui ((void)f_close(...)) — uma falha de sync silenciosa
 * contava como sucesso (messages_processed++) mesmo a linha nunca tendo
 * sido persistida de verdade (arquivo aparecia vazio no cartao). Agora o
 * retorno de f_close() e' conferido e tratado como erro real. */
static int append_line(const char *path, const char *line)
{
    FIL *file = &s_append_file;
    FRESULT fr;
    FRESULT fr_close;
    UINT written;
    size_t len = strlen(line);

    ensure_dir_path(path);

    fr = f_open(file, path, FA_WRITE | FA_OPEN_APPEND);
    if (fr != FR_OK) {
        s_stats.last_write_step = 1u;
        s_stats.last_write_fresult = (uint8_t)fr;
        return -1;
    }

    fr = f_write(file, line, (UINT)len, &written);
    if (fr != FR_OK) {
        s_stats.last_write_step = 2u;
        s_stats.last_write_fresult = (uint8_t)fr;
        (void)f_close(file);
        return -1;
    }
    if (written != (UINT)len) {
        s_stats.last_write_step = 4u;
        s_stats.last_write_fresult = 0xFFu;
        (void)f_close(file);
        return -1;
    }
    fr = f_write(file, "\n", 1u, &written);
    if (fr != FR_OK) {
        s_stats.last_write_step = 3u;
        s_stats.last_write_fresult = (uint8_t)fr;
        (void)f_close(file);
        return -1;
    }

    fr_close = f_close(file);
    if (fr_close != FR_OK) {
        s_stats.last_write_step = 5u;
        s_stats.last_write_fresult = (uint8_t)fr_close;
        return -1;
    }

    return 0;
}

static int process_log_message(const StorageTask_QueueMsg_t *msg)
{
    StorageFormat_LogEntry_t entry;
    AuditChain_t *chain;
    const char *subdir;
    char line[STORAGE_FORMAT_LINE_MAX];
    char path[STORAGE_FORMAT_PATH_MAX];

    memset(&entry, 0, sizeof(entry));
    entry.datetime = msg->datetime;
    entry.tz_offset_hours = msg->tz_offset_hours;
    entry.seq = msg->seq;
    entry.code = msg->code;
    entry.test_id = msg->test_id;
    entry.state_name = msg->state_name;
    entry.pressure_mmhg = msg->pressure_mmhg;
    entry.message = msg->message;

    switch (msg->type) {
        case STORAGE_MSG_LOG_EVENT: entry.level = STORAGE_LOG_LEVEL_EVT; chain = &s_chain_events; subdir = "events"; break;
        case STORAGE_MSG_LOG_ERROR: entry.level = STORAGE_LOG_LEVEL_ERR; chain = &s_chain_errors; subdir = "errors"; break;
        case STORAGE_MSG_LOG_AUDIT: entry.level = STORAGE_LOG_LEVEL_AUDIT; chain = &s_chain_audit; subdir = "audit"; break;
        default: return -1;
    }

    StorageFormat_BuildLogLine(&entry, chain, line, sizeof(line));
    StorageFormat_BuildLogPath(subdir, msg->datetime.year, msg->datetime.month, path, sizeof(path));

    return append_line(path, line);
}

static int process_test_rec_message(const StorageTask_QueueMsg_t *msg)
{
    StorageFormat_RecInfo_t info;
    CycleRecord_NcCode_t nc;
    AuditChain_t index_chain; /* index.csv tambem e' append-only, mas sem
                               * encadeamento de hash no proprio Apendice C
                               * (so' os logs de /logs/ sao encadeados) —
                               * usado aqui apenas como string vazia. */
    char rec_line[STORAGE_FORMAT_LINE_MAX];
    char index_line[STORAGE_FORMAT_LINE_MAX];
    char rec_path[STORAGE_FORMAT_PATH_MAX];
    char index_path[STORAGE_FORMAT_PATH_MAX];
    int rc;

    memset(&info, 0, sizeof(info));
    info.test_id = msg->test_id;
    info.participant_id = msg->participant_id;
    info.operator_id = msg->operator_id;
    info.cycle_number = msg->cycle_number;
    info.datetime = msg->datetime;
    info.metrics = msg->metrics;
    info.device_id = msg->device_id;
    info.firmware_version = msg->firmware_version;
    info.last_calibration = msg->last_calibration;

    nc = StorageFormat_BuildRecLine(&info, rec_line, sizeof(rec_line));
    StorageFormat_BuildTestRecPath(msg->test_id, msg->datetime.year, msg->datetime.month,
                                   rec_path, sizeof(rec_path));
    rc = append_line(rec_path, rec_line);
    if (rc != 0) { return rc; }

    /* index.csv: hash so' por integridade do PROPRIO arquivo index (nao
     * documentado no Apendice C como encadeado entre linhas — usa um hash
     * de uso unico sobre esta linha, sem corrente). */
    AuditChain_Init(&index_chain, NULL);
    {
        char line_no_hash[STORAGE_FORMAT_LINE_MAX];
        char hash_hex[AUDIT_CHAIN_HASH_HEX_LEN + 1u];

        StorageFormat_BuildIndexLine(&info, nc, "", line_no_hash, sizeof(line_no_hash));
        AuditChain_AppendLine(&index_chain, line_no_hash, hash_hex, sizeof(hash_hex));
        StorageFormat_BuildIndexLine(&info, nc, hash_hex, index_line, sizeof(index_line));
    }
    StorageFormat_BuildIndexPath(index_path, sizeof(index_path));
    return append_line(index_path, index_line);
}

static int process_message(const StorageTask_QueueMsg_t *msg)
{
    if (msg->type == STORAGE_MSG_TEST_REC) {
        return process_test_rec_message(msg);
    }
    return process_log_message(msg);
}

void StorageTask_Run(void)
{
    uint32_t remount_wait_ms = 0u;
    StorageTask_QueueMsg_t msg;

    for (;;) {
        /* osThreadGetStackSpace() reporta a MINIMA folga ja' observada (high
         * water mark), nao a atual — monotonicamente decrescente, serve
         * como diagnostico permanente (ver storage_task.h). */
        s_stats.storage_task_stack_free_bytes = osThreadGetStackSpace(osThreadGetId());

        if (s_stats.sd_mounted == 0u) {
            if (remount_wait_ms == 0u) {
                FRESULT fr;

                /* Diagnostico (ver nota em storage_task.h): le a deteccao
                 * fisica direto, fora do f_mount, para separar "nao
                 * detectou o cartao" de "detectou mas nao montou". */
                s_stats.card_detected = (BSP_SD_IsDetected() == SD_PRESENT) ? 1u : 0u;

                /* opt=1: forca montagem/deteccao agora, nao espera o 1o
                 * f_open() (quero saber do cartao ausente/corrompido logo,
                 * nao so' quando alguem tentar gravar). */
                fr = f_mount(&SDFatFS, SDPath, 1);
                s_stats.last_mount_fresult = (uint8_t)fr;
                if (fr == FR_OK) {
                    s_stats.sd_mounted = 1u;
                } else {
                    s_stats.remount_attempts++;
                }
            }
            remount_wait_ms += STORAGE_TASK_POLL_MS;
            if (remount_wait_ms >= STORAGE_REMOUNT_PERIOD_MS) {
                remount_wait_ms = 0u;
            }
        }

        if (osMessageQueueGet(s_queue, &msg, NULL, STORAGE_TASK_POLL_MS) == osOK) {
            if (s_stats.sd_mounted == 0u) {
                s_stats.messages_discarded_no_sd++;
            } else if (process_message(&msg) != 0) {
                s_stats.write_errors++;
                /* Escrita falhou — provavel cartao removido no meio da
                 * operacao. Desmonta explicitamente (nunca trava tentando
                 * escrever num handle morto) e deixa o remount periodico
                 * acima detectar quando (se) o cartao voltar — E005. */
                (void)f_mount(NULL, SDPath, 0);
                s_stats.sd_mounted = 0u;
            } else {
                s_stats.messages_processed++;
            }
        }
    }
}

/*
 * Pendente (fora do escopo desta primeira versao, nunca testada em
 * hardware):
 *  - Carregar o ultimo hash de cada log ao trocar de mes/no boot (hoje
 *    sempre reinicia do genesis — quebra o encadeamento intermensal que o
 *    Apendice C pede, "1o registro do mes referencia o ultimo hash do mes
 *    anterior").
 *  - Buffer de ENSAIO em RAM + f_sync periodico (curva de pressao
 *    amostrada, .csv) — este arquivo ainda nem tem gerador de conteudo.
 *  - f_sync a cada N linhas/segundos em vez de so' no f_close (reduz risco
 *    de perda do ULTIMO registro num corte de energia, ao custo de mais
 *    desgaste do cartao).
 *
 * Conferido (nao e' pendencia): _FS_REENTRANT=1 (ffconf.h) ja' tem os
 * callbacks de sincronizacao implementados em
 * Middlewares/Third_Party/FatFs/src/option/syscall.c (mutex/semaforo
 * CMSIS-RTOS2 criado dentro do proprio f_mount()) — util quando USB+
 * storage dividirem o mesmo volume (Fase 7), nao usado ainda porque so'
 * esta tarefa toca FatFS.
 */
