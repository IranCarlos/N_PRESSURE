/* Ring buffer SPSC de bytes. Ver usb_rx_ring.h. */
#include "usb_rx_ring.h"

#define RING_MASK (USB_RX_RING_CAPACITY - 1U)

void UsbRxRing_Init(UsbRxRing_t *ring)
{
    ring->head = 0U;
    ring->tail = 0U;
}

size_t UsbRxRing_Push(UsbRxRing_t *ring, const uint8_t *data, size_t len)
{
    size_t pushed = 0U;
    uint16_t head = ring->head;
    uint16_t tail = ring->tail; /* snapshot - o consumidor pode avancar depois, nunca antes */

    while (pushed < len) {
        uint16_t used = (uint16_t)(head - tail);
        if (used >= (uint16_t)USB_RX_RING_CAPACITY) {
            break; /* cheio - descarta o resto (overflow, nao ha' onde guardar) */
        }
        ring->buf[head & RING_MASK] = data[pushed];
        head++;
        pushed++;
    }

    ring->head = head; /* publica por ultimo: so' depois que os bytes ja' estao em buf[] */
    return pushed;
}

size_t UsbRxRing_Pop(UsbRxRing_t *ring, uint8_t *out, size_t len)
{
    size_t popped = 0U;
    uint16_t tail = ring->tail;
    uint16_t head = ring->head; /* snapshot - o produtor pode avancar depois, nunca antes */

    while (popped < len) {
        uint16_t available = (uint16_t)(head - tail);
        if (available == 0U) {
            break;
        }
        out[popped] = ring->buf[tail & RING_MASK];
        tail++;
        popped++;
    }

    ring->tail = tail;
    return popped;
}

size_t UsbRxRing_Available(const UsbRxRing_t *ring)
{
    return (size_t)(uint16_t)(ring->head - ring->tail);
}
