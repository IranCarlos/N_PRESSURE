#include "lv_port_disp.h"
#include "st7789.h"
#include "system_types.h"

#define MY_DISP_HOR_RES 320
#define MY_DISP_VER_RES 240

// Buffers de desenho: Vamos usar 1/10 da tela. Com DMA, Double Buffering é super recomendado!
#define DRAW_BUF_SIZE (MY_DISP_HOR_RES * MY_DISP_VER_RES / 20)
// Alinhados a 32 bytes (linha de D-Cache do Cortex-M7) para que
// SCB_CleanDCache_by_Addr() nunca precise arredondar sobre memória vizinha
// não relacionada. O L4 original não tem cache, então isso é exigência nova.
// DMA_BUFFER: coloca em RAM_D1 (alcancavel por DMA1) em vez de DTCMRAM
// (default do linker, NAO alcancavel por DMA no H7) — ver system_types.h.
static uint16_t buf_1[DRAW_BUF_SIZE] __attribute__((aligned(32))) DMA_BUFFER;
static uint16_t buf_2[DRAW_BUF_SIZE] __attribute__((aligned(32))) DMA_BUFFER; // O segundo buffer permite renderizar enquanto o DMA envia

static lv_display_t * my_disp;
static lv_display_t * s_pending_flush_disp;

// Função de Flush customizada para o V9
static void disp_flush(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map)
{
    HAL_StatusTypeDef hal_status;
    ST7789_SetWindow(area->x1, area->y1, area->x2, area->y2);

    uint32_t pixel_count = (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1);
    uint32_t size_in_bytes = pixel_count * 2;

    // --- SWAP DE BYTES POR SOFTWARE ---
    uint16_t * color_p = (uint16_t *)px_map;
    for(uint32_t i = 0; i < pixel_count; i++) {
        // Inverte o byte alto com o byte baixo de cada pixel RGB565
        color_p[i] = (color_p[i] >> 8) | (color_p[i] << 8);
    }
    // -----------------------------------

    // O DMA lê a RAM diretamente; sem isto, o SPI poderia transmitir dados
    // obsoletos ainda presos na D-Cache do Cortex-M7 (não é um problema no
    // L4 original, que não tem cache de dados).
    SCB_CleanDCache_by_Addr((uint32_t *)px_map, (int32_t)((size_in_bytes + 31u) & ~31u));

    ST7789_DC_DATA();
    ST7789_CS_LOW();

    s_pending_flush_disp = disp;
    hal_status = HAL_SPI_Transmit_DMA(&hspi1, px_map, (uint16_t)size_in_bytes);
    if (hal_status != HAL_OK) {
        s_pending_flush_disp = NULL;
        ST7789_CS_HIGH();
        lv_display_flush_ready(disp);
    }
}

void lv_port_disp_init(void)
{
    // Inicializa o Hardware
    ST7789_Init();

    // LVGL V9: Cria o display
    my_disp = lv_display_create(MY_DISP_HOR_RES, MY_DISP_VER_RES);
    
    // Configura os buffers
    lv_display_set_buffers(my_disp, buf_1, buf_2, sizeof(buf_1), LV_DISPLAY_RENDER_MODE_PARTIAL);
    
    // Registra a função de flush
    lv_display_set_flush_cb(my_disp, disp_flush);
}

// Callback de conclusão do DMA (Você deve colocar isso aqui ou no seu main.c)
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == hspi1.Instance) {
        lv_display_t * disp = s_pending_flush_disp;

        s_pending_flush_disp = NULL;
        ST7789_CS_HIGH();

        if (disp != NULL) {
            lv_display_flush_ready(disp);
        }
    }
}