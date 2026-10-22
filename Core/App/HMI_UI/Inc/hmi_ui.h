/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-07-28
 * Modified: 2026-10-17
 */

/**
 * @file hmi_ui.h
 * @brief Facade da IHM: cria o tema/grupo do encoder, orquestra as telas
 *        (Boot -> Home <-> Menu -> Novo Ensaio / Ajustes) e os popups de
 *        resultado/erro.
 *
 * Depende so' de LVGL e de ui_backend.h — compila igual no firmware e no
 * simulador de PC (pc_sim/). No firmware, o backend precisa estar ligado
 * (UiBackendStm32_Bind()) ANTES de HMI_Init().
 *
 * Tudo aqui roda na tarefa que chama lv_timer_handler() (LVGL nao e'
 * thread-safe).
 */

#ifndef HMI_UI_H
#define HMI_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/* `encoder_indev`: dispositivo LVGL do encoder (pode ser NULL no
 * simulador se o indev for ligado depois com HMI_BindIndev()). */
void HMI_Init(lv_indev_t * encoder_indev);

/* Liga mais um dispositivo de entrada ao grupo da IHM (ex.: teclado no
 * simulador). */
void HMI_BindIndev(lv_indev_t * indev);

/* Chamar periodicamente (~20 ms) na mesma tarefa do lv_timer_handler(). */
void HMI_Update(void);

#ifdef __cplusplus
}
#endif

#endif /* HMI_UI_H */
