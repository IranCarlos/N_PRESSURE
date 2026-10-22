/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-22
 */
#include "usb_proto_policy.h"

UsbProtoRspStatus_t UsbProtoPolicy_StartTest(SystemState_t state)
{
    return (state == SYSTEM_STATE_IDLE) ? USB_PROTO_RSP_OK : USB_PROTO_RSP_ESTADO_INVALIDO;
}

UsbProtoRspStatus_t UsbProtoPolicy_AbortTest(SystemState_t state, uint8_t *raise_abort)
{
    UsbProtoRspStatus_t st;
    uint8_t raise = 0u;

    switch (state) {
        case SYSTEM_STATE_INFLATING:
        case SYSTEM_STATE_STABILIZING:
        case SYSTEM_STATE_STABILIZED:
        case SYSTEM_STATE_MANUAL:   /* Fase 7.7: desinflar = sair do manual (estado seguro) */
            raise = 1u;
            st = USB_PROTO_RSP_OK;
            break;
        case SYSTEM_STATE_IDLE:
        case SYSTEM_STATE_DEFLATING_FAST:
        case SYSTEM_STATE_FAULT:
            st = USB_PROTO_RSP_OK; /* ja' seguro / ja' desinflando */
            break;
        case SYSTEM_STATE_BOOT:
        case SYSTEM_STATE_CONFIG:
        case SYSTEM_STATE_LEAK_TEST:
        default:
            st = USB_PROTO_RSP_ESTADO_INVALIDO;
            break;
    }

    if (raise_abort != 0) {
        *raise_abort = raise;
    }
    return st;
}

UsbProtoRspStatus_t UsbProtoPolicy_IdleOnly(SystemState_t state)
{
    return (state == SYSTEM_STATE_IDLE) ? USB_PROTO_RSP_OK : USB_PROTO_RSP_ESTADO_INVALIDO;
}

UsbProtoRspStatus_t UsbProtoPolicy_Setpoint(float setpoint_mmhg, float max_setpoint_mmhg)
{
    /* (x != x) so' e' verdadeiro para NaN; os limites tambem rejeitam +-inf. */
    if (setpoint_mmhg != setpoint_mmhg) {
        return USB_PROTO_RSP_PARAM_INVALIDO;
    }
    if ((setpoint_mmhg < 0.0f) || (setpoint_mmhg > max_setpoint_mmhg)) {
        return USB_PROTO_RSP_FORA_DE_FAIXA;
    }
    return USB_PROTO_RSP_OK;
}
