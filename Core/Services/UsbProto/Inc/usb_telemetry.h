/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-22
 *
 * Fase 7.4: telemetria continua (STREAM_START/STOP, Apendice A). C puro,
 * testado no host.
 *
 * Fila circular sem trava, UM produtor e UM consumidor:
 *   - produtor: controlTask, a 1 kHz, logo depois de ControlLoop_Update().
 *     Chama UsbTelemetry_TickDue() todo tick (so' incrementa um contador) e,
 *     quando der a vez pela decimacao, monta a amostra e chama
 *     UsbTelemetry_Push(). NUNCA bloqueia: fila cheia descarta a amostra
 *     nova e conta em `dropped` — o controle nao espera o USB (Parte A,
 *     seguranca 6).
 *   - consumidor: usbProtoTask, esvazia em lotes com
 *     UsbTelemetry_DrainToPayload() e manda um frame TYPE=STREAM por lote.
 *
 * Taxa: STREAM_START pede rate_hz; o divisor = 1000 / rate_hz (inteiro), a
 * taxa efetiva (1000 / divisor) volta na resposta. Limites por papel
 * (Apendice A): USER <= 50 Hz, DEV <= 1000 Hz.
 *
 * Payload de um frame STREAM (little-endian):
 *   u16 n_amostras, u32 descartadas_total, n x amostra(16 B):
 *     u32 t_ms, f32 pressao_mmhg, f32 setpoint_mmhg, u8 estado,
 *     u8 bomba_pct (0..100), u8 valvulas (bit0=V1 bit1=V2 bit2=V3, 1=aberta),
 *     u8 erro (Apendice D, 0 = nenhum)
 */
#ifndef USB_TELEMETRY_H
#define USB_TELEMETRY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define USB_TELEMETRY_TICK_HZ          (1000U) /* ritmo do produtor (controlTask) */
#define USB_TELEMETRY_RING_LEN         (128U)  /* potencia de 2; 128 ms a 1 kHz */
#define USB_TELEMETRY_SAMPLE_WIRE_LEN  (16U)
#define USB_TELEMETRY_HEADER_WIRE_LEN  (6U)    /* u16 n + u32 descartadas */
#define USB_TELEMETRY_MAX_RATE_USER_HZ (50U)
#define USB_TELEMETRY_MAX_RATE_DEV_HZ  (1000U)

#define USB_TELEMETRY_VALVE_V1 (1U << 0)
#define USB_TELEMETRY_VALVE_V2 (1U << 1)
#define USB_TELEMETRY_VALVE_V3 (1U << 2)

typedef struct {
    uint32_t t_ms;
    float    pressure_mmhg;
    float    setpoint_mmhg;
    uint8_t  state;
    uint8_t  pump_pct;
    uint8_t  valves;
    uint8_t  error;
} UsbTelemetrySample_t;

typedef struct {
    UsbTelemetrySample_t buf[USB_TELEMETRY_RING_LEN];
    volatile uint32_t head;    /* escrito SO' pelo produtor */
    volatile uint32_t tail;    /* escrito SO' pelo consumidor */
    volatile uint32_t divisor; /* 0 = parado; escrito pelo consumidor, lido pelo produtor */
    volatile uint32_t dropped; /* escrito SO' pelo produtor */
    uint32_t tick_count;       /* SO' produtor */
} UsbTelemetry_t;

void UsbTelemetry_Init(UsbTelemetry_t *t);

/* --- consumidor (usbProtoTask) --- */

/* Divisor para a taxa pedida (1..USB_TELEMETRY_TICK_HZ); 0 se fora disso. */
uint32_t UsbTelemetry_DivisorForRate(uint32_t rate_hz);
/* Liga com `divisor` (>0) descartando o que houver na fila. */
void UsbTelemetry_Start(UsbTelemetry_t *t, uint32_t divisor);
void UsbTelemetry_Stop(UsbTelemetry_t *t);
uint8_t UsbTelemetry_IsRunning(const UsbTelemetry_t *t);
uint32_t UsbTelemetry_Pending(const UsbTelemetry_t *t);
/* Monta um payload STREAM com ate' o que couber em `cap`. Devolve o
 * tamanho escrito, ou 0 se nao ha' amostra pendente (ou cap pequeno). */
size_t UsbTelemetry_DrainToPayload(UsbTelemetry_t *t, uint8_t *out, size_t cap);

/* --- produtor (controlTask, 1 kHz) --- */

/* 1 se este tick deve gerar amostra (telemetria ligada e vez da decimacao). */
uint8_t UsbTelemetry_TickDue(UsbTelemetry_t *t);
void UsbTelemetry_Push(UsbTelemetry_t *t, const UsbTelemetrySample_t *s);

#ifdef __cplusplus
}
#endif

#endif /* USB_TELEMETRY_H */
