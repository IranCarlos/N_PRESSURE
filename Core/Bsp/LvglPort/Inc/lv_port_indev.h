/**
 * @file lv_port_indev.h
 * @author Eng. de Sistemas Embarcados Sênior
 * @brief Portabilidade do dispositivo de entrada (Encoder) para o LVGL.
 * @version 0.1
 * @date 2026-07-29
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef LV_PORT_INDEV_H
#define LV_PORT_INDEV_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

void lv_port_indev_init(void);
/**
 * @brief Retorna o dispositivo de entrada padrão (o encoder).
 * @return lv_indev_t* Ponteiro para o indev do encoder.
 */
lv_indev_t * lv_port_indev_get_default(void);

#ifdef __cplusplus
}
#endif

#endif /*LV_PORT_INDEV_H*/