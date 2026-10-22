/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-12
 * Modified: 2026-10-17
 */

/*
 * Layout 320x240:
 *   y=6    Novo Ensaio (titulo)
 *   y=40   [ Operador (OP)                [  003 ] ]  caixa 304x32, spin 96x26
 *   y=76   [ Participante (P)             [  037 ] ]
 *   y=112  [ Setpoint (mmHg)              [  160 ] ]
 *   y=200  [ OK Confirmar 150x32 ]  [ < Cancelar 150x32 ]
 *
 * "Confirmar" so' grava operador/participante/setpoint e volta para a Home:
 * o ensaio e' iniciado pelo PEDAL, nunca pelo encoder (pedido do usuario,
 * 2026-10-17).
 */

#include "ui_new_test_screen.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "ui_session.h"
#include "ui_backend.h"
#include "hmi_encoder_group.h"

static UI_NewTestScreen_StartedCb_t s_started_cb;
static UI_NewTestScreen_CancelCb_t s_cancel_cb;

static lv_obj_t * s_screen;
static lv_obj_t * s_spin_operator;
static lv_obj_t * s_spin_participant;
static lv_obj_t * s_spin_setpoint;

static lv_obj_t * ui_create_field(int32_t y, const char * title, int32_t min, int32_t max);
static void ui_start_event_cb(lv_event_t * e);
static void ui_cancel_event_cb(lv_event_t * e);

void UI_NewTestScreen_Init(UI_NewTestScreen_StartedCb_t started_cb,
                           UI_NewTestScreen_CancelCb_t cancel_cb)
{
    s_started_cb = started_cb;
    s_cancel_cb = cancel_cb;
}

void UI_NewTestScreen_Show(void)
{
    lv_obj_t * title;
    lv_obj_t * btn_start;
    lv_obj_t * btn_cancel;
    static lv_obj_t * focusables[5];
    HMI_EncoderGroupConfig_t group_cfg;
    UiSession_t * session = UiSession_Get();

    s_screen = lv_obj_create(NULL);
    UiTheme_StyleScreen(s_screen);

    title = lv_label_create(s_screen);
    lv_label_set_text_static(title, "Novo Ensaio");
    UiTheme_LabelTitle(title);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    s_spin_operator = ui_create_field(40, "Operador (OP)", 0, 99);
    lv_spinbox_set_value(s_spin_operator, (int32_t)session->operator_id);

    s_spin_participant = ui_create_field(76, "Participante (P)", 0, 999);
    lv_spinbox_set_value(s_spin_participant, (int32_t)session->participant_id);

    s_spin_setpoint = ui_create_field(112, "Setpoint (mmHg)", 0,
                                      (int32_t)UiBackend_GetMaxSetpointMmHg());
    lv_spinbox_set_value(s_spin_setpoint, (int32_t)UiBackend_GetSetpointMmHg());

    btn_start = UiW_Button(s_screen, 150, 32, LV_SYMBOL_OK " Confirmar", ui_start_event_cb);
    lv_obj_align(btn_start, LV_ALIGN_BOTTOM_LEFT, 8, -8);

    btn_cancel = UiW_Button(s_screen, 150, 32, LV_SYMBOL_LEFT " Cancelar", ui_cancel_event_cb);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_RIGHT, -8, -8);

    focusables[0] = s_spin_operator;
    focusables[1] = s_spin_participant;
    focusables[2] = s_spin_setpoint;
    focusables[3] = btn_start;
    focusables[4] = btn_cancel;

    group_cfg.focus_objs = focusables;
    group_cfg.count = HMI_ARRAY_COUNT(focusables);
    group_cfg.initial_focus_idx = 0U;
    group_cfg.wrap = true;
    (void)HMI_EncoderGroup_Load(&group_cfg);

    UiW_LoadScreen(s_screen, &s_screen);
}

static lv_obj_t * ui_create_field(int32_t y, const char * title, int32_t min, int32_t max)
{
    lv_obj_t * box = lv_obj_create(s_screen);
    lv_obj_t * lbl = UiW_LabelBody(box, title);
    lv_obj_t * spin = UiW_Spinbox(box, 96, 26, min, max, 3u, 1, NULL);

    lv_obj_set_size(box, LV_HOR_RES - 16, 32);
    lv_obj_set_pos(box, 8, y);
    UiTheme_StyleBox(box);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 6, 0);
    lv_obj_align(spin, LV_ALIGN_RIGHT_MID, -4, 0);

    return spin;
}

static void ui_start_event_cb(lv_event_t * e)
{
    LV_UNUSED(e);

    HMI_EncoderGroup_SetEditing(false);

    UiSession_SetOperator((uint16_t)lv_spinbox_get_value(s_spin_operator));
    UiSession_SetParticipant((uint16_t)lv_spinbox_get_value(s_spin_participant));

    UiBackend_SetSetpoint((float)lv_spinbox_get_value(s_spin_setpoint));
    /* Nao infla aqui: volta para a Home e o ensaio comeca pelo pedal. */

    if (s_started_cb != NULL) {
        s_started_cb();
    }
}

static void ui_cancel_event_cb(lv_event_t * e)
{
    LV_UNUSED(e);

    HMI_EncoderGroup_SetEditing(false);

    if (s_cancel_cb != NULL) {
        s_cancel_cb();
    }
}
