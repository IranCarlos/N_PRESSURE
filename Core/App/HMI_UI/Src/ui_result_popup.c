/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-12
 * Modified: 2026-10-17
 */

/*
 * Layout (sobre lv_layer_top, fundo preto 50%):
 *   card 280x170 centralizado
 *     y=6    Ensaio concluido           (titulo)
 *     y=34   Conforme (NC00)            (Montserrat 16, OK/DANGER)
 *     y=64   Pico: N mmHg
 *     y=84   Tempo ate setpoint: N s
 *     y=104  Overshoot: N mmHg
 *     y=124  proxima medida ou fim da bateria
 *     base   [ Fechar ] ou [ Refazer bateria ] [ Outro paciente ]
 */

#include "ui_result_popup.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "ui_session.h"
#include "hmi_encoder_group.h"
#include <string.h>

#define UI_RESULT_AUTO_CLOSE_MS (8000u)

static UI_ResultPopup_ClosedCb_t s_closed_cb;
static UI_ResultPopup_ClosedCb_t s_next_patient_cb;
static bool s_needs_choice; /* fim da bateria: espera a escolha, sem fechar sozinho */
static lv_obj_t * s_dim_bg;
static uint32_t s_shown_tick;

static void ui_close_event_cb(lv_event_t * e);
static void ui_redo_event_cb(lv_event_t * e);
static void ui_next_patient_event_cb(lv_event_t * e);
static void ui_close(void);
static lv_obj_t * ui_metric_line(lv_obj_t * card, int32_t y);

void UI_ResultPopup_Init(UI_ResultPopup_ClosedCb_t closed_cb, UI_ResultPopup_ClosedCb_t next_patient_cb)
{
    s_closed_cb = closed_cb;
    s_next_patient_cb = next_patient_cb;
}

void UI_ResultPopup_Show(const UiCycleResult_t * result, UiSeriesStep_t step)
{
    lv_obj_t * card;
    lv_obj_t * title;
    lv_obj_t * nc_label;
    lv_obj_t * line;
    lv_obj_t * btn_close;
    static lv_obj_t * focusables[2];
    HMI_EncoderGroupConfig_t group_cfg;
    UiSession_t * session = UiSession_Get();
    bool aborted = (strcmp(result->nc_code, "NC07") == 0);

    if (result == NULL) {
        return;
    }

    /* Tom de conclusao, distinto do alarme de erro. Bloqueia a uiTask por
     * ~250 ms (Buzzer_Beep e' sincrono) — aceitavel, evento raro. */
    if (aborted) {
        UiSession_Beep(1200u, 150u);
        UiSession_Beep(800u, 250u);
    } else if (result->conforme) {
        UiSession_Beep(1800u, 100u);
        UiSession_Beep(2400u, 150u);
    } else {
        UiSession_Beep(900u, 250u);
    }

    if (s_dim_bg != NULL) {
        lv_obj_delete(s_dim_bg);
        s_dim_bg = NULL;
    }

    s_dim_bg = lv_obj_create(lv_layer_top());
    UiTheme_StyleOverlay(s_dim_bg);
    lv_obj_set_size(s_dim_bg, LV_HOR_RES, LV_VER_RES);

    card = lv_obj_create(s_dim_bg);
    lv_obj_set_size(card, LV_HOR_RES - 40, 192);
    lv_obj_center(card);
    UiTheme_StyleBox(card);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    title = lv_label_create(card);
    lv_label_set_text_static(title, aborted ? "Ensaio cancelado" : "Ensaio concluido");
    UiTheme_LabelTitle(title);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    nc_label = lv_label_create(card);
    lv_label_set_text_fmt(nc_label, "%s (%s)", result->conforme ? "Conforme" : "Nao conforme",
                          result->nc_code);
    lv_obj_set_style_text_color(nc_label,
                                result->conforme ? UiTheme_ColorOk() : UiTheme_ColorDanger(), 0);
    lv_obj_set_style_text_font(nc_label, &lv_font_montserrat_16, 0);
    lv_obj_align(nc_label, LV_ALIGN_TOP_MID, 0, 34);

    line = ui_metric_line(card, 64);
    lv_label_set_text_fmt(line, "Pico: %d mmHg", (int)result->peak_mmhg);
    line = ui_metric_line(card, 84);
    lv_label_set_text_fmt(line, "Tempo ate setpoint: %d s", (int)result->inflate_time_s);
    line = ui_metric_line(card, 104);
    lv_label_set_text_fmt(line, "Overshoot: %d mmHg", (int)result->overshoot_mmhg);

    line = ui_metric_line(card, 124);
    s_needs_choice = (step == UI_SERIES_COMPLETE);
    if (step == UI_SERIES_NEXT) {
        lv_label_set_text_fmt(line, "Proxima: medida %u de %u", (unsigned int)session->measure,
                              (unsigned int)UI_SERIES_LENGTH);
    } else if (step == UI_SERIES_REPEAT) {
        lv_label_set_text_fmt(line, "Repetir: medida %u de %u", (unsigned int)session->measure,
                              (unsigned int)UI_SERIES_LENGTH);
    } else {
        lv_label_set_text_fmt(line, "P%03u: %u medidas concluidas", (unsigned int)session->participant_id,
                              (unsigned int)UI_SERIES_LENGTH);
    }

    /* Enquanto aberto, o encoder so' alcanca os botoes do popup. */
    if (s_needs_choice) {
        btn_close = UiW_Button(card, 124, 28, "Refazer bateria", ui_redo_event_cb);
        lv_obj_align(btn_close, LV_ALIGN_BOTTOM_LEFT, 4, -6);
        focusables[0] = btn_close;
        btn_close = UiW_Button(card, 124, 28, "Outro paciente", ui_next_patient_event_cb);
        lv_obj_align(btn_close, LV_ALIGN_BOTTOM_RIGHT, -4, -6);
        focusables[1] = btn_close;
        group_cfg.count = 2U;
    } else {
        btn_close = UiW_Button(card, 100, 28, "Fechar", ui_close_event_cb);
        lv_obj_align(btn_close, LV_ALIGN_BOTTOM_MID, 0, -6);
        focusables[0] = btn_close;
        group_cfg.count = 1U;
    }
    group_cfg.focus_objs = focusables;
    group_cfg.initial_focus_idx = 0U;
    group_cfg.wrap = true;
    (void)HMI_EncoderGroup_Load(&group_cfg);

    s_shown_tick = UiBackend_GetTickMs();
}

void UI_ResultPopup_Update(void)
{
    if ((s_dim_bg != NULL) && !s_needs_choice &&
        ((UiBackend_GetTickMs() - s_shown_tick) >= UI_RESULT_AUTO_CLOSE_MS)) {
        ui_close();
    }
}

bool UI_ResultPopup_IsActive(void)
{
    return (s_dim_bg != NULL);
}

void UI_ResultPopup_Dismiss(void)
{
    if (s_dim_bg != NULL) {
        lv_obj_delete(s_dim_bg);
        s_dim_bg = NULL;
    }
}

static lv_obj_t * ui_metric_line(lv_obj_t * card, int32_t y)
{
    lv_obj_t * lbl = lv_label_create(card);

    UiTheme_LabelBody(lbl);
    lv_obj_set_pos(lbl, 12, y);
    return lbl;
}

static void ui_close_event_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    ui_close();
}

static void ui_redo_event_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    UiSession_SeriesRestart();
    ui_close();
}

/* O proximo paciente ja' vem sugerido; a tela de Novo Ensaio confirma o numero. */
static void ui_next_patient_event_cb(lv_event_t * e)
{
    UiSession_t * session = UiSession_Get();

    LV_UNUSED(e);
    UiSession_SetParticipant((uint16_t)(session->participant_id + 1u));
    lv_obj_delete_async(s_dim_bg);
    s_dim_bg = NULL;
    if (s_next_patient_cb != NULL) {
        s_next_patient_cb();
    }
}

static void ui_close(void)
{
    if (s_dim_bg == NULL) {
        return;
    }

    /* Assincrono: ui_close() tambem roda dentro do callback do proprio
     * botao "Fechar", que e' filho de s_dim_bg. */
    lv_obj_delete_async(s_dim_bg);
    s_dim_bg = NULL;

    if (s_closed_cb != NULL) {
        s_closed_cb();
    }
}
