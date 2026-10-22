/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-17
 */

#include "ui_theme.h"

/* Paleta oficial: BG_SCREEN/BG_BOX/ACCENT sao os RGB exatos do PDF de marca
 * (16092C, 291B54, 8B8BF0). OK/WARN/DANGER sao semanticos (verde/amarelo/
 * vermelho), nao de marca. */
#define UI_COLOR_BG_SCREEN    (0x16092Cu)
#define UI_COLOR_BG_BOX       (0x291B54u)
#define UI_COLOR_BORDER       (0x4A3A7Au)
#define UI_COLOR_TITLE        (0xFFFFFFu)
#define UI_COLOR_BODY         (0xD9D4F5u)
#define UI_COLOR_HINT         (0x9C90C9u)
#define UI_COLOR_BIG_VALUE    (0xFFFFFFu)
#define UI_COLOR_BTN_BG       (0x332368u)
#define UI_COLOR_BTN_PRESSED  (0x4A3A8Cu)
#define UI_COLOR_ACCENT       (0x8B8BF0u)
#define UI_COLOR_INPUT_BG     (0x241A47u)
#define UI_COLOR_INPUT_BORDER (0x5B4A96u)
#define UI_COLOR_SELECTED     (0x4A3A8Cu)
#define UI_COLOR_OK           (0x9EE7B3u)
#define UI_COLOR_WARN         (0xF5D76Eu)
#define UI_COLOR_DANGER       (0xFF6B6Bu)
#define UI_COLOR_NEUTRAL      (0xA79BDBu)
#define UI_COLOR_INFLATING    (0xFFB347u)
#define UI_COLOR_STABILIZING  (0x66E2FFu)
#define UI_COLOR_DEFLATING    (0xF36C6Cu)

static lv_style_t s_style_screen;
static lv_style_t s_style_box;
static lv_style_t s_style_btn;
static lv_style_t s_style_btn_pressed;
static lv_style_t s_style_focus;
static lv_style_t s_style_input;
static lv_style_t s_style_overlay;
static lv_style_t s_style_title;
static lv_style_t s_style_body;
static lv_style_t s_style_hint;
static lv_style_t s_style_big;
static bool s_initialized;

void UiTheme_Init(void)
{
    if (s_initialized) {
        return;
    }

    lv_style_init(&s_style_screen);
    lv_style_set_bg_color(&s_style_screen, lv_color_hex(UI_COLOR_BG_SCREEN));
    lv_style_set_bg_opa(&s_style_screen, LV_OPA_COVER);

    lv_style_init(&s_style_box);
    lv_style_set_radius(&s_style_box, 4);
    lv_style_set_border_width(&s_style_box, 1);
    lv_style_set_border_color(&s_style_box, lv_color_hex(UI_COLOR_BORDER));
    lv_style_set_bg_color(&s_style_box, lv_color_hex(UI_COLOR_BG_BOX));
    lv_style_set_bg_opa(&s_style_box, LV_OPA_COVER);
    /* Padding zero: as coordenadas dos filhos (layouts em cada tela) sao em
     * px a partir da borda da caixa, sem depender do padding do tema padrao
     * do LVGL (que muda com LV_DPI_DEF). */
    lv_style_set_pad_all(&s_style_box, 0);

    lv_style_init(&s_style_btn);
    lv_style_set_radius(&s_style_btn, 6);
    lv_style_set_bg_color(&s_style_btn, lv_color_hex(UI_COLOR_BTN_BG));
    lv_style_set_bg_opa(&s_style_btn, LV_OPA_COVER);
    lv_style_set_shadow_width(&s_style_btn, 0); /* sombra custa CPU em SPI/RGB565 */

    lv_style_init(&s_style_btn_pressed);
    lv_style_set_bg_color(&s_style_btn_pressed, lv_color_hex(UI_COLOR_BTN_PRESSED));

    lv_style_init(&s_style_focus);
    lv_style_set_outline_width(&s_style_focus, 2);
    lv_style_set_outline_color(&s_style_focus, lv_color_hex(UI_COLOR_ACCENT));
    lv_style_set_outline_opa(&s_style_focus, LV_OPA_COVER);

    lv_style_init(&s_style_input);
    lv_style_set_bg_color(&s_style_input, lv_color_hex(UI_COLOR_INPUT_BG));
    lv_style_set_border_color(&s_style_input, lv_color_hex(UI_COLOR_INPUT_BORDER));
    lv_style_set_anim_duration(&s_style_input, 0);
    /* Campos de 22-26 px de altura: o padding do tema padrao empurrava o
     * texto para baixo/fora do campo. */
    lv_style_set_pad_ver(&s_style_input, 3);
    lv_style_set_pad_hor(&s_style_input, 6);

    lv_style_init(&s_style_overlay);
    lv_style_set_bg_color(&s_style_overlay, lv_color_black());
    lv_style_set_bg_opa(&s_style_overlay, LV_OPA_50);

    lv_style_init(&s_style_title);
    lv_style_set_text_color(&s_style_title, lv_color_hex(UI_COLOR_TITLE));
    lv_style_set_text_font(&s_style_title, &lv_font_montserrat_22);

    lv_style_init(&s_style_body);
    lv_style_set_text_color(&s_style_body, lv_color_hex(UI_COLOR_BODY));
    lv_style_set_text_font(&s_style_body, &lv_font_montserrat_14);

    lv_style_init(&s_style_hint);
    lv_style_set_text_color(&s_style_hint, lv_color_hex(UI_COLOR_HINT));
    lv_style_set_text_font(&s_style_hint, &lv_font_montserrat_14);

    lv_style_init(&s_style_big);
    lv_style_set_text_color(&s_style_big, lv_color_hex(UI_COLOR_BIG_VALUE));
    lv_style_set_text_font(&s_style_big, &lv_font_montserrat_34);

    s_initialized = true;
}

void UiTheme_StyleScreen(lv_obj_t * screen)
{
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, LV_HOR_RES, LV_VER_RES);
    lv_obj_add_style(screen, &s_style_screen, 0);
}

void UiTheme_StyleBox(lv_obj_t * box)
{
    lv_obj_add_style(box, &s_style_box, 0);
}

void UiTheme_StyleButton(lv_obj_t * btn)
{
    lv_obj_add_style(btn, &s_style_btn, 0);
    lv_obj_add_style(btn, &s_style_btn_pressed, LV_STATE_PRESSED);
    UiTheme_StyleFocusOutline(btn);
}

void UiTheme_StyleFocusOutline(lv_obj_t * obj)
{
    lv_obj_add_style(obj, &s_style_focus, LV_STATE_FOCUSED);
}

void UiTheme_StyleInputField(lv_obj_t * obj)
{
    lv_obj_add_style(obj, &s_style_input, 0);
}

void UiTheme_StyleOverlay(lv_obj_t * obj)
{
    lv_obj_remove_style_all(obj);
    lv_obj_add_style(obj, &s_style_overlay, 0);
}

void UiTheme_LabelTitle(lv_obj_t * label)
{
    lv_obj_add_style(label, &s_style_title, 0);
}

void UiTheme_LabelBody(lv_obj_t * label)
{
    lv_obj_add_style(label, &s_style_body, 0);
}

void UiTheme_LabelHint(lv_obj_t * label)
{
    lv_obj_add_style(label, &s_style_hint, 0);
}

void UiTheme_LabelBig(lv_obj_t * label)
{
    lv_obj_add_style(label, &s_style_big, 0);
}

lv_color_t UiTheme_ColorForState(UiState_t state)
{
    uint32_t hex;

    switch (state) {
        case UI_STATE_INFLATING:      hex = UI_COLOR_INFLATING;   break;
        case UI_STATE_STABILIZING:    hex = UI_COLOR_STABILIZING; break;
        case UI_STATE_STABILIZED:     hex = UI_COLOR_OK;          break;
        case UI_STATE_DEFLATING_FAST: hex = UI_COLOR_DEFLATING;   break;
        case UI_STATE_LEAK_TEST:      hex = UI_COLOR_WARN;        break;
        case UI_STATE_FAULT:          hex = UI_COLOR_DANGER;      break;
        case UI_STATE_IDLE:
        case UI_STATE_CONFIG:
        case UI_STATE_BOOT:
        default:                      hex = UI_COLOR_NEUTRAL;     break;
    }

    return lv_color_hex(hex);
}

const char * UiTheme_TextForState(UiState_t state)
{
    switch (state) {
        case UI_STATE_IDLE:           return "Aguardando";
        case UI_STATE_INFLATING:      return "Inflando...";
        case UI_STATE_STABILIZING:    return "Estabilizando";
        case UI_STATE_STABILIZED:     return "Estabilizado - Pronto p/ desinflar";
        case UI_STATE_DEFLATING_FAST: return "Desinflando";
        case UI_STATE_LEAK_TEST:      return "Teste de vazamento";
        case UI_STATE_CONFIG:         return "Modo configuracao";
        case UI_STATE_FAULT:          return "Falha";
        case UI_STATE_BOOT:
        default:                      return "Inicializando";
    }
}

lv_color_t UiTheme_ColorOk(void)       { return lv_color_hex(UI_COLOR_OK); }
lv_color_t UiTheme_ColorWarn(void)     { return lv_color_hex(UI_COLOR_WARN); }
lv_color_t UiTheme_ColorDanger(void)   { return lv_color_hex(UI_COLOR_DANGER); }
lv_color_t UiTheme_ColorAccent(void)   { return lv_color_hex(UI_COLOR_ACCENT); }
lv_color_t UiTheme_ColorTrack(void)    { return lv_color_hex(UI_COLOR_INPUT_BG); }
lv_color_t UiTheme_ColorSelected(void) { return lv_color_hex(UI_COLOR_SELECTED); }
