/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-12
 * Modified: 2026-10-17
 */

/*
 * Layout 320x240:
 *   y=8    Menu (titulo, centralizado)
 *   y=44   [ Novo Ensaio ........................ 296x36 ]
 *   y=94   [ Ajustes ............................ 296x36 ]
 *   y=144  [ < Voltar ........................... 296x36 ]
 */

#include "ui_menu_screen.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "hmi_encoder_group.h"

static UI_MenuScreen_Callbacks_t s_callbacks;
static lv_obj_t * s_screen;

static lv_obj_t * ui_create_item(int32_t y, const char * text, lv_event_cb_t event_cb);
static void ui_new_test_event_cb(lv_event_t * e);
static void ui_settings_event_cb(lv_event_t * e);
static void ui_back_event_cb(lv_event_t * e);

void UI_MenuScreen_Init(const UI_MenuScreen_Callbacks_t * callbacks)
{
    if (callbacks != NULL) {
        s_callbacks = *callbacks;
    }
}

void UI_MenuScreen_Show(void)
{
    lv_obj_t * title;
    static lv_obj_t * focusables[3];
    HMI_EncoderGroupConfig_t group_cfg;

    s_screen = lv_obj_create(NULL);
    UiTheme_StyleScreen(s_screen);

    title = lv_label_create(s_screen);
    lv_label_set_text_static(title, "Menu");
    UiTheme_LabelTitle(title);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    focusables[0] = ui_create_item(44, "Novo Ensaio", ui_new_test_event_cb);
    focusables[1] = ui_create_item(94, "Ajustes", ui_settings_event_cb);
    focusables[2] = ui_create_item(144, LV_SYMBOL_LEFT " Voltar", ui_back_event_cb);

    group_cfg.focus_objs = focusables;
    group_cfg.count = HMI_ARRAY_COUNT(focusables);
    group_cfg.initial_focus_idx = 0U;
    group_cfg.wrap = true;
    (void)HMI_EncoderGroup_Load(&group_cfg);

    UiW_LoadScreen(s_screen, &s_screen);
}

static lv_obj_t * ui_create_item(int32_t y, const char * text, lv_event_cb_t event_cb)
{
    lv_obj_t * btn = UiW_Button(s_screen, LV_HOR_RES - 24, 36, text, event_cb);

    lv_obj_set_pos(btn, 12, y);
    return btn;
}

static void ui_new_test_event_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    if (s_callbacks.new_test_cb != NULL) {
        s_callbacks.new_test_cb();
    }
}

static void ui_settings_event_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    if (s_callbacks.settings_cb != NULL) {
        s_callbacks.settings_cb();
    }
}

static void ui_back_event_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    if (s_callbacks.back_cb != NULL) {
        s_callbacks.back_cb();
    }
}
