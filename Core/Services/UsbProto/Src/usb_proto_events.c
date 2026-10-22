/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-22
 */
#include "usb_proto_events.h"
#include <string.h>

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
    p[2] = (uint8_t)((v >> 16) & 0xFFU);
    p[3] = (uint8_t)((v >> 24) & 0xFFU);
}

static void put_f32(uint8_t *p, float v)
{
    uint32_t u;
    memcpy(&u, &v, sizeof(u)); /* sem type punning */
    put_u32(p, u);
}

size_t UsbProtoEvt_StateChanged(uint8_t *out, uint8_t from_state, uint8_t to_state)
{
    out[0] = from_state;
    out[1] = to_state;
    return 2U;
}

size_t UsbProtoEvt_CycleFinished(uint8_t *out, const UsbProtoEvtCycle_t *c)
{
    put_f32(&out[0], c->setpoint_mmhg);
    put_f32(&out[4], c->peak_mmhg);
    put_f32(&out[8], c->residual_mmhg);
    put_u32(&out[12], c->inflate_time_ms);
    put_u32(&out[16], c->plateau_time_ms);
    put_u32(&out[20], c->deflate_time_ms);
    out[24] = c->aborted_by_operator;
    out[25] = c->technical_fault;
    out[26] = c->nc_code;
    return 27U;
}

size_t UsbProtoEvt_Error(uint8_t *out, uint8_t code)
{
    out[0] = code;
    return 1U;
}

size_t UsbProtoEvt_Sd(uint8_t *out, uint8_t mounted)
{
    out[0] = (mounted != 0U) ? 1U : 0U;
    return 1U;
}
