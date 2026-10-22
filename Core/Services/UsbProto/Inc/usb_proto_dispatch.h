/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-14
 *
 * Tabela de comando->handler do protocolo USB (Apêndice A do prompt do
 * projeto). Fase 7.1: CMD_HELLO e CMD_GET_STATUS. Fase 7.2: sessão
 * USER/DEV via CMD_AUTH_CHALLENGE/RESPONSE/LOGOUT (ver usb_proto_auth.h) e
 * checagem de papel por comando. Fase 7.3: SET_SETPOINT, SET_DATETIME,
 * START_TEST, ABORT_TEST e SET_OPERATOR (papel U,D - acesso livre),
 * executados por UsbProtoActions_t injetadas. Os demais códigos do
 * Apêndice A já ficam reservados como defines (comentados com a sub-fase
 * que vai implementá-los) pra não precisar renumerar nada depois.
 *
 * C puro, sem HAL: os dados de status vêm de fora via UsbProtoStatusSnapshot_t
 * e a sessão de autenticação via UsbProtoAuthSession_t (usb_proto_auth.h) -
 * quem chama (Core/Src/main.c, StartUsbProtoTask) é quem conhece
 * SystemRuntime_t/ControlLoop_Get*()/UID_BASE e preenche o ctx; este módulo
 * só serializa/despacha, igual ControlLoop_Init() injeta os pinos em vez de
 * incluir main.h.
 */
#ifndef USB_PROTO_DISPATCH_H
#define USB_PROTO_DISPATCH_H

#include <stddef.h>
#include <stdint.h>
#include "usb_proto_auth.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Apêndice A: máscara de papel U=USER, D=DEV. Checada de verdade a partir
 * desta entrega (Fase 7.2) para os comandos que já existem - HELLO/
 * GET_STATUS continuam "U,D" (qualquer sessão), AUTH_CHALLENGE/RESPONSE só
 * em sessão USER, AUTH_LOGOUT só em sessão DEV (ver handlers em
 * usb_proto_dispatch.c). */
#define USB_PROTO_CMD_HELLO            (0x0001U) /* U,D - Fase 7.1 */
#define USB_PROTO_CMD_GET_STATUS       (0x0002U) /* U,D - Fase 7.1 */
#define USB_PROTO_CMD_SET_SETPOINT     (0x0003U) /* U,D - Fase 7.3 */
#define USB_PROTO_CMD_SET_DATETIME     (0x0004U) /* U,D - Fase 7.3 */
#define USB_PROTO_CMD_START_TEST       (0x0005U) /* U,D - Fase 7.3 */
#define USB_PROTO_CMD_ABORT_TEST       (0x0006U) /* U,D - Fase 7.3 */
#define USB_PROTO_CMD_SET_OPERATOR     (0x0007U) /* U,D - Fase 7.3 */
#define USB_PROTO_CMD_STREAM_START     (0x0010U) /* U,D - Fase 7.4 */
#define USB_PROTO_CMD_STREAM_STOP      (0x0011U) /* U,D - Fase 7.4 */
#define USB_PROTO_CMD_FS_LIST          (0x0020U) /* U,D - Fase 7.5 */
#define USB_PROTO_CMD_FS_STAT          (0x0021U) /* U,D - Fase 7.5 */
#define USB_PROTO_CMD_FS_READ          (0x0022U) /* U,D - Fase 7.5 */
#define USB_PROTO_CMD_FS_CRC           (0x0023U) /* U,D - Fase 7.5 */
#define USB_PROTO_CMD_GET_LOG_TAIL     (0x0030U) /* U,D - Fase 7.4 */
#define USB_PROTO_CMD_AUTH_CHALLENGE   (0x0040U) /* U   - Fase 7.2 */
#define USB_PROTO_CMD_AUTH_RESPONSE    (0x0041U) /* U   - Fase 7.2 */
#define USB_PROTO_CMD_AUTH_LOGOUT      (0x0042U) /* D   - Fase 7.2 */
#define USB_PROTO_CMD_PARAM_LIST       (0x0100U) /* D   - Fase 7.6 */
#define USB_PROTO_CMD_PARAM_GET        (0x0101U) /* D   - Fase 7.6 */
#define USB_PROTO_CMD_PARAM_SET        (0x0102U) /* D   - Fase 7.6 */
#define USB_PROTO_CMD_PARAM_COMMIT     (0x0103U) /* D   - Fase 7.6 */
#define USB_PROTO_CMD_PARAM_REVERT     (0x0104U) /* D   - Fase 7.6 */
#define USB_PROTO_CMD_MANUAL_ENTER     (0x0110U) /* D   - Fase 7.7 */
#define USB_PROTO_CMD_MANUAL_EXIT      (0x0111U) /* D   - Fase 7.7 */
#define USB_PROTO_CMD_MANUAL_SET       (0x0112U) /* D   - Fase 7.7 */
#define USB_PROTO_CMD_MANUAL_HEARTBEAT (0x0113U) /* D   - Fase 7.7 */
#define USB_PROTO_CMD_CAL_START        (0x0120U) /* D   - Fase 7.8 */
#define USB_PROTO_CMD_CAL_POINT        (0x0121U) /* D   - Fase 7.8 */
#define USB_PROTO_CMD_CAL_APPLY        (0x0122U) /* D   - Fase 7.8 */
#define USB_PROTO_CMD_CAL_ABORT        (0x0123U) /* D   - Fase 7.8 */
#define USB_PROTO_CMD_IDENT_TLAG       (0x0130U) /* D   - Fase 7.9 */
#define USB_PROTO_CMD_SELFTEST         (0x0140U) /* D   - Fase 7.9 */
#define USB_PROTO_CMD_GET_DIAG         (0x0150U) /* D   - Fase 7.9 */
#define USB_PROTO_CMD_REBOOT           (0x0160U) /* D   - Fase 7.9 */

/* Apêndice A: status (1o byte do payload de toda RSP). */
typedef enum {
    USB_PROTO_RSP_OK               = 0,
    USB_PROTO_RSP_CMD_DESCONHECIDO = 1,
    USB_PROTO_RSP_SEM_PERMISSAO    = 2,
    USB_PROTO_RSP_PARAM_INVALIDO   = 3,
    USB_PROTO_RSP_OCUPADO          = 4,
    USB_PROTO_RSP_ESTADO_INVALIDO  = 5,
    USB_PROTO_RSP_ERRO_SD          = 6,
    USB_PROTO_RSP_FORA_DE_FAIXA    = 7,
    USB_PROTO_RSP_NAO_AUTENTICADO  = 8
} UsbProtoRspStatus_t;

#define USB_PROTO_DEVICE_ID_FIELD_LEN  (16U)
#define USB_PROTO_FW_VERSION_FIELD_LEN (12U)

/* Snapshot minimo que CMD_GET_STATUS serializa. Quem chama e' quem le
 * SystemRuntime_t/ControlLoop_Get*() de verdade e preenche isto - este
 * modulo nao inclui main.h nem conhece esses tipos, pra ficar testavel no
 * host. */
typedef struct {
    uint8_t  system_state; /* valor numerico de SystemState_t */
    float    pressure_mmhg;
    float    setpoint_mmhg;
    uint8_t  rtc_valid;
    uint8_t  sd_mounted;
    uint8_t  active_error; /* valor numerico de ControlCore_Error_t */
    uint32_t cycle_number;
} UsbProtoStatusSnapshot_t;

/* Fase 7.3: data/hora decodificada de SET_DATETIME (ja' validada pelo
 * dispatch: ano 2000..2099, mes/dia coerentes, h/m/s em faixa). */
typedef struct {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
} UsbProtoDateTime_t;

/* Fase 7.3: acoes de controle. O dispatch so' valida o FORMATO do payload
 * e chama a acao; a decisao "pode neste estado?" (usb_proto_policy.h) e a
 * execucao ficam com quem implementa (Core/App/Usb_Port) — este modulo
 * continua sem conhecer Control_Loop/RTC. Cada acao devolve o status da
 * resposta. Ponteiro NULL (ou ctx->actions NULL) -> OCUPADO. */
typedef struct {
    UsbProtoRspStatus_t (*set_setpoint)(float setpoint_mmhg);
    UsbProtoRspStatus_t (*set_datetime)(const UsbProtoDateTime_t *dt);
    UsbProtoRspStatus_t (*start_test)(void);
    UsbProtoRspStatus_t (*abort_test)(void);
    UsbProtoRspStatus_t (*set_operator)(uint16_t operator_id, uint16_t participant_id);
} UsbProtoActions_t;

/* Payloads de requisicao da Fase 7.3 (little-endian, tamanho exato):
 *   SET_SETPOINT : float32 setpoint_mmhg                      (4 B)
 *   SET_DATETIME : u16 ano, u8 mes, dia, hora, min, seg        (7 B)
 *   START_TEST   : vazio                                       (0 B)
 *   ABORT_TEST   : vazio                                       (0 B)
 *   SET_OPERATOR : u16 operator_id, u16 participant_id         (4 B; 1..9999 cada)
 * Resposta das cinco: so' o byte de status. */
#define USB_PROTO_SET_SETPOINT_LEN  (4U)
#define USB_PROTO_SET_DATETIME_LEN  (7U)
#define USB_PROTO_SET_OPERATOR_LEN  (4U)
#define USB_PROTO_SESSION_ID_MAX    (9999U) /* "OP9999"/"P9999": mesma faixa da IHM */

/* status == NULL -> CMD_GET_STATUS responde OCUPADO (snapshot ainda nao
 * disponivel; nao deveria acontecer em operacao normal, mas evita servir
 * lixo se a task de protocolo rodar antes da primeira atualizacao).
 * device_serial (UID de 96 bits, UID_BASE no H7) e' o "serial" que o HELLO
 * devolve e o "serial" usado no HMAC de AUTH_RESPONSE - mesmo dado, dois
 * usos. auth == NULL -> qualquer coisa que dependa de sessao (HELLO
 * reportar o papel, AUTH_*) responde OCUPADO, mesmo criterio de status. */
typedef struct {
    const UsbProtoStatusSnapshot_t *status;
    const uint8_t *device_serial; /* USB_PROTO_AUTH_SERIAL_LEN (12) bytes */
    UsbProtoAuthSession_t *auth;
    uint32_t now_ms; /* usado por AUTH_CHALLENGE (emissao) e AUTH_RESPONSE (expiracao) */
    const uint8_t *challenge_random; /* USB_PROTO_AUTH_NONCE_LEN (16) bytes "aleatorios"
                                       * injetados pelo chamador - so' usado por AUTH_CHALLENGE */
    const UsbProtoActions_t *actions; /* Fase 7.3 - comandos de controle (setpoint, hora, ensaio, operador) */
} UsbProtoDispatchCtx_t;

/**
 * @brief Processa um comando e monta o payload de resposta (status no byte 0).
 * @return Numero de bytes escritos em rsp_payload (>=1), ou 0 se rsp_cap for
 *         pequeno demais pra nem o status byte (erro de uso do chamador).
 */
size_t UsbProtoDispatch_Handle(uint16_t cmd,
                                const uint8_t *req_payload, uint16_t req_len,
                                const UsbProtoDispatchCtx_t *ctx,
                                uint8_t *rsp_payload, size_t rsp_cap);

#ifdef __cplusplus
}
#endif

#endif /* USB_PROTO_DISPATCH_H */
