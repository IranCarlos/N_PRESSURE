/* Teste de host de usb_crc32 (sem HAL). Ver test/host/run.sh. */
#include "usb_crc32.h"
#include <string.h>
#include <stdio.h>

static int s_fail;
#define CHECK(c) do { if (!(c)) { printf("FALHA %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)

static void test_empty_input_is_init_value(void)
{
    uint8_t dummy[1] = { 0 };
    CHECK(UsbCrc32_Compute(dummy, 0U) == 0xFFFFFFFFUL);
}

static void test_known_vector_check_string(void)
{
    /* Vetor de referencia do algoritmo CRC-32/MPEG-2 (poly 0x04C11DB7, init
     * 0xFFFFFFFF, sem reflect-in/out, sem XOR final - mesma config usada
     * aqui, ver usb_crc32.h) para a string ASCII "123456789". */
    const uint8_t data[] = "123456789";
    CHECK(UsbCrc32_Compute(data, strlen((const char *)data)) == 0x0376E6E7UL);
}

static void test_different_inputs_differ(void)
{
    const uint8_t a[] = { 0x01, 0x02, 0x03, 0x04 };
    const uint8_t b[] = { 0x01, 0x02, 0x03, 0x05 };
    CHECK(UsbCrc32_Compute(a, sizeof(a)) != UsbCrc32_Compute(b, sizeof(b)));
}

static void test_deterministic(void)
{
    const uint8_t a[] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE };
    CHECK(UsbCrc32_Compute(a, sizeof(a)) == UsbCrc32_Compute(a, sizeof(a)));
}

/* Fase 7.5: CRC em pedacos (FS_CRC le o arquivo em blocos) == de uma vez. */
static void test_incremental_matches_one_shot(void)
{
    const uint8_t *data = (const uint8_t *)"123456789";
    uint32_t crc = UsbCrc32_Init();
    crc = UsbCrc32_Update(crc, data, 4U);
    crc = UsbCrc32_Update(crc, &data[4], 0U);
    crc = UsbCrc32_Update(crc, &data[4], 5U);
    CHECK(crc == 0x0376E6E7UL);
}

int main(void)
{
    test_incremental_matches_one_shot();
    test_empty_input_is_init_value();
    test_known_vector_check_string();
    test_different_inputs_differ();
    test_deterministic();
    printf(s_fail ? "usb_crc32: %d FALHA(S)\n" : "usb_crc32: OK\n", s_fail);
    return s_fail != 0;
}
