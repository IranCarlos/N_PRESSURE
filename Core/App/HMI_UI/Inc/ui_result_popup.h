/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-12
 * Modified: 2026-10-17
 */

/**
 * @file ui_result_popup.h
 * @brief Popup "Ensaio concluido" (conforme / nao conforme + metricas),
 *        sobre lv_layer_top(). Fecha pelo botao ou sozinho em 8 s.
 */

#ifndef UI_RESULT_POPUP_H
#define UI_RESULT_POPUP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "ui_backend.h"

typedef void (*UI_ResultPopup_ClosedCb_t)(void);

void UI_ResultPopup_Init(UI_ResultPopup_ClosedCb_t closed_cb);
void UI_ResultPopup_Show(const UiCycleResult_t * result);
void UI_ResultPopup_Update(void);
bool UI_ResultPopup_IsActive(void);
/* Fecha sem chamar o closed_cb (ex.: um erro chegou e tem prioridade). */
void UI_ResultPopup_Dismiss(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_RESULT_POPUP_H */
