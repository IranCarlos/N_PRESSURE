/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-07-29
 * Modified: 2026-08-03
 */

/**
 * @file ui_config_screen.h
 * @brief Tela de configuracao e painel de teste de vazamento.
 */

#ifndef UI_CONFIG_SCREEN_H
#define UI_CONFIG_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "system_types.h"
#include "lvgl.h"

typedef void (*UI_ConfigScreen_BackRequestCb_t)(void);

typedef struct {
    uint32_t remaining_ms;
    float drop_mmhg;
    uint8_t active;
    uint8_t passed;
    uint8_t finished;
} UI_LeakTestStatus_t;

void UI_ConfigScreen_Init(SystemRuntime_t * runtime,
                          SystemConfig_t * config,
                          UI_ConfigScreen_BackRequestCb_t back_cb);

void UI_ConfigScreen_Show(void);
void UI_ConfigScreen_Update(void);
lv_obj_t * UI_ConfigScreen_GetHandle(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_CONFIG_SCREEN_H */
