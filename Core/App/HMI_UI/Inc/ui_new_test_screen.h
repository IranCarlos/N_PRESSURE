/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-12
 * Modified: 2026-10-17
 */

/**
 * @file ui_new_test_screen.h
 * @brief Novo ensaio: operador, participante e setpoint; "Confirmar" grava e
 *        volta para a Home; o ensaio comeca pelo pedal (nao infla aqui).
 */

#ifndef UI_NEW_TEST_SCREEN_H
#define UI_NEW_TEST_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*UI_NewTestScreen_StartedCb_t)(void);
typedef void (*UI_NewTestScreen_CancelCb_t)(void);

void UI_NewTestScreen_Init(UI_NewTestScreen_StartedCb_t started_cb,
                           UI_NewTestScreen_CancelCb_t cancel_cb);
void UI_NewTestScreen_Show(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_NEW_TEST_SCREEN_H */
