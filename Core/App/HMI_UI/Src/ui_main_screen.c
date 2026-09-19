/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-07-29
 * Modified: 2026-08-03
 */

/**
 * @file ui_main_screen.c
 * @brief Tela de boot e tela principal com layout flat para ST7789.
 */

#include "ui_main_screen.h"

#include "hmi_encoder_group.h"
#include "control_loop.h"

#define UI_PRESSURE_MIN_MMHG      (0)
#define UI_PRESSURE_MAX_MMHG      (300)
#define UI_PRESSURE_MIN_SPAN_MMHG (50)
#define UI_BOOT_TIMER_MS          (30)
#define UI_BOOT_STEP              (3)

static SystemRuntime_t * s_runtime;
static SystemConfig_t * s_cfg;
static UI_MainScreen_BootDoneCb_t s_boot_done_cb;
static UI_MainScreen_ConfigRequestCb_t s_config_request_cb;

static lv_obj_t * s_boot_screen;
static lv_obj_t * s_boot_bar;
static lv_timer_t * s_boot_timer;
static uint8_t s_boot_progress;

static lv_obj_t * s_main_screen;
static lv_obj_t * s_status_label;
static lv_obj_t * s_pressure_value_label;
static lv_obj_t * s_pressure_bar;
static lv_obj_t * s_setpoint_label;
static lv_obj_t * s_config_button;

static const char * ui_state_to_text(SystemState_t state);
static void ui_init_boot_screen(void);
static void ui_init_main_screen(void);
static void ui_boot_timer_cb(lv_timer_t * timer);
static void ui_config_button_event_cb(lv_event_t * e);

void UI_MainScreen_Init(SystemRuntime_t * runtime,
                        SystemConfig_t * config,
                        UI_MainScreen_BootDoneCb_t boot_done_cb,
                        UI_MainScreen_ConfigRequestCb_t config_request_cb)
{
    s_runtime = runtime;
    s_cfg = config;
    s_boot_done_cb = boot_done_cb;
    s_config_request_cb = config_request_cb;
}

void UI_MainScreen_ShowBoot(void)
{
    lv_obj_t * old_boot = s_boot_screen;

    if (s_boot_screen != NULL) {
        s_boot_screen = NULL;
        s_boot_bar = NULL;
    }

    ui_init_boot_screen();
    lv_screen_load(s_boot_screen);

    if (s_boot_timer != NULL) {
        lv_timer_delete(s_boot_timer);
    }

    s_boot_progress = 0U;
    lv_bar_set_value(s_boot_bar, 0, LV_ANIM_OFF);
    s_boot_timer = lv_timer_create(ui_boot_timer_cb, UI_BOOT_TIMER_MS, NULL);

    if ((old_boot != NULL) && (old_boot != s_boot_screen)) {
        lv_obj_del(old_boot);
    }
}

void UI_MainScreen_Show(void)
{
    lv_obj_t * old_boot = s_boot_screen;
    lv_obj_t * focusables[1];
    HMI_EncoderGroupConfig_t group_cfg;

    ui_init_main_screen();

    if (old_boot != NULL) {
        s_boot_screen = NULL;
        s_boot_bar = NULL;
    }

    focusables[0] = s_config_button;

    group_cfg.focus_objs = focusables;
    group_cfg.count = 1U;
    group_cfg.initial_focus_idx = 0U;
    group_cfg.wrap = true;
    (void)HMI_EncoderGroup_Load(&group_cfg);

    lv_screen_load(s_main_screen);

    UI_MainScreen_Update();
}

void UI_MainScreen_Update(void)
{
    int32_t pressure_val;
    int32_t pressure_limit;
    lv_color_t indicator_color = lv_color_hex(0x63D471);

    if ((s_runtime == NULL) || (s_main_screen == NULL)) {
        return;
    }

    if (lv_screen_active() != s_main_screen) {
        return;
    }

    pressure_limit = UI_PRESSURE_MAX_MMHG;
    if (s_cfg != NULL) {
        pressure_limit = (int32_t)s_cfg->max_setpoint_mmhg;
    }
    if (pressure_limit < UI_PRESSURE_MIN_SPAN_MMHG) {
        pressure_limit = UI_PRESSURE_MIN_SPAN_MMHG;
    }

    lv_bar_set_range(s_pressure_bar, UI_PRESSURE_MIN_MMHG, pressure_limit);

    pressure_val = (int32_t)s_runtime->pressure_display;
    if (pressure_val < UI_PRESSURE_MIN_MMHG) {
        pressure_val = UI_PRESSURE_MIN_MMHG;
    }
    if (pressure_val > pressure_limit) {
        pressure_val = pressure_limit;
    }

    lv_label_set_text(s_status_label, ui_state_to_text(s_runtime->state));
    lv_label_set_text_fmt(s_pressure_value_label, "%03ld mmHg", pressure_val);
    lv_bar_set_value(s_pressure_bar, pressure_val, LV_ANIM_OFF);

    switch (s_runtime->state) {
        case SYSTEM_STATE_INFLATING:
            indicator_color = lv_color_hex(0xFFB347);
            break;
        case SYSTEM_STATE_STABILIZING:
            indicator_color = lv_color_hex(0x66E2FF);
            break;
        case SYSTEM_STATE_STABILIZED:
            indicator_color = lv_color_hex(0x63D471);
            break;
        case SYSTEM_STATE_DEFLATING_FAST:
            indicator_color = lv_color_hex(0xF36C6C);
            break;
        case SYSTEM_STATE_LEAK_TEST:
            indicator_color = lv_color_hex(0xF5D76E);
            break;
        case SYSTEM_STATE_FAULT:
            indicator_color = lv_color_hex(0xFF6B6B);
            break;
        case SYSTEM_STATE_IDLE:
        case SYSTEM_STATE_CONFIG:
        case SYSTEM_STATE_BOOT:
        default:
            indicator_color = lv_color_hex(0xA7C9D9);
            break;
    }

    lv_obj_set_style_bg_color(s_pressure_bar, indicator_color, LV_PART_INDICATOR);

    lv_label_set_text_fmt(s_setpoint_label, "Setpoint atual: %03d mmHg", (int)s_runtime->setpoint_mmhg);
}

lv_obj_t * UI_MainScreen_GetHandle(void)
{
    return s_main_screen;
}

static const char * ui_state_to_text(SystemState_t state)
{
    switch (state) {
        case SYSTEM_STATE_IDLE:
            return "Aguardando";
        case SYSTEM_STATE_INFLATING:
            return "Inflando...";
        case SYSTEM_STATE_STABILIZING:
            return "Estabilizando";
        case SYSTEM_STATE_STABILIZED:
            return "Estabilizado - Pronto p/ desinflar";
        case SYSTEM_STATE_DEFLATING_FAST:
            return "Desinflando";
        case SYSTEM_STATE_LEAK_TEST:
            return "Teste de vazamento";
        case SYSTEM_STATE_CONFIG:
            return "Modo configuracao";
        case SYSTEM_STATE_FAULT:
            return "Falha";
        case SYSTEM_STATE_BOOT:
        default:
            return "Inicializando";
    }
}

static void ui_init_boot_screen(void)
{
    lv_obj_t * title;
    lv_obj_t * subtitle;
    lv_obj_t * credit;

    s_boot_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_boot_screen);
    lv_obj_set_size(s_boot_screen, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(s_boot_screen, lv_color_hex(0x0B1620), 0);
    lv_obj_set_style_bg_opa(s_boot_screen, LV_OPA_COVER, 0);

    title = lv_label_create(s_boot_screen);
    lv_label_set_text(title, "ANGIOLOGISTA");
    lv_obj_set_style_text_color(title, lv_color_hex(0xD8ECF8), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -24);

    subtitle = lv_label_create(s_boot_screen);
    lv_label_set_text(subtitle, "Inicializando sistema");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x9EBED1), 0);
    lv_obj_align(subtitle, LV_ALIGN_CENTER, 0, 6);

    credit = lv_label_create(s_boot_screen);
    lv_label_set_text(credit, "by Dr. Leonardo Nobrega");
    lv_obj_set_style_text_color(credit, lv_color_hex(0x7FA7BE), 0);
    lv_obj_set_style_text_font(credit, &lv_font_montserrat_14, 0);
    lv_obj_align(credit, LV_ALIGN_CENTER, 0, 30);

    s_boot_bar = lv_bar_create(s_boot_screen);
    lv_obj_set_size(s_boot_bar, LV_HOR_RES - 48, 14);
    lv_obj_align(s_boot_bar, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_bar_set_range(s_boot_bar, 0, 100);
    lv_obj_set_style_bg_color(s_boot_bar, lv_color_hex(0x243746), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_boot_bar, lv_color_hex(0x4CC9F0), LV_PART_INDICATOR);
}

static void ui_init_main_screen(void)
{
        int32_t pressure_limit = UI_PRESSURE_MAX_MMHG;

        if (s_cfg != NULL) {
            pressure_limit = (int32_t)s_cfg->max_setpoint_mmhg;
        }
        if (pressure_limit < UI_PRESSURE_MIN_SPAN_MMHG) {
            pressure_limit = UI_PRESSURE_MIN_SPAN_MMHG;
        }

    lv_obj_t * top_bar;
    lv_obj_t * pressure_box;
    lv_obj_t * usage_hint;
    lv_obj_t * config_icon;

    if (s_main_screen != NULL) {
        lv_obj_del(s_main_screen);
        s_main_screen = NULL;
    }

    s_main_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_main_screen);
    lv_obj_set_size(s_main_screen, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(s_main_screen, lv_color_hex(0x0D1B24), 0);
    lv_obj_set_style_bg_opa(s_main_screen, LV_OPA_COVER, 0);

    top_bar = lv_obj_create(s_main_screen);
    lv_obj_set_size(top_bar, LV_HOR_RES - 12, 28);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_set_style_radius(top_bar, 4, 0);
    lv_obj_set_style_border_width(top_bar, 1, 0);
    lv_obj_set_style_border_color(top_bar, lv_color_hex(0x3A5568), 0);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x152634), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);

    s_status_label = lv_label_create(top_bar);
    lv_label_set_text(s_status_label, "Aguardando");
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xE8F1F7), 0);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_14, 0);
    lv_obj_center(s_status_label);

    pressure_box = lv_obj_create(s_main_screen);
    lv_obj_set_size(pressure_box, LV_HOR_RES - 12, 104);
    lv_obj_align(pressure_box, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_radius(pressure_box, 4, 0);
    lv_obj_set_style_border_width(pressure_box, 1, 0);
    lv_obj_set_style_border_color(pressure_box, lv_color_hex(0x3A5568), 0);
    lv_obj_set_style_bg_color(pressure_box, lv_color_hex(0x13222F), 0);
    lv_obj_set_style_bg_opa(pressure_box, LV_OPA_COVER, 0);

    s_pressure_value_label = lv_label_create(pressure_box);
    lv_label_set_text(s_pressure_value_label, "000 mmHg");
    lv_obj_set_style_text_font(s_pressure_value_label, &lv_font_montserrat_34, 0);
    lv_obj_set_style_text_color(s_pressure_value_label, lv_color_hex(0xF4FAFF), 0);
    lv_obj_align(s_pressure_value_label, LV_ALIGN_TOP_MID, 0, 4);

    s_pressure_bar = lv_bar_create(pressure_box);
    lv_obj_set_size(s_pressure_bar, LV_HOR_RES - 42, 14);
    lv_obj_align(s_pressure_bar, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_bar_set_range(s_pressure_bar, UI_PRESSURE_MIN_MMHG, pressure_limit);
    lv_obj_set_style_bg_color(s_pressure_bar, lv_color_hex(0x273948), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_pressure_bar, lv_color_hex(0x66E2FF), LV_PART_INDICATOR);

    s_setpoint_label = lv_label_create(s_main_screen);
    lv_label_set_text(s_setpoint_label, "Setpoint atual: 120 mmHg");
    lv_obj_set_style_text_color(s_setpoint_label, lv_color_hex(0xDDEBF3), 0);
    lv_obj_set_style_text_font(s_setpoint_label, &lv_font_montserrat_16, 0);
    lv_obj_align(s_setpoint_label, LV_ALIGN_TOP_LEFT, 10, 150);

    usage_hint = lv_label_create(s_main_screen);
    lv_label_set_text(usage_hint, "Pressione o encoder para iniciar/parar");
    lv_obj_set_style_text_color(usage_hint, lv_color_hex(0x8DB3C7), 0);
    lv_obj_set_style_text_font(usage_hint, &lv_font_montserrat_14, 0);
    lv_obj_align(usage_hint, LV_ALIGN_TOP_LEFT, 10, 172);

    s_config_button = lv_button_create(s_main_screen);
    lv_obj_set_size(s_config_button, 30, 30);
    lv_obj_align(s_config_button, LV_ALIGN_BOTTOM_RIGHT, -6, -6);
    lv_obj_set_style_radius(s_config_button, 6, 0);
    lv_obj_set_style_bg_color(s_config_button, lv_color_hex(0x1D3A4A), 0);
    lv_obj_set_style_bg_color(s_config_button, lv_color_hex(0x285065), LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(s_config_button, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(s_config_button, lv_color_hex(0x8EE6FF), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(s_config_button, ui_config_button_event_cb, LV_EVENT_CLICKED, NULL);

    config_icon = lv_label_create(s_config_button);
    lv_label_set_text(config_icon, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_font(config_icon, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(config_icon, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(config_icon, lv_color_hex(0xF2FAFF), 0);
    lv_obj_center(config_icon);
}

static void ui_boot_timer_cb(lv_timer_t * timer)
{
    LV_UNUSED(timer);

    if (s_boot_progress < 100U) {
        s_boot_progress = (uint8_t)(s_boot_progress + UI_BOOT_STEP);
        if (s_boot_progress > 100U) {
            s_boot_progress = 100U;
        }
    }

    lv_bar_set_value(s_boot_bar, s_boot_progress, LV_ANIM_OFF);

    if (s_boot_progress >= 100U) {
        if (s_boot_timer != NULL) {
            lv_timer_delete(s_boot_timer);
            s_boot_timer = NULL;
        }

        if (s_boot_done_cb != NULL) {
            s_boot_done_cb();
        }
    }
}

static void ui_config_button_event_cb(lv_event_t * e)
{
    LV_UNUSED(e);

    HMI_EncoderGroup_SetEditing(false);

    if (s_config_request_cb != NULL) {
        s_config_request_cb();
    }
}
