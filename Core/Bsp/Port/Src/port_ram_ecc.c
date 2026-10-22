/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-17
 */

#include "port_ram_ecc.h"

#define AXI_SRAM_BASE        (0x24000000UL)
#define AXI_SRAM_MAX_BYTES   (320UL * 1024UL)
#define TCM_AXI_STEP_BYTES   (64UL * 1024UL)

void PortRamEcc_InitAxiSram(void)
{
    /* FLASH_OPTSR2_CUR.TCM_AXI_SHARED (bits 1:0): cada passo tira 64 KB da
     * AXI SRAM e da' ao ITCM — 00 = 320 KB, 01 = 256 KB, 10 = 192 KB,
     * 11 = 128 KB (RM0468, "AXI SRAM"). */
    uint32_t shared = (FLASH->OPTSR2_CUR & FLASH_OPTSR2_TCM_AXI_SHARED_Msk) >>
                      FLASH_OPTSR2_TCM_AXI_SHARED_Pos;
    uint32_t size = AXI_SRAM_MAX_BYTES - (shared * TCM_AXI_STEP_BYTES);
    volatile uint64_t *p = (volatile uint64_t *)AXI_SRAM_BASE;
    volatile uint64_t *end = (volatile uint64_t *)(AXI_SRAM_BASE + size);

    /* Escrita de 64 bits alinhada = palavra de ECC inteira: o controlador
     * grava dado + ECC novos sem ler o conteudo antigo. */
    while (p < end) {
        *p = 0u;
        p++;
    }

    __DSB(); /* garante que tudo saiu do write buffer antes de seguir */
}

/* Fim de .bss (definido pelo linker script). */
extern uint32_t _end;

void PortRamEcc_InitDtcmFree(void)
{
    uintptr_t start = ((uintptr_t)&_end + 7u) & ~(uintptr_t)7u;
    /* So' abaixo do SP atual (o quadro desta funcao e dos chamadores fica
     * acima dele e ja' foi escrito com palavras inteiras pelo prologo). */
    uintptr_t stop = (uintptr_t)__get_MSP() & ~(uintptr_t)7u;
    volatile uint32_t *p = (volatile uint32_t *)start;
    volatile uint32_t *end = (volatile uint32_t *)stop;

    /* Palavra de 32 bits inteira = palavra de ECC da DTCM inteira. */
    while (p < end) {
        *p = 0u;
        p++;
    }

    __DSB();
}
