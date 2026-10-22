/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-17
 */

/**
 * @file ui_theme.h
 * @brief Identidade visual da IHM (Docs/Nobrega Technology_e_myPRESSURE_
 *        Oficial.pdf) como estilos LVGL compartilhados.
 *
 * Os estilos sao `static lv_style_t` criados uma vez em UiTheme_Init() e
 * aplicados com lv_obj_add_style() — nenhuma tela usa cor hex solta nem
 * repete lv_obj_set_style_*() com os mesmos valores.
 */

#ifndef UI_THEME_H
#define UI_THEME_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "ui_backend.h"

/* Chamar uma vez, depois de lv_init() e antes de criar qualquer tela. */
void UiTheme_Init(void);

void UiTheme_StyleScreen(lv_obj_t * screen);       /* fundo cheio da tela */
void UiTheme_StyleBox(lv_obj_t * box);             /* caixa/painel */
void UiTheme_StyleButton(lv_obj_t * btn);          /* botao + foco */
void UiTheme_StyleFocusOutline(lv_obj_t * obj);    /* so' contorno de foco */
void UiTheme_StyleInputField(lv_obj_t * obj);      /* spinbox/roller (sem foco) */
void UiTheme_StyleOverlay(lv_obj_t * obj);         /* fundo escurecido dos popups */

void UiTheme_LabelTitle(lv_obj_t * label);  /* Montserrat 22, branco */
void UiTheme_LabelBody(lv_obj_t * label);   /* Montserrat 14 */
void UiTheme_LabelHint(lv_obj_t * label);   /* Montserrat 14, apagado */
void UiTheme_LabelBig(lv_obj_t * label);    /* Montserrat 34, valor grande */

lv_color_t UiTheme_ColorForState(UiState_t state);
const char * UiTheme_TextForState(UiState_t state);

lv_color_t UiTheme_ColorOk(void);
lv_color_t UiTheme_ColorWarn(void);
lv_color_t UiTheme_ColorDanger(void);
lv_color_t UiTheme_ColorAccent(void);
lv_color_t UiTheme_ColorTrack(void);     /* trilho vazio de lv_bar */
lv_color_t UiTheme_ColorSelected(void);  /* LV_PART_SELECTED do roller */

#ifdef __cplusplus
}
#endif

#endif /* UI_THEME_H */
