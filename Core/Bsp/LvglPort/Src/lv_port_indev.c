/**
 * @file lv_port_indev.c
 * @author Eng. de Sistemas Embarcados Sênior
 * @brief Implementação da ponte entre o Encoder e o LVGL.
 * @version 0.1
 * @date 2026-07-29
 *
 * @copyright Copyright (c) 2026
 */

#include "lv_port_indev.h"
#include "encoder.h"

static lv_indev_t * indev_encoder;
static int32_t s_enc_substep_acc;
static int32_t s_last_count;

/**
 * @brief Função de callback que será chamada pelo LVGL para ler o estado do encoder.
 * @param driver Ponteiro para o driver do dispositivo de entrada.
 * @param data Ponteiro para a estrutura onde os dados lidos devem ser armazenados.
 */
static void encoder_read_cb(lv_indev_t * indev, lv_indev_data_t * data)
{
    Encoder_State_t* enc_state = Encoder_GetState();
    int32_t count_diff;
    int32_t lvgl_steps;

    LV_UNUSED(indev);

    // Usa diferenca acumulada de contagem para nao perder passos entre
    // chamadas do Encoder_Update e do read_cb do LVGL.
    count_diff = enc_state->count - s_last_count;
    s_last_count = enc_state->count;

    s_enc_substep_acc += count_diff;
    lvgl_steps = s_enc_substep_acc / 2;
    s_enc_substep_acc -= (lvgl_steps * 2);
    data->enc_diff = lvgl_steps;

    // Estado do botão (pressionado ou liberado)
    if (enc_state->enc_btn) {
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/**
 * @brief Inicializa e registra o encoder como um dispositivo de entrada no LVGL.
 */
void lv_port_indev_init(void)
{
    Encoder_State_t* enc_state = Encoder_GetState();

    s_enc_substep_acc = 0;
    s_last_count = enc_state->count;

    // Cria um novo dispositivo de entrada
    indev_encoder = lv_indev_create();

    // Define o tipo como Encoder
    lv_indev_set_type(indev_encoder, LV_INDEV_TYPE_ENCODER);

    // Associa a função de callback para leitura
    lv_indev_set_read_cb(indev_encoder, encoder_read_cb);
}

/**
 * @brief Retorna o dispositivo de entrada padrão (o encoder).
 */
lv_indev_t * lv_port_indev_get_default(void)
{
    return indev_encoder;
}