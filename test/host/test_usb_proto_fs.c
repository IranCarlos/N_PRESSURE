/* Teste de host de usb_proto_fs (Fase 7.5, sem HAL). Ver test/host/run.sh. */
#include "usb_proto_fs.h"
#include <stdio.h>
#include <string.h>

static int s_fail;
#define CHECK(c) do { if (!(c)) { printf("FALHA %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)

static UsbProtoFs_PathCheck_t chk(const char *p, UsbProtoRole_t role, uint8_t listing)
{
    char out[USB_PROTO_FS_PATH_MAX + 1U];
    return UsbProtoFs_CheckPath((const uint8_t *)p, strlen(p), role, listing, out);
}

static void test_format_rules(void)
{
    char long_path[USB_PROTO_FS_PATH_MAX + 2U];

    CHECK(chk("/tests/2026/10/T00000123.rec", USB_PROTO_ROLE_DEV, 0U) == USB_PROTO_FS_PATH_OK);
    CHECK(chk("/", USB_PROTO_ROLE_DEV, 1U) == USB_PROTO_FS_PATH_OK);
    CHECK(chk("tests", USB_PROTO_ROLE_DEV, 0U) == USB_PROTO_FS_PATH_INVALID);      /* relativo */
    CHECK(chk("/tests/", USB_PROTO_ROLE_DEV, 1U) == USB_PROTO_FS_PATH_INVALID);    /* barra no fim */
    CHECK(chk("/tests//2026", USB_PROTO_ROLE_DEV, 1U) == USB_PROTO_FS_PATH_INVALID);
    CHECK(chk("/tests/../config", USB_PROTO_ROLE_DEV, 0U) == USB_PROTO_FS_PATH_INVALID);
    CHECK(chk("/tests/./x", USB_PROTO_ROLE_DEV, 0U) == USB_PROTO_FS_PATH_INVALID);
    CHECK(chk("/..", USB_PROTO_ROLE_DEV, 0U) == USB_PROTO_FS_PATH_INVALID);
    CHECK(chk("/a\\b", USB_PROTO_ROLE_DEV, 0U) == USB_PROTO_FS_PATH_INVALID);
    CHECK(chk("/0:x", USB_PROTO_ROLE_DEV, 0U) == USB_PROTO_FS_PATH_INVALID);       /* troca de unidade */
    CHECK(chk("/a*b", USB_PROTO_ROLE_DEV, 0U) == USB_PROTO_FS_PATH_INVALID);
    CHECK(chk("/..x", USB_PROTO_ROLE_DEV, 0U) == USB_PROTO_FS_PATH_OK);            /* nome comum */
    CHECK(UsbProtoFs_CheckPath((const uint8_t *)"/", 0U, USB_PROTO_ROLE_DEV, 1U, long_path)
          == USB_PROTO_FS_PATH_INVALID);

    memset(long_path, 'a', sizeof(long_path));
    long_path[0] = '/';
    long_path[USB_PROTO_FS_PATH_MAX + 1U] = '\0'; /* 97 caracteres */
    CHECK(chk(long_path, USB_PROTO_ROLE_DEV, 0U) == USB_PROTO_FS_PATH_INVALID);
    long_path[USB_PROTO_FS_PATH_MAX] = '\0';      /* 96: limite */
    CHECK(chk(long_path, USB_PROTO_ROLE_DEV, 0U) == USB_PROTO_FS_PATH_OK);
}

static void test_control_char_rejected(void)
{
    const uint8_t p[] = { '/', 'a', 0x00U, 'b' }; /* NUL no meio nao trunca a checagem */
    char out[USB_PROTO_FS_PATH_MAX + 1U];
    CHECK(UsbProtoFs_CheckPath(p, sizeof(p), USB_PROTO_ROLE_DEV, 0U, out) == USB_PROTO_FS_PATH_INVALID);
}

static void test_user_roots(void)
{
    CHECK(chk("/tests", USB_PROTO_ROLE_USER, 1U) == USB_PROTO_FS_PATH_OK);
    CHECK(chk("/tests/2026/10/T00000123.csv", USB_PROTO_ROLE_USER, 0U) == USB_PROTO_FS_PATH_OK);
    CHECK(chk("/logs/events/2026-10.log", USB_PROTO_ROLE_USER, 0U) == USB_PROTO_FS_PATH_OK);
    CHECK(chk("/logs/errors/2026-10.log", USB_PROTO_ROLE_USER, 0U) == USB_PROTO_FS_PATH_OK);
    CHECK(chk("/users/operators.csv", USB_PROTO_ROLE_USER, 0U) == USB_PROTO_FS_PATH_OK);
    CHECK(chk("/reports", USB_PROTO_ROLE_USER, 1U) == USB_PROTO_FS_PATH_OK);

    CHECK(chk("/logs/audit/2026-10.log", USB_PROTO_ROLE_USER, 0U) == USB_PROTO_FS_PATH_DENIED);
    CHECK(chk("/config/cal_backup.bin", USB_PROTO_ROLE_USER, 0U) == USB_PROTO_FS_PATH_DENIED);
    CHECK(chk("/testsX/a", USB_PROTO_ROLE_USER, 0U) == USB_PROTO_FS_PATH_DENIED);  /* prefixo falso */
    CHECK(chk("/logs/eventsX", USB_PROTO_ROLE_USER, 0U) == USB_PROTO_FS_PATH_DENIED);

    /* "/" e "/logs": so' listar */
    CHECK(chk("/", USB_PROTO_ROLE_USER, 1U) == USB_PROTO_FS_PATH_OK);
    CHECK(chk("/logs", USB_PROTO_ROLE_USER, 1U) == USB_PROTO_FS_PATH_OK);
    CHECK(chk("/logs", USB_PROTO_ROLE_USER, 0U) == USB_PROTO_FS_PATH_DENIED);
    CHECK(chk("/logs/audit", USB_PROTO_ROLE_USER, 1U) == USB_PROTO_FS_PATH_DENIED);

    CHECK(chk("/logs/audit/2026-10.log", USB_PROTO_ROLE_DEV, 0U) == USB_PROTO_FS_PATH_OK);
}

static void test_output_is_terminated_copy(void)
{
    const uint8_t p[] = { '/', 't', 'e', 's', 't', 's' }; /* sem terminador no payload */
    char out[USB_PROTO_FS_PATH_MAX + 1U];
    memset(out, 'X', sizeof(out));
    CHECK(UsbProtoFs_CheckPath(p, sizeof(p), USB_PROTO_ROLE_USER, 1U, out) == USB_PROTO_FS_PATH_OK);
    CHECK(strcmp(out, "/tests") == 0);
}

int main(void)
{
    test_format_rules();
    test_control_char_rejected();
    test_user_roots();
    test_output_is_terminated_copy();
    printf(s_fail ? "usb_proto_fs: %d FALHA(S)\n" : "usb_proto_fs: OK\n", s_fail);
    return s_fail != 0;
}
