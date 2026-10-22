/* Teste de host de usb_rx_ring (sem HAL). Ver test/host/run.sh. */
#include "usb_rx_ring.h"
#include <string.h>
#include <stdio.h>

static int s_fail;
#define CHECK(c) do { if (!(c)) { printf("FALHA %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)

static void test_push_pop_roundtrip(void)
{
    UsbRxRing_t ring;
    const uint8_t in[] = { 1, 2, 3, 4, 5 };
    uint8_t out[8] = {0};

    UsbRxRing_Init(&ring);
    CHECK(UsbRxRing_Push(&ring, in, sizeof(in)) == sizeof(in));
    CHECK(UsbRxRing_Available(&ring) == sizeof(in));
    CHECK(UsbRxRing_Pop(&ring, out, sizeof(in)) == sizeof(in));
    CHECK(memcmp(in, out, sizeof(in)) == 0);
    CHECK(UsbRxRing_Available(&ring) == 0U);
}

static void test_pop_more_than_available(void)
{
    UsbRxRing_t ring;
    const uint8_t in[] = { 0xAA, 0xBB };
    uint8_t out[8] = {0};

    UsbRxRing_Init(&ring);
    UsbRxRing_Push(&ring, in, sizeof(in));
    CHECK(UsbRxRing_Pop(&ring, out, sizeof(out)) == sizeof(in));
}

static void test_overflow_drops_tail_end(void)
{
    UsbRxRing_t ring;
    uint8_t in[USB_RX_RING_CAPACITY + 10];
    uint8_t out[USB_RX_RING_CAPACITY + 10];
    size_t i;

    for (i = 0U; i < sizeof(in); i++) {
        in[i] = (uint8_t)(i & 0xFFU);
    }
    UsbRxRing_Init(&ring);
    CHECK(UsbRxRing_Push(&ring, in, sizeof(in)) == USB_RX_RING_CAPACITY);
    CHECK(UsbRxRing_Available(&ring) == USB_RX_RING_CAPACITY);
    CHECK(UsbRxRing_Pop(&ring, out, sizeof(out)) == USB_RX_RING_CAPACITY);
    CHECK(memcmp(in, out, USB_RX_RING_CAPACITY) == 0);
}

static void test_wraparound(void)
{
    UsbRxRing_t ring;
    uint8_t chunk[USB_RX_RING_CAPACITY - 4U];
    uint8_t out[USB_RX_RING_CAPACITY - 4U];
    const uint8_t more[] = { 10, 20, 30, 40, 50, 60 };
    uint8_t out2[sizeof(more)];
    size_t i;

    for (i = 0U; i < sizeof(chunk); i++) {
        chunk[i] = (uint8_t)(i & 0xFFU);
    }
    UsbRxRing_Init(&ring);
    /* Enche quase tudo, esvazia, e enche de novo para forcar head/tail a
     * darem a volta no buffer circular (head e' uint16_t sempre crescente,
     * so' a mascara que faz o wraparound no indice fisico). */
    CHECK(UsbRxRing_Push(&ring, chunk, sizeof(chunk)) == sizeof(chunk));
    CHECK(UsbRxRing_Pop(&ring, out, sizeof(chunk)) == sizeof(chunk));
    CHECK(memcmp(chunk, out, sizeof(chunk)) == 0);

    CHECK(UsbRxRing_Push(&ring, more, sizeof(more)) == sizeof(more));
    CHECK(UsbRxRing_Pop(&ring, out2, sizeof(more)) == sizeof(more));
    CHECK(memcmp(more, out2, sizeof(more)) == 0);
}

int main(void)
{
    test_push_pop_roundtrip();
    test_pop_more_than_available();
    test_overflow_drops_tail_end();
    test_wraparound();
    printf(s_fail ? "usb_rx_ring: %d FALHA(S)\n" : "usb_rx_ring: OK\n", s_fail);
    return s_fail != 0;
}
