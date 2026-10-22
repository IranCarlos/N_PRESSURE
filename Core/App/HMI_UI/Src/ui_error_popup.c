/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-12
 * Modified: 2026-10-17
 */

/*
 * Layout (sobre lv_layer_top, fundo preto 50%):
 *   card 280x164 centralizado, borda DANGER 2px
 *     y=6    ! E001                     (Montserrat 22, DANGER)
 *     y=40   descricao curta            (body, quebra de linha)
 *     y=72   acao recomendada           (hint, quebra de linha)
 *     y=132  [ Fechar 100x26 ]
 */

#include "ui_error_popup.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "ui_session.h"
#include "hmi_encoder_group.h"

typedef struct {
    uint16_t code;
    const char * short_text;
    const char * action;
} UiErrorEntry_t;

/* Apendice D (Docs/PROMPT_PROJETO_STM32H723_MANGUITO.md) — so' os codigos
 * ja' implementados em Control_Core (control_core.h). */
static const UiErrorEntry_t s_error_table[] = {
    { 1u,  "Sobrepressao detectada",
           "Ensaio invalidado. Verifique o manguito e reinicie." },
    { 2u,  "Falha no sensor de pressao",
           "Novos ensaios bloqueados. Verifique a conexao do sensor." },
    { 3u,  "Nao atingiu o setpoint a tempo",
           "Ensaio abortado e desinflado. Verifique vazamentos/manguito." },
    { 4u,  "Tempo de desinflar excedido",
           "Valvulas mantidas abertas. Verifique o circuito pneumatico." },
    { 13u, "Vazamento detectado",
           "Ensaio encerrado com aviso. Verifique conexoes/manguito." },
};

static UI_ErrorPopup_ClosedCb_t s_closed_cb;
static lv_obj_t * s_dim_bg;

static const UiErrorEntry_t * ui_find_entry(uint16_t code);
static lv_obj_t * ui_wrapped_label(lv_obj_t * card, const char * text, int32_t y, bool hint);
static void ui_close_event_cb(lv_event_t * e);

void UI_ErrorPopup_Init(UI_ErrorPopup_ClosedCb_t closed_cb)
{
    s_closed_cb = closed_cb;
}

void UI_ErrorPopup_Show(uint16_t error_code)
{
    const UiErrorEntry_t * entry = ui_find_entry(error_code);
    const char * short_text = "Erro nao catalogado";
    const char * action = "Consulte o registro de eventos.";
    lv_obj_t * card;
    lv_obj_t * title;
    lv_obj_t * btn_close;
    static lv_obj_t * focusables[1];
    HMI_EncoderGroupConfig_t group_cfg;

    if (entry != NULL) {
        short_text = entry->short_text;
        action = entry->action;
    }

    if (s_dim_bg != NULL) {
        lv_obj_delete(s_dim_bg);
        s_dim_bg = NULL;
    }

    /* 3 bipes graves — distinto do tom de conclusao. */
    UiSession_Beep(600u, 120u);
    UiSession_Beep(600u, 120u);
    UiSession_Beep(600u, 120u);

    s_dim_bg = lv_obj_create(lv_layer_top());
    UiTheme_StyleOverlay(s_dim_bg);
    lv_obj_set_size(s_dim_bg, LV_HOR_RES, LV_VER_RES);

    card = lv_obj_create(s_dim_bg);
    lv_obj_set_size(card, LV_HOR_RES - 40, 164);
    lv_obj_center(card);
    UiTheme_StyleBox(card);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_color(card, UiTheme_ColorDanger(), 0);
    lv_obj_set_style_border_width(card, 2, 0);

    title = lv_label_create(card);
    lv_label_set_text_fmt(title, LV_SYMBOL_WARNING " E%03u", (unsigned int)error_code);
    UiTheme_LabelTitle(title);
    lv_obj_set_style_text_color(title, UiTheme_ColorDanger(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    (void)ui_wrapped_label(card, short_text, 40, false);
    (void)ui_wrapped_label(card, action, 72, true);

    btn_close = UiW_Button(card, 100, 26, "Fechar", ui_close_event_cb);
    lv_obj_align(btn_close, LV_ALIGN_BOTTOM_MID, 0, -6);

    focusables[0] = btn_close;
    group_cfg.focus_objs = focusables;
    group_cfg.count = HMI_ARRAY_COUNT(focusables);
    group_cfg.initial_focus_idx = 0U;
    group_cfg.wrap = true;
    (void)HMI_EncoderGroup_Load(&group_cfg);
}

bool UI_ErrorPopup_IsActive(void)
{
    return (s_dim_bg != NULL);
}

static const UiErrorEntry_t * ui_find_entry(uint16_t code)
{
    size_t i;

    for (i = 0u; i < (sizeof(s_error_table) / sizeof(s_error_table[0])); i++) {
        if (s_error_table[i].code == code) {
            return &s_error_table[i];
        }
    }
    return NULL;
}

static lv_obj_t * ui_wrapped_label(lv_obj_t * card, const char * text, int32_t y, bool hint)
{
    lv_obj_t * lbl = hint ? UiW_LabelHint(card, text) : UiW_LabelBody(card, text);

    lv_obj_set_width(lbl, LV_HOR_RES - 60);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, y);
    return lbl;
}

static void ui_close_event_cb(lv_event_t * e)
{
    LV_UNUSED(e);

    if (s_dim_bg == NULL) {
        return;
    }

    lv_obj_delete_async(s_dim_bg); /* estamos dentro do callback de um filho */
    s_dim_bg = NULL;

    if (s_closed_cb != NULL) {
        s_closed_cb();
    }
}
