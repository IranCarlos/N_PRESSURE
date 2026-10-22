/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-12
 * Modified: 2026-10-17
 */

/*
 * Layout 320x240:
 *   +--------------------------------------+
 *   |                                      |
 *   |             myPRESSURE   (34, y=-36) |
 *   |          Nobrega Technology (y=+2)   |
 *   |        Inicializando sistema (y=+24) |
 *   |                                      |
 *   |      Cartao SD: verificando... (-36) |
 *   |      Relogio: verificando...   (-18) |
 *   +--------------------------------------+
 */

#include "ui_boot_screen.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "ui_backend.h"

#define UI_BOOT_MIN_MS (1500u)

static UI_BootScreen_DoneCb_t s_done_cb;

static lv_obj_t * s_screen;
static lv_obj_t * s_status_sd;
static lv_obj_t * s_status_rtc;

static uint32_t s_shown_tick;
static bool s_done_fired;

void UI_BootScreen_Init(UI_BootScreen_DoneCb_t done_cb)
{
    s_done_cb = done_cb;
}

void UI_BootScreen_Show(void)
{
    lv_obj_t * title;
    lv_obj_t * subtitle;
    lv_obj_t * credit;

    s_screen = lv_obj_create(NULL);
    UiTheme_StyleScreen(s_screen);

    /* "myPRESSURE" e' o produto (marca Nobrega Technology). */
    title = lv_label_create(s_screen);
    lv_label_set_text_static(title, "myPRESSURE");
    UiTheme_LabelBig(title);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -36);

    subtitle = lv_label_create(s_screen);
    lv_label_set_text_static(subtitle, "Nobrega Technology");
    UiTheme_LabelBody(subtitle);
    lv_obj_set_style_text_color(subtitle, UiTheme_ColorAccent(), 0);
    lv_obj_align(subtitle, LV_ALIGN_CENTER, 0, 2);

    credit = UiW_LabelHint(s_screen, "Inicializando sistema");
    lv_obj_align(credit, LV_ALIGN_CENTER, 0, 24);

    s_status_sd = UiW_LabelHint(s_screen, "Cartao SD: verificando...");
    lv_obj_align(s_status_sd, LV_ALIGN_BOTTOM_MID, 0, -36);

    s_status_rtc = UiW_LabelHint(s_screen, "Relogio: verificando...");
    lv_obj_align(s_status_rtc, LV_ALIGN_BOTTOM_MID, 0, -18);

    UiW_LoadScreen(s_screen, &s_screen);

    s_shown_tick = UiBackend_GetTickMs();
    s_done_fired = false;
}

void UI_BootScreen_Update(void)
{
    bool skip_clicked;

    if ((s_screen == NULL) || s_done_fired || (lv_screen_active() != s_screen)) {
        return;
    }

    lv_label_set_text_static(s_status_sd, UiBackend_IsSdMounted()
        ? "Cartao SD: OK" : "Cartao SD: ausente/verificando");
    lv_label_set_text_static(s_status_rtc, UiBackend_IsRtcValid()
        ? "Relogio: OK" : "Relogio: ajustar data/hora");

    skip_clicked = UiBackend_ConsumeUserButton();

    if (skip_clicked || ((UiBackend_GetTickMs() - s_shown_tick) >= UI_BOOT_MIN_MS)) {
        s_done_fired = true;
        if (s_done_cb != NULL) {
            s_done_cb();
        }
    }
}
