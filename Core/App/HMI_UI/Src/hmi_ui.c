/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-07-28
 * Modified: 2026-10-17
 */

/**
 * @file hmi_ui.c
 * @brief Navegacao entre telas e popups. As telas nao se conhecem: cada uma
 *        so' chama os callbacks registrados aqui.
 *
 *   BOOT --(1,5 s ou clique)--> HOME <--> MENU --> NOVO ENSAIO --(Confirmar)--> HOME (ensaio comeca pelo pedal)
 *                                            \--> AJUSTES --(Voltar: salva)--> MENU
 *   Popups (lv_layer_top): RESULTADO DO ENSAIO, ERRO (erro tem prioridade).
 */

#include "hmi_ui.h"
#include "hmi_encoder_group.h"
#include "ui_backend.h"
#include "ui_theme.h"
#include "ui_session.h"
#include "ui_boot_screen.h"
#include "ui_home_screen.h"
#include "ui_menu_screen.h"
#include "ui_new_test_screen.h"
#include "ui_settings_screen.h"
#include "ui_result_popup.h"
#include "ui_error_popup.h"

typedef enum {
    HMI_SCREEN_BOOT = 0,
    HMI_SCREEN_HOME,
    HMI_SCREEN_MENU,
    HMI_SCREEN_NEW_TEST,
    HMI_SCREEN_SETTINGS
} HmiScreen_t;

static HmiScreen_t s_active_screen;
static uint16_t s_last_error = UI_ERR_NONE;
static bool s_result_pending;
static UiCycleResult_t s_pending_result;

static void ui_show_screen(HmiScreen_t screen);
static void ui_on_boot_done_cb(void);
static void ui_on_menu_request_cb(void);
static void ui_on_menu_new_test_cb(void);
static void ui_on_menu_settings_cb(void);
static void ui_on_home_cb(void);
static void ui_on_menu_cb(void);
static void ui_on_settings_back_cb(void);
static void ui_poll_popups(void);
static void ui_on_popup_closed_cb(void);

void HMI_Init(lv_indev_t * encoder_indev)
{
    UI_MenuScreen_Callbacks_t menu_callbacks;

    UiTheme_Init();
    UiSession_Init();

    HMI_EncoderGroup_Init();
    HMI_EncoderGroup_BindIndev(encoder_indev);

    UI_BootScreen_Init(ui_on_boot_done_cb);
    UI_HomeScreen_Init(ui_on_menu_request_cb);

    menu_callbacks.new_test_cb = ui_on_menu_new_test_cb;
    menu_callbacks.settings_cb = ui_on_menu_settings_cb;
    menu_callbacks.back_cb = ui_on_home_cb;
    UI_MenuScreen_Init(&menu_callbacks);

    UI_NewTestScreen_Init(ui_on_home_cb, ui_on_menu_cb);
    UI_SettingsScreen_Init(ui_on_settings_back_cb);

    UI_ResultPopup_Init(ui_on_popup_closed_cb);
    UI_ErrorPopup_Init(ui_on_popup_closed_cb);

    ui_show_screen(HMI_SCREEN_BOOT);
}

void HMI_BindIndev(lv_indev_t * indev)
{
    HMI_EncoderGroup_BindIndev(indev);
}

void HMI_Update(void)
{
    UI_HomeScreen_TrackCycle();

    switch (s_active_screen) {
        case HMI_SCREEN_BOOT:     UI_BootScreen_Update();     break;
        case HMI_SCREEN_HOME:     UI_HomeScreen_Update();     break;
        case HMI_SCREEN_SETTINGS: UI_SettingsScreen_Update(); break;
        case HMI_SCREEN_MENU:
        case HMI_SCREEN_NEW_TEST:
        default:
            /* telas estaticas */
            break;
    }

    ui_poll_popups();
}

/* --- Navegacao --- */

/* Cada Show() recria a tela do zero e recarrega o grupo do encoder. */
static void ui_show_screen(HmiScreen_t screen)
{
    s_active_screen = screen;

    switch (screen) {
        case HMI_SCREEN_BOOT:     UI_BootScreen_Show();     break;
        case HMI_SCREEN_HOME:     UI_HomeScreen_Show();     break;
        case HMI_SCREEN_MENU:     UI_MenuScreen_Show();     break;
        case HMI_SCREEN_NEW_TEST: UI_NewTestScreen_Show();  break;
        case HMI_SCREEN_SETTINGS: UI_SettingsScreen_Show(); break;
        default:                  UI_HomeScreen_Show();     break;
    }
}

static void ui_on_boot_done_cb(void)     { ui_show_screen(HMI_SCREEN_HOME); }
static void ui_on_menu_request_cb(void)  { ui_show_screen(HMI_SCREEN_MENU); }
static void ui_on_menu_new_test_cb(void) { ui_show_screen(HMI_SCREEN_NEW_TEST); }
static void ui_on_home_cb(void)          { ui_show_screen(HMI_SCREEN_HOME); }
static void ui_on_menu_cb(void)          { ui_show_screen(HMI_SCREEN_MENU); }

static void ui_on_menu_settings_cb(void)
{
    UiBackend_EnterConfigMode();
    ui_show_screen(HMI_SCREEN_SETTINGS);
}

static void ui_on_settings_back_cb(void)
{
    UiBackend_ExitConfigMode(); /* tambem persiste a configuracao */
    ui_show_screen(HMI_SCREEN_MENU);
}

/* --- Popups --- */

static void ui_poll_popups(void)
{
    uint16_t current_error = UiBackend_GetLastError();
    UiCycleResult_t result;

    if ((current_error != UI_ERR_NONE) && (current_error != s_last_error) &&
        (s_active_screen != HMI_SCREEN_BOOT)) {
        UI_ResultPopup_Dismiss(); /* erro tem prioridade sobre o resultado */
        UI_ErrorPopup_Show(current_error);
    }
    if (s_active_screen != HMI_SCREEN_BOOT) {
        s_last_error = current_error;
    }

    /* A borda de "ciclo finalizado" e' consumida sempre, mas ignorada nos
     * Ajustes: o fim do teste de vazamento usa a mesma maquina de estados e
     * nao pode gerar popup clinico nem registro bogus no SD. */
    if (UiBackend_PollCycleResult(&result) && (s_active_screen != HMI_SCREEN_SETTINGS)) {
        UiSession_t * session = UiSession_Get();

        UiBackend_SubmitCycleRecord(session->operator_id, session->participant_id,
                                    session->cycle_number);
        s_pending_result = result;
        s_result_pending = true;
    }

    if (s_result_pending && !UI_ErrorPopup_IsActive()) {
        s_result_pending = false;
        UI_ResultPopup_Show(&s_pending_result);
    }

    UI_ResultPopup_Update();
}

/* Popup fechado: devolve o grupo do encoder a' tela de baixo recriando-a
 * (simples e seguro; popup e' evento raro). Se ainda houver outro popup
 * aberto/pendente ele assume no proximo HMI_Update(). */
static void ui_on_popup_closed_cb(void)
{
    if (s_active_screen != HMI_SCREEN_BOOT) {
        ui_show_screen(s_active_screen);
    }
}
