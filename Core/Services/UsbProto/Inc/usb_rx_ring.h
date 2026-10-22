/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-14
 *
 * Ring buffer de bytes SPSC (single-producer/single-consumer), sem lock,
 * para desacoplar o callback de recepcao do CDC (ISR, via HAL_PCD_IRQHandler
 * -> USBD_LL_DataOutStage -> CDC_Receive_HS) da usbProtoTask que decodifica
 * o COBS fora de contexto de interrupcao - mesmo padrao "ISR so' enfileira,
 * a task processa" do storage_task. C puro, sem HAL/RTOS - testavel no
 * host; quem chama de verdade e' Core/Src/main.c (StartUsbProtoTask) e
 * USB_DEVICE/App/usbd_cdc_if.c (CDC_Receive_HS).
 *
 * Seguro sem lock porque cada lado so' escreve seu proprio indice (head = so'
 * o produtor escreve, tail = so' o consumidor escreve) e so' le o indice do
 * outro lado - classico SPSC lock-free em um unico core (Cortex-M7, sem
 * SMP). Capacidade deve ser potencia de 2 (mascara em vez de modulo).
 */
#ifndef USB_RX_RING_H
#define USB_RX_RING_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define USB_RX_RING_CAPACITY (1024U) /* potencia de 2 */

typedef struct {
    uint8_t  buf[USB_RX_RING_CAPACITY];
    volatile uint16_t head; /* proximo slot livre para o produtor escrever */
    volatile uint16_t tail; /* proximo slot que o consumidor le */
} UsbRxRing_t;

void UsbRxRing_Init(UsbRxRing_t *ring);

/* Produtor (ISR). Copia o que couber; retorna quantos bytes de fato
 * entraram (pode ser < len se o ring estiver cheio - no overflow). */
size_t UsbRxRing_Push(UsbRxRing_t *ring, const uint8_t *data, size_t len);

/* Consumidor (task). Retorna quantos bytes de fato saíram (pode ser < len
 * se o ring tiver menos disponivel). */
size_t UsbRxRing_Pop(UsbRxRing_t *ring, uint8_t *out, size_t len);

size_t UsbRxRing_Available(const UsbRxRing_t *ring);

#ifdef __cplusplus
}
#endif

#endif /* USB_RX_RING_H */
