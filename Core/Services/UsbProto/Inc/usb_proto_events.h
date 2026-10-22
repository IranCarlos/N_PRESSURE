/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-22
 *
 * Fase 7.4: eventos assincronos (frames TYPE=EVT, Apendice A). C puro,
 * testado no host. O equipamento manda sem o PC pedir; o campo CMD do frame
 * leva o codigo do evento abaixo e SEQ = 0 (nao responde a nenhum pedido).
 *
 * So' sao enviados depois que o PC mandou algum comando valido nesta
 * conexao (ver StartUsbProtoTask): um terminal serial qualquer aberto na
 * porta nao recebe lixo binario sem ter falado o protocolo.
 *
 * Payloads (little-endian):
 *   EVT_ESTADO_MUDOU     u8 estado_anterior, u8 estado_novo
 *   EVT_ENSAIO_CONCLUIDO f32 setpoint, f32 pico, f32 residual (mmHg),
 *                        u32 t_inflar_ms, u32 t_plato_ms, u32 t_desinflar_ms,
 *                        u8 abortado_pelo_operador, u8 falha_tecnica, u8 nc
 *                        (CycleRecord_NcCode_t: 0 = NC00 conforme)
 *   EVT_ERRO             u8 codigo (Apendice D; 0 = erro limpo)
 *   EVT_SD               u8 montado (1 = inserido/montado, 0 = removido)
 *   EVT_AUTH_EXPIROU     vazio (sessao DEV voltou para USER por inatividade)
 */
#ifndef USB_PROTO_EVENTS_H
#define USB_PROTO_EVENTS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define USB_PROTO_EVT_ESTADO_MUDOU     (0x0301U)
#define USB_PROTO_EVT_ENSAIO_CONCLUIDO (0x0302U)
#define USB_PROTO_EVT_ERRO             (0x0303U)
#define USB_PROTO_EVT_SD               (0x0304U)
#define USB_PROTO_EVT_AUTH_EXPIROU     (0x0305U)

#define USB_PROTO_EVT_MAX_PAYLOAD      (27U) /* o maior: ENSAIO_CONCLUIDO */

typedef struct {
    float    setpoint_mmhg;
    float    peak_mmhg;
    float    residual_mmhg;
    uint32_t inflate_time_ms;
    uint32_t plateau_time_ms;
    uint32_t deflate_time_ms;
    uint8_t  aborted_by_operator;
    uint8_t  technical_fault;
    uint8_t  nc_code;
} UsbProtoEvtCycle_t;

/* Cada builder escreve o payload em `out` (>= USB_PROTO_EVT_MAX_PAYLOAD) e
 * devolve o tamanho. */
size_t UsbProtoEvt_StateChanged(uint8_t *out, uint8_t from_state, uint8_t to_state);
size_t UsbProtoEvt_CycleFinished(uint8_t *out, const UsbProtoEvtCycle_t *c);
size_t UsbProtoEvt_Error(uint8_t *out, uint8_t code);
size_t UsbProtoEvt_Sd(uint8_t *out, uint8_t mounted);

#ifdef __cplusplus
}
#endif

#endif /* USB_PROTO_EVENTS_H */
