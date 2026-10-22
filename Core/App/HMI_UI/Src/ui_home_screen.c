/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-12
 * Modified: 2026-10-17
 */

/*
 * Layout 320x240 (y absoluto):
 *   y=4   [ banner de estado ..................... 308x26 ]
 *   y=36  [ caixa de pressao                       308x86 ]
 *         [            123 mmHg  (Montserrat 34)          ]
 *         [  ==========-------------- barra 278x12        ]
 *   y=128  Setpoint: 160 mmHg              42s | pico 163 mmHg
 *   y=150  Operador: OP03 | Participante: P037
 *   y=170  17/10/2026 14:32                         SD OK
 *   y=212  Pressione o encoder/pedal...           [ menu 34x28 ]
 *
 * "1 clique em IDLE inicia o ensaio" NAO e' tratado aqui: no firmware a
 * inputTask (1 ms) chama ControlLoop_StartInflation() direto, pelo
 * requisito de latencia pedal->valvula <= 2 ms (a UI roda a 20 ms).
 */

#include "ui_home_screen.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "ui_session.h"
#include "ui_backend.h"
#include "hmi_encoder_group.h"

#define UI_PRESSURE_MIN_MMHG      (0)
#define UI_PRESSURE_MIN_SPAN_MMHG (50)
#define UI_VALUE_UNKNOWN          (-32768)

static UI_HomeScreen_MenuRequestCb_t s_menu_request_cb;

static lv_obj_t * s_screen;
static lv_obj_t * s_status_text;
static lv_obj_t * s_pressure_value_label;
static lv_obj_t * s_pressure_bar;
static lv_obj_t * s_setpoint_label;
static lv_obj_t * s_elapsed_peak_label;
static lv_obj_t * s_session_label;
static lv_obj_t * s_datetime_label;
static lv_obj_t * s_sd_label;

/* Acompanhamento do ciclo (sobrevive a recriacao da tela de proposito). */
static UiState_t s_last_state = UI_STATE_BOOT;
static uint32_t s_cycle_start_tick;
static float s_cycle_peak_mmhg;

/* Cache do ultimo valor desenhado: so' invalida o que mudou (o display e'
 * SPI — redesenhar a tela inteira a cada 20 ms nao cabe na banda). */
static int32_t s_shown_pressure;
static int32_t s_shown_setpoint;
static int32_t s_shown_elapsed;
static int32_t s_shown_peak;
static int32_t s_shown_minute;
static int32_t s_shown_state;
static int32_t s_shown_sd;
static uint32_t s_shown_session;

static int32_t ui_pressure_limit(void);
static void ui_menu_button_event_cb(lv_event_t * e);
static void ui_invalidate_cache(void);

void UI_HomeScreen_Init(UI_HomeScreen_MenuRequestCb_t menu_request_cb)
{
    s_menu_request_cb = menu_request_cb;
}

void UI_HomeScreen_Show(void)
{
    lv_obj_t * status_box;
    lv_obj_t * pressure_box;
    lv_obj_t * usage_hint;
    lv_obj_t * menu_button;
    static lv_obj_t * focusables[1];
    HMI_EncoderGroupConfig_t group_cfg;

    s_screen = lv_obj_create(NULL);
    UiTheme_StyleScreen(s_screen);

    /* Banner de estado */
    status_box = lv_obj_create(s_screen);
    lv_obj_set_size(status_box, LV_HOR_RES - 12, 26);
    lv_obj_align(status_box, LV_ALIGN_TOP_MID, 0, 4);
    UiTheme_StyleBox(status_box);
    lv_obj_remove_flag(status_box, LV_OBJ_FLAG_SCROLLABLE);

    s_status_text = UiW_LabelBody(status_box, "");
    lv_obj_center(s_status_text);

    /* Caixa de pressao */
    pressure_box = lv_obj_create(s_screen);
    lv_obj_set_size(pressure_box, LV_HOR_RES - 12, 86);
    lv_obj_align(pressure_box, LV_ALIGN_TOP_MID, 0, 36);
    UiTheme_StyleBox(pressure_box);
    lv_obj_remove_flag(pressure_box, LV_OBJ_FLAG_SCROLLABLE);

    s_pressure_value_label = lv_label_create(pressure_box);
    lv_label_set_text_static(s_pressure_value_label, "--- mmHg");
    UiTheme_LabelBig(s_pressure_value_label);
    lv_obj_align(s_pressure_value_label, LV_ALIGN_TOP_MID, 0, 2);

    s_pressure_bar = lv_bar_create(pressure_box);
    lv_obj_set_size(s_pressure_bar, LV_HOR_RES - 42, 12);
    lv_obj_align(s_pressure_bar, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_bar_set_range(s_pressure_bar, UI_PRESSURE_MIN_MMHG, ui_pressure_limit());
    lv_obj_set_style_bg_color(s_pressure_bar, UiTheme_ColorTrack(), LV_PART_MAIN);
    lv_obj_set_style_anim_duration(s_pressure_bar, 0, LV_PART_MAIN);

    /* Linha: setpoint | tempo / pico */
    s_setpoint_label = UiW_LabelBody(s_screen, "Setpoint: --- mmHg");
    lv_obj_align(s_setpoint_label, LV_ALIGN_TOP_LEFT, 8, 128);

    s_elapsed_peak_label = UiW_LabelBody(s_screen, "");
    lv_obj_align(s_elapsed_peak_label, LV_ALIGN_TOP_RIGHT, -8, 128);

    /* Linha: sessao */
    s_session_label = UiW_LabelHint(s_screen, "Operador: -- | Participante: --");
    lv_obj_align(s_session_label, LV_ALIGN_TOP_LEFT, 8, 150);

    /* Linha: data/hora | SD */
    s_datetime_label = UiW_LabelHint(s_screen, "--/--/---- --:--");
    lv_obj_align(s_datetime_label, LV_ALIGN_TOP_LEFT, 8, 170);

    s_sd_label = UiW_LabelHint(s_screen, LV_SYMBOL_SD_CARD " --");
    lv_obj_align(s_sd_label, LV_ALIGN_TOP_RIGHT, -8, 170);

    usage_hint = UiW_LabelHint(s_screen, "Pressione o pedal para iniciar/parar");
    lv_obj_set_width(usage_hint, LV_HOR_RES - 60);
    lv_label_set_long_mode(usage_hint, LV_LABEL_LONG_WRAP);
    lv_obj_align(usage_hint, LV_ALIGN_BOTTOM_LEFT, 8, -6);

    menu_button = UiW_Button(s_screen, 34, 28, LV_SYMBOL_LIST, ui_menu_button_event_cb);
    lv_obj_align(menu_button, LV_ALIGN_BOTTOM_RIGHT, -6, -6);

    focusables[0] = menu_button;
    group_cfg.focus_objs = focusables;
    group_cfg.count = HMI_ARRAY_COUNT(focusables);
    group_cfg.initial_focus_idx = 0U;
    group_cfg.wrap = true;
    (void)HMI_EncoderGroup_Load(&group_cfg);

    UiW_LoadScreen(s_screen, &s_screen);

    ui_invalidate_cache();
    UI_HomeScreen_Update();
}

void UI_HomeScreen_TrackCycle(void)
{
    UiState_t state = UiBackend_GetState();
    float pressure = UiBackend_GetPressureMmHg();

    if ((s_last_state == UI_STATE_IDLE) && (state != UI_STATE_IDLE) &&
        (state != UI_STATE_CONFIG) && (state != UI_STATE_LEAK_TEST) &&
        (state != UI_STATE_MANUAL)) {
        s_cycle_start_tick = UiBackend_GetTickMs();
        s_cycle_peak_mmhg = pressure;
        UiSession_NextCycle();
    } else if (state != UI_STATE_IDLE) {
        if (pressure > s_cycle_peak_mmhg) {
            s_cycle_peak_mmhg = pressure;
        }
    } else {
        /* IDLE: nada a acumular */
    }

    s_last_state = state;
}

void UI_HomeScreen_Update(void)
{
    UiState_t state;
    int32_t pressure_val;
    int32_t pressure_limit;
    int32_t setpoint;
    UiDateTime_t dt;
    UiSession_t * session;
    uint32_t session_key;
    int32_t sd;

    if ((s_screen == NULL) || (lv_screen_active() != s_screen)) {
        return;
    }

    state = UiBackend_GetState();

    if ((int32_t)state != s_shown_state) {
        s_shown_state = (int32_t)state;
        lv_label_set_text_static(s_status_text, UiTheme_TextForState(state));
        lv_obj_set_style_text_color(s_status_text, UiTheme_ColorForState(state), 0);
        lv_obj_set_style_bg_color(s_pressure_bar, UiTheme_ColorForState(state), LV_PART_INDICATOR);
    }

    pressure_limit = ui_pressure_limit();
    pressure_val = (int32_t)UiBackend_GetPressureMmHg();
    if (pressure_val < UI_PRESSURE_MIN_MMHG) {
        pressure_val = UI_PRESSURE_MIN_MMHG;
    }
    if (pressure_val > pressure_limit) {
        pressure_val = pressure_limit;
    }
    if (pressure_val != s_shown_pressure) {
        s_shown_pressure = pressure_val;
        lv_label_set_text_fmt(s_pressure_value_label, "%03d mmHg", (int)pressure_val);
        lv_bar_set_range(s_pressure_bar, UI_PRESSURE_MIN_MMHG, pressure_limit);
        lv_bar_set_value(s_pressure_bar, pressure_val, LV_ANIM_OFF);
    }

    setpoint = (int32_t)UiBackend_GetSetpointMmHg();
    if (setpoint != s_shown_setpoint) {
        s_shown_setpoint = setpoint;
        if (setpoint > (int32_t)UiBackend_GetSetpointWarnMmHg()) {
            /* Margem de sobrepressao reduzida (sensor perto do fundo de
             * escala) - mesmo limiar do alerta da tela Novo Ensaio. */
            lv_label_set_text_fmt(s_setpoint_label, LV_SYMBOL_WARNING " Setpoint: %03d mmHg", (int)setpoint);
            lv_obj_set_style_text_color(s_setpoint_label, UiTheme_ColorWarn(), 0);
        } else {
            lv_label_set_text_fmt(s_setpoint_label, "Setpoint: %03d mmHg", (int)setpoint);
            lv_obj_remove_local_style_prop(s_setpoint_label, LV_STYLE_TEXT_COLOR, 0);
        }
    }

    if (state == UI_STATE_IDLE) {
        if (s_shown_elapsed != UI_VALUE_UNKNOWN) {
            s_shown_elapsed = UI_VALUE_UNKNOWN;
            s_shown_peak = UI_VALUE_UNKNOWN;
            lv_label_set_text_static(s_elapsed_peak_label, "");
        }
    } else {
        int32_t elapsed_s = (int32_t)((UiBackend_GetTickMs() - s_cycle_start_tick) / 1000u);
        int32_t peak = (int32_t)s_cycle_peak_mmhg;

        if ((elapsed_s != s_shown_elapsed) || (peak != s_shown_peak)) {
            s_shown_elapsed = elapsed_s;
            s_shown_peak = peak;
            lv_label_set_text_fmt(s_elapsed_peak_label, "%ds | pico %d mmHg",
                                  (int)elapsed_s, (int)peak);
        }
    }

    session = UiSession_Get();
    session_key = ((uint32_t)session->operator_id << 16) | (uint32_t)session->participant_id;
    if (session_key != s_shown_session) {
        s_shown_session = session_key;
        lv_label_set_text_fmt(s_session_label, "Operador: OP%02u | Participante: P%03u",
                              (unsigned int)session->operator_id,
                              (unsigned int)session->participant_id);
    }

    if (UiBackend_GetDateTime(&dt)) {
        int32_t minute_key = ((int32_t)dt.day * 1440) + ((int32_t)dt.hour * 60) + (int32_t)dt.minute;

        if (minute_key != s_shown_minute) {
            s_shown_minute = minute_key;
            lv_label_set_text_fmt(s_datetime_label, "%02u/%02u/%04u %02u:%02u",
                                  (unsigned int)dt.day, (unsigned int)dt.month,
                                  (unsigned int)dt.year, (unsigned int)dt.hour,
                                  (unsigned int)dt.minute);
        }
    }

    sd = UiBackend_IsSdMounted() ? 1 : 0;
    if (sd != s_shown_sd) {
        s_shown_sd = sd;
        lv_label_set_text_static(s_sd_label, (sd != 0) ? LV_SYMBOL_SD_CARD " OK"
                                                       : LV_SYMBOL_SD_CARD " --");
        lv_obj_set_style_text_color(s_sd_label,
                                    (sd != 0) ? UiTheme_ColorOk() : UiTheme_ColorWarn(), 0);
    }
}

static int32_t ui_pressure_limit(void)
{
    int32_t limit = (int32_t)UiBackend_GetMaxSetpointMmHg();

    if (limit < UI_PRESSURE_MIN_SPAN_MMHG) {
        limit = UI_PRESSURE_MIN_SPAN_MMHG;
    }
    return limit;
}

static void ui_invalidate_cache(void)
{
    s_shown_pressure = UI_VALUE_UNKNOWN;
    s_shown_setpoint = UI_VALUE_UNKNOWN;
    s_shown_elapsed = UI_VALUE_UNKNOWN + 1; /* forca limpar o rotulo em IDLE */
    s_shown_peak = UI_VALUE_UNKNOWN;
    s_shown_minute = UI_VALUE_UNKNOWN;
    s_shown_state = UI_VALUE_UNKNOWN;
    s_shown_sd = UI_VALUE_UNKNOWN;
    s_shown_session = 0xFFFFFFFFu;
}

static void ui_menu_button_event_cb(lv_event_t * e)
{
    LV_UNUSED(e);

    if (s_menu_request_cb != NULL) {
        s_menu_request_cb();
    }
}
