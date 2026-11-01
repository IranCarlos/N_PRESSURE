/* Fase 5.3 — caminhos e conteudo dos arquivos do SD (Apendice C). */
#include "storage_format.h"
#include "fmt_fixed.h"
#include <stdio.h>
#include <string.h>

static int s_fail;
#define CHECK(c) do { if (!(c)) { printf("FALHA %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)
#define CHECK_CONTAINS(hay, needle) CHECK(strstr((hay), (needle)) != NULL)

static void test_paths(void)
{
    char path[STORAGE_FORMAT_PATH_MAX];

    StorageFormat_BuildTestRecPath(123u, 2026u, 8u, path, sizeof(path));
    CHECK(strcmp(path, "/tests/2026/08/T00000123.rec") == 0);

    StorageFormat_BuildTestCsvPath(123u, 2026u, 8u, path, sizeof(path));
    CHECK(strcmp(path, "/tests/2026/08/T00000123.csv") == 0);

    StorageFormat_BuildIndexPath(path, sizeof(path));
    CHECK(strcmp(path, "/tests/index.csv") == 0);

    StorageFormat_BuildLogPath("events", 2026u, 9u, path, sizeof(path));
    CHECK(strcmp(path, "/logs/events/2026-09.log") == 0);
    StorageFormat_BuildLogPath("errors", 2026u, 9u, path, sizeof(path));
    CHECK(strcmp(path, "/logs/errors/2026-09.log") == 0);
    StorageFormat_BuildLogPath("audit", 2026u, 9u, path, sizeof(path));
    CHECK(strcmp(path, "/logs/audit/2026-09.log") == 0);
}

static StorageFormat_RecInfo_t base_rec_info(void)
{
    StorageFormat_RecInfo_t info;
    memset(&info, 0, sizeof(info));
    info.test_id = 123u;
    info.participant_id = "P037";
    info.operator_id = "OP01";
    info.cycle_number = 2u;
    info.datetime.year = 2026u; info.datetime.month = 8u; info.datetime.day = 11u;
    info.datetime.hour = 9u; info.datetime.minute = 42u; info.datetime.second = 16u;
    info.device_id = "nPressure NP001";
    info.firmware_version = "FW 1.0.3";
    info.last_calibration.year = 2026u; info.last_calibration.month = 8u; info.last_calibration.day = 2u;
    info.last_calibration.hour = 18u; info.last_calibration.minute = 23u; info.last_calibration.second = 47u;
    return info;
}

/* Mesmos dois exemplos de Docs/dev/formato-logs.md §4 ja' usados em
 * test_audit_trail.c — aqui conferindo que o .rec/index montados refletem
 * o mesmo resultado (NC00/NC05) e os numeros certos, nao o texto literal
 * do documento-fonte (ver nota de decisao em storage_format.h: ponto
 * decimal, nao virgula). */
static void test_rec_line_conforme(void)
{
    char line[STORAGE_FORMAT_LINE_MAX];
    StorageFormat_RecInfo_t info = base_rec_info();
    CycleRecord_NcCode_t nc;

    info.metrics.setpoint_mmhg = 100.0f; info.metrics.peak_mmhg = 100.4f;
    info.metrics.plateau_mean_mmhg = 99.8f; info.metrics.plateau_min_mmhg = 99.2f;
    info.metrics.plateau_max_mmhg = 100.4f;
    info.metrics.inflate_time_s = 3.2f; info.metrics.plateau_time_s = 8.0f;
    info.metrics.deflate_time_s = 0.94f; info.metrics.residual_mmhg = 1.2f;

    nc = StorageFormat_BuildRecLine(&info, line, sizeof(line));
    printf("  [rec_conforme] %s\n", line);

    CHECK(nc == CYCLE_NC00_CONFORME);
    CHECK_CONTAINS(line, "T00000123");
    CHECK_CONTAINS(line, "P037");
    CHECK_CONTAINS(line, "Conforme");
    CHECK_CONTAINS(line, "NC00");
    CHECK_CONTAINS(line, "E000"); /* sem falha tecnica */
    CHECK_CONTAINS(line, "100.4"); /* pico, ponto decimal — ver nota em storage_format.h */
    CHECK_CONTAINS(line, "0.9");   /* descompressao 0.94s arredondado p/ 1 casa */
}

/* Linha completa no cartao: com 256 B o fim do .rec e o hash do log se perdiam. */
static void test_lines_fit_buffer(void)
{
    char line[STORAGE_FORMAT_LINE_MAX];
    StorageFormat_RecInfo_t info = base_rec_info();
    StorageFormat_LogEntry_t entry;
    AuditChain_t chain;
    char hash[AUDIT_CHAIN_HASH_HEX_LEN + 1u];
    static const char msg[] = "rec=0123456789abcdef01234567 csv=0123456789abcdef01234567 xxxxxx";

    info.metrics.setpoint_mmhg = 170.0f;
    (void)StorageFormat_BuildRecLine(&info, line, sizeof(line));
    CHECK(strlen(line) < (STORAGE_FORMAT_LINE_MAX - 2u));
    CHECK_CONTAINS(line, "ultima calibracao");

    memset(&entry, 0, sizeof(entry));
    entry.code = "E013";
    entry.test_id = 99999999u;
    entry.state_name = "ESTABILIZANDO";
    entry.message = msg;
    AuditChain_Init(&chain, NULL);
    StorageFormat_BuildLogLine(&entry, &chain, line, sizeof(line));
    CHECK(strlen(line) < (STORAGE_FORMAT_LINE_MAX - 2u));
    CHECK(StorageFormat_FindLastHash(line, strlen(line), hash) == 1u);
    CHECK(strcmp(hash, chain.prev_hash_hex) == 0);
}

static void test_rec_line_without_plateau(void)
{
    char line[STORAGE_FORMAT_LINE_MAX];
    StorageFormat_RecInfo_t info = base_rec_info();

    info.metrics.plateau_min_mmhg = 1.0e9f;
    info.metrics.plateau_max_mmhg = -1.0e9f;
    (void)StorageFormat_BuildRecLine(&info, line, sizeof(line));
    CHECK_CONTAINS(line, "Faixa 0.0-0.0");
}

static void test_index_scan_any_block_size(void)
{
    static const char text[] = "T00000031;x;y\nlixo;T00000099;\nT00000123;a;b\nT00000007;z;w\nT0000;n\n";
    size_t step;

    for (step = 1u; step <= 7u; step++) {
        StorageFormat_IndexScan_t scan;
        size_t pos;

        memset(&scan, 0, sizeof(scan));
        for (pos = 0u; pos < (sizeof(text) - 1u); pos += step) {
            size_t n = ((pos + step) > (sizeof(text) - 1u)) ? ((sizeof(text) - 1u) - pos) : step;

            StorageFormat_IndexScanFeed(&scan, (const uint8_t *)&text[pos], n);
        }
        CHECK(scan.max_id == 123u);
    }
}

static void test_find_last_hash(void)
{
    char hash[AUDIT_CHAIN_HASH_HEX_LEN + 1u];
    char text[300];
    const char *a = "1111111111111111111111111111111111111111111111111111111111111111";
    const char *b = "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789";

    (void)snprintf(text, sizeof(text), "x;hash=%s\ny;hash=%s\n", a, b);
    CHECK(StorageFormat_FindLastHash(text, strlen(text), hash) == 1u);
    CHECK(strcmp(hash, b) == 0);

    /* firmware antigo: a ultima linha perdeu o fim do hash */
    (void)snprintf(text, sizeof(text), "y;hash=%.57s\n", b);
    CHECK(StorageFormat_FindLastHash(text, strlen(text), hash) == 0u);
    CHECK(StorageFormat_FindLastHash("", 0u, hash) == 0u);
}

static void test_curve_line(void)
{
    char line[32];

    StorageFormat_BuildCurveLine(0u, 0, line, sizeof(line));
    CHECK(strcmp(line, "0.00,0.0") == 0);
    StorageFormat_BuildCurveLine(32u, 1004, line, sizeof(line));
    CHECK(strcmp(line, "3.20,100.4") == 0);
    StorageFormat_BuildCurveLine(2399u, -3, line, sizeof(line));
    CHECK(strcmp(line, "239.90,-0.3") == 0);
}

static void test_tail_offset_reaches_end_of_file(void)
{
    uint32_t size;

    CHECK(StorageFormat_TailReadOffset(0u, 1024u) == 0u);
    CHECK(StorageFormat_TailReadOffset(1024u, 1024u) == 0u);
    for (size = 1025u; size < 1100u; size++) {
        uint32_t off = StorageFormat_TailReadOffset(size, 1024u);

        CHECK((off % 4u) == 0u);
        CHECK((off + 1024u) >= size);
        CHECK(off <= (size - 1021u));
    }
}

static void test_fmt_fixed(void)
{
    char t[FMT_FIXED_LEN];

    Fmt_Fixed(t, sizeof(t), 1.00123f, 5u, 0u);
    CHECK(strcmp(t, "1.00123") == 0);
    Fmt_Fixed(t, sizeof(t), -2.5f, 2u, 0u);
    CHECK(strcmp(t, "-2.50") == 0);
    Fmt_Fixed(t, sizeof(t), 0.05f, 2u, 0u);
    CHECK(strcmp(t, "0.05") == 0);
    Fmt_Fixed(t, sizeof(t), 1000.0f, 4u, 1u);
    CHECK(strcmp(t, "1000") == 0);
    Fmt_Fixed(t, sizeof(t), 0.45f, 4u, 1u);
    CHECK(strcmp(t, "0.45") == 0);
    Fmt_Fixed(t, sizeof(t), 0.0f, 4u, 1u);
    CHECK(strcmp(t, "0") == 0);
    Fmt_Fixed(t, sizeof(t), -0.001f, 1u, 0u);
    CHECK(strcmp(t, "0.0") == 0);
    Fmt_Fixed(t, sizeof(t), 1.0e6f, 4u, 1u); /* perde casas, nao o numero */
    CHECK(strcmp(t, "1000000") == 0);
    Fmt_Fixed(t, sizeof(t), 9.99999f, 1u, 0u);
    CHECK(strcmp(t, "10.0") == 0);
}

static void test_rec_line_nc05(void)
{
    char line[STORAGE_FORMAT_LINE_MAX];
    StorageFormat_RecInfo_t info = base_rec_info();
    info.test_id = 126u;
    CycleRecord_NcCode_t nc;

    info.metrics.setpoint_mmhg = 100.0f; info.metrics.peak_mmhg = 100.4f;
    info.metrics.plateau_mean_mmhg = 99.8f; info.metrics.plateau_min_mmhg = 99.2f;
    info.metrics.plateau_max_mmhg = 100.4f;
    info.metrics.inflate_time_s = 3.2f; info.metrics.plateau_time_s = 7.0f;
    info.metrics.deflate_time_s = 3.6f; info.metrics.residual_mmhg = 1.2f;

    nc = StorageFormat_BuildRecLine(&info, line, sizeof(line));
    printf("  [rec_nc05] %s\n", line);

    CHECK(nc == CYCLE_NC05_DESCOMPRESSAO_FORA_FAIXA);
    CHECK_CONTAINS(line, "T00000126");
    CHECK_CONTAINS(line, "Nao conforme");
    CHECK_CONTAINS(line, "NC05");
}

static void test_rec_line_technical_fault(void)
{
    char line[STORAGE_FORMAT_LINE_MAX];
    StorageFormat_RecInfo_t info = base_rec_info();

    info.metrics.setpoint_mmhg = 100.0f;
    info.metrics.technical_fault = 1u;

    (void)StorageFormat_BuildRecLine(&info, line, sizeof(line));
    CHECK_CONTAINS(line, "E0XX");
    CHECK_CONTAINS(line, "NC08");
}

static void test_index_line(void)
{
    char line[STORAGE_FORMAT_LINE_MAX];
    StorageFormat_RecInfo_t info = base_rec_info();
    info.metrics.setpoint_mmhg = 100.0f;
    info.metrics.peak_mmhg = 100.4f;

    StorageFormat_BuildIndexLine(&info, CYCLE_NC00_CONFORME, "deadbeef", line, sizeof(line));
    printf("  [index] %s\n", line);

    CHECK_CONTAINS(line, "T00000123;");
    CHECK_CONTAINS(line, "OP01");
    CHECK_CONTAINS(line, "100.4");
    CHECK_CONTAINS(line, "NC00");
    CHECK_CONTAINS(line, "deadbeef");
}

static void test_log_line_hash_chain(void)
{
    AuditChain_t chain;
    StorageFormat_LogEntry_t entry;
    char line1[STORAGE_FORMAT_LINE_MAX];
    char line2[STORAGE_FORMAT_LINE_MAX];
    char prev_after_line1[AUDIT_CHAIN_HASH_HEX_LEN + 1u];

    AuditChain_Init(&chain, NULL); /* genesis: 1o arquivo do mes */

    memset(&entry, 0, sizeof(entry));
    entry.datetime.year = 2026u; entry.datetime.month = 9u; entry.datetime.day = 19u;
    entry.datetime.hour = 14u; entry.datetime.minute = 32u; entry.datetime.second = 10u;
    entry.tz_offset_hours = -3;
    entry.seq = 123u;
    entry.level = STORAGE_LOG_LEVEL_ERR;
    entry.code = "E003";
    entry.test_id = 45u;
    entry.state_name = "INFLANDO";
    entry.pressure_mmhg = 187.4f;
    entry.message = "Timeout de inflar";

    StorageFormat_BuildLogLine(&entry, &chain, line1, sizeof(line1));
    printf("  [log1] %s\n", line1);

    CHECK_CONTAINS(line1, "2026-09-19T14:32:10-03:00");
    CHECK_CONTAINS(line1, "seq=000123");
    CHECK_CONTAINS(line1, "lvl=ERR");
    CHECK_CONTAINS(line1, "code=E003");
    CHECK_CONTAINS(line1, "test=T00000045");
    CHECK_CONTAINS(line1, "state=INFLANDO");
    CHECK_CONTAINS(line1, "p=187.4");
    CHECK_CONTAINS(line1, "msg=Timeout de inflar");
    CHECK_CONTAINS(line1, "prev=");
    CHECK_CONTAINS(line1, "hash=");
    /* 1a linha do mes referencia o hash do genesis (ou do ultimo hash do
     * mes anterior, se AuditChain_Init tivesse recebido um). */
    CHECK_CONTAINS(line1, AuditChain_GenesisHashHex);

    strcpy(prev_after_line1, chain.prev_hash_hex);

    entry.seq = 124u;
    entry.level = STORAGE_LOG_LEVEL_EVT;
    entry.code = "";
    entry.test_id = 0u; /* sem ensaio associado */
    entry.message = "Ciclo concluido";
    StorageFormat_BuildLogLine(&entry, &chain, line2, sizeof(line2));
    printf("  [log2] %s\n", line2);

    CHECK_CONTAINS(line2, "test=;"); /* sem ensaio associado */
    CHECK_CONTAINS(line2, prev_after_line1); /* encadeado a partir da linha anterior */
    CHECK(strcmp(chain.prev_hash_hex, prev_after_line1) != 0); /* avancou de novo */
}

int main(void)
{
    test_paths();
    test_rec_line_conforme();
    test_lines_fit_buffer();
    test_rec_line_without_plateau();
    test_index_scan_any_block_size();
    test_find_last_hash();
    test_curve_line();
    test_fmt_fixed();
    test_tail_offset_reaches_end_of_file();
    test_rec_line_nc05();
    test_rec_line_technical_fault();
    test_index_line();
    test_log_line_hash_chain();
    printf(s_fail ? "storage_format: %d FALHA(S)\n" : "storage_format: OK\n", s_fail);
    return s_fail != 0;
}
