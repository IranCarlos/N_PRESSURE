/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-12
 * Modified: 2026-10-17
 */

/**
 * @file ui_menu_screen.h
 * @brief Menu: Novo Ensaio / Ajustes / Voltar.
 */

#ifndef UI_MENU_SCREEN_H
#define UI_MENU_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*new_test_cb)(void);
    void (*settings_cb)(void);
    void (*back_cb)(void);
} UI_MenuScreen_Callbacks_t;

void UI_MenuScreen_Init(const UI_MenuScreen_Callbacks_t * callbacks);
void UI_MenuScreen_Show(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_MENU_SCREEN_H */
