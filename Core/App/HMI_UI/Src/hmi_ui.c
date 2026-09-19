/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-07-28
 * Modified: 2026-08-03
 */

/**
 * @file hmi_ui.c
 * @author Eng. de Sistemas Embarcados Sênior
 * @brief Implementação da IHM com LVGL.
 * @version 0.2
 * @date 2026-07-29
 *
 * @copyright Copyright (c) 2026
 */

#include "hmi_ui.h"
#include "hmi_encoder_group.h"
#include "ui_main_screen.h"
#include "ui_config_screen.h"
#include "control_loop.h"
#include "eeprom_storage_io.h"
#include "lv_port_indev.h"

/* --- Ponteiros globais --- */
static SystemConfig_t* g_system_cfg;

/* --- Protótipos de Funções Privadas --- */
static void ui_on_boot_done_cb(void);
static void ui_on_config_request_cb(void);
static void ui_on_config_back_cb(void);

/* --- Implementação --- */

void HMI_Init(SystemRuntime_t* p_runtime_data, SystemConfig_t* p_system_cfg) {
    g_system_cfg = p_system_cfg;

    // 1. Cria e vincula o grupo de navegacao ao indev do encoder
    HMI_EncoderGroup_Init();
    HMI_EncoderGroup_BindIndev(lv_port_indev_get_default());

    // 2. Inicializa e exibe as telas de boot/main
    UI_MainScreen_Init(p_runtime_data, g_system_cfg, ui_on_boot_done_cb, ui_on_config_request_cb);
    UI_ConfigScreen_Init(p_runtime_data, g_system_cfg, ui_on_config_back_cb);
    UI_MainScreen_ShowBoot();
}

void HMI_Update(void) {
    UI_MainScreen_Update();
    UI_ConfigScreen_Update();
}

/* --- Callbacks de Eventos --- */

static void ui_on_boot_done_cb(void) {
    UI_MainScreen_Show();
}

static void ui_on_config_request_cb(void) {
    ControlLoop_EnterConfigMode();
    UI_ConfigScreen_Show();
}

static void ui_on_config_back_cb(void) {
    if (g_system_cfg != NULL) {
        (void)EEPROM_Storage_Save(g_system_cfg);
    }

    ControlLoop_ExitConfigMode();
    UI_MainScreen_Show();
}