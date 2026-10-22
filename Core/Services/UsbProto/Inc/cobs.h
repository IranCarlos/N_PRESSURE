/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-14
 *
 * COBS (Consistent Overhead Byte Stuffing), em C puro - sem HAL. Remove
 * todo byte 0x00 do payload trocando-o por "contagem até o próximo zero",
 * permitindo usar 0x00 como delimitador de frame no fio (Apêndice A do
 * prompt do projeto: "cada frame é codificado com COBS e terminado por
 * byte 0x00"). O delimitador 0x00 em si NÃO é escrito por este módulo -
 * quem fizer a leitura/escrita do fio (Core/Src/main.c, StartUsbProtoTask)
 * é quem concatena/corta no 0x00.
 *
 * Referência: Cheshire & Baker, "Consistent Overhead Byte Stuffing", 1999.
 */
#ifndef COBS_H
#define COBS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pior caso: 1 byte de overhead a cada 254 bytes de dados, mais o primeiro
 * byte de código. */
#define COBS_MAX_ENCODED_LEN(in_len) ((in_len) + ((in_len) + 253U) / 254U + 1U)

typedef enum {
    COBS_OK = 0,
    COBS_ERR_OUTPUT_OVERFLOW = 1, /* out_cap insuficiente */
    COBS_ERR_MALFORMED = 2        /* decode: código de comprimento inconsistente com in_len */
} Cobs_Status_t;

/**
 * @brief Codifica in[0..in_len) em out, sem o delimitador 0x00 final.
 * @param out_len Preenchido com o numero de bytes escritos em out.
 */
Cobs_Status_t Cobs_Encode(const uint8_t *in, size_t in_len,
                           uint8_t *out, size_t out_cap, size_t *out_len);

/**
 * @brief Decodifica in[0..in_len) (já sem o delimitador 0x00) em out.
 * @param out_len Preenchido com o numero de bytes escritos em out.
 */
Cobs_Status_t Cobs_Decode(const uint8_t *in, size_t in_len,
                           uint8_t *out, size_t out_cap, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* COBS_H */
