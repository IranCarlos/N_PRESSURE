/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-22
 */

/**
 * @file params.h
 * @brief Tabela de parametros de controle (Fase 7.6, Apendice E): nome,
 *        unidade, faixa e valor de cada ajuste da malha, num lugar so'.
 *
 * C puro (sem HAL), testado no host. Nao guarda os valores: aponta para
 * onde o controle ja' os le (SystemConfig_t, e dentro do ControlCore_t a
 * copia de seguranca e o ajuste fino), injetados por Params_Bind() - mesmo
 * padrao de ControlLoop_Init(). Escrever aqui muda o controle na hora;
 * quem chama (UsbProto, so' em sessao DEV) so' permite em STANDBY.
 *
 * Os valores de FABRICA sao os que estiverem nos alvos no momento do
 * Params_Bind() (EEPROM_Storage_LoadDefaults, s_control_safety,
 * ControlCore_DefaultTuning) - uma unica fonte, sem segunda copia aqui.
 *
 * Persistencia: Params_Serialize()/Params_Deserialize() montam/leem um
 * bloco com magic, versao, numero de sequencia, pares (id, valor) e CRC32.
 * Por id e nao por posicao: um firmware novo com parametros a mais carrega
 * os antigos e completa o resto com o default; um parametro removido e'
 * ignorado. Quem grava (eeprom_storage_io) mantem duas copias A/B.
 */

#ifndef PARAMS_H
#define PARAMS_H

#include <stddef.h>
#include <stdint.h>
#include "control_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PARAM_TYPE_F32 = 0,
    PARAM_TYPE_U32 = 1   /* inteiro (ms) - no protocolo tambem viaja como float */
} Param_Type_t;

typedef enum {
    PARAM_GROUP_PID = 0,
    PARAM_GROUP_ESTABILIZACAO,
    PARAM_GROUP_VALVULA,
    PARAM_GROUP_APROXIMACAO,
    PARAM_GROUP_SEGURANCA,
    PARAM_GROUP_VAZAMENTO,
    PARAM_GROUP_LIMITES
} Param_Group_t;

typedef struct {
    uint16_t    id;     /* estavel entre versoes de firmware - nunca reaproveitar */
    uint8_t     type;   /* Param_Type_t */
    uint8_t     group;  /* Param_Group_t */
    const char *name;   /* ex.: "pid.kp" (<= 23 caracteres, ASCII) */
    const char *unit;   /* ex.: "mmHg", "ms", "" (<= 7 caracteres) */
    float       min;
    float       max;
} Param_Desc_t;

typedef struct {
    SystemConfig_t             *cfg;
    ControlCore_SafetyConfig_t *safety;
    ControlCore_TuningConfig_t *tuning;
} Params_Targets_t;

typedef enum {
    PARAMS_OK = 0,
    PARAMS_ERR_ID,          /* id desconhecido */
    PARAMS_ERR_RANGE,       /* fora de [min, max], NaN ou nao inteiro (U32) */
    PARAMS_ERR_CONSISTENCY  /* conflita com outro parametro (ex.: min > max) */
} Params_Result_t;

#define PARAMS_BLOB_MAX (16U + (64U * 8U)) /* cabecalho + ate' 64 pares + crc */

/* Liga a tabela aos alvos e memoriza os valores atuais como "de fabrica"
 * e como "salvos" (nada a gravar ate' alguem mudar). */
void Params_Bind(const Params_Targets_t *t);

uint16_t Params_Count(void);
const Param_Desc_t *Params_DescAt(uint16_t index); /* NULL se fora da tabela */
int32_t Params_IndexOf(uint16_t id);               /* -1 se nao existir */

float Params_Get(uint16_t index);
float Params_GetDefault(uint16_t index);

/* Valida faixa e coerencia e aplica no alvo. */
Params_Result_t Params_Set(uint16_t index, float value);

/* 1 se algum valor difere do ultimo salvo/carregado. */
uint8_t Params_IsDirty(void);
/* Depois de gravar com sucesso: o atual passa a ser o "salvo". */
void Params_MarkSaved(void);
/* Descarta o que nao foi salvo (volta ao ultimo salvo/carregado). */
void Params_RevertToSaved(void);
/* Volta tudo ao valor de fabrica (nao grava; precisa de commit). */
void Params_RestoreDefaults(void);

/* Monta o bloco para gravar. Devolve o tamanho (0 se nao couber). */
size_t Params_Serialize(uint8_t *out, size_t cap, uint32_t seq);

/* Valida o bloco (magic, versao, tamanho, CRC). Devolve 1 e a sequencia se
 * valido, SEM aplicar. */
uint8_t Params_ValidateBlob(const uint8_t *in, size_t len, uint32_t *seq);

/* Aplica um bloco ja' validado: cada par com id conhecido e valor na faixa
 * vai para o alvo; o resto fica no default. Marca o resultado como salvo.
 * Devolve quantos parametros vieram do bloco. */
uint16_t Params_ApplyBlob(const uint8_t *in, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* PARAMS_H */
