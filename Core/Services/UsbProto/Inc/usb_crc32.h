/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-14
 *
 * CRC32 do protocolo USB (Apêndice A: "CRC32: polinômio 0x04C11DB7, usar o
 * CRC do hardware"). Implementação em C puro (sem HAL) pra ficar testável
 * no host agora; o polinômio/init/sem-reflexão usados aqui reproduzem o
 * comportamento padrão de fábrica do periférico CRC do STM32H7 (poly
 * 0x04C11DB7, init 0xFFFFFFFF, sem reflect-in/out, sem XOR final) alimentado
 * byte a byte - mesmo precedente do SHA256 (Core/Services/Audit_Trail/
 * Inc/sha256.h): software agora, trocar pelo periférico de hardware depois
 * é otimização, não requisito. Se/quando migrar, confirmar que a config do
 * HAL_CRC bate com isto (sem inverter entrada/saida) antes de trocar.
 */
#ifndef USB_CRC32_H
#define USB_CRC32_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t UsbCrc32_Compute(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* USB_CRC32_H */
