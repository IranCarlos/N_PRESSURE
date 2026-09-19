/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-07-29
 * Modified: 2026-08-03
 */

/**
 * @file hmi_encoder_group.c
 * @brief Implementa o gerenciamento de foco da UI por encoder.
 */

#include "hmi_encoder_group.h"

static lv_group_t * s_encoder_group;
static lv_indev_t * s_encoder_indev;

void HMI_EncoderGroup_Init(void)
{
    if (s_encoder_group == NULL) {
        s_encoder_group = lv_group_create();
        lv_group_set_wrap(s_encoder_group, true);
        lv_group_set_editing(s_encoder_group, false);
    }
}

void HMI_EncoderGroup_BindIndev(lv_indev_t * indev)
{
    s_encoder_indev = indev;

    if ((s_encoder_group != NULL) && (s_encoder_indev != NULL)) {
        lv_indev_set_group(s_encoder_indev, s_encoder_group);
    }
}

lv_group_t * HMI_EncoderGroup_Get(void)
{
    return s_encoder_group;
}

void HMI_EncoderGroup_Clear(void)
{
    if (s_encoder_group == NULL) {
        return;
    }

    lv_group_remove_all_objs(s_encoder_group);
    lv_group_set_editing(s_encoder_group, false);
}

bool HMI_EncoderGroup_Add(lv_obj_t * obj)
{
    if ((s_encoder_group == NULL) || (obj == NULL)) {
        return false;
    }

    lv_group_add_obj(s_encoder_group, obj);
    return true;
}

bool HMI_EncoderGroup_Load(const HMI_EncoderGroupConfig_t * config)
{
    uint16_t i;
    lv_obj_t * first_obj = NULL;
    lv_obj_t * focus_obj = NULL;

    if ((s_encoder_group == NULL) || (config == NULL) || (config->focus_objs == NULL) || (config->count == 0U)) {
        return false;
    }

    HMI_EncoderGroup_Clear();
    lv_group_set_wrap(s_encoder_group, config->wrap);

    for (i = 0U; i < config->count; i++) {
        lv_obj_t * obj = config->focus_objs[i];

        if (obj == NULL) {
            continue;
        }

        lv_group_add_obj(s_encoder_group, obj);

        if (first_obj == NULL) {
            first_obj = obj;
        }

        if (i == config->initial_focus_idx) {
            focus_obj = obj;
        }
    }

    if (focus_obj == NULL) {
        focus_obj = first_obj;
    }

    if (focus_obj != NULL) {
        lv_group_focus_obj(focus_obj);
        lv_group_set_editing(s_encoder_group, false);
        return true;
    }

    return false;
}

void HMI_EncoderGroup_SetEditing(bool editing)
{
    if (s_encoder_group == NULL) {
        return;
    }

    lv_group_set_editing(s_encoder_group, editing);
}
