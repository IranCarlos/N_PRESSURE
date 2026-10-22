/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-12
 * Modified: 2026-10-17
 */

/**
 * @file ui_home_screen.h
 * @brief Tela principal: estado, pressao ao vivo, setpoint, tempo/pico do
 *        ensaio, sessao, data/hora e SD. Unico focavel: botao de menu.
 */

#ifndef UI_HOME_SCREEN_H
#define UI_HOME_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*UI_HomeScreen_MenuRequestCb_t)(void);

void UI_HomeScreen_Init(UI_HomeScreen_MenuRequestCb_t menu_request_cb);
void UI_HomeScreen_Show(void);
void UI_HomeScreen_Update(void);

/* Acompanha a transicao IDLE -> ativo (contador de ciclo da sessao, tempo e
 * pico do ensaio). Chamado a cada tick pelo facade em QUALQUER tela, para
 * nao perder o inicio de um ensaio disparado pelo pedal fora da Home. */
void UI_HomeScreen_TrackCycle(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_HOME_SCREEN_H */
