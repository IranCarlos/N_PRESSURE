/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-17
 */

#include "ui_widgets.h"
#include "ui_theme.h"
#include "hmi_encoder_group.h"

static void uiw_set_editing(bool editing, lv_obj_t * scroll_parent)
{
    HMI_EncoderGroup_SetEditing(editing);
    if (scroll_parent != NULL) {
        lv_obj_set_scroll_dir(scroll_parent, editing ? LV_DIR_NONE : LV_DIR_VER);
    }
}

static void uiw_editable_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * target = lv_event_get_target(e);
    lv_obj_t * scroll_parent = (lv_obj_t *)lv_event_get_user_data(e);
    lv_group_t * group = HMI_EncoderGroup_Get();

    if (group == NULL) {
        return;
    }

    if (code == LV_EVENT_CLICKED) {
        if (!lv_group_get_editing(group)) {
            uiw_set_editing(true, scroll_parent);
        } else if (lv_obj_check_type(target, &lv_spinbox_class)) {
            lv_spinbox_step_next(target);
        } else {
            /* roller: girar ja' troca a opcao, clique nao faz nada extra */
        }
    } else if ((code == LV_EVENT_LONG_PRESSED) || (code == LV_EVENT_DEFOCUSED)) {
        uiw_set_editing(false, scroll_parent);
    } else {
        /* demais eventos nao interessam aqui */
    }
}

static void uiw_spinbox_scroll_cb(lv_event_t * e)
{
    lv_obj_t * spin = lv_event_get_target(e);

    if ((lv_obj_get_scroll_y(spin) != 0) || (lv_obj_get_scroll_x(spin) != 0)) {
        lv_obj_scroll_to(spin, 0, 0, LV_ANIM_OFF);
    }
}

static void uiw_attach_editable(lv_obj_t * obj, lv_obj_t * scroll_parent)
{
    UiTheme_LabelBody(obj);
    UiTheme_StyleInputField(obj);
    UiTheme_StyleFocusOutline(obj);
    lv_obj_add_event_cb(obj, uiw_editable_event_cb, LV_EVENT_CLICKED, scroll_parent);
    lv_obj_add_event_cb(obj, uiw_editable_event_cb, LV_EVENT_LONG_PRESSED, scroll_parent);
    lv_obj_add_event_cb(obj, uiw_editable_event_cb, LV_EVENT_DEFOCUSED, scroll_parent);
}

static void uiw_screen_deleted_cb(lv_event_t * e)
{
    lv_obj_t ** slot = (lv_obj_t **)lv_event_get_user_data(e);

    /* So' zera se o modulo ainda aponta para ESTA tela (ele pode ja' ter
     * criado a substituta antes da exclusao assincrona acontecer). */
    if ((slot != NULL) && (*slot == lv_event_get_target(e))) {
        *slot = NULL;
    }
}

void UiW_LoadScreen(lv_obj_t * scr, lv_obj_t ** slot)
{
    lv_obj_t * prev = lv_screen_active();

    lv_obj_add_event_cb(scr, uiw_screen_deleted_cb, LV_EVENT_DELETE, slot);
    lv_screen_load(scr);

    /* Assincrono: a troca costuma ser pedida de dentro do callback de um
     * botao da propria tela anterior. */
    if ((prev != NULL) && (prev != scr)) {
        lv_obj_delete_async(prev);
    }
}

lv_obj_t * UiW_Button(lv_obj_t * parent, int32_t w, int32_t h, const char * text,
                      lv_event_cb_t clicked_cb)
{
    lv_obj_t * btn = lv_button_create(parent);
    lv_obj_t * lbl = lv_label_create(btn);

    lv_obj_set_size(btn, w, h);
    UiTheme_StyleButton(btn);
    if (clicked_cb != NULL) {
        lv_obj_add_event_cb(btn, clicked_cb, LV_EVENT_CLICKED, NULL);
    }

    lv_label_set_text_static(lbl, text);
    UiTheme_LabelBody(lbl);
    lv_obj_center(lbl);

    return btn;
}

lv_obj_t * UiW_Spinbox(lv_obj_t * parent, int32_t w, int32_t h,
                       int32_t min, int32_t max, uint32_t digits, int32_t step,
                       lv_obj_t * scroll_parent)
{
    lv_obj_t * spin = lv_spinbox_create(parent);

    lv_obj_set_size(spin, w, h);
    lv_spinbox_set_range(spin, min, max);
    lv_spinbox_set_digit_format(spin, digits, 0);
    lv_spinbox_set_step(spin, (uint32_t)step);
    lv_spinbox_set_value(spin, min);
    lv_obj_set_style_text_align(spin, LV_TEXT_ALIGN_RIGHT, 0);
    uiw_attach_editable(spin, scroll_parent);
    /* Os digitos sempre cabem: o textarea interno do spinbox calcula a
     * posicao do cursor antes do layout e rolava o texto para cima, para
     * fora do campo, ate' o campo receber foco. Trava a rolagem em (0,0). */
    lv_obj_remove_flag(spin, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(spin, uiw_spinbox_scroll_cb, LV_EVENT_SCROLL, NULL);

    return spin;
}

lv_obj_t * UiW_Roller(lv_obj_t * parent, int32_t w, int32_t h, const char * options,
                      lv_obj_t * scroll_parent)
{
    lv_obj_t * roller = lv_roller_create(parent);

    lv_roller_set_options(roller, options, LV_ROLLER_MODE_NORMAL);
    lv_obj_set_size(roller, w, h);
    lv_roller_set_visible_row_count(roller, 1);
    lv_obj_set_style_bg_color(roller, UiTheme_ColorSelected(), LV_PART_SELECTED);
    uiw_attach_editable(roller, scroll_parent);

    return roller;
}

lv_obj_t * UiW_TitledBox(lv_obj_t * parent, int32_t x, int32_t y, int32_t w, int32_t h,
                         const char * title)
{
    lv_obj_t * box = lv_obj_create(parent);
    lv_obj_t * lbl = UiW_LabelHint(box, title);

    lv_obj_set_size(box, w, h);
    lv_obj_set_pos(box, x, y);
    UiTheme_StyleBox(box);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 5, 2);

    return box;
}

lv_obj_t * UiW_LabelBody(lv_obj_t * parent, const char * text)
{
    lv_obj_t * lbl = lv_label_create(parent);

    lv_label_set_text_static(lbl, text);
    UiTheme_LabelBody(lbl);
    return lbl;
}

lv_obj_t * UiW_LabelHint(lv_obj_t * parent, const char * text)
{
    lv_obj_t * lbl = lv_label_create(parent);

    lv_label_set_text_static(lbl, text);
    UiTheme_LabelHint(lbl);
    return lbl;
}
