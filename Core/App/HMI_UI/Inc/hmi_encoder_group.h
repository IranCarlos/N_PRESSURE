/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-07-29
 * Modified: 2026-10-17
 */

/**
 * @file hmi_encoder_group.h
 * @brief Grupo LVGL unico do encoder: cada tela recarrega a lista de
 *        objetos focaveis ao ser mostrada.
 */

#ifndef HMI_ENCODER_GROUP_H
#define HMI_ENCODER_GROUP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

/* Quantidade de elementos de um array estatico — use para preencher
 * HMI_EncoderGroupConfig_t.count em vez de um #define escrito a mao (um
 * #define desatualizado ja' estourou a pilha da uiTask). */
#define HMI_ARRAY_COUNT(a) ((uint16_t)(sizeof(a) / sizeof((a)[0])))

typedef struct {
    lv_obj_t * const * focus_objs;
    uint16_t count;
    uint16_t initial_focus_idx;
    bool wrap;
} HMI_EncoderGroupConfig_t;

void HMI_EncoderGroup_Init(void);
void HMI_EncoderGroup_BindIndev(lv_indev_t * indev);
lv_group_t * HMI_EncoderGroup_Get(void);
void HMI_EncoderGroup_Clear(void);
bool HMI_EncoderGroup_Load(const HMI_EncoderGroupConfig_t * config);
void HMI_EncoderGroup_SetEditing(bool editing);

#ifdef __cplusplus
}
#endif

#endif /* HMI_ENCODER_GROUP_H */
