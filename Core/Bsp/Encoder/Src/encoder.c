/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-07-07
 * Modified: 2026-08-03
 */

#include "encoder.h"

/* ------------------------------------------------------------------ */
/*  Private state                                                      */
/* ------------------------------------------------------------------ */

static TIM_HandleTypeDef *s_htim         = NULL;
static Encoder_State_t    s_state        = {0};
static int32_t            s_last_count   = 0;

/* Per-button debounce state */
static uint8_t  s_enc_raw_prev    = 0;
static uint8_t  s_usr_raw_prev    = 0;
static uint8_t  s_enc_stable      = 0;
static uint8_t  s_usr_stable      = 0;
static uint32_t s_enc_last_ms     = 0;
static uint32_t s_usr_last_ms     = 0;

/* ------------------------------------------------------------------ */
/*  Private helpers                                                    */
/* ------------------------------------------------------------------ */

/** Read a GPIO pin as a logical pressed (1) / released (0) value. */
static uint8_t read_btn(GPIO_TypeDef *port, uint16_t pin)
{
    return (HAL_GPIO_ReadPin(port, pin) == ENCODER_BUTTON_ACTIVE_LEVEL) ? 1u : 0u;
}

#if (ENCODER_BUTTONS_FROM_EXTI == 0u)
/**
 * @brief  Run one debounce cycle for a single button.
 *
 * Uses the "wait for stable" algorithm:
 *   - Any change in the raw input resets the timer.
 *   - If the raw input has been stable for ENCODER_DEBOUNCE_MS, the
 *     debounced output is updated.
 *   - A press event (raw 0->1 transition) increments the counter and
 *     sets the event flag.
 */
static void debounce(uint8_t   raw,
                     uint8_t  *raw_prev,
                     uint8_t  *stable,
                     uint32_t *last_ms,
                     uint8_t  *state_out,
                     uint8_t  *event_out,
                     uint32_t *press_count)
{
    uint32_t now = HAL_GetTick();

    /* Restart the stability timer on any input change */
    if (raw != *raw_prev) {
        *last_ms  = now;
        *raw_prev = raw;
    }

    /* Accept new state only after it has been stable long enough */
    if ((now - *last_ms) >= ENCODER_DEBOUNCE_MS) {
        if (raw != *stable) {
            *stable    = raw;
            *state_out = raw;
            if (raw == 1u) {        /* Rising edge = button pressed */
                *event_out = 1u;
                (*press_count)++;
            }
        }
    }
}
#endif

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

void Encoder_Init(TIM_HandleTypeDef *htim)
{
    s_htim = htim;

    /* Zero the counter and start the timer in encoder mode */
    __HAL_TIM_SET_COUNTER(htim, 0u);
    HAL_TIM_Encoder_Start(htim, TIM_CHANNEL_ALL);

    s_last_count = 0;

    /* Sample buttons at startup to avoid spurious events */
    s_enc_stable   = read_btn(ENCODER_BT_GPIO_Port, ENCODER_BT_Pin);
    s_usr_stable   = read_btn(BT_USER_GPIO_Port,    BT_USER_Pin);
    s_enc_raw_prev = s_enc_stable;
    s_usr_raw_prev = s_usr_stable;
    s_enc_last_ms  = HAL_GetTick();
    s_usr_last_ms  = HAL_GetTick();

    s_state.enc_btn  = s_enc_stable;
    s_state.user_btn = s_usr_stable;
}

void Encoder_Update(void)
{
    if (s_htim == NULL) { return; }

    /* ---- Encoder position ----------------------------------------- */
    int32_t current = (int32_t)__HAL_TIM_GET_COUNTER(s_htim);

    s_state.delta = current - s_last_count;
    s_state.count = current;
    s_state.steps = current / 2;    /* 1 step per detent (TI2 = x2 mode) */
    s_last_count  = current;

    if      (s_state.delta > 0) { s_state.direction = ENCODER_DIR_CW;   }
    else if (s_state.delta < 0) { s_state.direction = ENCODER_DIR_CCW;  }
    else                        { s_state.direction = ENCODER_DIR_NONE; }

    /* ---- Encoder push-button -------------------------------------- */
#if (ENCODER_BUTTONS_FROM_EXTI == 0u)
    debounce(read_btn(ENCODER_BT_GPIO_Port, ENCODER_BT_Pin),
             &s_enc_raw_prev, &s_enc_stable, &s_enc_last_ms,
             &s_state.enc_btn, &s_state.enc_btn_event,
             &s_state.enc_btn_presses);

    /* ---- User push-button ---------------------------------------- */
    debounce(read_btn(BT_USER_GPIO_Port, BT_USER_Pin),
             &s_usr_raw_prev, &s_usr_stable, &s_usr_last_ms,
             &s_state.user_btn, &s_state.user_btn_event,
             &s_state.user_btn_presses);
#endif
}

Encoder_State_t *Encoder_GetState(void)
{
    return &s_state;
}

uint8_t Encoder_GetEncBtnEvent(void)
{
    uint8_t ev = s_state.enc_btn_event;
    s_state.enc_btn_event = 0u;
    return ev;
}

uint8_t Encoder_GetUserBtnEvent(void)
{
    uint8_t ev = s_state.user_btn_event;
    s_state.user_btn_event = 0u;
    return ev;
}

void Encoder_ResetCount(void)
{
    if (s_htim == NULL) { return; }
    __HAL_TIM_SET_COUNTER(s_htim, 0u);
    s_state.count = 0;
    s_state.steps = 0;
    s_state.delta = 0;
    s_last_count  = 0;
}

void Encoder_ResetPressCounters(void)
{
    s_state.enc_btn_presses  = 0u;
    s_state.user_btn_presses = 0u;
}

void Encoder_OnButtonIrq(uint16_t gpio_pin)
{
#if (ENCODER_BUTTONS_FROM_EXTI == 1u)
    uint32_t now = HAL_GetTick();

    if (gpio_pin == ENCODER_BT_Pin) {
        uint8_t raw = read_btn(ENCODER_BT_GPIO_Port, ENCODER_BT_Pin);

        if ((now - s_enc_last_ms) < ENCODER_DEBOUNCE_MS) {
            return;
        }

        s_enc_last_ms = now;
        s_state.enc_btn = raw;
        if (raw == 1u) {
            s_state.enc_btn_event = 1u;
            s_state.enc_btn_presses++;
        }
    }

    if (gpio_pin == BT_USER_Pin) {
        uint8_t raw = read_btn(BT_USER_GPIO_Port, BT_USER_Pin);

        if ((now - s_usr_last_ms) < ENCODER_DEBOUNCE_MS) {
            return;
        }

        s_usr_last_ms = now;
        s_state.user_btn = raw;
        if (raw == 1u) {
            s_state.user_btn_event = 1u;
            s_state.user_btn_presses++;
        }
    }
#else
    (void)gpio_pin;
#endif
}

