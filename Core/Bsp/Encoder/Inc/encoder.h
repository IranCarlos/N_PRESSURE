/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-07-07
 * Modified: 2026-08-03
 */

#ifndef __ENCODER_H
#define __ENCODER_H

#include "main.h"
#include <stdint.h>

/* ------------------------------------------------------------------ */
/*  Configuration                                                      */
/* ------------------------------------------------------------------ */

/** Active logic level of the push-buttons. */
#define ENCODER_BUTTON_ACTIVE_LEVEL   GPIO_PIN_RESET

/** Debounce window in milliseconds. */
#define ENCODER_DEBOUNCE_MS           8u

/** 1 = botoes via EXTI IRQ, 0 = botoes via polling em Encoder_Update(). */
#define ENCODER_BUTTONS_FROM_EXTI     0u

/* ------------------------------------------------------------------ */
/*  Types                                                              */
/* ------------------------------------------------------------------ */

/** Rotation direction reported by Encoder_Update(). */
typedef enum {
    ENCODER_DIR_NONE = 0,   /**< No movement detected in this update cycle. */
    ENCODER_DIR_CW,         /**< Clockwise  (counter increasing).            */
    ENCODER_DIR_CCW         /**< Counter-clockwise (counter decreasing).      */
} Encoder_Direction_t;

/** Complete encoder state snapshot. */
typedef struct {
    int32_t             count;            /**< Raw TIM counter cast to int32.       */
    int32_t             steps;            /**< count / 2  (1 per detent, TI2 mode). */
    int32_t             delta;            /**< Counter change since last Update().  */
    Encoder_Direction_t direction;        /**< Movement direction.                  */
    uint8_t             enc_btn;          /**< Encoder button: 1=pressed, 0=released. */
    uint8_t             user_btn;         /**< User button:    1=pressed, 0=released. */
    uint8_t             enc_btn_event;    /**< Set on press; auto-cleared by GetEncBtnEvent(). */
    uint8_t             user_btn_event;   /**< Set on press; auto-cleared by GetUserBtnEvent(). */
    uint32_t            enc_btn_presses;  /**< Cumulative encoder button press count. */
    uint32_t            user_btn_presses; /**< Cumulative user button press count.    */
} Encoder_State_t;

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

/**
 * @brief  Initialise the encoder driver.
 * @param  htim  Pointer to the TIM handle configured in encoder mode.
 *               Starts the timer and resets the counter to 0.
 */
void Encoder_Init(TIM_HandleTypeDef *htim);

/**
 * @brief  Update encoder state (position + buttons).
 *         Call this periodically from the main loop (e.g., every 10-20 ms).
 */
void Encoder_Update(void);

/**
 * @brief  Get a read-only pointer to the current encoder state.
 * @return Pointer to the internal Encoder_State_t (valid until next Update()).
 */
Encoder_State_t *Encoder_GetState(void);

/**
 * @brief  Consume the encoder-button press event.
 * @return 1 if the button was pressed since the last call, 0 otherwise.
 *         Calling this clears the internal event flag.
 */
uint8_t Encoder_GetEncBtnEvent(void);

/**
 * @brief  Consume the user-button press event.
 * @return 1 if the button was pressed since the last call, 0 otherwise.
 *         Calling this clears the internal event flag.
 */
uint8_t Encoder_GetUserBtnEvent(void);

/**
 * @brief  Reset the encoder counter to zero.
 */
void Encoder_ResetCount(void);

/**
 * @brief  Reset both press counters to zero.
 */
void Encoder_ResetPressCounters(void);

/**
 * @brief Notifica uma interrupcao EXTI de botao para atualizacao imediata.
 * @param gpio_pin Pino recebido em HAL_GPIO_EXTI_Callback.
 */
void Encoder_OnButtonIrq(uint16_t gpio_pin);

#endif /* __ENCODER_H */

