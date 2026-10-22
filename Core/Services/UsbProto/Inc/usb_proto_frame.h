/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-14
 *
 * Cabeçalho do frame do protocolo USB (Apêndice A do prompt do projeto),
 * já decodificado do COBS (ver cobs.h) e sem o delimitador 0x00:
 *
 *   VER(1B) TYPE(1B) SEQ(2B LE) CMD(2B LE) LEN(2B LE) PAYLOAD(0..1024B) CRC32(4B LE)
 *
 * CRC32 cobre VER..PAYLOAD (ver usb_crc32.h). C puro, sem HAL/USB - só monta
 * e valida bytes; quem lê/escreve o fio de verdade é Core/Src/main.c
 * (StartUsbProtoTask).
 */
#ifndef USB_PROTO_FRAME_H
#define USB_PROTO_FRAME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define USB_PROTO_VERSION       (0x01U)
#define USB_PROTO_MAX_PAYLOAD   (1024U)
#define USB_PROTO_HEADER_LEN    (8U)  /* VER+TYPE+SEQ+CMD+LEN */
#define USB_PROTO_CRC_LEN       (4U)
#define USB_PROTO_FRAME_MIN_LEN (USB_PROTO_HEADER_LEN + USB_PROTO_CRC_LEN)
#define USB_PROTO_FRAME_MAX_LEN (USB_PROTO_HEADER_LEN + USB_PROTO_MAX_PAYLOAD + USB_PROTO_CRC_LEN)

typedef enum {
    USB_PROTO_TYPE_REQ    = 0x01,
    USB_PROTO_TYPE_RSP    = 0x02,
    USB_PROTO_TYPE_EVT    = 0x03,
    USB_PROTO_TYPE_STREAM = 0x04
} UsbProtoType_t;

typedef enum {
    USB_PROTO_FRAME_OK = 0,
    USB_PROTO_FRAME_ERR_TOO_SHORT,       /* buf_len < USB_PROTO_FRAME_MIN_LEN */
    USB_PROTO_FRAME_ERR_LEN_MISMATCH,    /* campo LEN nao bate com buf_len */
    USB_PROTO_FRAME_ERR_PAYLOAD_TOO_BIG, /* LEN > USB_PROTO_MAX_PAYLOAD */
    USB_PROTO_FRAME_ERR_CRC_MISMATCH,
    USB_PROTO_FRAME_ERR_OUTPUT_OVERFLOW  /* Build: out_cap insuficiente */
} UsbProtoFrame_Status_t;

typedef struct {
    uint8_t        ver;
    UsbProtoType_t type;
    uint16_t       seq;
    uint16_t       cmd;
    uint16_t       len;
    const uint8_t *payload; /* ponteiro pra dentro do buffer de entrada (Parse) ou do chamador (Build); len bytes */
} UsbProtoFrame_t;

/**
 * @brief Valida e decompõe um frame já decodificado do COBS (sem o 0x00).
 *        out_frame->payload aponta pra dentro de buf (nao copia).
 */
UsbProtoFrame_Status_t UsbProtoFrame_Parse(const uint8_t *buf, size_t buf_len,
                                            UsbProtoFrame_t *out_frame);

/**
 * @brief Monta o buffer do frame (header+payload+crc) a partir de frame->payload
 *        (frame->len bytes), pronto pra passar pro Cobs_Encode().
 * @param out_len Preenchido com o total de bytes escritos em out_buf.
 */
UsbProtoFrame_Status_t UsbProtoFrame_Build(const UsbProtoFrame_t *frame,
                                            uint8_t *out_buf, size_t out_cap,
                                            size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* USB_PROTO_FRAME_H */
