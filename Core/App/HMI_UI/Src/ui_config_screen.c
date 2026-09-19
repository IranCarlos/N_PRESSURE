/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-07-29
 * Modified: 2026-08-03
 */

/**
 * @file ui_config_screen.c
 * @brief Tela de configuracao flat para renderizacao estavel no ST7789.
 */

#include "ui_config_screen.h"

#include "hmi_encoder_group.h"
#include "control_loop.h"
#include "stm32h7xx_hal.h"

#define UI_FP_SCALE_100        (80)
#define UI_PUMP_PWM_STEP_PCT   (10U)
#define UI_PUMP_PWM_MAX_PCT    (100U)
#define UI_SETPOINT_MIN_MMHG   (0)

static SystemRuntime_t * s_runtime;
static SystemConfig_t * s_cfg;
static UI_ConfigScreen_BackRequestCb_t s_back_cb;

static lv_obj_t * s_config_screen;
static lv_obj_t * s_content;

static lv_obj_t * s_spin_setpoint;
static lv_obj_t * s_spin_kp;
static lv_obj_t * s_spin_ki;
static lv_obj_t * s_spin_kd;
static lv_obj_t * s_spin_ff;
static lv_obj_t * s_spin_out_min;
static lv_obj_t * s_spin_out_max;
static lv_obj_t * s_spin_i_min;
static lv_obj_t * s_spin_i_max;
static lv_obj_t * s_spin_offset;
static lv_obj_t * s_spin_scale;
static lv_obj_t * s_spin_ts_ms;
static lv_obj_t * s_spin_max_setpoint;
static lv_obj_t * s_spin_stab_band;
static lv_obj_t * s_spin_stab_time_ms;
static lv_obj_t * s_roller_unit;

static lv_obj_t * s_leak_remaining_label;
static lv_obj_t * s_leak_drop_label;
static lv_obj_t * s_leak_result_label;
static lv_obj_t * s_pump_pwm_label;

static lv_obj_t * s_btn_pump_dec;
static lv_obj_t * s_btn_pump_inc;
static lv_obj_t * s_btn_valve1;
static lv_obj_t * s_btn_valve2;
static lv_obj_t * s_btn_valve3;
static lv_obj_t * s_btn_leak_test;
static lv_obj_t * s_btn_back;

static uint8_t s_leak_active;
static uint32_t s_leak_start_tick;
static float s_leak_start_pressure;
static uint8_t s_pump_pwm_pct;
static uint8_t s_valve1_on;
static uint8_t s_valve2_on;
static uint8_t s_valve3_on; /**< Valvula de calibracao (conexao de manometro externo). */

static void ui_create_screen(void);
static lv_obj_t * ui_create_param_box(lv_obj_t * parent, int32_t x, int32_t y, const char * title);
static lv_obj_t * ui_create_spinbox(lv_obj_t * parent, int32_t min, int32_t max, int32_t step);
static void ui_refresh_from_cfg(void);
static void ui_apply_spinbox_to_cfg(lv_obj_t * spinbox, float * field);
static void ui_editable_focus_event_cb(lv_event_t * e);
static void ui_value_changed_cb(lv_event_t * e);
static void ui_unit_changed_cb(lv_event_t * e);
static void ui_pump_dec_btn_event_cb(lv_event_t * e);
static void ui_pump_inc_btn_event_cb(lv_event_t * e);
static void ui_valve1_btn_event_cb(lv_event_t * e);
static void ui_valve2_btn_event_cb(lv_event_t * e);
static void ui_valve3_btn_event_cb(lv_event_t * e);
static void ui_leak_test_btn_event_cb(lv_event_t * e);
static void ui_back_btn_event_cb(lv_event_t * e);
static void ui_apply_manual_actuators(void);
static void ui_refresh_actuator_widgets(void);
static void ui_update_leak_panel(void);
static uint8_t ui_is_spinbox_obj(lv_obj_t * obj);
static void ui_request_back(void);

void UI_ConfigScreen_Init(SystemRuntime_t * runtime,
                          SystemConfig_t * config,
                          UI_ConfigScreen_BackRequestCb_t back_cb)
{
    s_runtime = runtime;
    s_cfg = config;
    s_back_cb = back_cb;
}

void UI_ConfigScreen_Show(void)
{
    lv_obj_t * focusables[24];
    HMI_EncoderGroupConfig_t group_cfg;

    ui_create_screen();
    ui_refresh_from_cfg();
    s_pump_pwm_pct = 0U;
    s_valve1_on = 0U;
    s_valve2_on = 0U;
    s_valve3_on = 0U;
    ControlLoop_DisableManualActuators();
    ui_refresh_actuator_widgets();

    focusables[0] = s_spin_setpoint;
    focusables[1] = s_spin_kp;
    focusables[2] = s_spin_ki;
    focusables[3] = s_spin_kd;
    focusables[4] = s_spin_ff;
    focusables[5] = s_spin_out_min;
    focusables[6] = s_spin_out_max;
    focusables[7] = s_spin_i_min;
    focusables[8] = s_spin_i_max;
    focusables[9] = s_spin_offset;
    focusables[10] = s_spin_scale;
    focusables[11] = s_spin_ts_ms;
    focusables[12] = s_spin_max_setpoint;
    focusables[13] = s_spin_stab_band;
    focusables[14] = s_spin_stab_time_ms;
    focusables[15] = s_roller_unit;
    focusables[16] = s_btn_pump_dec;
    focusables[17] = s_btn_pump_inc;
    focusables[18] = s_btn_valve1;
    focusables[19] = s_btn_valve2;
    focusables[20] = s_btn_valve3;
    focusables[21] = s_btn_leak_test;
    focusables[22] = s_btn_back;

    group_cfg.focus_objs = focusables;
    group_cfg.count = 23U;
    group_cfg.initial_focus_idx = 0U;
    group_cfg.wrap = true;
    (void)HMI_EncoderGroup_Load(&group_cfg);

    lv_screen_load(s_config_screen);
    ui_update_leak_panel();
}

void UI_ConfigScreen_Update(void)
{
    if ((s_runtime == NULL) || (s_cfg == NULL) || (s_config_screen == NULL)) {
        return;
    }

    if (lv_screen_active() != s_config_screen) {
        return;
    }

    ui_update_leak_panel();
}

lv_obj_t * UI_ConfigScreen_GetHandle(void)
{
    return s_config_screen;
}

static void ui_create_screen(void)
{
    lv_obj_t * header;
    lv_obj_t * title;
    lv_obj_t * hint;
    lv_obj_t * box;
    lv_obj_t * lbl;
    lv_obj_t * pump_panel;
    lv_obj_t * valve_panel;
    lv_obj_t * leak_panel;

    if (s_config_screen != NULL) {
        lv_obj_del(s_config_screen);
        s_config_screen = NULL;
    }

    s_config_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_config_screen);
    lv_obj_set_size(s_config_screen, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(s_config_screen, lv_color_hex(0x0D1B24), 0);
    lv_obj_set_style_bg_opa(s_config_screen, LV_OPA_COVER, 0);

    header = lv_obj_create(s_config_screen);
    lv_obj_set_size(header, LV_HOR_RES - 12, 24);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_set_style_radius(header, 3, 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_border_color(header, lv_color_hex(0x3A5568), 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x152634), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);

    title = lv_label_create(header);
    lv_label_set_text(title, "Configuracao");
    lv_obj_set_style_text_color(title, lv_color_hex(0xECF5FB), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 6, 0);

    hint = lv_label_create(header);
    lv_label_set_text(hint, "Lista de parametros");
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA9C7D9), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_align(hint, LV_ALIGN_RIGHT_MID, -6, 0);

    s_content = lv_obj_create(s_config_screen);
    lv_obj_set_size(s_content, LV_HOR_RES - 12, LV_VER_RES - 76);
    lv_obj_align(s_content, LV_ALIGN_TOP_MID, 0, 32);
    lv_obj_set_style_radius(s_content, 3, 0);
    lv_obj_set_style_border_width(s_content, 1, 0);
    lv_obj_set_style_border_color(s_content, lv_color_hex(0x3A5568), 0);
    lv_obj_set_style_bg_color(s_content, lv_color_hex(0x10202C), 0);
    lv_obj_set_style_bg_opa(s_content, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(s_content, LV_SCROLLBAR_MODE_OFF);

    box = ui_create_param_box(s_content, 6, 6, "Setpoint");
    s_spin_setpoint = ui_create_spinbox(box, UI_SETPOINT_MIN_MMHG, (int32_t)s_cfg->max_setpoint_mmhg, 1);
    lv_spinbox_set_digit_format(s_spin_setpoint, 3, 0);

    box = ui_create_param_box(s_content, 6, 38, "KP");
    s_spin_kp = ui_create_spinbox(box, 0, 9999, 5);

    box = ui_create_param_box(s_content, 6, 70, "KI");
    s_spin_ki = ui_create_spinbox(box, 0, 5000, 5);

    box = ui_create_param_box(s_content, 6, 102, "KD");
    s_spin_kd = ui_create_spinbox(box, 0, 5000, 1);

    box = ui_create_param_box(s_content, 6, 134, "FF");
    s_spin_ff = ui_create_spinbox(box, 0, 9999, 5);

    box = ui_create_param_box(s_content, 6, 166, "Out min");
    s_spin_out_min = ui_create_spinbox(box, -5000, 5000, 5);

    box = ui_create_param_box(s_content, 6, 198, "Out max");
    s_spin_out_max = ui_create_spinbox(box, 1, 5000, 5);

    box = ui_create_param_box(s_content, 6, 230, "I min");
    s_spin_i_min = ui_create_spinbox(box, -5000, 5000, 5);

    box = ui_create_param_box(s_content, 6, 262, "I max");
    s_spin_i_max = ui_create_spinbox(box, -5000, 5000, 5);

    box = ui_create_param_box(s_content, 6, 294, "Offset");
    s_spin_offset = ui_create_spinbox(box, -5000, 5000, 1);

    box = ui_create_param_box(s_content, 6, 326, "Scale");
    s_spin_scale = ui_create_spinbox(box, 0, 5000, 1);

    box = ui_create_param_box(s_content, 6, 358, "Ts PID (ms)");
    s_spin_ts_ms = ui_create_spinbox(box, 1, 1000, 1);
    lv_spinbox_set_digit_format(s_spin_ts_ms, 4, 0);

    box = ui_create_param_box(s_content, 6, 390, "Max Setpoint");
    s_spin_max_setpoint = ui_create_spinbox(box, 10, 300, 1);
    lv_spinbox_set_digit_format(s_spin_max_setpoint, 3, 0);

    box = ui_create_param_box(s_content, 6, 422, "Banda Estab.");
    s_spin_stab_band = ui_create_spinbox(box, 0, 5000, 1);

    box = ui_create_param_box(s_content, 6, 454, "Tempo Estab.");
    s_spin_stab_time_ms = ui_create_spinbox(box, 100, 60000, 10);
    lv_spinbox_set_digit_format(s_spin_stab_time_ms, 5, 0);

    box = ui_create_param_box(s_content, 6, 486, "Unidade");
    s_roller_unit = lv_roller_create(box);
    lv_roller_set_options(s_roller_unit, "kPa\nhPa\nmmHg\npsi\ncmH2O\nbar", LV_ROLLER_MODE_NORMAL);
    lv_obj_set_size(s_roller_unit, 120, 22);
    lv_obj_align(s_roller_unit, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_roller_set_visible_row_count(s_roller_unit, 1);
    lv_obj_set_style_text_font(s_roller_unit, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(s_roller_unit, lv_color_hex(0x1F3442), 0);
    lv_obj_set_style_border_color(s_roller_unit, lv_color_hex(0x4E6B7F), 0);
    lv_obj_set_style_bg_color(s_roller_unit, lv_color_hex(0x2A5670), LV_PART_SELECTED);
    lv_obj_set_style_anim_duration(s_roller_unit, 0, 0);
    lv_obj_set_style_outline_width(s_roller_unit, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(s_roller_unit, lv_color_hex(0x8EE6FF), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(s_roller_unit, ui_editable_focus_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_roller_unit, ui_editable_focus_event_cb, LV_EVENT_DEFOCUSED, NULL);
    lv_obj_add_event_cb(s_roller_unit, ui_unit_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    pump_panel = lv_obj_create(s_content);
    lv_obj_set_size(pump_panel, LV_HOR_RES - 28, 30);
    lv_obj_set_pos(pump_panel, 6, 522);
    lv_obj_set_style_radius(pump_panel, 3, 0);
    lv_obj_set_style_border_width(pump_panel, 1, 0);
    lv_obj_set_style_border_color(pump_panel, lv_color_hex(0x3A5568), 0);
    lv_obj_set_style_bg_color(pump_panel, lv_color_hex(0x13222F), 0);
    lv_obj_set_style_bg_opa(pump_panel, LV_OPA_COVER, 0);

    s_pump_pwm_label = lv_label_create(pump_panel);
    lv_label_set_text(s_pump_pwm_label, "Bomba PWM: 0%");
    lv_obj_set_style_text_color(s_pump_pwm_label, lv_color_hex(0xCFE0EC), 0);
    lv_obj_set_style_text_font(s_pump_pwm_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_pump_pwm_label, LV_ALIGN_LEFT_MID, 6, 0);

    s_btn_pump_dec = lv_button_create(pump_panel);
    lv_obj_set_size(s_btn_pump_dec, 34, 20);
    lv_obj_align(s_btn_pump_dec, LV_ALIGN_RIGHT_MID, -42, 0);
    lv_obj_set_style_bg_color(s_btn_pump_dec, lv_color_hex(0x3A5568), 0);
    lv_obj_set_style_bg_color(s_btn_pump_dec, lv_color_hex(0x4B6A81), LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(s_btn_pump_dec, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(s_btn_pump_dec, lv_color_hex(0x8EE6FF), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(s_btn_pump_dec, ui_pump_dec_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lbl = lv_label_create(s_btn_pump_dec);
    lv_label_set_text(lbl, "-");
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xF4FBFF), 0);
    lv_obj_center(lbl);

    s_btn_pump_inc = lv_button_create(pump_panel);
    lv_obj_set_size(s_btn_pump_inc, 34, 20);
    lv_obj_align(s_btn_pump_inc, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_bg_color(s_btn_pump_inc, lv_color_hex(0x3A5568), 0);
    lv_obj_set_style_bg_color(s_btn_pump_inc, lv_color_hex(0x4B6A81), LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(s_btn_pump_inc, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(s_btn_pump_inc, lv_color_hex(0x8EE6FF), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(s_btn_pump_inc, ui_pump_inc_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lbl = lv_label_create(s_btn_pump_inc);
    lv_label_set_text(lbl, "+");
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xF4FBFF), 0);
    lv_obj_center(lbl);

    valve_panel = lv_obj_create(s_content);
    lv_obj_set_size(valve_panel, LV_HOR_RES - 28, 32);
    lv_obj_set_pos(valve_panel, 6, 556);
    lv_obj_set_style_radius(valve_panel, 3, 0);
    lv_obj_set_style_border_width(valve_panel, 1, 0);
    lv_obj_set_style_border_color(valve_panel, lv_color_hex(0x3A5568), 0);
    lv_obj_set_style_bg_color(valve_panel, lv_color_hex(0x13222F), 0);
    lv_obj_set_style_bg_opa(valve_panel, LV_OPA_COVER, 0);

    s_btn_valve1 = lv_button_create(valve_panel);
    lv_obj_set_size(s_btn_valve1, 84, 20);
    lv_obj_align(s_btn_valve1, LV_ALIGN_LEFT_MID, 6, 0);
    lv_obj_set_style_bg_color(s_btn_valve1, lv_color_hex(0x5A3A3A), 0);
    lv_obj_set_style_bg_color(s_btn_valve1, lv_color_hex(0x7A4C4C), LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(s_btn_valve1, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(s_btn_valve1, lv_color_hex(0x8EE6FF), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(s_btn_valve1, ui_valve1_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lbl = lv_label_create(s_btn_valve1);
    lv_label_set_text(lbl, "V1: OFF");
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xF4FBFF), 0);
    lv_obj_center(lbl);

    s_btn_valve2 = lv_button_create(valve_panel);
    lv_obj_set_size(s_btn_valve2, 84, 20);
    lv_obj_align(s_btn_valve2, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_btn_valve2, lv_color_hex(0x5A3A3A), 0);
    lv_obj_set_style_bg_color(s_btn_valve2, lv_color_hex(0x7A4C4C), LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(s_btn_valve2, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(s_btn_valve2, lv_color_hex(0x8EE6FF), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(s_btn_valve2, ui_valve2_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lbl = lv_label_create(s_btn_valve2);
    lv_label_set_text(lbl, "V2: OFF");
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xF4FBFF), 0);
    lv_obj_center(lbl);

    s_btn_valve3 = lv_button_create(valve_panel);
    lv_obj_set_size(s_btn_valve3, 84, 20);
    lv_obj_align(s_btn_valve3, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_set_style_bg_color(s_btn_valve3, lv_color_hex(0x5A3A3A), 0);
    lv_obj_set_style_bg_color(s_btn_valve3, lv_color_hex(0x7A4C4C), LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(s_btn_valve3, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(s_btn_valve3, lv_color_hex(0x8EE6FF), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(s_btn_valve3, ui_valve3_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lbl = lv_label_create(s_btn_valve3);
    lv_label_set_text(lbl, "V3: OFF");
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xF4FBFF), 0);
    lv_obj_center(lbl);

    leak_panel = lv_obj_create(s_content);
    lv_obj_set_size(leak_panel, LV_HOR_RES - 28, 38);
    lv_obj_set_pos(leak_panel, 6, 592);
    lv_obj_set_style_radius(leak_panel, 3, 0);
    lv_obj_set_style_border_width(leak_panel, 1, 0);
    lv_obj_set_style_border_color(leak_panel, lv_color_hex(0x3A5568), 0);
    lv_obj_set_style_bg_color(leak_panel, lv_color_hex(0x13222F), 0);
    lv_obj_set_style_bg_opa(leak_panel, LV_OPA_COVER, 0);

    s_leak_remaining_label = lv_label_create(leak_panel);
    lv_label_set_text(s_leak_remaining_label, "Tempo: -- s");
    lv_obj_set_style_text_color(s_leak_remaining_label, lv_color_hex(0xCFE0EC), 0);
    lv_obj_set_style_text_font(s_leak_remaining_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_leak_remaining_label, LV_ALIGN_TOP_LEFT, 6, 2);

    s_leak_drop_label = lv_label_create(leak_panel);
    lv_label_set_text(s_leak_drop_label, "Queda: -- mmHg");
    lv_obj_set_style_text_color(s_leak_drop_label, lv_color_hex(0xCFE0EC), 0);
    lv_obj_set_style_text_font(s_leak_drop_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_leak_drop_label, LV_ALIGN_TOP_LEFT, 6, 18);

    s_leak_result_label = lv_label_create(leak_panel);
    lv_label_set_text(s_leak_result_label, "Resultado: aguardando");
    lv_obj_set_style_text_color(s_leak_result_label, lv_color_hex(0xF5D7A3), 0);
    lv_obj_set_style_text_font(s_leak_result_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_leak_result_label, LV_ALIGN_TOP_RIGHT, -6, 10);

    s_btn_leak_test = lv_button_create(s_config_screen);
    lv_obj_set_size(s_btn_leak_test, 152, 28);
    lv_obj_align(s_btn_leak_test, LV_ALIGN_BOTTOM_LEFT, 6, -6);
    lv_obj_set_style_radius(s_btn_leak_test, 3, 0);
    lv_obj_set_style_bg_color(s_btn_leak_test, lv_color_hex(0x245D7A), 0);
    lv_obj_set_style_bg_color(s_btn_leak_test, lv_color_hex(0x2E7598), LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(s_btn_leak_test, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(s_btn_leak_test, lv_color_hex(0x8EE6FF), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(s_btn_leak_test, ui_leak_test_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lbl = lv_label_create(s_btn_leak_test);
    lv_label_set_text(lbl, "Iniciar Teste");
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xF4FBFF), 0);
    lv_obj_center(lbl);

    s_btn_back = lv_button_create(s_config_screen);
    lv_obj_set_size(s_btn_back, 152, 28);
    lv_obj_align(s_btn_back, LV_ALIGN_BOTTOM_RIGHT, -6, -6);
    lv_obj_set_style_radius(s_btn_back, 3, 0);
    lv_obj_set_style_bg_color(s_btn_back, lv_color_hex(0x3A5568), 0);
    lv_obj_set_style_bg_color(s_btn_back, lv_color_hex(0x4B6A81), LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(s_btn_back, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(s_btn_back, lv_color_hex(0x8EE6FF), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(s_btn_back, ui_back_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lbl = lv_label_create(s_btn_back);
    lv_label_set_text(lbl, LV_SYMBOL_LEFT " Salvar e Voltar");
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xF4FBFF), 0);
    lv_obj_center(lbl);

    lv_obj_set_height(s_content, LV_VER_RES - 70);
    lv_obj_update_layout(s_content);
    lv_obj_set_scroll_dir(s_content, LV_DIR_VER);
}

static lv_obj_t * ui_create_param_box(lv_obj_t * parent, int32_t x, int32_t y, const char * title)
{
    lv_obj_t * box = lv_obj_create(parent);
    lv_obj_t * lbl = lv_label_create(box);

    lv_obj_set_size(box, LV_HOR_RES - 28, 28);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_style_radius(box, 3, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(0x3A5568), 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x13222F), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);

    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xB5D1E0), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 5, 0);

    return box;
}

static lv_obj_t * ui_create_spinbox(lv_obj_t * parent, int32_t min, int32_t max, int32_t step)
{
    lv_obj_t * spin = lv_spinbox_create(parent);

    lv_obj_set_size(spin, 88, 24);
    lv_obj_align(spin, LV_ALIGN_RIGHT_MID, -2, 0);
    lv_spinbox_set_range(spin, min, max);
    lv_spinbox_set_digit_format(spin, 4, 2);
    lv_spinbox_set_step(spin, step);
    lv_spinbox_set_value(spin, 0);
    lv_obj_set_style_text_font(spin, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(spin, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_pad_left(spin, 2, 0);
    lv_obj_set_style_pad_right(spin, 2, 0);
    lv_obj_set_style_bg_color(spin, lv_color_hex(0x1F3442), 0);
    lv_obj_set_style_border_color(spin, lv_color_hex(0x4E6B7F), 0);
    lv_obj_set_style_anim_duration(spin, 0, 0);
    lv_obj_set_style_outline_width(spin, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(spin, lv_color_hex(0x8EE6FF), LV_STATE_FOCUSED);

    lv_obj_add_event_cb(spin, ui_editable_focus_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(spin, ui_editable_focus_event_cb, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_event_cb(spin, ui_editable_focus_event_cb, LV_EVENT_DEFOCUSED, NULL);
    lv_obj_add_event_cb(spin, ui_value_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    return spin;
}

static void ui_refresh_from_cfg(void)
{
    if (s_cfg == NULL) {
        return;
    }

    lv_spinbox_set_value(s_spin_setpoint, (int32_t)s_cfg->current_setpoint_mmhg);
    lv_spinbox_set_value(s_spin_kp, (int32_t)(s_cfg->pid.kp * UI_FP_SCALE_100));
    lv_spinbox_set_value(s_spin_ki, (int32_t)(s_cfg->pid.ki * UI_FP_SCALE_100));
    lv_spinbox_set_value(s_spin_kd, (int32_t)(s_cfg->pid.kd * UI_FP_SCALE_100));
    lv_spinbox_set_value(s_spin_ff, (int32_t)(s_cfg->pid.ff * UI_FP_SCALE_100));
    lv_spinbox_set_value(s_spin_out_min, (int32_t)(s_cfg->pid.out_min * UI_FP_SCALE_100));
    lv_spinbox_set_value(s_spin_out_max, (int32_t)(s_cfg->pid.out_max * UI_FP_SCALE_100));
    lv_spinbox_set_value(s_spin_i_min, (int32_t)(s_cfg->pid.i_min * UI_FP_SCALE_100));
    lv_spinbox_set_value(s_spin_i_max, (int32_t)(s_cfg->pid.i_max * UI_FP_SCALE_100));
    lv_spinbox_set_value(s_spin_offset, (int32_t)(s_cfg->sensor_cal.offset_kpa * UI_FP_SCALE_100));
    lv_spinbox_set_value(s_spin_scale, (int32_t)(s_cfg->sensor_cal.scale * UI_FP_SCALE_100));
    lv_spinbox_set_value(s_spin_ts_ms, (int32_t)s_cfg->pid.sample_time_ms);
    lv_spinbox_set_value(s_spin_max_setpoint, (int32_t)s_cfg->max_setpoint_mmhg);
    lv_spinbox_set_value(s_spin_stab_band, (int32_t)(s_cfg->stabilization.band_mmhg * UI_FP_SCALE_100));
    lv_spinbox_set_value(s_spin_stab_time_ms, (int32_t)s_cfg->stabilization.time_ms);
    lv_roller_set_selected(s_roller_unit, (uint16_t)s_cfg->unit, LV_ANIM_OFF);
}

static void ui_apply_spinbox_to_cfg(lv_obj_t * spinbox, float * field)
{
    int32_t value;

    if ((spinbox == NULL) || (field == NULL)) {
        return;
    }

    value = lv_spinbox_get_value(spinbox);
    *field = (float)value / (float)UI_FP_SCALE_100;
}

static void ui_editable_focus_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * target = lv_event_get_target(e);
    lv_group_t * group = HMI_EncoderGroup_Get();

    if (group == NULL) {
        return;
    }

    if (code == LV_EVENT_CLICKED) {
        if (lv_group_get_editing(group) == false) {
            HMI_EncoderGroup_SetEditing(true);
            lv_obj_set_scroll_dir(s_content, LV_DIR_NONE);
        } else {
            if (ui_is_spinbox_obj(target) != 0U) {
                lv_spinbox_step_next(target);
            }
        }
    } else if (code == LV_EVENT_LONG_PRESSED) {
        HMI_EncoderGroup_SetEditing(false);
        lv_obj_set_scroll_dir(s_content, LV_DIR_VER);
    } else if (code == LV_EVENT_DEFOCUSED) {
        HMI_EncoderGroup_SetEditing(false);
        lv_obj_set_scroll_dir(s_content, LV_DIR_VER);
    }
}

static void ui_value_changed_cb(lv_event_t * e)
{
    lv_obj_t * target = lv_event_get_target(e);

    // Ignora eventos de mudança de valor enquanto a tela não estiver ativa
    if (lv_screen_active() != s_config_screen) {
        return;
    }

    if (s_cfg == NULL) {
        return;
    }

    if (target == s_spin_kp) {
        ui_apply_spinbox_to_cfg(s_spin_kp, &s_cfg->pid.kp);
    } else if (target == s_spin_setpoint) {
        float setpoint = (float)lv_spinbox_get_value(s_spin_setpoint);
        if (setpoint < 0.0f) {
            setpoint = 0.0f;
        }
        if (setpoint > s_cfg->max_setpoint_mmhg) {
            setpoint = s_cfg->max_setpoint_mmhg;
        }
        s_cfg->current_setpoint_mmhg = setpoint;
        ControlLoop_SetSetpointMmHg(setpoint);
    } else if (target == s_spin_ki) {
        ui_apply_spinbox_to_cfg(s_spin_ki, &s_cfg->pid.ki);
    } else if (target == s_spin_kd) {
        ui_apply_spinbox_to_cfg(s_spin_kd, &s_cfg->pid.kd);
    } else if (target == s_spin_ff) {
        ui_apply_spinbox_to_cfg(s_spin_ff, &s_cfg->pid.ff);
    } else if (target == s_spin_out_min) {
        ui_apply_spinbox_to_cfg(s_spin_out_min, &s_cfg->pid.out_min);
    } else if (target == s_spin_out_max) {
        ui_apply_spinbox_to_cfg(s_spin_out_max, &s_cfg->pid.out_max);
    } else if (target == s_spin_i_min) {
        ui_apply_spinbox_to_cfg(s_spin_i_min, &s_cfg->pid.i_min);
    } else if (target == s_spin_i_max) {
        ui_apply_spinbox_to_cfg(s_spin_i_max, &s_cfg->pid.i_max);
    } else if (target == s_spin_offset) {
        ui_apply_spinbox_to_cfg(s_spin_offset, &s_cfg->sensor_cal.offset_kpa);
    } else if (target == s_spin_scale) {
        ui_apply_spinbox_to_cfg(s_spin_scale, &s_cfg->sensor_cal.scale);
    } else if (target == s_spin_ts_ms) {
        int32_t ts_ms = lv_spinbox_get_value(s_spin_ts_ms);
        if (ts_ms < 1) {
            ts_ms = 1;
        }
        if (ts_ms > 1000) {
            ts_ms = 1000;
        }
        s_cfg->pid.sample_time_ms = (uint16_t)ts_ms;
    } else if (target == s_spin_max_setpoint) {
        int32_t max_sp = lv_spinbox_get_value(s_spin_max_setpoint);
        if (max_sp < 10) {
            max_sp = 10;
        }
        if (max_sp > 300) {
            max_sp = 300;
        }
        s_cfg->max_setpoint_mmhg = (float)max_sp;
        lv_spinbox_set_range(s_spin_setpoint, UI_SETPOINT_MIN_MMHG, max_sp);
        if (s_cfg->current_setpoint_mmhg > s_cfg->max_setpoint_mmhg) {
            s_cfg->current_setpoint_mmhg = s_cfg->max_setpoint_mmhg;
            lv_spinbox_set_value(s_spin_setpoint, (int32_t)s_cfg->current_setpoint_mmhg);
            ControlLoop_SetSetpointMmHg(s_cfg->current_setpoint_mmhg);
        }
    } else if (target == s_spin_stab_band) {
        ui_apply_spinbox_to_cfg(s_spin_stab_band, &s_cfg->stabilization.band_mmhg);
    } else if (target == s_spin_stab_time_ms) {
        int32_t stab_ms = lv_spinbox_get_value(s_spin_stab_time_ms);
        if (stab_ms < 100) {
            stab_ms = 100;
        }
        if (stab_ms > 60000) {
            stab_ms = 60000;
        }
        s_cfg->stabilization.time_ms = (uint32_t)stab_ms;
    }
}

static void ui_unit_changed_cb(lv_event_t * e)
{
    uint16_t selected;

    LV_UNUSED(e);

    if (s_cfg == NULL) {
        return;
    }

    selected = lv_roller_get_selected(s_roller_unit);
    if (selected >= (uint16_t)SYS_PRESSURE_UNIT_COUNT) {
        selected = (uint16_t)SYS_PRESSURE_UNIT_MMHG;
    }

    s_cfg->unit = (PressureUnit_t)selected;
}

static void ui_apply_manual_actuators(void)
{
    ControlLoop_SetManualActuators((float)s_pump_pwm_pct / 100.0f, s_valve1_on, s_valve2_on, s_valve3_on);
}

static void ui_refresh_actuator_widgets(void)
{
    lv_obj_t * lbl_v1;
    lv_obj_t * lbl_v2;
    lv_obj_t * lbl_v3;

    lv_label_set_text_fmt(s_pump_pwm_label, "Bomba PWM: %u%%", (unsigned int)s_pump_pwm_pct);

    lbl_v1 = lv_obj_get_child(s_btn_valve1, 0);
    lbl_v2 = lv_obj_get_child(s_btn_valve2, 0);
    lbl_v3 = lv_obj_get_child(s_btn_valve3, 0);

    if (s_valve1_on != 0U) {
        lv_label_set_text(lbl_v1, "V1: ON");
        lv_obj_set_style_bg_color(s_btn_valve1, lv_color_hex(0x2E7D57), 0);
        lv_obj_set_style_bg_color(s_btn_valve1, lv_color_hex(0x3D9A6D), LV_STATE_PRESSED);
    } else {
        lv_label_set_text(lbl_v1, "V1: OFF");
        lv_obj_set_style_bg_color(s_btn_valve1, lv_color_hex(0x5A3A3A), 0);
        lv_obj_set_style_bg_color(s_btn_valve1, lv_color_hex(0x7A4C4C), LV_STATE_PRESSED);
    }

    if (s_valve2_on != 0U) {
        lv_label_set_text(lbl_v2, "V2: ON");
        lv_obj_set_style_bg_color(s_btn_valve2, lv_color_hex(0x2E7D57), 0);
        lv_obj_set_style_bg_color(s_btn_valve2, lv_color_hex(0x3D9A6D), LV_STATE_PRESSED);
    } else {
        lv_label_set_text(lbl_v2, "V2: OFF");
        lv_obj_set_style_bg_color(s_btn_valve2, lv_color_hex(0x5A3A3A), 0);
        lv_obj_set_style_bg_color(s_btn_valve2, lv_color_hex(0x7A4C4C), LV_STATE_PRESSED);
    }

    if (s_valve3_on != 0U) {
        lv_label_set_text(lbl_v3, "V3: ON");
        lv_obj_set_style_bg_color(s_btn_valve3, lv_color_hex(0x2E7D57), 0);
        lv_obj_set_style_bg_color(s_btn_valve3, lv_color_hex(0x3D9A6D), LV_STATE_PRESSED);
    } else {
        lv_label_set_text(lbl_v3, "V3: OFF");
        lv_obj_set_style_bg_color(s_btn_valve3, lv_color_hex(0x5A3A3A), 0);
        lv_obj_set_style_bg_color(s_btn_valve3, lv_color_hex(0x7A4C4C), LV_STATE_PRESSED);
    }
}

static void ui_pump_dec_btn_event_cb(lv_event_t * e)
{
    LV_UNUSED(e);

    if (s_pump_pwm_pct >= UI_PUMP_PWM_STEP_PCT) {
        s_pump_pwm_pct = (uint8_t)(s_pump_pwm_pct - UI_PUMP_PWM_STEP_PCT);
    } else {
        s_pump_pwm_pct = 0U;
    }

    ui_apply_manual_actuators();
    ui_refresh_actuator_widgets();
}

static void ui_pump_inc_btn_event_cb(lv_event_t * e)
{
    uint32_t next;

    LV_UNUSED(e);

    next = (uint32_t)s_pump_pwm_pct + UI_PUMP_PWM_STEP_PCT;
    if (next > UI_PUMP_PWM_MAX_PCT) {
        next = UI_PUMP_PWM_MAX_PCT;
    }
    s_pump_pwm_pct = (uint8_t)next;

    ui_apply_manual_actuators();
    ui_refresh_actuator_widgets();
}

static void ui_valve1_btn_event_cb(lv_event_t * e)
{
    LV_UNUSED(e);

    s_valve1_on = (s_valve1_on == 0U) ? 1U : 0U;
    ui_apply_manual_actuators();
    ui_refresh_actuator_widgets();
}

static void ui_valve2_btn_event_cb(lv_event_t * e)
{
    LV_UNUSED(e);

    s_valve2_on = (s_valve2_on == 0U) ? 1U : 0U;
    ui_apply_manual_actuators();
    ui_refresh_actuator_widgets();
}

static void ui_valve3_btn_event_cb(lv_event_t * e)
{
    LV_UNUSED(e);

    s_valve3_on = (s_valve3_on == 0U) ? 1U : 0U;
    ui_apply_manual_actuators();
    ui_refresh_actuator_widgets();
}

static void ui_leak_test_btn_event_cb(lv_event_t * e)
{
    LV_UNUSED(e);

    if ((s_runtime == NULL) || (s_cfg == NULL)) {
        return;
    }

    HMI_EncoderGroup_SetEditing(false);

    s_pump_pwm_pct = 0U;
    s_valve1_on = 0U;
    s_valve2_on = 0U;
    s_valve3_on = 0U;
    ControlLoop_DisableManualActuators();
    ui_refresh_actuator_widgets();

    s_leak_active = 1U;
    s_leak_start_tick = HAL_GetTick();
    s_leak_start_pressure = s_runtime->pressure_display;

    lv_label_set_text(s_leak_result_label, "Resultado: em execucao");
    lv_obj_set_style_text_color(s_leak_result_label, lv_color_hex(0xF5D7A3), 0);

    ControlLoop_StartLeakTest();
}

static void ui_back_btn_event_cb(lv_event_t * e)
{
    LV_UNUSED(e);

    ui_request_back();
}

static void ui_update_leak_panel(void)
{
    uint32_t now;
    uint32_t elapsed;
    uint32_t remaining;
    float drop;

    if ((s_runtime == NULL) || (s_cfg == NULL)) {
        return;
    }

    if ((s_runtime->state == SYSTEM_STATE_CONFIG) && (s_runtime->bt_user_pressed != 0U)) {
        s_runtime->bt_user_pressed = 0U;
        ui_request_back();
        return;
    }

    now = HAL_GetTick();

    if (s_leak_active != 0U) {
        elapsed = now - s_leak_start_tick;
        if (elapsed >= s_cfg->leak_test.hold_time_ms) {
            remaining = 0U;
        } else {
            remaining = s_cfg->leak_test.hold_time_ms - elapsed;
        }

        if (s_runtime->pressure_display > s_leak_start_pressure) {
            s_leak_start_pressure = s_runtime->pressure_display;
        }

        drop = s_leak_start_pressure - s_runtime->pressure_display;
        if (drop < 0.0f) {
            drop = 0.0f;
        }

        lv_label_set_text_fmt(s_leak_remaining_label, "Tempo: %lu s", (unsigned long)(remaining / 1000UL));
        lv_label_set_text_fmt(s_leak_drop_label, "Queda: %.1f mmHg", (double)drop);

        if (s_runtime->state != SYSTEM_STATE_LEAK_TEST) {
            s_leak_active = 0U;

            if (drop <= s_cfg->leak_test.max_drop_mmhg) {
                lv_label_set_text(s_leak_result_label, "Resultado: APROVADO");
                lv_obj_set_style_text_color(s_leak_result_label, lv_color_hex(0x9EE7B3), 0);
            } else {
                lv_label_set_text(s_leak_result_label, "Resultado: REPROVADO");
                lv_obj_set_style_text_color(s_leak_result_label, lv_color_hex(0xFFA7A7), 0);
            }
        }
    } else {
        lv_label_set_text_fmt(s_leak_remaining_label, "Tempo: %lu s", (unsigned long)(s_cfg->leak_test.hold_time_ms / 1000UL));
    }
}

static uint8_t ui_is_spinbox_obj(lv_obj_t * obj)
{
    return (obj == s_spin_setpoint) ||
           (obj == s_spin_kp) ||
           (obj == s_spin_ki) ||
           (obj == s_spin_kd) ||
           (obj == s_spin_ff) ||
           (obj == s_spin_out_min) ||
           (obj == s_spin_out_max) ||
           (obj == s_spin_i_min) ||
           (obj == s_spin_i_max) ||
           (obj == s_spin_offset) ||
           (obj == s_spin_scale) ||
           (obj == s_spin_ts_ms) ||
           (obj == s_spin_max_setpoint) ||
           (obj == s_spin_stab_band) ||
           (obj == s_spin_stab_time_ms);
}

static void ui_request_back(void)
{
    HMI_EncoderGroup_SetEditing(false);
    lv_obj_set_scroll_dir(s_content, LV_DIR_VER);

    s_pump_pwm_pct = 0U;
    s_valve1_on = 0U;
    s_valve2_on = 0U;
    s_valve3_on = 0U;
    ControlLoop_DisableManualActuators();

    if (s_back_cb != NULL) {
        s_back_cb();
    }
}
