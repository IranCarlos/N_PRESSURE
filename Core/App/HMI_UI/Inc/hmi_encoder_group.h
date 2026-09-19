/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-07-29
 * Modified: 2026-08-03
 */

/**
 * @file hmi_encoder_group.h
 * @brief Gerencia o grupo LVGL para navegacao via encoder.
 */

#ifndef HMI_ENCODER_GROUP_H
#define HMI_ENCODER_GROUP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

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
bool HMI_EncoderGroup_Add(lv_obj_t * obj);
bool HMI_EncoderGroup_Load(const HMI_EncoderGroupConfig_t * config);
void HMI_EncoderGroup_SetEditing(bool editing);

#ifdef __cplusplus
}
#endif

#endif /* HMI_ENCODER_GROUP_H */
