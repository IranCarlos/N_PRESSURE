/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-12
 * Modified: 2026-10-17
 */

/**
 * @file ui_boot_screen.h
 * @brief Tela de abertura: marca + status de SD/RTC. Sai sozinha apos
 *        UI_BOOT_MIN_MS ou ao clique do botao do usuario/pedal.
 */

#ifndef UI_BOOT_SCREEN_H
#define UI_BOOT_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*UI_BootScreen_DoneCb_t)(void);

void UI_BootScreen_Init(UI_BootScreen_DoneCb_t done_cb);
void UI_BootScreen_Show(void);
void UI_BootScreen_Update(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_BOOT_SCREEN_H */
