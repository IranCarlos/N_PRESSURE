/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-12
 * Modified: 2026-10-17
 */

/**
 * @file ui_error_popup.h
 * @brief Popup de erro (codigo Exxx, descricao, acao recomendada), sobre
 *        lv_layer_top(). So' fecha pelo botao — erro nao some sozinho.
 *
 * O alarme sonoro aqui e' so' a notificacao da IHM; a sinalizacao critica
 * nao pode depender da GUI estar viva.
 */

#ifndef UI_ERROR_POPUP_H
#define UI_ERROR_POPUP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

typedef void (*UI_ErrorPopup_ClosedCb_t)(void);

void UI_ErrorPopup_Init(UI_ErrorPopup_ClosedCb_t closed_cb);
void UI_ErrorPopup_Show(uint16_t error_code);
bool UI_ErrorPopup_IsActive(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_ERROR_POPUP_H */
