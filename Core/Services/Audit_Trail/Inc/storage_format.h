/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-11
 *
 * Caminhos e conteudo dos arquivos do cartao SD (Apendice C do prompt do
 * projeto + Docs/dev/formato-logs.md). C puro, sem HAL/FatFS — so' monta
 * strings; quem abre/escreve arquivo de verdade e' o storage_task
 * (Fase 5.3, ainda nao implementado).
 *
 * IMPORTANTE — decisoes de unificacao/formato ainda NAO confirmadas pelo
 * usuario (revisar antes de gravar em ensaio real):
 *  - O "ID000123" (6 digitos) do documento-fonte (Trilha) e o "T00000123"
 *    (8 digitos) do Apendice C sao tratados como o MESMO identificador,
 *    com a largura de 8 digitos do Apendice C adotada em todo lugar —
 *    formato-logs.md §7 ja sinalizava essa divergencia sem resolver.
 *  - Numeros no `.rec` usam PONTO decimal (100.4), nao virgula (100,4)
 *    como no exemplo do documento-fonte — por consistencia com o resto do
 *    firmware/docs, que usa ponto em todo lugar (Live Watch, logs, os
 *    outros arquivos de Docs/dev). O documento-fonte provavelmente so'
 *    reflete a notacao PT-BR de quem escreveu o exemplo, nao um requisito
 *    de formato.
 *  - `/tests/index.csv` segue o Apendice C literal (`;`-separado, com
 *    hash) — a "Exportação CSV" de formato-logs.md §5 (`,`-separado, sem
 *    hash, colunas diferentes) e' um artefato DIFERENTE, para gerar sob
 *    demanda (ex.: Fase 7, ferramenta de PC), nao o que o firmware grava
 *    a cada ensaio.
 */
#ifndef STORAGE_FORMAT_H
#define STORAGE_FORMAT_H

#include <stdint.h>
#include <stddef.h>
#include "cycle_record.h"
#include "audit_chain.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STORAGE_FORMAT_PATH_MAX   (48u)
#define STORAGE_FORMAT_LINE_MAX   (256u)

typedef struct {
    uint16_t year;   /* ex.: 2026 */
    uint8_t  month;  /* 1-12 */
    uint8_t  day;    /* 1-31 */
    uint8_t  hour;   /* 0-23 */
    uint8_t  minute; /* 0-59 */
    uint8_t  second; /* 0-59 */
} StorageFormat_DateTime_t;

/* --- Caminhos (Apendice C) --- */

/* "/tests/2026/10/T00000123.rec" (out_size >= STORAGE_FORMAT_PATH_MAX). */
void StorageFormat_BuildTestRecPath(uint32_t test_id, uint16_t year, uint8_t month,
                                    char *out, size_t out_size);
/* Mesma pasta/ID, extensao .csv (curva de pressao). */
void StorageFormat_BuildTestCsvPath(uint32_t test_id, uint16_t year, uint8_t month,
                                    char *out, size_t out_size);
/* "/tests/index.csv" — fixo, sem variar por data. */
void StorageFormat_BuildIndexPath(char *out, size_t out_size);
/* subdir: "events", "errors" ou "audit" -> "/logs/<subdir>/2026-10.log". */
void StorageFormat_BuildLogPath(const char *subdir, uint16_t year, uint8_t month,
                                char *out, size_t out_size);

/* --- Conteudo --- */

typedef struct {
    uint32_t test_id;
    const char *participant_id;   /* ex.: "P037" (paciente/sujeito — P2) */
    const char *operator_id;      /* ex.: "OP01" (quem opera o equipamento — P2) */
    uint16_t    cycle_number;     /* "Ciclo N" */
    StorageFormat_DateTime_t datetime;
    CycleRecord_Metrics_t metrics; /* mesmas metricas usadas em CycleRecord_Evaluate() */
    const char *device_id;        /* ex.: "nPressure NP001" */
    const char *firmware_version; /* ex.: "FW 1.0.3" */
    StorageFormat_DateTime_t last_calibration;
} StorageFormat_RecInfo_t;

/* Monta a linha do .rec (resumo do ensaio — campos do documento-fonte +
 * NC0x [avaliado internamente via CycleRecord_Evaluate] + E0xx separados,
 * ver cycle_record.h e a nota de decisao P1 acima do arquivo). Devolve o
 * codigo NC usado (CYCLE_NC00_CONFORME.. ) para quem grava tambem usar em
 * StorageFormat_BuildIndexLine(). */
CycleRecord_NcCode_t StorageFormat_BuildRecLine(const StorageFormat_RecInfo_t *info,
                                                char *out, size_t out_size);

/* "/tests/index.csv": id;data;operador;setpoint;pico;resultado;hash
 * (Apendice C literal). hash_hex: ja' calculado via AuditChain_AppendLine
 * sobre esta mesma linha (sem o campo hash) — ver audit_chain.h. */
void StorageFormat_BuildIndexLine(const StorageFormat_RecInfo_t *info,
                                  CycleRecord_NcCode_t nc_code, const char *hash_hex,
                                  char *out, size_t out_size);

typedef enum {
    STORAGE_LOG_LEVEL_EVT = 0, /* evento normal */
    STORAGE_LOG_LEVEL_ERR,     /* erro tecnico (E0xx) */
    STORAGE_LOG_LEVEL_AUDIT    /* sessao DEV, alteracao de parametro, calibracao */
} StorageFormat_LogLevel_t;

typedef struct {
    StorageFormat_DateTime_t datetime;
    int8_t      tz_offset_hours; /* ex.: -3 para America/Sao_Paulo */
    uint32_t    seq;             /* contador sequencial, nao reinicia por mes */
    StorageFormat_LogLevel_t level;
    const char *code;            /* ex.: "E003", ou "" se nao aplicavel */
    uint32_t    test_id;         /* 0 = nao associado a um ensaio */
    const char *state_name;      /* ex.: "INFLANDO" */
    float       pressure_mmhg;
    const char *message;         /* texto livre, sem ';' (delimitador do formato) */
} StorageFormat_LogEntry_t;

/* Monta "<ISO8601>;seq=NNNNNN;lvl=X;code=E0xx;test=T00000045;state=S;p=P;
 * msg=M;prev=<hash anterior>;hash=<calculado>" (Apendice C). O hash e'
 * calculado aqui dentro via AuditChain_AppendLine(chain, ...) sobre tudo
 * ANTES do campo hash — chain e' atualizado (proxima linha encadeia daqui). */
void StorageFormat_BuildLogLine(const StorageFormat_LogEntry_t *entry,
                                AuditChain_t *chain,
                                char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* STORAGE_FORMAT_H */
