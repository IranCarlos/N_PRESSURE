/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-07-29
 * Modified: 2026-10-17
 */

/**
 * @file ui_settings_screen.h
 * @brief Ajustes de nivel USUARIO (prompt do projeto, secao 2.6): setpoint,
 *        unidade, buzzer, calibracao guiada (manometro na V3), teste de
 *        vazamento, data/hora, operador e informacoes.
 *
 * Sem ganhos de PID, PWM livre, valvulas soltas ou offset/escala crus —
 * isso pertence ao app DEV de PC (USB autenticado), nao a' IHM.
 */

#ifndef UI_SETTINGS_SCREEN_H
#define UI_SETTINGS_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*UI_SettingsScreen_BackRequestCb_t)(void);

void UI_SettingsScreen_Init(UI_SettingsScreen_BackRequestCb_t back_cb);
void UI_SettingsScreen_Show(void);
void UI_SettingsScreen_Update(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_SETTINGS_SCREEN_H */
