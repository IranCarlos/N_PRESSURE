/* Teste de host de usb_telemetry (Fase 7.4, sem HAL). Ver test/host/run.sh. */
#include "usb_telemetry.h"
#include <stdio.h>
#include <string.h>

static int s_fail;
#define CHECK(c) do { if (!(c)) { printf("FALHA %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)

static UsbTelemetry_t t; /* ~2 KB: estatico, nao na pilha */

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static float get_f32(const uint8_t *p)
{
    uint32_t u = get_u32(p);
    float v;
    memcpy(&v, &u, sizeof(v));
    return v;
}

/* Simula n ticks de 1 ms do produtor; amostra i tem t_ms = i e pressao = i/10. */
static void run_ticks(uint32_t first, uint32_t n)
{
    uint32_t i;
    for (i = first; i < first + n; i++) {
        if (UsbTelemetry_TickDue(&t) != 0U) {
            UsbTelemetrySample_t s = { i, (float)i / 10.0f, 120.0f, 2U, 55U, USB_TELEMETRY_VALVE_V1, 0U };
            UsbTelemetry_Push(&t, &s);
        }
    }
}

static void test_divisor_for_rate(void)
{
    CHECK(UsbTelemetry_DivisorForRate(0U) == 0U);
    CHECK(UsbTelemetry_DivisorForRate(1001U) == 0U);
    CHECK(UsbTelemetry_DivisorForRate(1000U) == 1U);
    CHECK(UsbTelemetry_DivisorForRate(50U) == 20U);
    CHECK(UsbTelemetry_DivisorForRate(30U) == 33U); /* 1000/33 = 30 Hz efetivos */
}

static void test_stopped_produces_nothing(void)
{
    UsbTelemetry_Init(&t);
    run_ticks(0U, 100U);
    CHECK(UsbTelemetry_Pending(&t) == 0U);
    CHECK(UsbTelemetry_IsRunning(&t) == 0U);
}

static void test_decimation_and_wire_format(void)
{
    uint8_t out[1024];
    size_t len;

    UsbTelemetry_Init(&t);
    UsbTelemetry_Start(&t, UsbTelemetry_DivisorForRate(50U)); /* 1 a cada 20 ticks */
    run_ticks(1U, 100U);                                         /* ticks 1..100 -> 20,40,60,80,100 */
    CHECK(UsbTelemetry_Pending(&t) == 5U);

    len = UsbTelemetry_DrainToPayload(&t, out, sizeof(out));
    CHECK(len == USB_TELEMETRY_HEADER_WIRE_LEN + 5U * USB_TELEMETRY_SAMPLE_WIRE_LEN);
    CHECK((out[0] | (out[1] << 8)) == 5);
    CHECK(get_u32(&out[2]) == 0U); /* nada descartado */
    CHECK(get_u32(&out[6]) == 20U);                    /* t_ms da 1a amostra */
    CHECK(get_f32(&out[10]) == 2.0f);                  /* pressao 20/10 */
    CHECK(get_f32(&out[14]) == 120.0f);
    CHECK(out[18] == 2U);                              /* estado */
    CHECK(out[19] == 55U);                             /* bomba % */
    CHECK(out[20] == USB_TELEMETRY_VALVE_V1);
    CHECK(get_u32(&out[6 + 4U * 16U]) == 100U);        /* t_ms da ultima */
    CHECK(UsbTelemetry_Pending(&t) == 0U);
    CHECK(UsbTelemetry_DrainToPayload(&t, out, sizeof(out)) == 0U);
}

static void test_drain_respects_cap(void)
{
    uint8_t out[USB_TELEMETRY_HEADER_WIRE_LEN + 3U * USB_TELEMETRY_SAMPLE_WIRE_LEN + 5U];
    size_t len;

    UsbTelemetry_Init(&t);
    UsbTelemetry_Start(&t, 1U);
    run_ticks(0U, 10U);
    len = UsbTelemetry_DrainToPayload(&t, out, sizeof(out));
    CHECK(len == USB_TELEMETRY_HEADER_WIRE_LEN + 3U * USB_TELEMETRY_SAMPLE_WIRE_LEN);
    CHECK(UsbTelemetry_Pending(&t) == 7U); /* o resto fica para o proximo frame */
}

static void test_full_ring_drops_without_blocking(void)
{
    uint8_t out[1024];

    UsbTelemetry_Init(&t);
    UsbTelemetry_Start(&t, 1U);
    run_ticks(0U, USB_TELEMETRY_RING_LEN + 10U); /* consumidor parado */
    CHECK(UsbTelemetry_Pending(&t) == USB_TELEMETRY_RING_LEN);
    (void)UsbTelemetry_DrainToPayload(&t, out, sizeof(out));
    CHECK(get_u32(&out[2]) == 10U);       /* descartadas reportadas ao PC */
    CHECK(get_u32(&out[6]) == 0U);        /* as MAIS ANTIGAS ficam; as novas e' que caem */
}

static void test_restart_discards_old_and_stop_halts(void)
{
    UsbTelemetry_Init(&t);
    UsbTelemetry_Start(&t, 1U);
    run_ticks(0U, 5U);
    UsbTelemetry_Start(&t, 1U); /* novo stream: o que sobrou do anterior e' descartado */
    CHECK(UsbTelemetry_Pending(&t) == 0U);
    UsbTelemetry_Stop(&t);
    run_ticks(5U, 5U);
    CHECK(UsbTelemetry_Pending(&t) == 0U);
}

static void test_wraparound_of_free_running_indices(void)
{
    uint8_t out[1024];

    UsbTelemetry_Init(&t);
    t.head = 0xFFFFFFF0U; /* indices perto de dar a volta em 32 bits */
    t.tail = 0xFFFFFFF0U;
    UsbTelemetry_Start(&t, 1U);
    run_ticks(0U, 40U);
    CHECK(UsbTelemetry_Pending(&t) == 40U);
    CHECK(UsbTelemetry_DrainToPayload(&t, out, sizeof(out)) ==
          USB_TELEMETRY_HEADER_WIRE_LEN + 40U * USB_TELEMETRY_SAMPLE_WIRE_LEN);
    CHECK(get_u32(&out[6 + 39U * 16U]) == 39U);
}

int main(void)
{
    test_divisor_for_rate();
    test_stopped_produces_nothing();
    test_decimation_and_wire_format();
    test_drain_respects_cap();
    test_full_ring_drops_without_blocking();
    test_restart_discards_old_and_stop_halts();
    test_wraparound_of_free_running_indices();
    printf(s_fail ? "usb_telemetry: %d FALHA(S)\n" : "usb_telemetry: OK\n", s_fail);
    return s_fail != 0;
}
