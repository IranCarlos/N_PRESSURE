/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-17
 */

/**
 * @file ui_widgets.h
 * @brief Construtores de widget ja' estilizados e com o comportamento
 *        padrao de encoder — usados por todas as telas para nao repetir o
 *        mesmo bloco de criacao/estilo/eventos em cada arquivo.
 *
 * Interacao de campo editavel (spinbox/roller):
 *   - clique fora de edicao  -> entra em edicao (girar altera o valor);
 *   - clique em edicao       -> spinbox avanca o digito (lv_spinbox_step_next);
 *   - long-press / perde foco -> sai da edicao.
 */

#ifndef UI_WIDGETS_H
#define UI_WIDGETS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/* Carrega `scr` e apaga (assincrono) a tela que estava ativa — so' uma
 * tela fica alocada por vez (o heap do LVGL e' 64 kB: manter todas as telas
 * vivas estourou o heap ao abrir os Ajustes). `slot` e' o ponteiro estatico
 * do modulo dono da tela: e' zerado quando ESTA tela for apagada. */
void UiW_LoadScreen(lv_obj_t * scr, lv_obj_t ** slot);

/* Botao com rotulo centralizado (texto estatico — o ponteiro precisa
 * continuar valido, use literais). */
lv_obj_t * UiW_Button(lv_obj_t * parent, int32_t w, int32_t h, const char * text,
                      lv_event_cb_t clicked_cb);

/* Spinbox inteiro [min,max] com `digits` digitos e passo `step`.
 * `scroll_parent` (pode ser NULL): container rolavel cujo scroll e'
 * travado enquanto o campo esta em edicao. */
lv_obj_t * UiW_Spinbox(lv_obj_t * parent, int32_t w, int32_t h,
                       int32_t min, int32_t max, uint32_t digits, int32_t step,
                       lv_obj_t * scroll_parent);

/* Roller de 1 linha visivel com as opcoes separadas por '\n'. */
lv_obj_t * UiW_Roller(lv_obj_t * parent, int32_t w, int32_t h, const char * options,
                      lv_obj_t * scroll_parent);

/* Caixa com titulo (hint) fixo no canto superior esquerdo. TOP_LEFT, nunca
 * LEFT_MID: com LEFT_MID o titulo desce para o meio quando a caixa e'
 * redimensionada depois e atropela o conteudo. */
lv_obj_t * UiW_TitledBox(lv_obj_t * parent, int32_t x, int32_t y, int32_t w, int32_t h,
                         const char * title);

/* Label com estilo (body/hint) ja' aplicado. */
lv_obj_t * UiW_LabelBody(lv_obj_t * parent, const char * text);
lv_obj_t * UiW_LabelHint(lv_obj_t * parent, const char * text);

#ifdef __cplusplus
}
#endif

#endif /* UI_WIDGETS_H */
