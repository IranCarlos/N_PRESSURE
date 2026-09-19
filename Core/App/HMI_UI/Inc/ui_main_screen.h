/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-07-29
 * Modified: 2026-08-03
 */

/**
 * @file ui_main_screen.h
 * @brief Tela de boot e tela principal da HMI.
 */

#ifndef UI_MAIN_SCREEN_H
#define UI_MAIN_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "system_types.h"
#include "lvgl.h"

typedef void (*UI_MainScreen_BootDoneCb_t)(void);
typedef void (*UI_MainScreen_ConfigRequestCb_t)(void);

void UI_MainScreen_Init(SystemRuntime_t * runtime,
                        SystemConfig_t * config,
                        UI_MainScreen_BootDoneCb_t boot_done_cb,
                        UI_MainScreen_ConfigRequestCb_t config_request_cb);

void UI_MainScreen_ShowBoot(void);
void UI_MainScreen_Show(void);
void UI_MainScreen_Update(void);
lv_obj_t * UI_MainScreen_GetHandle(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_MAIN_SCREEN_H */
