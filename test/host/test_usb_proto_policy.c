/* Teste de host de usb_proto_policy (Fase 7.3, sem HAL). Ver test/host/run.sh.
 * Cobre a tabela estado -> resultado do topo de usb_proto_policy.h. */
#include "usb_proto_policy.h"
#include <stdio.h>

static int s_fail;
#define CHECK(c) do { if (!(c)) { printf("FALHA %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)

static const SystemState_t ALL_STATES[] = {
    SYSTEM_STATE_BOOT, SYSTEM_STATE_IDLE, SYSTEM_STATE_INFLATING, SYSTEM_STATE_STABILIZING,
    SYSTEM_STATE_STABILIZED, SYSTEM_STATE_DEFLATING_FAST, SYSTEM_STATE_LEAK_TEST,
    SYSTEM_STATE_CONFIG, SYSTEM_STATE_FAULT, SYSTEM_STATE_MANUAL
};
#define N_STATES (sizeof(ALL_STATES) / sizeof(ALL_STATES[0]))

static void test_start_only_in_idle(void)
{
    size_t i;
    for (i = 0U; i < N_STATES; i++) {
        UsbProtoRspStatus_t expected = (ALL_STATES[i] == SYSTEM_STATE_IDLE)
                                       ? USB_PROTO_RSP_OK : USB_PROTO_RSP_ESTADO_INVALIDO;
        CHECK(UsbProtoPolicy_StartTest(ALL_STATES[i]) == expected);
    }
}

static void test_abort_raises_only_in_active_test(void)
{
    size_t i;
    for (i = 0U; i < N_STATES; i++) {
        SystemState_t s = ALL_STATES[i];
        uint8_t raise = 0xAAU;
        UsbProtoRspStatus_t st = UsbProtoPolicy_AbortTest(s, &raise);
        int active = (s == SYSTEM_STATE_INFLATING) || (s == SYSTEM_STATE_STABILIZING) ||
                     (s == SYSTEM_STATE_STABILIZED) || (s == SYSTEM_STATE_MANUAL);
        int already_safe = (s == SYSTEM_STATE_IDLE) || (s == SYSTEM_STATE_DEFLATING_FAST) ||
                           (s == SYSTEM_STATE_FAULT);

        CHECK(raise == (active ? 1U : 0U));
        if (active || already_safe) {
            CHECK(st == USB_PROTO_RSP_OK);
        } else {
            CHECK(st == USB_PROTO_RSP_ESTADO_INVALIDO); /* BOOT/CONFIG/LEAK_TEST: controle local */
        }
    }
}

static void test_abort_null_out_is_safe(void)
{
    CHECK(UsbProtoPolicy_AbortTest(SYSTEM_STATE_INFLATING, NULL) == USB_PROTO_RSP_OK);
}

static void test_settings_only_in_idle(void)
{
    size_t i;
    for (i = 0U; i < N_STATES; i++) {
        UsbProtoRspStatus_t expected = (ALL_STATES[i] == SYSTEM_STATE_IDLE)
                                       ? USB_PROTO_RSP_OK : USB_PROTO_RSP_ESTADO_INVALIDO;
        CHECK(UsbProtoPolicy_IdleOnly(ALL_STATES[i]) == expected);
    }
}

static void test_setpoint_range(void)
{
    volatile float zero = 0.0f; /* evita divisao por zero constante em tempo de compilacao */
    float nan_v = zero / zero;
    float inf_v = 1.0f / zero;

    CHECK(UsbProtoPolicy_Setpoint(0.0f, 300.0f) == USB_PROTO_RSP_OK);
    CHECK(UsbProtoPolicy_Setpoint(120.5f, 300.0f) == USB_PROTO_RSP_OK);
    CHECK(UsbProtoPolicy_Setpoint(300.0f, 300.0f) == USB_PROTO_RSP_OK);
    CHECK(UsbProtoPolicy_Setpoint(300.1f, 300.0f) == USB_PROTO_RSP_FORA_DE_FAIXA);
    CHECK(UsbProtoPolicy_Setpoint(-0.1f, 300.0f) == USB_PROTO_RSP_FORA_DE_FAIXA);
    CHECK(UsbProtoPolicy_Setpoint(inf_v, 300.0f) == USB_PROTO_RSP_FORA_DE_FAIXA);
    CHECK(UsbProtoPolicy_Setpoint(nan_v, 300.0f) == USB_PROTO_RSP_PARAM_INVALIDO);
}

int main(void)
{
    test_start_only_in_idle();
    test_abort_raises_only_in_active_test();
    test_abort_null_out_is_safe();
    test_settings_only_in_idle();
    test_setpoint_range();
    printf(s_fail ? "usb_proto_policy: %d FALHA(S)\n" : "usb_proto_policy: OK\n", s_fail);
    return s_fail != 0;
}
