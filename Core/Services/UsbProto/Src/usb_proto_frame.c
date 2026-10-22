/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-14
 */
#include "usb_proto_frame.h"
#include "usb_crc32.h"

static uint16_t read_u16_le(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_u16_le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
}

static void write_u32_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
    p[2] = (uint8_t)((v >> 16) & 0xFFU);
    p[3] = (uint8_t)((v >> 24) & 0xFFU);
}

UsbProtoFrame_Status_t UsbProtoFrame_Parse(const uint8_t *buf, size_t buf_len,
                                            UsbProtoFrame_t *out_frame)
{
    uint16_t len;
    uint32_t crc_calc;
    uint32_t crc_recv;
    size_t crc_offset;

    if (buf_len < USB_PROTO_FRAME_MIN_LEN) {
        return USB_PROTO_FRAME_ERR_TOO_SHORT;
    }

    len = read_u16_le(&buf[6]);
    if (len > USB_PROTO_MAX_PAYLOAD) {
        return USB_PROTO_FRAME_ERR_PAYLOAD_TOO_BIG;
    }

    crc_offset = (size_t)USB_PROTO_HEADER_LEN + (size_t)len;
    if (buf_len != crc_offset + USB_PROTO_CRC_LEN) {
        return USB_PROTO_FRAME_ERR_LEN_MISMATCH;
    }

    crc_calc = UsbCrc32_Compute(buf, crc_offset);
    crc_recv = read_u32_le(&buf[crc_offset]);
    if (crc_calc != crc_recv) {
        return USB_PROTO_FRAME_ERR_CRC_MISMATCH;
    }

    out_frame->ver = buf[0];
    out_frame->type = (UsbProtoType_t)buf[1];
    out_frame->seq = read_u16_le(&buf[2]);
    out_frame->cmd = read_u16_le(&buf[4]);
    out_frame->len = len;
    out_frame->payload = (len > 0U) ? &buf[USB_PROTO_HEADER_LEN] : NULL;
    return USB_PROTO_FRAME_OK;
}

UsbProtoFrame_Status_t UsbProtoFrame_Build(const UsbProtoFrame_t *frame,
                                            uint8_t *out_buf, size_t out_cap,
                                            size_t *out_len)
{
    size_t crc_offset;
    uint32_t crc_calc;

    if (frame->len > USB_PROTO_MAX_PAYLOAD) {
        return USB_PROTO_FRAME_ERR_PAYLOAD_TOO_BIG;
    }

    crc_offset = (size_t)USB_PROTO_HEADER_LEN + (size_t)frame->len;
    if (out_cap < crc_offset + USB_PROTO_CRC_LEN) {
        return USB_PROTO_FRAME_ERR_OUTPUT_OVERFLOW;
    }

    out_buf[0] = frame->ver;
    out_buf[1] = (uint8_t)frame->type;
    write_u16_le(&out_buf[2], frame->seq);
    write_u16_le(&out_buf[4], frame->cmd);
    write_u16_le(&out_buf[6], frame->len);
    if (frame->len > 0U) {
        size_t i;
        for (i = 0U; i < frame->len; i++) {
            out_buf[USB_PROTO_HEADER_LEN + i] = frame->payload[i];
        }
    }

    crc_calc = UsbCrc32_Compute(out_buf, crc_offset);
    write_u32_le(&out_buf[crc_offset], crc_calc);

    *out_len = crc_offset + USB_PROTO_CRC_LEN;
    return USB_PROTO_FRAME_OK;
}
