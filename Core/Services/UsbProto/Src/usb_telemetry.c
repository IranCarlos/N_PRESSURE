/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-22
 */
#include "usb_telemetry.h"
#include <string.h>

/* Indice na fila: RING_LEN e' potencia de 2, entao "& (LEN-1)" == "% LEN"
 * (mascara dos bits baixos). head/tail correm livres em 32 bits; a
 * diferenca sem sinal (head - tail) da' a ocupacao mesmo apos o overflow. */
#define RING_MASK (USB_TELEMETRY_RING_LEN - 1U)

/* Barreira de memoria entre escrever a amostra e publicar o indice (e o
 * inverso no consumidor). Builtin do GCC: vira DMB no Cortex-M7 e funciona
 * igual no gcc do host (testes). */
#define TELEMETRY_FENCE() __atomic_thread_fence(__ATOMIC_SEQ_CST)

void UsbTelemetry_Init(UsbTelemetry_t *t)
{
    memset(t, 0, sizeof(*t));
}

uint32_t UsbTelemetry_DivisorForRate(uint32_t rate_hz)
{
    if ((rate_hz == 0U) || (rate_hz > USB_TELEMETRY_TICK_HZ)) {
        return 0U;
    }
    return USB_TELEMETRY_TICK_HZ / rate_hz;
}

void UsbTelemetry_Start(UsbTelemetry_t *t, uint32_t divisor)
{
    if (divisor == 0U) {
        return;
    }
    t->divisor = 0U;    /* para o produtor enquanto reinicia */
    TELEMETRY_FENCE();
    t->tail = t->head;  /* descarta o que sobrou de um stream anterior */
    t->dropped = 0U;    /* o produtor nao escreve com divisor=0 */
    TELEMETRY_FENCE();
    t->divisor = divisor;
}

void UsbTelemetry_Stop(UsbTelemetry_t *t)
{
    t->divisor = 0U;
}

uint8_t UsbTelemetry_IsRunning(const UsbTelemetry_t *t)
{
    return (t->divisor != 0U) ? 1U : 0U;
}

uint32_t UsbTelemetry_Pending(const UsbTelemetry_t *t)
{
    return t->head - t->tail;
}

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
}

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

size_t UsbTelemetry_DrainToPayload(UsbTelemetry_t *t, uint8_t *out, size_t cap)
{
    uint32_t pending;
    uint32_t room;
    uint32_t n;
    uint32_t i;
    size_t idx;

    if (cap < (USB_TELEMETRY_HEADER_WIRE_LEN + USB_TELEMETRY_SAMPLE_WIRE_LEN)) {
        return 0U;
    }
    pending = t->head - t->tail;
    if (pending == 0U) {
        return 0U;
    }
    TELEMETRY_FENCE(); /* le as amostras depois de ver o head */

    room = (uint32_t)((cap - USB_TELEMETRY_HEADER_WIRE_LEN) / USB_TELEMETRY_SAMPLE_WIRE_LEN);
    n = (pending < room) ? pending : room;
    if (n > 0xFFFFU) {
        n = 0xFFFFU;
    }

    put_u16(&out[0], (uint16_t)n);
    put_u32(&out[2], t->dropped);
    idx = USB_TELEMETRY_HEADER_WIRE_LEN;
    for (i = 0U; i < n; i++) {
        const UsbTelemetrySample_t *s = &t->buf[(t->tail + i) & RING_MASK];
        put_u32(&out[idx], s->t_ms);
        put_f32(&out[idx + 4U], s->pressure_mmhg);
        put_f32(&out[idx + 8U], s->setpoint_mmhg);
        out[idx + 12U] = s->state;
        out[idx + 13U] = s->pump_pct;
        out[idx + 14U] = s->valves;
        out[idx + 15U] = s->error;
        idx += USB_TELEMETRY_SAMPLE_WIRE_LEN;
    }

    TELEMETRY_FENCE(); /* termina de ler antes de liberar as posicoes */
    t->tail = t->tail + n;
    return idx;
}

uint8_t UsbTelemetry_TickDue(UsbTelemetry_t *t)
{
    uint32_t div = t->divisor;

    if (div == 0U) {
        t->tick_count = 0U;
        return 0U;
    }
    t->tick_count++;
    if (t->tick_count < div) {
        return 0U;
    }
    t->tick_count = 0U;
    return 1U;
}

void UsbTelemetry_Push(UsbTelemetry_t *t, const UsbTelemetrySample_t *s)
{
    uint32_t head = t->head;

    if ((head - t->tail) >= USB_TELEMETRY_RING_LEN) {
        t->dropped = t->dropped + 1U; /* cheia: o controle nao espera o USB */
        return;
    }
    t->buf[head & RING_MASK] = *s;
    TELEMETRY_FENCE(); /* amostra completa antes de publicar o head */
    t->head = head + 1U;
}
