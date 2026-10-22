/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-22
 */
#include "usb_proto_fs.h"
#include <string.h>

/* Raizes que o papel USER pode ler (o conteudo, nao so' listar). */
static const char *const k_user_roots[] = {
    "/tests", "/logs/events", "/logs/errors", "/users", "/reports"
};

static uint8_t char_ok(uint8_t c)
{
    if ((c < 0x20U) || (c > 0x7EU)) {
        return 0U;
    }
    switch (c) {
        case (uint8_t)'\\':
        case (uint8_t)':':
        case (uint8_t)'*':
        case (uint8_t)'?':
        case (uint8_t)'"':
        case (uint8_t)'<':
        case (uint8_t)'>':
        case (uint8_t)'|':
            return 0U;
        default:
            return 1U;
    }
}

/* `p` comeca com `root` e logo depois vem o fim ou '/' (evita que
 * "/testsX" case com "/tests"). */
static uint8_t under_root(const char *p, const char *root)
{
    size_t n = strlen(root);
    return ((strncmp(p, root, n) == 0) && ((p[n] == '\0') || (p[n] == '/'))) ? 1U : 0U;
}

UsbProtoFs_PathCheck_t UsbProtoFs_CheckPath(const uint8_t *path, size_t len, UsbProtoRole_t role,
                                            uint8_t for_listing, char *out)
{
    size_t i;
    size_t comp_start;
    size_t r;

    if ((path == NULL) || (out == NULL) || (len == 0U) || (len > USB_PROTO_FS_PATH_MAX) ||
        (path[0] != (uint8_t)'/')) {
        return USB_PROTO_FS_PATH_INVALID;
    }
    if ((len > 1U) && (path[len - 1U] == (uint8_t)'/')) {
        return USB_PROTO_FS_PATH_INVALID; /* barra no fim so' na raiz */
    }

    /* Percorre componente a componente (entre '/'): rejeita vazio ("//"),
     * "." e ".." - nada de sair da arvore nem caminhos ambiguos. */
    comp_start = 1U;
    for (i = 1U; i <= len; i++) {
        if ((i == len) || (path[i] == (uint8_t)'/')) {
            size_t comp_len = i - comp_start;
            if ((len > 1U) && (comp_len == 0U)) {
                return USB_PROTO_FS_PATH_INVALID;
            }
            if ((comp_len == 1U) && (path[comp_start] == (uint8_t)'.')) {
                return USB_PROTO_FS_PATH_INVALID;
            }
            if ((comp_len == 2U) && (path[comp_start] == (uint8_t)'.') &&
                (path[comp_start + 1U] == (uint8_t)'.')) {
                return USB_PROTO_FS_PATH_INVALID;
            }
            comp_start = i + 1U;
        } else if (char_ok(path[i]) == 0U) {
            return USB_PROTO_FS_PATH_INVALID;
        } else {
            /* caractere comum */
        }
    }

    memcpy(out, path, len);
    out[len] = '\0';

    if (role == USB_PROTO_ROLE_DEV) {
        return USB_PROTO_FS_PATH_OK;
    }
    if ((for_listing != 0U) && ((strcmp(out, "/") == 0) || (strcmp(out, "/logs") == 0))) {
        return USB_PROTO_FS_PATH_OK;
    }
    for (r = 0U; r < (sizeof(k_user_roots) / sizeof(k_user_roots[0])); r++) {
        if (under_root(out, k_user_roots[r]) != 0U) {
            return USB_PROTO_FS_PATH_OK;
        }
    }
    return USB_PROTO_FS_PATH_DENIED;
}
