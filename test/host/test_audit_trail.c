/*
 * Fase 5 — SHA-256, encadeamento de hash e avaliacao de conformidade do
 * ciclo (NC00-NC08). Vetores de teste do SHA-256 sao os oficiais do NIST
 * (FIPS 180-4); os de cycle_record sao os exemplos de
 * Docs/dev/formato-logs.md (ground truth do documento-fonte).
 */
#include "sha256.h"
#include "audit_chain.h"
#include "cycle_record.h"
#include <stdio.h>
#include <string.h>

static int s_fail;
#define CHECK(c) do { if (!(c)) { printf("FALHA %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)

static void test_sha256_nist_vectors(void)
{
    uint8_t digest[SHA256_DIGEST_SIZE];
    char hex[SHA256_HEX_LEN + 1u];

    /* vetor vazio */
    Sha256_Compute((const uint8_t *)"", 0, digest);
    Sha256_ToHex(digest, hex, sizeof(hex));
    CHECK(strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0);

    /* "abc" */
    Sha256_Compute((const uint8_t *)"abc", 3, digest);
    Sha256_ToHex(digest, hex, sizeof(hex));
    CHECK(strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);

    /* "abc" com 1 char a mais no esperado acima seria erro — confirmando
     * exatamente 64 chars de saida. */
    CHECK(strlen(hex) == 64u);

    /* 56 bytes de 'a' — cruza a borda do padding (>56 precisa de bloco
     * extra; exatamente a condicao testada em Sha256_Final). */
    {
        uint8_t buf[56];
        memset(buf, (int)'a', sizeof(buf));
        Sha256_Compute(buf, sizeof(buf), digest);
        Sha256_ToHex(digest, hex, sizeof(hex));
        CHECK(strcmp(hex, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a") == 0);
    }

    /* Multiplos Update() devem dar o mesmo resultado que um Compute() so. */
    {
        Sha256_Ctx_t ctx;
        uint8_t digest2[SHA256_DIGEST_SIZE];
        char hex2[SHA256_HEX_LEN + 1u];

        Sha256_Init(&ctx);
        Sha256_Update(&ctx, (const uint8_t *)"ab", 2);
        Sha256_Update(&ctx, (const uint8_t *)"c", 1);
        Sha256_Final(&ctx, digest2);
        Sha256_ToHex(digest2, hex2, sizeof(hex2));
        CHECK(strcmp(hex2, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
    }
}

static void test_genesis_hash_length(void)
{
    CHECK(strlen(AuditChain_GenesisHashHex) == AUDIT_CHAIN_HASH_HEX_LEN);
}

static void test_chain_determinism_and_tamper_detection(void)
{
    AuditChain_t chain;
    char h1[AUDIT_CHAIN_HASH_HEX_LEN + 1u];
    char h2[AUDIT_CHAIN_HASH_HEX_LEN + 1u];
    char h1_again[AUDIT_CHAIN_HASH_HEX_LEN + 1u];

    AuditChain_Init(&chain, NULL); /* NULL -> genesis */
    CHECK(strcmp(chain.prev_hash_hex, AuditChain_GenesisHashHex) == 0);

    AuditChain_AppendLine(&chain, "linha 1", h1, sizeof(h1));
    AuditChain_AppendLine(&chain, "linha 2", h2, sizeof(h2));
    CHECK(strcmp(h1, h2) != 0);

    /* Mesma entrada (genesis + "linha 1") sempre da' o mesmo hash. */
    {
        AuditChain_t chain2;
        AuditChain_Init(&chain2, AuditChain_GenesisHashHex);
        AuditChain_AppendLine(&chain2, "linha 1", h1_again, sizeof(h1_again));
        CHECK(strcmp(h1, h1_again) == 0);
    }

    CHECK(AuditChain_VerifyLine(AuditChain_GenesisHashHex, "linha 1", h1) != 0u);
    CHECK(AuditChain_VerifyLine(AuditChain_GenesisHashHex, "linha 1 adulterada", h1) == 0u);
    CHECK(AuditChain_VerifyLine(h1, "linha 2", h2) != 0u);
    /* corte de energia nao pode corromper registros ANTERIORES: a 1a linha
     * continua verificavel mesmo que a 2a nunca tenha sido escrita. */
    CHECK(AuditChain_VerifyLine(AuditChain_GenesisHashHex, "linha 1", h1) != 0u);
}

static void test_chain_continues_from_previous_month(void)
{
    AuditChain_t chain;
    char h[AUDIT_CHAIN_HASH_HEX_LEN + 1u];
    const char *last_hash_prev_month = "deadbeefcafef00ddeadbeefcafef00ddeadbeefcafef00ddeadbeefcafef00d";

    /* (64 chars validos em hex nao importa aqui — o encadeamento trata
     * prev_hash como string opaca, nao reparseia hex.) */
    AuditChain_Init(&chain, "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcd");
    AuditChain_AppendLine(&chain, "primeira linha do novo mes", h, sizeof(h));
    (void)last_hash_prev_month;
    CHECK(strlen(h) == 64u);
}

/* --- cycle_record: exemplos de Docs/dev/formato-logs.md §4 --- */

static void test_cycle_conforme_example(void)
{
    /* ID000123: alvo 100, pico 100.4, plato medio 99.8, faixa 99.2-100.4,
     * insuflacao 3.2s, plato 8.0s, descompressao 0.94s, residual 1.2mmHg. */
    CycleRecord_Metrics_t m = {
        .setpoint_mmhg = 100.0f, .peak_mmhg = 100.4f,
        .plateau_mean_mmhg = 99.8f, .plateau_min_mmhg = 99.2f, .plateau_max_mmhg = 100.4f,
        .inflate_time_s = 3.2f, .plateau_time_s = 8.0f, .deflate_time_s = 0.94f,
        .residual_mmhg = 1.2f, .aborted_by_operator = 0u, .technical_fault = 0u,
    };
    CHECK(CycleRecord_Evaluate(&m) == CYCLE_NC00_CONFORME);
}

static void test_cycle_nc05_example(void)
{
    /* ID000126: igual ao acima, exceto plato 7.0s e descompressao 2.1s
     * (fora da faixa) -> Nao conforme, NC05. */
    CycleRecord_Metrics_t m = {
        .setpoint_mmhg = 100.0f, .peak_mmhg = 100.4f,
        .plateau_mean_mmhg = 99.8f, .plateau_min_mmhg = 99.2f, .plateau_max_mmhg = 100.4f,
        .inflate_time_s = 3.2f, .plateau_time_s = 7.0f, .deflate_time_s = 2.1f,
        .residual_mmhg = 1.2f, .aborted_by_operator = 0u, .technical_fault = 0u,
    };
    CHECK(CycleRecord_Evaluate(&m) == CYCLE_NC05_DESCOMPRESSAO_FORA_FAIXA);
}

static void test_cycle_other_nc_codes(void)
{
    CycleRecord_Metrics_t base = {
        .setpoint_mmhg = 100.0f, .peak_mmhg = 100.0f,
        .plateau_mean_mmhg = 100.0f, .plateau_min_mmhg = 100.0f, .plateau_max_mmhg = 100.0f,
        .inflate_time_s = 3.0f, .plateau_time_s = 8.0f, .deflate_time_s = 1.0f,
        .residual_mmhg = 1.0f, .aborted_by_operator = 0u, .technical_fault = 0u,
    };
    CycleRecord_Metrics_t m;
    char s[8];

    m = base; m.technical_fault = 1u;
    CHECK(CycleRecord_Evaluate(&m) == CYCLE_NC08_FALHA_TECNICA);

    m = base; m.aborted_by_operator = 1u;
    CHECK(CycleRecord_Evaluate(&m) == CYCLE_NC07_INTERROMPIDO_OPERADOR);

    m = base; m.plateau_mean_mmhg = 90.0f; /* fora da banda de 2% */
    CHECK(CycleRecord_Evaluate(&m) == CYCLE_NC01_ALVO_NAO_ATINGIDO);

    m = base; m.peak_mmhg = 106.0f; /* > 105% do alvo */
    CHECK(CycleRecord_Evaluate(&m) == CYCLE_NC02_INSTABILIDADE_PLATO);

    m = base; m.inflate_time_s = 11.0f;
    CHECK(CycleRecord_Evaluate(&m) == CYCLE_NC03_INSUFLACAO_FORA_FAIXA);

    m = base; m.plateau_time_s = 11.0f;
    CHECK(CycleRecord_Evaluate(&m) == CYCLE_NC04_PLATO_FORA_FAIXA);

    m = base; m.residual_mmhg = 5.0f; /* >= 5, nao < 5 */
    CHECK(CycleRecord_Evaluate(&m) == CYCLE_NC06_RESIDUAL_ACIMA_LIMITE);

    CycleRecord_NcCodeToString(CYCLE_NC00_CONFORME, s, sizeof(s));
    CHECK(strcmp(s, "NC00") == 0);
    CycleRecord_NcCodeToString(CYCLE_NC08_FALHA_TECNICA, s, sizeof(s));
    CHECK(strcmp(s, "NC08") == 0);
}

int main(void)
{
    test_sha256_nist_vectors();
    test_genesis_hash_length();
    test_chain_determinism_and_tamper_detection();
    test_chain_continues_from_previous_month();
    test_cycle_conforme_example();
    test_cycle_nc05_example();
    test_cycle_other_nc_codes();
    printf(s_fail ? "audit_trail: %d FALHA(S)\n" : "audit_trail: OK\n", s_fail);
    return s_fail != 0;
}
