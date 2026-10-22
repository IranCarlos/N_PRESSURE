/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-22
 *
 * Fase 7.5: regras de caminho e de permissao dos comandos FS_* (Apendice A).
 * C puro, testado no host. SOMENTE LEITURA: nenhum comando USB apaga,
 * renomeia ou grava no cartao (Parte A, "Auditoria").
 *
 * Caminho valido (antes de qualquer acesso ao cartao):
 *   - absoluto ('/' no inicio), 1..USB_PROTO_FS_PATH_MAX caracteres;
 *   - ASCII imprimivel, sem \ : * ? " < > | (separador e' so' '/');
 *   - sem "//", sem componente "." ou "..", sem '/' no fim (exceto "/").
 *
 * Permissao por papel (mesmo criterio do GET_LOG_TAIL):
 *   - DEV: tudo.
 *   - USER: "/" e "/logs" (so' para listar), e o que estiver dentro de
 *     /tests, /logs/events, /logs/errors, /users, /reports.
 *     /config (parametros/calibracao) e /logs/audit (sessoes DEV,
 *     alteracoes de parametro) ficam so' para DEV.
 */
#ifndef USB_PROTO_FS_H
#define USB_PROTO_FS_H

#include <stddef.h>
#include <stdint.h>
#include "usb_proto_auth.h"

#ifdef __cplusplus
extern "C" {
#endif

#define USB_PROTO_FS_PATH_MAX   (96U)

typedef enum {
    USB_PROTO_FS_PATH_OK = 0,
    USB_PROTO_FS_PATH_INVALID,   /* formato -> PARAM_INVALIDO */
    USB_PROTO_FS_PATH_DENIED     /* papel insuficiente -> SEM_PERMISSAO */
} UsbProtoFs_PathCheck_t;

/* `path` NAO precisa ser terminado em '\0' (vem direto do payload, `len`
 * bytes). Se valido e permitido, copia para `out` (>= USB_PROTO_FS_PATH_MAX+1)
 * com terminador. `for_listing` = 1 para FS_LIST (libera "/" e "/logs"
 * para USER). */
UsbProtoFs_PathCheck_t UsbProtoFs_CheckPath(const uint8_t *path, size_t len, UsbProtoRole_t role,
                                            uint8_t for_listing, char *out);

#ifdef __cplusplus
}
#endif

#endif /* USB_PROTO_FS_H */
