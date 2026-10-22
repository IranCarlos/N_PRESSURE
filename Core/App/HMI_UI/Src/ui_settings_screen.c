/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-07-29
 * Modified: 2026-10-17
 */

/*
 * Layout 320x240:
 *   y=4    [ Ajustes                    Role para ver mais ]  cabecalho fixo 308x24
 *   y=32   +-- area rolavel 308x170 ---------------------------+
 *          | y=6   [ Setpoint                      [ 160 ] ]   |
 *          | y=40  [ Unidade                    [ mmHg  ] ]    |
 *          | y=74  [ Buzzer          [Testar] [ Ligado ]  ]    |
 *          | y=108 [ Calibracao (manometro na V3)         ]    |
 *          |       [ Leitura bruta / Zerar / Pressao de teste ] |
 *          |       [ P1 [spin][Capturar] OK / P2 ... / Salvar ] |
 *          | y=326 [ Teste de vazamento: tempo, queda, result ] |
 *          | y=408 [ Data/Hora DD MM AAAA HH MM  [Aplicar]    ] |
 *          | y=500 [ Operador ativo (OP)            [ 03 ]    ] |
 *          | y=536 [ Informacoes (so' leitura)                ] |
 *          +---------------------------------------------------+
 *   y=206  [ Teste de Vazamento 152x28 ]  [ < Voltar 152x28 ]  rodape fixo
 *
 * Unica excecao deliberada a "sem atuador manual": a calibracao abre V3 e
 * aplica uma pressao de teste (fluxo guiado, restrito a essa finalidade).
 */

#include "ui_settings_screen.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "ui_session.h"
#include "ui_backend.h"
#include "hmi_encoder_group.h"

#define UI_CAL_PUMP_STEP_PCT (10)
#define UI_ROW_H             (30)

static UI_SettingsScreen_BackRequestCb_t s_back_cb;

static lv_obj_t * s_screen;
static lv_obj_t * s_content;

static lv_obj_t * s_spin_setpoint;
static lv_obj_t * s_roller_unit;
static lv_obj_t * s_btn_buzzer_test;
static lv_obj_t * s_btn_buzzer_toggle;
static lv_obj_t * s_lbl_buzzer_toggle;

static lv_obj_t * s_lbl_cal_reading;
static lv_obj_t * s_btn_cal_zero;
static lv_obj_t * s_spin_cal_pump_pct;
static lv_obj_t * s_spin_cal_ref1;
static lv_obj_t * s_btn_cal_capture1;
static lv_obj_t * s_lbl_cal_point1;
static lv_obj_t * s_spin_cal_ref2;
static lv_obj_t * s_btn_cal_capture2;
static lv_obj_t * s_lbl_cal_point2;
static lv_obj_t * s_btn_cal_save;
static lv_obj_t * s_lbl_cal_result;

static lv_obj_t * s_leak_box;
static lv_obj_t * s_leak_remaining_label;
static lv_obj_t * s_leak_drop_label;
static lv_obj_t * s_leak_result_label;

static lv_obj_t * s_spin_day;
static lv_obj_t * s_spin_month;
static lv_obj_t * s_spin_year;
static lv_obj_t * s_spin_hour;
static lv_obj_t * s_spin_minute;
static lv_obj_t * s_btn_apply_datetime;
static lv_obj_t * s_lbl_datetime_result;

static lv_obj_t * s_spin_operator;

static lv_obj_t * s_info_sd_label;
static lv_obj_t * s_info_cycle_label;

static lv_obj_t * s_btn_leak_test;
static lv_obj_t * s_btn_back;

static bool s_leak_active;
static uint32_t s_leak_start_tick;
static float s_leak_start_pressure;

static int32_t s_shown_cal_reading;
static int32_t s_shown_leak_remaining_s;
static int32_t s_shown_leak_drop_tenths;
static int32_t s_shown_sd;
static int32_t s_shown_cycle;

static void ui_create_screen(void);
static lv_obj_t * ui_row_box(int32_t y, const char * title);
static void ui_create_calibration(int32_t y);
static void ui_create_leak_panel(int32_t y);
static void ui_create_datetime(int32_t y);
static void ui_create_info(int32_t y);
static void ui_refresh_datetime_fields(void);
static void ui_set_buzzer_label(void);
static void ui_update_leak_panel(void);
static void ui_update_info_panel(void);
static void ui_update_cal_reading(void);
static void ui_request_back(void);

static void ui_setpoint_changed_cb(lv_event_t * e);
static void ui_unit_changed_cb(lv_event_t * e);
static void ui_buzzer_toggle_event_cb(lv_event_t * e);
static void ui_buzzer_test_event_cb(lv_event_t * e);
static void ui_cal_zero_event_cb(lv_event_t * e);
static void ui_cal_pump_changed_cb(lv_event_t * e);
static void ui_cal_capture1_event_cb(lv_event_t * e);
static void ui_cal_capture2_event_cb(lv_event_t * e);
static void ui_cal_save_event_cb(lv_event_t * e);
static void ui_leak_test_btn_event_cb(lv_event_t * e);
static void ui_leak_btn_focused_cb(lv_event_t * e);
static void ui_apply_datetime_btn_event_cb(lv_event_t * e);
static void ui_operator_changed_cb(lv_event_t * e);
static void ui_back_btn_event_cb(lv_event_t * e);

void UI_SettingsScreen_Init(UI_SettingsScreen_BackRequestCb_t back_cb)
{
    s_back_cb = back_cb;
}

void UI_SettingsScreen_Show(void)
{
    static lv_obj_t * focusables[20];
    HMI_EncoderGroupConfig_t group_cfg;
    uint16_t i = 0U;

    /* Calibracao sempre comeca com a bomba desligada e V3 fechada. */
    UiBackend_CalAbort();
    s_leak_active = false;

    ui_create_screen();
    ui_refresh_datetime_fields();

    /* Ordem = ordem visual. Ao adicionar/remover um item aqui, ajuste o
     * tamanho de focusables[] — o LV_ASSERT abaixo acusa a divergencia
     * (um contador desatualizado ja' estourou a pilha da uiTask uma vez). */
    focusables[i++] = s_spin_setpoint;
    focusables[i++] = s_roller_unit;
    focusables[i++] = s_btn_buzzer_test;
    focusables[i++] = s_btn_buzzer_toggle;
    focusables[i++] = s_btn_cal_zero;
    focusables[i++] = s_spin_cal_pump_pct;
    focusables[i++] = s_spin_cal_ref1;
    focusables[i++] = s_btn_cal_capture1;
    focusables[i++] = s_spin_cal_ref2;
    focusables[i++] = s_btn_cal_capture2;
    focusables[i++] = s_btn_cal_save;
    focusables[i++] = s_spin_day;
    focusables[i++] = s_spin_month;
    focusables[i++] = s_spin_year;
    focusables[i++] = s_spin_hour;
    focusables[i++] = s_spin_minute;
    focusables[i++] = s_btn_apply_datetime;
    focusables[i++] = s_spin_operator;
    focusables[i++] = s_btn_leak_test;
    focusables[i++] = s_btn_back;
    LV_ASSERT(i == HMI_ARRAY_COUNT(focusables));

    group_cfg.focus_objs = focusables;
    group_cfg.count = i;
    group_cfg.initial_focus_idx = 0U;
    group_cfg.wrap = true;
    (void)HMI_EncoderGroup_Load(&group_cfg);

    UiW_LoadScreen(s_screen, &s_screen);

    s_shown_cal_reading = INT32_MIN;
    s_shown_leak_remaining_s = INT32_MIN;
    s_shown_leak_drop_tenths = INT32_MIN;
    s_shown_sd = INT32_MIN;
    s_shown_cycle = INT32_MIN;
    UI_SettingsScreen_Update();
}

void UI_SettingsScreen_Update(void)
{
    if ((s_screen == NULL) || (lv_screen_active() != s_screen)) {
        return;
    }

    /* Botao fisico/pedal em CONFIG = sair dos ajustes (fora de CONFIG o
     * evento pertence ao Control_Loop, ex.: abortar o teste de vazamento). */
    if ((UiBackend_GetState() == UI_STATE_CONFIG) && UiBackend_ConsumeUserButton()) {
        ui_request_back();
        return;
    }

    ui_update_leak_panel();
    ui_update_info_panel();
    ui_update_cal_reading();
}

/* ------------------------------------------------------------------------ */
/* Construcao                                                               */
/* ------------------------------------------------------------------------ */

static void ui_create_screen(void)
{
    lv_obj_t * header;
    lv_obj_t * lbl;
    lv_obj_t * box;

    s_screen = lv_obj_create(NULL);
    UiTheme_StyleScreen(s_screen);

    header = lv_obj_create(s_screen);
    lv_obj_set_size(header, LV_HOR_RES - 12, 24);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 4);
    UiTheme_StyleBox(header);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lbl = UiW_LabelBody(header, "Ajustes");
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 6, 0);

    lbl = UiW_LabelHint(header, "Role para ver mais");
    lv_obj_align(lbl, LV_ALIGN_RIGHT_MID, -6, 0);

    s_content = lv_obj_create(s_screen);
    lv_obj_set_size(s_content, LV_HOR_RES - 12, LV_VER_RES - 70);
    lv_obj_align(s_content, LV_ALIGN_TOP_MID, 0, 32);
    UiTheme_StyleBox(s_content);
    lv_obj_set_style_pad_bottom(s_content, 6, 0);
    lv_obj_set_scrollbar_mode(s_content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(s_content, LV_DIR_VER);

    /* --- Setpoint --- */
    box = ui_row_box(6, "Setpoint (mmHg)");
    s_spin_setpoint = UiW_Spinbox(box, 88, 24, 0, (int32_t)UiBackend_GetMaxSetpointMmHg(),
                                  3u, 1, s_content);
    lv_obj_align(s_spin_setpoint, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_spinbox_set_value(s_spin_setpoint, (int32_t)UiBackend_GetSetpointMmHg());
    lv_obj_add_event_cb(s_spin_setpoint, ui_setpoint_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* --- Unidade --- */
    box = ui_row_box(40, "Unidade");
    s_roller_unit = UiW_Roller(box, 120, 24, "kPa\nhPa\nmmHg\npsi\ncmH2O\nbar", s_content);
    lv_obj_align(s_roller_unit, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_roller_set_selected(s_roller_unit, (uint32_t)UiBackend_GetUnit(), LV_ANIM_OFF);
    lv_obj_add_event_cb(s_roller_unit, ui_unit_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* --- Buzzer --- */
    box = ui_row_box(74, "Buzzer");
    s_btn_buzzer_test = UiW_Button(box, 64, 22, "Testar", ui_buzzer_test_event_cb);
    lv_obj_align(s_btn_buzzer_test, LV_ALIGN_RIGHT_MID, -96, 0);

    s_btn_buzzer_toggle = UiW_Button(box, 84, 22, "", ui_buzzer_toggle_event_cb);
    lv_obj_align(s_btn_buzzer_toggle, LV_ALIGN_RIGHT_MID, -6, 0);
    s_lbl_buzzer_toggle = lv_obj_get_child(s_btn_buzzer_toggle, 0);
    ui_set_buzzer_label();

    ui_create_calibration(108);
    ui_create_leak_panel(326);
    ui_create_datetime(408);

    /* --- Operador --- */
    box = ui_row_box(500, "Operador ativo (OP)");
    s_spin_operator = UiW_Spinbox(box, 70, 24, 0, 99, 2u, 1, s_content);
    lv_obj_align(s_spin_operator, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_spinbox_set_value(s_spin_operator, (int32_t)UiSession_Get()->operator_id);
    lv_obj_add_event_cb(s_spin_operator, ui_operator_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    ui_create_info(536);

    /* --- Rodape fixo --- */
    s_btn_leak_test = UiW_Button(s_screen, 152, 28, "Teste de Vazamento", ui_leak_test_btn_event_cb);
    lv_obj_align(s_btn_leak_test, LV_ALIGN_BOTTOM_LEFT, 6, -6);
    /* Ao chegar no rodape, mostra o fim da lista (Informacoes nao tem item
     * focavel, entao o scroll-on-focus nunca a traria para a vista). */
    lv_obj_add_event_cb(s_btn_leak_test, ui_leak_btn_focused_cb, LV_EVENT_FOCUSED, NULL);

    s_btn_back = UiW_Button(s_screen, 152, 28, LV_SYMBOL_LEFT " Voltar", ui_back_btn_event_cb);
    lv_obj_align(s_btn_back, LV_ALIGN_BOTTOM_RIGHT, -6, -6);
}

static lv_obj_t * ui_row_box(int32_t y, const char * title)
{
    lv_obj_t * box = lv_obj_create(s_content);
    lv_obj_t * lbl = UiW_LabelBody(box, title);

    lv_obj_set_size(box, LV_HOR_RES - 26, UI_ROW_H);
    lv_obj_set_pos(box, 6, y);
    UiTheme_StyleBox(box);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 6, 0);

    return box;
}

static void ui_create_calibration(int32_t y)
{
    lv_obj_t * box = UiW_TitledBox(s_content, 6, y, LV_HOR_RES - 26, 212,
                                   "Calibracao (manometro na V3)");
    lv_obj_t * lbl;

    s_lbl_cal_reading = UiW_LabelBody(box, "Leitura bruta: -- mmHg");
    lv_obj_set_pos(s_lbl_cal_reading, 6, 22);

    s_btn_cal_zero = UiW_Button(box, 110, 24, "Zerar (ar)", ui_cal_zero_event_cb);
    lv_obj_set_pos(s_btn_cal_zero, 6, 44);

    lbl = UiW_LabelHint(box, "Pressao de teste (%)");
    lv_obj_set_pos(lbl, 6, 78);
    s_spin_cal_pump_pct = UiW_Spinbox(box, 70, 24, 0, 100, 3u, UI_CAL_PUMP_STEP_PCT, s_content);
    lv_obj_align(s_spin_cal_pump_pct, LV_ALIGN_TOP_RIGHT, -6, 74);
    lv_obj_add_event_cb(s_spin_cal_pump_pct, ui_cal_pump_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lbl = UiW_LabelHint(box, "P1");
    lv_obj_set_pos(lbl, 6, 108);
    s_spin_cal_ref1 = UiW_Spinbox(box, 60, 24, 0, 300, 3u, 1, s_content);
    lv_obj_set_pos(s_spin_cal_ref1, 28, 104);
    s_btn_cal_capture1 = UiW_Button(box, 84, 24, "Capturar", ui_cal_capture1_event_cb);
    lv_obj_set_pos(s_btn_cal_capture1, 96, 104);
    s_lbl_cal_point1 = UiW_LabelHint(box, "--");
    lv_obj_set_pos(s_lbl_cal_point1, 188, 108);

    lbl = UiW_LabelHint(box, "P2");
    lv_obj_set_pos(lbl, 6, 138);
    s_spin_cal_ref2 = UiW_Spinbox(box, 60, 24, 0, 300, 3u, 1, s_content);
    lv_obj_set_pos(s_spin_cal_ref2, 28, 134);
    s_btn_cal_capture2 = UiW_Button(box, 84, 24, "Capturar", ui_cal_capture2_event_cb);
    lv_obj_set_pos(s_btn_cal_capture2, 96, 134);
    s_lbl_cal_point2 = UiW_LabelHint(box, "--");
    lv_obj_set_pos(s_lbl_cal_point2, 188, 138);

    s_btn_cal_save = UiW_Button(box, 150, 24, "Calcular e salvar", ui_cal_save_event_cb);
    lv_obj_set_pos(s_btn_cal_save, 6, 164);

    /* Linha propria para o resultado (texto longo nao cabe ao lado). */
    s_lbl_cal_result = UiW_LabelHint(box, "");
    lv_obj_set_pos(s_lbl_cal_result, 6, 192);
}

static void ui_create_leak_panel(int32_t y)
{
    lv_obj_t * box = UiW_TitledBox(s_content, 6, y, LV_HOR_RES - 26, 76, "Teste de vazamento");

    s_leak_box = box;
    s_leak_remaining_label = UiW_LabelBody(box, "Tempo: -- s");
    lv_obj_set_pos(s_leak_remaining_label, 6, 20);

    s_leak_drop_label = UiW_LabelBody(box, "Queda: -- mmHg");
    lv_obj_set_pos(s_leak_drop_label, 6, 38);

    s_leak_result_label = UiW_LabelBody(box, "Resultado: aguardando");
    lv_obj_set_style_text_color(s_leak_result_label, UiTheme_ColorWarn(), 0);
    lv_obj_set_pos(s_leak_result_label, 6, 56);
}

static void ui_create_datetime(int32_t y)
{
    lv_obj_t * box = UiW_TitledBox(s_content, 6, y, LV_HOR_RES - 26, 86,
                                   "Data/Hora (DD MM AAAA HH MM)");

    s_spin_day = UiW_Spinbox(box, 42, 24, 1, 31, 2u, 1, s_content);
    lv_obj_set_pos(s_spin_day, 6, 24);
    s_spin_month = UiW_Spinbox(box, 42, 24, 1, 12, 2u, 1, s_content);
    lv_obj_set_pos(s_spin_month, 52, 24);
    s_spin_year = UiW_Spinbox(box, 62, 24, 2024, 2099, 4u, 1, s_content);
    lv_obj_set_pos(s_spin_year, 98, 24);
    s_spin_hour = UiW_Spinbox(box, 42, 24, 0, 23, 2u, 1, s_content);
    lv_obj_set_pos(s_spin_hour, 172, 24);
    s_spin_minute = UiW_Spinbox(box, 42, 24, 0, 59, 2u, 1, s_content);
    lv_obj_set_pos(s_spin_minute, 218, 24);

    s_btn_apply_datetime = UiW_Button(box, 100, 24, "Aplicar", ui_apply_datetime_btn_event_cb);
    lv_obj_set_pos(s_btn_apply_datetime, 6, 56);

    s_lbl_datetime_result = UiW_LabelHint(box, "");
    lv_obj_set_pos(s_lbl_datetime_result, 116, 60);
}

static void ui_create_info(int32_t y)
{
    lv_obj_t * box = UiW_TitledBox(s_content, 6, y, LV_HOR_RES - 26, 76, "Informacoes");
    lv_obj_t * fw_label = lv_label_create(box);

    lv_label_set_text_fmt(fw_label, "%s | FW %s", UiBackend_GetDeviceId(),
                          UiBackend_GetFirmwareVersion());
    UiTheme_LabelBody(fw_label);
    lv_obj_set_pos(fw_label, 6, 18);

    s_info_sd_label = UiW_LabelBody(box, "Cartao SD: --");
    lv_obj_set_pos(s_info_sd_label, 6, 36);

    s_info_cycle_label = UiW_LabelBody(box, "Ensaios nesta sessao: 0");
    lv_obj_set_pos(s_info_cycle_label, 6, 54);
}

/* ------------------------------------------------------------------------ */
/* Atualizacao periodica                                                    */
/* ------------------------------------------------------------------------ */

static void ui_refresh_datetime_fields(void)
{
    UiDateTime_t dt;

    if (!UiBackend_GetDateTime(&dt)) {
        return;
    }

    lv_spinbox_set_value(s_spin_day, (int32_t)dt.day);
    lv_spinbox_set_value(s_spin_month, (int32_t)dt.month);
    lv_spinbox_set_value(s_spin_year, (int32_t)dt.year);
    lv_spinbox_set_value(s_spin_hour, (int32_t)dt.hour);
    lv_spinbox_set_value(s_spin_minute, (int32_t)dt.minute);
}

static void ui_set_buzzer_label(void)
{
    lv_label_set_text_static(s_lbl_buzzer_toggle,
                             (UiSession_Get()->buzzer_enabled != 0u) ? "Ligado" : "Mudo");
}

static void ui_update_leak_panel(void)
{
    uint32_t hold_ms = UiBackend_GetLeakHoldTimeMs();
    uint32_t remaining_ms = hold_ms;
    float drop = 0.0f;
    int32_t remaining_s;
    int32_t drop_tenths;

    if (s_leak_active) {
        float pressure = UiBackend_GetPressureMmHg();
        uint32_t elapsed = UiBackend_GetTickMs() - s_leak_start_tick;

        remaining_ms = (elapsed >= hold_ms) ? 0u : (hold_ms - elapsed);

        /* Referencia = maior pressao vista (a pressurizacao inicial ainda
         * esta subindo nos primeiros instantes). */
        if (pressure > s_leak_start_pressure) {
            s_leak_start_pressure = pressure;
        }
        drop = s_leak_start_pressure - pressure;
        if (drop < 0.0f) {
            drop = 0.0f;
        }

        if (UiBackend_GetState() != UI_STATE_LEAK_TEST) {
            bool approved = (drop <= UiBackend_GetLeakMaxDropMmHg());

            s_leak_active = false;
            lv_label_set_text_static(s_leak_result_label, approved ? "Resultado: APROVADO"
                                                                   : "Resultado: REPROVADO");
            lv_obj_set_style_text_color(s_leak_result_label,
                                        approved ? UiTheme_ColorOk() : UiTheme_ColorDanger(), 0);
        }
    }

    remaining_s = (int32_t)(remaining_ms / 1000u);
    if (remaining_s != s_shown_leak_remaining_s) {
        s_shown_leak_remaining_s = remaining_s;
        lv_label_set_text_fmt(s_leak_remaining_label, "Tempo: %d s", (int)remaining_s);
    }

    /* Decimos inteiros: o sprintf embutido do LVGL nao formata float
     * (LV_SPRINTF_USE_FLOAT = 0), "%.1f" sairia como "f". */
    drop_tenths = (int32_t)((drop * 10.0f) + 0.5f);
    if (s_leak_active && (drop_tenths != s_shown_leak_drop_tenths)) {
        s_shown_leak_drop_tenths = drop_tenths;
        lv_label_set_text_fmt(s_leak_drop_label, "Queda: %d.%d mmHg",
                              (int)(drop_tenths / 10), (int)(drop_tenths % 10));
    }
}

static void ui_update_info_panel(void)
{
    int32_t sd = UiBackend_IsSdMounted() ? 1 : 0;
    int32_t cycle = (int32_t)UiSession_Get()->cycle_number;

    if (sd != s_shown_sd) {
        s_shown_sd = sd;
        lv_label_set_text_static(s_info_sd_label, (sd != 0) ? "Cartao SD: OK" : "Cartao SD: ausente");
    }
    if (cycle != s_shown_cycle) {
        s_shown_cycle = cycle;
        lv_label_set_text_fmt(s_info_cycle_label, "Ensaios nesta sessao: %d", (int)cycle);
    }
}

static void ui_update_cal_reading(void)
{
    int32_t reading = (int32_t)UiBackend_GetRawPressureMmHg();

    if (reading != s_shown_cal_reading) {
        s_shown_cal_reading = reading;
        lv_label_set_text_fmt(s_lbl_cal_reading, "Leitura bruta: %d mmHg", (int)reading);
    }
}

static void ui_request_back(void)
{
    HMI_EncoderGroup_SetEditing(false);
    UiBackend_CalAbort();
    s_leak_active = false;

    if (s_back_cb != NULL) {
        s_back_cb();
    }
}

/* ------------------------------------------------------------------------ */
/* Eventos                                                                  */
/* ------------------------------------------------------------------------ */

static void ui_setpoint_changed_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    UiBackend_SetSetpoint((float)lv_spinbox_get_value(s_spin_setpoint));
}

static void ui_unit_changed_cb(lv_event_t * e)
{
    uint32_t selected = lv_roller_get_selected(s_roller_unit);

    LV_UNUSED(e);

    if (selected >= (uint32_t)UI_UNIT_COUNT) {
        selected = (uint32_t)UI_UNIT_MMHG;
    }
    UiBackend_SetUnit((UiUnit_t)selected);
}

static void ui_buzzer_toggle_event_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    UiSession_SetBuzzerEnabled((uint8_t)((UiSession_Get()->buzzer_enabled != 0u) ? 0u : 1u));
    ui_set_buzzer_label();
}

static void ui_buzzer_test_event_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    /* Teste toca mesmo com o buzzer mudo — e' para conferir o hardware. */
    UiBackend_BuzzerBeep(2000u, 150u);
}

static void ui_cal_zero_event_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    UiBackend_CalZero();
    lv_label_set_text_static(s_lbl_cal_result, "Zerado.");
}

static void ui_cal_pump_changed_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    UiBackend_CalSetPumpPct((uint8_t)lv_spinbox_get_value(s_spin_cal_pump_pct));
}

static void ui_cal_capture1_event_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    UiBackend_CalCapture(1u, (float)lv_spinbox_get_value(s_spin_cal_ref1));
    lv_label_set_text_static(s_lbl_cal_point1, "OK");
}

static void ui_cal_capture2_event_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    UiBackend_CalCapture(2u, (float)lv_spinbox_get_value(s_spin_cal_ref2));
    lv_label_set_text_static(s_lbl_cal_point2, "OK");
}

static void ui_cal_save_event_cb(lv_event_t * e)
{
    char msg[40];

    LV_UNUSED(e);

    msg[0] = '\0';
    if (UiBackend_CalComputeAndSave(msg, sizeof(msg))) {
        /* Backend ja' desligou a bomba e fechou V3. */
        lv_spinbox_set_value(s_spin_cal_pump_pct, 0);
    }
    lv_label_set_text(s_lbl_cal_result, msg);
}

static void ui_leak_test_btn_event_cb(lv_event_t * e)
{
    LV_UNUSED(e);

    HMI_EncoderGroup_SetEditing(false);

    UiBackend_CalAbort();
    lv_spinbox_set_value(s_spin_cal_pump_pct, 0);

    s_leak_active = true;
    s_leak_start_tick = UiBackend_GetTickMs();
    s_leak_start_pressure = UiBackend_GetPressureMmHg();
    s_shown_leak_drop_tenths = INT32_MIN;

    lv_label_set_text_static(s_leak_result_label, "Resultado: em execucao");
    lv_obj_set_style_text_color(s_leak_result_label, UiTheme_ColorWarn(), 0);

    UiBackend_StartLeakTest();

    /* O foco esta' no rodape; traz o painel do teste para a vista. */
    lv_obj_scroll_to_view(s_leak_box, LV_ANIM_OFF);
}

static void ui_leak_btn_focused_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    lv_obj_scroll_to_y(s_content, LV_COORD_MAX, LV_ANIM_OFF);
}

static void ui_apply_datetime_btn_event_cb(lv_event_t * e)
{
    UiDateTime_t dt;

    LV_UNUSED(e);

    HMI_EncoderGroup_SetEditing(false);

    dt.day = (uint8_t)lv_spinbox_get_value(s_spin_day);
    dt.month = (uint8_t)lv_spinbox_get_value(s_spin_month);
    dt.year = (uint16_t)lv_spinbox_get_value(s_spin_year);
    dt.hour = (uint8_t)lv_spinbox_get_value(s_spin_hour);
    dt.minute = (uint8_t)lv_spinbox_get_value(s_spin_minute);
    dt.second = 0u;

    lv_label_set_text_static(s_lbl_datetime_result,
                             UiBackend_SetDateTime(&dt) ? "Aplicado." : "Data invalida.");
}

static void ui_operator_changed_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    UiSession_SetOperator((uint16_t)lv_spinbox_get_value(s_spin_operator));
}

static void ui_back_btn_event_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    ui_request_back();
}
