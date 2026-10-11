/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-11
 *
 * Encadeamento por hash dos logs (Apendice C/D do prompt do projeto;
 * Docs/dev/formato-logs.md regra 2): cada linha carrega
 * hash = SHA256(prev_hash_hex || conteudo_da_linha); a 1a linha do mes
 * referencia o ultimo hash do mes anterior. C puro, sem FatFS/SD — so'
 * decide o hash e monta a linha final; quem grava no cartao e' outra
 * camada (storage_task, Fase 5.2).
 */
#ifndef AUDIT_CHAIN_H
#define AUDIT_CHAIN_H

#include <stdint.h>
#include <stddef.h>
#include "sha256.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIT_CHAIN_HASH_HEX_LEN SHA256_HEX_LEN /* 64 chars, sem terminador */

/* Hash "genesis" (primeira linha de sempre, sem mes anterior): 64 zeros. */
extern const char AuditChain_GenesisHashHex[AUDIT_CHAIN_HASH_HEX_LEN + 1u];

typedef struct {
    char prev_hash_hex[AUDIT_CHAIN_HASH_HEX_LEN + 1u];
} AuditChain_t;

/* prev_hash_hex: ultimo hash conhecido (ex.: lido do fim do arquivo do mes
 * anterior, ou AuditChain_GenesisHashHex se nao houver historico). */
void AuditChain_Init(AuditChain_t *chain, const char *prev_hash_hex);

/*
 * Calcula hash = SHA256(prev_hash_hex || line_content) e:
 *  - escreve o hex do novo hash em out_hash_hex (>= 65 bytes);
 *  - atualiza chain->prev_hash_hex para o novo valor (proxima linha
 *    encadeia a partir daqui).
 * line_content NAO deve incluir o campo hash (ele e' o que falta montar).
 */
void AuditChain_AppendLine(AuditChain_t *chain, const char *line_content,
                           char *out_hash_hex, size_t out_hash_hex_size);

/* Verifica se hash_hex == SHA256(prev_hash_hex || line_content). Usado para
 * auditar um arquivo existente (corte de energia durante gravacao nao pode
 * corromper registros anteriores — criterio de aceitacao da Fase 5). */
uint8_t AuditChain_VerifyLine(const char *prev_hash_hex, const char *line_content,
                              const char *hash_hex);

#ifdef __cplusplus
}
#endif

#endif /* AUDIT_CHAIN_H */
