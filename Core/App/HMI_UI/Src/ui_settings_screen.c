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
 *          | y=74  [ Som do ensaio   [Testar] [ Ligado ]  ]    |
 *          | y=108 [ Som do encoder           [ Ligado ]  ]    |
 *          | y=142 [ Verificar calibracao (manometro na V3) ]  |
 *          |       [ Leitura, situacao                        ] |
 *          |       [ Pressao de teste [spin] [Ir]             ] |
 *          |       [ Manometro [spin] [Capturar P1]           ] |
 *          |       [ [Zerar] [Parar] [Verificar]              ] |
 *          | y=334 [ Teste de vazamento: situacao, queda      ] |
 *          | y=416 [ Data/Hora DD MM AAAA HH MM  [Aplicar]    ] |
 *          | y=508 [ Operador ativo (OP)            [ 03 ]    ] |
 *          | y=544 [ Informacoes (so' leitura)                ] |
 *          +---------------------------------------------------+
 *   y=206  [ Teste de Vazamento 152x28 ]  [ < Voltar 152x28 ]  rodape fixo
 *
 * Unica excecao deliberada a "sem atuador manual": a verificacao abre V3 e vai
 * a uma pressao de teste (fluxo guiado, a calibracao completa e' no app DEV).
 */

#include "ui_settings_screen.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "ui_session.h"
#include "ui_backend.h"
#include "hmi_encoder_group.h"

#define UI_CAL_TARGET_STEP   (10)
#define UI_ROW_H             (30)

static UI_SettingsScreen_BackRequestCb_t s_back_cb;

static lv_obj_t * s_screen;
static lv_obj_t * s_content;

static lv_obj_t * s_spin_setpoint;
static lv_obj_t * s_roller_unit;
static lv_obj_t * s_btn_buzzer_test;
static lv_obj_t * s_btn_buzzer_toggle;
static lv_obj_t * s_lbl_buzzer_toggle;
static lv_obj_t * s_btn_click_toggle;
static lv_obj_t * s_lbl_click_toggle;

static lv_obj_t * s_lbl_cal_reading;
static lv_obj_t * s_lbl_cal_state;
static lv_obj_t * s_spin_cal_target;
static lv_obj_t * s_btn_cal_go;
static lv_obj_t * s_spin_cal_ref;
static lv_obj_t * s_btn_cal_capture;
static lv_obj_t * s_lbl_cal_capture;
static lv_obj_t * s_btn_cal_zero;
static lv_obj_t * s_btn_cal_stop;
static lv_obj_t * s_btn_cal_save;
static lv_obj_t * s_lbl_cal_result;

static uint8_t s_cal_next_point;
static int32_t s_shown_service;

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

static int32_t s_shown_leak_phase;

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
static void ui_click_toggle_event_cb(lv_event_t * e);
static void ui_cal_zero_event_cb(lv_event_t * e);
static void ui_cal_go_event_cb(lv_event_t * e);
static void ui_cal_stop_event_cb(lv_event_t * e);
static void ui_cal_capture_event_cb(lv_event_t * e);
static void ui_cal_save_event_cb(lv_event_t * e);
static void ui_set_capture_label(void);
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
    static lv_obj_t * focusables[21];
    HMI_EncoderGroupConfig_t group_cfg;
    uint16_t i = 0U;

    /* Calibracao sempre comeca com a bomba desligada e V3 fechada. */
    UiBackend_CalAbort();
    s_cal_next_point = 1u;

    ui_create_screen();
    ui_refresh_datetime_fields();

    /* Ordem = ordem visual. Ao adicionar/remover um item aqui, ajuste o
     * tamanho de focusables[] — o LV_ASSERT abaixo acusa a divergencia
     * (um contador desatualizado ja' estourou a pilha da uiTask uma vez). */
    focusables[i++] = s_spin_setpoint;
    focusables[i++] = s_roller_unit;
    focusables[i++] = s_btn_buzzer_test;
    focusables[i++] = s_btn_buzzer_toggle;
    focusables[i++] = s_btn_click_toggle;
    focusables[i++] = s_spin_cal_target;
    focusables[i++] = s_btn_cal_go;
    focusables[i++] = s_spin_cal_ref;
    focusables[i++] = s_btn_cal_capture;
    focusables[i++] = s_btn_cal_zero;
    focusables[i++] = s_btn_cal_stop;
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
    s_shown_leak_phase = INT32_MIN;
    s_shown_service = INT32_MIN;
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

    s_lbl_click_toggle = NULL; /* o objeto da abertura anterior ja' nao existe */
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

    /* --- Sons: status do ensaio e clique do encoder sao independentes --- */
    box = ui_row_box(74, "Som do ensaio");
    s_btn_buzzer_test = UiW_Button(box, 64, 22, "Testar", ui_buzzer_test_event_cb);
    lv_obj_align(s_btn_buzzer_test, LV_ALIGN_RIGHT_MID, -96, 0);

    s_btn_buzzer_toggle = UiW_Button(box, 84, 22, "", ui_buzzer_toggle_event_cb);
    lv_obj_align(s_btn_buzzer_toggle, LV_ALIGN_RIGHT_MID, -6, 0);
    s_lbl_buzzer_toggle = lv_obj_get_child(s_btn_buzzer_toggle, 0);
    ui_set_buzzer_label();

    box = ui_row_box(108, "Som do encoder");
    s_btn_click_toggle = UiW_Button(box, 84, 22, "", ui_click_toggle_event_cb);
    lv_obj_align(s_btn_click_toggle, LV_ALIGN_RIGHT_MID, -6, 0);
    s_lbl_click_toggle = lv_obj_get_child(s_btn_click_toggle, 0);
    ui_set_buzzer_label();

    ui_create_calibration(142);
    ui_create_leak_panel(334);
    ui_create_datetime(416);

    /* --- Operador --- */
    box = ui_row_box(508, "Operador ativo (OP)");
    s_spin_operator = UiW_Spinbox(box, 70, 24, 0, 99, 2u, 1, s_content);
    lv_obj_align(s_spin_operator, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_spinbox_set_value(s_spin_operator, (int32_t)UiSession_Get()->operator_id);
    lv_obj_add_event_cb(s_spin_operator, ui_operator_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    ui_create_info(544);

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
    lv_obj_t * box = UiW_TitledBox(s_content, 6, y, LV_HOR_RES - 26, 184,
                                   "Verificar calibracao (manometro na V3)");
    lv_obj_t * lbl;

    s_lbl_cal_reading = UiW_LabelBody(box, "Leitura: -- mmHg");
    lv_obj_set_pos(s_lbl_cal_reading, 6, 22);
    s_lbl_cal_state = UiW_LabelHint(box, "Parado");
    lv_obj_align(s_lbl_cal_state, LV_ALIGN_TOP_RIGHT, -6, 22);

    lbl = UiW_LabelHint(box, "Alvo (mmHg)");
    lv_obj_set_pos(lbl, 6, 50);
    s_spin_cal_target = UiW_Spinbox(box, 70, 24, 0, 170, 3u, UI_CAL_TARGET_STEP, s_content);
    lv_obj_set_pos(s_spin_cal_target, 118, 46);
    lv_spinbox_set_value(s_spin_cal_target, 50);
    s_btn_cal_go = UiW_Button(box, 84, 24, "Ir", ui_cal_go_event_cb);
    lv_obj_set_pos(s_btn_cal_go, 196, 46);

    lbl = UiW_LabelHint(box, "Manometro");
    lv_obj_set_pos(lbl, 6, 80);
    s_spin_cal_ref = UiW_Spinbox(box, 70, 24, 0, 300, 3u, 1, s_content);
    lv_obj_set_pos(s_spin_cal_ref, 118, 76);
    s_btn_cal_capture = UiW_Button(box, 84, 24, "", ui_cal_capture_event_cb);
    lv_obj_set_pos(s_btn_cal_capture, 196, 76);
    s_lbl_cal_capture = lv_obj_get_child(s_btn_cal_capture, 0);
    s_cal_next_point = 1u;
    ui_set_capture_label();

    s_btn_cal_zero = UiW_Button(box, 84, 24, "Zerar (ar)", ui_cal_zero_event_cb);
    lv_obj_set_pos(s_btn_cal_zero, 6, 106);
    s_btn_cal_stop = UiW_Button(box, 84, 24, "Parar", ui_cal_stop_event_cb);
    lv_obj_set_pos(s_btn_cal_stop, 96, 106);
    s_btn_cal_save = UiW_Button(box, 94, 24, "Verificar", ui_cal_save_event_cb);
    lv_obj_set_pos(s_btn_cal_save, 186, 106);

    /* Linha propria para o resultado (texto longo nao cabe ao lado). */
    s_lbl_cal_result = UiW_LabelHint(box, "");
    lv_obj_set_pos(s_lbl_cal_result, 6, 138);
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
    if (s_lbl_click_toggle != NULL) {
        lv_label_set_text_static(s_lbl_click_toggle,
                                 (UiSession_Get()->click_enabled != 0u) ? "Ligado" : "Mudo");
    }
}

static void ui_set_capture_label(void)
{
    lv_label_set_text_fmt(s_lbl_cal_capture, "Capturar P%u", (unsigned int)s_cal_next_point);
}

static void ui_update_leak_panel(void)
{
    UiLeakStatus_t s;
    int32_t remaining_s;
    int32_t drop_tenths;

    UiBackend_GetLeakStatus(&s);

    if ((int32_t)s.phase != s_shown_leak_phase) {
        bool first_paint = (s_shown_leak_phase == INT32_MIN);

        s_shown_leak_phase = (int32_t)s.phase;
        switch (s.phase) {
            case UI_LEAK_RISING:
                lv_label_set_text_static(s_leak_result_label, "Resultado: subindo/estabilizando");
                lv_obj_set_style_text_color(s_leak_result_label, UiTheme_ColorWarn(), 0);
                lv_obj_scroll_to_view(s_leak_box, LV_ANIM_OFF);
                break;
            case UI_LEAK_OBSERVING:
                lv_label_set_text_static(s_leak_result_label, "Resultado: observando a queda");
                break;
            case UI_LEAK_PASSED:
                lv_label_set_text_static(s_leak_result_label, "Resultado: APROVADO");
                lv_obj_set_style_text_color(s_leak_result_label, UiTheme_ColorOk(), 0);
                if (!first_paint) { UiSession_Beep(1800u, 100u); UiSession_Beep(2400u, 150u); }
                break;
            case UI_LEAK_FAILED:
                lv_label_set_text_static(s_leak_result_label, s.held ? "Resultado: REPROVADO"
                                                                     : "REPROVADO: nao manteve a pressao");
                lv_obj_set_style_text_color(s_leak_result_label, UiTheme_ColorDanger(), 0);
                if (!first_paint) { UiSession_Beep(900u, 250u); }
                break;
            case UI_LEAK_ABORTED:
                lv_label_set_text_static(s_leak_result_label, "Resultado: interrompido, sem veredito");
                lv_obj_set_style_text_color(s_leak_result_label, UiTheme_ColorWarn(), 0);
                break;
            case UI_LEAK_NONE:
            default:
                lv_label_set_text_static(s_leak_result_label, "Resultado: aguardando");
                break;
        }
    }

    remaining_s = (int32_t)(s.remaining_ms / 1000u);
    if (remaining_s != s_shown_leak_remaining_s) {
        s_shown_leak_remaining_s = remaining_s;
        lv_label_set_text_fmt(s_leak_remaining_label, "Tempo: %d s", (int)remaining_s);
    }

    /* Decimos inteiros: o sprintf embutido do LVGL nao formata float
     * (LV_SPRINTF_USE_FLOAT = 0), "%.1f" sairia como "f". */
    drop_tenths = (s.drop_mmhg > 0.0f) ? (int32_t)((s.drop_mmhg * 10.0f) + 0.5f) : 0;
    if ((s.phase != UI_LEAK_NONE) && (s.phase != UI_LEAK_RISING) && (drop_tenths != s_shown_leak_drop_tenths)) {
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
    /* Leitura CALIBRADA: e' ela que o operador compara com o manometro. */
    int32_t reading = (int32_t)UiBackend_GetPressureMmHg();
    UiServiceState_t service = UiBackend_ServiceState();

    if (reading != s_shown_cal_reading) {
        s_shown_cal_reading = reading;
        lv_label_set_text_fmt(s_lbl_cal_reading, "Leitura: %d mmHg", (int)reading);
    }
    if ((int32_t)service != s_shown_service) {
        s_shown_service = (int32_t)service;
        switch (service) {
            case UI_SERVICE_MOVING:
                lv_label_set_text_static(s_lbl_cal_state, "Indo...");
                break;
            case UI_SERVICE_STABLE:
                lv_label_set_text_static(s_lbl_cal_state, "Estavel: leia");
                UiSession_Beep(2200u, 120u);
                break;
            case UI_SERVICE_FAILED:
                lv_label_set_text_static(s_lbl_cal_state, "Nao estabilizou");
                UiSession_Beep(900u, 250u);
                break;
            case UI_SERVICE_IDLE:
            default:
                lv_label_set_text_static(s_lbl_cal_state, "Parado");
                break;
        }
    }
}

static void ui_request_back(void)
{
    HMI_EncoderGroup_SetEditing(false);
    UiBackend_CalAbort();

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

static void ui_click_toggle_event_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    UiSession_SetClickEnabled((uint8_t)((UiSession_Get()->click_enabled != 0u) ? 0u : 1u));
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
    UiBackend_ServiceGoTo(0.0f); /* abre tudo antes de zerar */
    lv_label_set_text_static(s_lbl_cal_result, "Zerado.");
}

static void ui_cal_go_event_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    HMI_EncoderGroup_SetEditing(false);
    lv_label_set_text_static(s_lbl_cal_result, "");
    UiBackend_ServiceGoTo((float)lv_spinbox_get_value(s_spin_cal_target));
}

static void ui_cal_stop_event_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    UiBackend_CalAbort();
    s_cal_next_point = 1u;
    ui_set_capture_label();
    lv_label_set_text_static(s_lbl_cal_result, "Parado: linha esvaziando.");
}

static void ui_cal_capture_event_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    HMI_EncoderGroup_SetEditing(false);
    if (!UiBackend_CalCapture(s_cal_next_point, (float)lv_spinbox_get_value(s_spin_cal_ref))) {
        lv_label_set_text_static(s_lbl_cal_result, "Pressao variando: aguarde.");
        return;
    }
    lv_label_set_text_fmt(s_lbl_cal_result, "P%u capturado.", (unsigned int)s_cal_next_point);
    s_cal_next_point = (uint8_t)((s_cal_next_point == 1u) ? 2u : 1u);
    ui_set_capture_label();
}

static void ui_cal_save_event_cb(lv_event_t * e)
{
    char msg[40];

    LV_UNUSED(e);

    msg[0] = '\0';
    switch (UiBackend_CalVerify(msg, sizeof(msg))) {
        case UI_CAL_OK:
        case UI_CAL_CORRECTED:
            lv_obj_set_style_text_color(s_lbl_cal_result, UiTheme_ColorOk(), 0);
            break;
        case UI_CAL_ALERT:
        case UI_CAL_FAILED:
            lv_obj_set_style_text_color(s_lbl_cal_result, UiTheme_ColorDanger(), 0);
            UiSession_Beep(600u, 120u);
            UiSession_Beep(600u, 120u);
            break;
        case UI_CAL_INCOMPLETE:
        default:
            lv_obj_remove_local_style_prop(s_lbl_cal_result, LV_STYLE_TEXT_COLOR, 0);
            break;
    }
    /* Backend ja' desligou a bomba e devolveu as valvulas ao controle. */
    s_cal_next_point = 1u;
    ui_set_capture_label();
    lv_label_set_text(s_lbl_cal_result, msg);
}

static void ui_leak_test_btn_event_cb(lv_event_t * e)
{
    LV_UNUSED(e);

    HMI_EncoderGroup_SetEditing(false);

    UiBackend_CalAbort();
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
