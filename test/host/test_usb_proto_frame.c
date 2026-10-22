/* Teste de host de usb_proto_frame (sem HAL). Ver test/host/run.sh. */
#include "usb_proto_frame.h"
#include <string.h>
#include <stdio.h>

static int s_fail;
#define CHECK(c) do { if (!(c)) { printf("FALHA %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)

static void test_build_then_parse_roundtrip(void)
{
    const uint8_t payload[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01 };
    UsbProtoFrame_t in_frame = {
        .ver = USB_PROTO_VERSION,
        .type = USB_PROTO_TYPE_REQ,
        .seq = 0x1234,
        .cmd = 0x0002,
        .len = (uint16_t)sizeof(payload),
        .payload = payload
    };
    uint8_t wire[USB_PROTO_FRAME_MAX_LEN];
    size_t wire_len = 0U;
    UsbProtoFrame_t out_frame;

    CHECK(UsbProtoFrame_Build(&in_frame, wire, sizeof(wire), &wire_len) == USB_PROTO_FRAME_OK);
    CHECK(wire_len == USB_PROTO_HEADER_LEN + sizeof(payload) + USB_PROTO_CRC_LEN);

    CHECK(UsbProtoFrame_Parse(wire, wire_len, &out_frame) == USB_PROTO_FRAME_OK);
    CHECK(out_frame.ver == in_frame.ver);
    CHECK(out_frame.type == in_frame.type);
    CHECK(out_frame.seq == in_frame.seq);
    CHECK(out_frame.cmd == in_frame.cmd);
    CHECK(out_frame.len == in_frame.len);
    CHECK(memcmp(out_frame.payload, payload, sizeof(payload)) == 0);
}

static void test_zero_length_payload(void)
{
    UsbProtoFrame_t in_frame = {
        .ver = USB_PROTO_VERSION, .type = USB_PROTO_TYPE_REQ,
        .seq = 1U, .cmd = 2U, .len = 0U, .payload = NULL
    };
    uint8_t wire[USB_PROTO_FRAME_MAX_LEN];
    size_t wire_len = 0U;
    UsbProtoFrame_t out_frame;

    CHECK(UsbProtoFrame_Build(&in_frame, wire, sizeof(wire), &wire_len) == USB_PROTO_FRAME_OK);
    CHECK(wire_len == USB_PROTO_FRAME_MIN_LEN);
    CHECK(UsbProtoFrame_Parse(wire, wire_len, &out_frame) == USB_PROTO_FRAME_OK);
    CHECK(out_frame.len == 0U);
    CHECK(out_frame.payload == NULL);
}

static void test_too_short_buffer(void)
{
    uint8_t buf[USB_PROTO_FRAME_MIN_LEN - 1U];
    UsbProtoFrame_t out_frame;
    memset(buf, 0, sizeof(buf));
    CHECK(UsbProtoFrame_Parse(buf, sizeof(buf), &out_frame) == USB_PROTO_FRAME_ERR_TOO_SHORT);
}

static void test_len_field_mismatch(void)
{
    const uint8_t payload[] = { 0x01, 0x02, 0x03 };
    UsbProtoFrame_t in_frame = {
        .ver = USB_PROTO_VERSION, .type = USB_PROTO_TYPE_REQ,
        .seq = 1U, .cmd = 1U, .len = (uint16_t)sizeof(payload), .payload = payload
    };
    uint8_t wire[USB_PROTO_FRAME_MAX_LEN];
    size_t wire_len = 0U;
    UsbProtoFrame_t out_frame;

    CHECK(UsbProtoFrame_Build(&in_frame, wire, sizeof(wire), &wire_len) == USB_PROTO_FRAME_OK);
    /* Corta 1 byte do fim do buffer - o campo LEN no cabecalho (ainda intacto)
     * continua dizendo 3, mas o buffer passado agora e' 1 byte mais curto do
     * que HEADER+3+CRC exige. */
    CHECK(UsbProtoFrame_Parse(wire, wire_len - 1U, &out_frame) == USB_PROTO_FRAME_ERR_LEN_MISMATCH);
}

static void test_payload_too_big(void)
{
    uint8_t buf[USB_PROTO_HEADER_LEN + USB_PROTO_CRC_LEN];
    memset(buf, 0, sizeof(buf));
    buf[6] = 0xFFU; /* LEN low byte */
    buf[7] = 0xFFU; /* LEN high byte -> 0xFFFF, bem acima de USB_PROTO_MAX_PAYLOAD */
    UsbProtoFrame_t out_frame;
    CHECK(UsbProtoFrame_Parse(buf, sizeof(buf), &out_frame) == USB_PROTO_FRAME_ERR_PAYLOAD_TOO_BIG);
}

static void test_crc_mismatch_detected(void)
{
    const uint8_t payload[] = { 0xAA, 0xBB };
    UsbProtoFrame_t in_frame = {
        .ver = USB_PROTO_VERSION, .type = USB_PROTO_TYPE_REQ,
        .seq = 1U, .cmd = 1U, .len = (uint16_t)sizeof(payload), .payload = payload
    };
    uint8_t wire[USB_PROTO_FRAME_MAX_LEN];
    size_t wire_len = 0U;
    UsbProtoFrame_t out_frame;

    CHECK(UsbProtoFrame_Build(&in_frame, wire, sizeof(wire), &wire_len) == USB_PROTO_FRAME_OK);
    wire[wire_len - 1U] ^= 0xFFU; /* corrompe 1 byte do CRC */
    CHECK(UsbProtoFrame_Parse(wire, wire_len, &out_frame) == USB_PROTO_FRAME_ERR_CRC_MISMATCH);
}

static void test_build_output_overflow(void)
{
    const uint8_t payload[] = { 0x01, 0x02, 0x03 };
    UsbProtoFrame_t in_frame = {
        .ver = USB_PROTO_VERSION, .type = USB_PROTO_TYPE_REQ,
        .seq = 1U, .cmd = 1U, .len = (uint16_t)sizeof(payload), .payload = payload
    };
    uint8_t small[4];
    size_t out_len = 0U;
    CHECK(UsbProtoFrame_Build(&in_frame, small, sizeof(small), &out_len) == USB_PROTO_FRAME_ERR_OUTPUT_OVERFLOW);
}

int main(void)
{
    test_build_then_parse_roundtrip();
    test_zero_length_payload();
    test_too_short_buffer();
    test_len_field_mismatch();
    test_payload_too_big();
    test_crc_mismatch_detected();
    test_build_output_overflow();
    printf(s_fail ? "usb_proto_frame: %d FALHA(S)\n" : "usb_proto_frame: OK\n", s_fail);
    return s_fail != 0;
}
