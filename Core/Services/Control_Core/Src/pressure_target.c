/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-31
 */
#include "pressure_target.h"
#include <string.h>

#define PT_BAND_MMHG            (2.0f)
#define PT_OVER_MARGIN_MMHG     (20.0f)
#define PT_LEAD_TIME_S          (0.3f)   /* inercia da bomba + tubo */
#define PT_RATE_PERIOD_MS       (100u)
#define PT_SETTLE_MS            (1500u)
#define PT_SLOPE_WINDOW_MS      (2000u)
#define PT_SLOPE_WINDOW_S       (2.0f)
#define PT_STABLE_SLOPE_MMHG_S  (0.5f)   /* o ar passa do tubo ao manguito por segundos */
#define PT_STEADY_SLOPE_RATIO   (0.8f)   /* assentar desacelera; vazamento segue reto */
#define PT_TIMEOUT_MS           (90000u)
#define PT_BURST_POWER          (0.6f)   /* a bomba so' parte a partir de 45% */
#define PT_BURST_MIN_MS         (100u)
#define PT_BURST_MAX_MS         (1000u)
#define PT_BURST_MARGIN_MMHG    (8.0f)
#define PT_BURST_GAIN_START     (33.0f)  /* mmHg/s */
#define PT_BURST_GAIN_MIN       (5.0f)
#define PT_VENT_MS              (60u)

typedef enum {
    PT_PHASE_SETTLE = 0,
    PT_PHASE_WINDOW1,
    PT_PHASE_WINDOW2,
    PT_PHASE_BURST,
    PT_PHASE_VENT
} PtPhase_t;

static void enter_phase(PressureTarget_t *pt, PtPhase_t phase, uint32_t len_ms)
{
    pt->phase = (uint8_t)phase;
    pt->phase_ms = 0u;
    pt->phase_len_ms = len_ms;
}

static void fail(PressureTarget_t *pt, PressureTarget_Fail_t reason)
{
    pt->state = PRESSURE_TARGET_FAILED;
    pt->fail = reason;
}

void PressureTarget_Start(PressureTarget_t *pt, float target_mmhg)
{
    memset(pt, 0, sizeof(*pt));
    pt->state = PRESSURE_TARGET_RISING;
    pt->target_mmhg = target_mmhg;
    pt->burst_gain_mmhg_s = PT_BURST_GAIN_START;
    pt->rate_p_mmhg = -1.0f;
}

void PressureTarget_Stop(PressureTarget_t *pt)
{
    pt->state = PRESSURE_TARGET_IDLE;
}

static void step_rising(PressureTarget_t *pt, float p, uint32_t dt_ms, PressureTarget_Output_t *out)
{
    float lead;

    if (pt->rate_p_mmhg < 0.0f) {
        pt->rate_p_mmhg = p;
    }
    pt->rate_ms += dt_ms;
    if (pt->rate_ms >= PT_RATE_PERIOD_MS) {
        float dt_s = (float)pt->rate_ms / 1000.0f;

        pt->rate_mmhg_s += (((p - pt->rate_p_mmhg) / dt_s) - pt->rate_mmhg_s) * 0.5f;
        pt->rate_p_mmhg = p;
        pt->rate_ms = 0u;
    }
    lead = (pt->rate_mmhg_s > 0.0f) ? (pt->rate_mmhg_s * PT_LEAD_TIME_S) : 0.0f;
    if ((p + lead) >= (pt->target_mmhg - PT_BAND_MMHG)) {
        pt->state = PRESSURE_TARGET_SETTLING;
        enter_phase(pt, PT_PHASE_SETTLE, PT_SETTLE_MS);
    } else {
        out->pump_power = 1.0f;
    }
}

/* Fim das janelas: decide entre estavel, rajada, alivio ou mais espera. */
static void evaluate(PressureTarget_t *pt, float p)
{
    float early = (pt->first_mmhg - pt->middle_mmhg) / PT_SLOPE_WINDOW_S;
    float slope = (pt->middle_mmhg - p) / PT_SLOPE_WINDOW_S;
    float abs_slope = (slope < 0.0f) ? -slope : slope;
    float start_error = pt->target_mmhg - pt->first_mmhg;
    float abs_start_error = (start_error < 0.0f) ? -start_error : start_error;
    float error = pt->target_mmhg - p;
    uint8_t settled = ((abs_slope <= PT_STABLE_SLOPE_MMHG_S) ||
                       (slope >= (early * PT_STEADY_SLOPE_RATIO))) ? 1u : 0u;

    /* A faixa vale no inicio da janela: um vazamento constante sai dela durante a
     * propria medida, e reabastecer impediria de medi-lo. */
    if ((abs_start_error <= PT_BAND_MMHG) && (settled != 0u)) {
        pt->state = PRESSURE_TARGET_STABLE;
    } else if (pt->elapsed_ms >= PT_TIMEOUT_MS) {
        fail(pt, PRESSURE_TARGET_FAIL_UNSTABLE);
    } else if (error > PT_BAND_MMHG) { /* na faixa e assentando: rajada so' atrapalharia */
        float len_ms = ((pt->target_mmhg + PT_BAND_MMHG - p) / pt->burst_gain_mmhg_s) * 1000.0f;

        if (len_ms < (float)PT_BURST_MIN_MS) { len_ms = (float)PT_BURST_MIN_MS; }
        if (len_ms > (float)PT_BURST_MAX_MS) { len_ms = (float)PT_BURST_MAX_MS; }
        pt->burst_start_mmhg = p;
        enter_phase(pt, PT_PHASE_BURST, (uint32_t)len_ms);
    } else if (error < -PT_BAND_MMHG) {
        enter_phase(pt, PT_PHASE_VENT, PT_VENT_MS);
    } else {
        enter_phase(pt, PT_PHASE_SETTLE, PT_SETTLE_MS);
    }
}

static void step_settling(PressureTarget_t *pt, float p, uint32_t dt_ms, PressureTarget_Output_t *out)
{
    PtPhase_t phase = (PtPhase_t)pt->phase;

    pt->phase_ms += dt_ms;
    switch (phase) {
        case PT_PHASE_BURST:
            out->pump_power = PT_BURST_POWER;
            if ((pt->phase_ms >= pt->phase_len_ms) || (p >= (pt->target_mmhg + PT_BURST_MARGIN_MMHG))) {
                float risen = p - pt->burst_start_mmhg;

                out->pump_power = 0.0f;
                if ((risen > 0.5f) && (pt->phase_ms > 0u)) {
                    /* Com ganho fixo a reposicao empatava com a perda e a pressao
                     * nao chegava a' faixa (bancada 2026-10-31). */
                    float gain = risen / ((float)pt->phase_ms / 1000.0f);

                    pt->burst_gain_mmhg_s = (gain < PT_BURST_GAIN_MIN) ? PT_BURST_GAIN_MIN : gain;
                }
                enter_phase(pt, PT_PHASE_SETTLE, PT_SETTLE_MS);
            }
            break;
        case PT_PHASE_VENT:
            out->vent_open = 1u;
            if (pt->phase_ms >= pt->phase_len_ms) {
                out->vent_open = 0u;
                enter_phase(pt, PT_PHASE_SETTLE, PT_SETTLE_MS);
            }
            break;
        case PT_PHASE_SETTLE:
            if (pt->phase_ms >= pt->phase_len_ms) {
                pt->first_mmhg = p;
                enter_phase(pt, PT_PHASE_WINDOW1, PT_SLOPE_WINDOW_MS);
            }
            break;
        case PT_PHASE_WINDOW1:
            if (pt->phase_ms >= pt->phase_len_ms) {
                pt->middle_mmhg = p;
                enter_phase(pt, PT_PHASE_WINDOW2, PT_SLOPE_WINDOW_MS);
            }
            break;
        case PT_PHASE_WINDOW2:
        default:
            if (pt->phase_ms >= pt->phase_len_ms) {
                evaluate(pt, p);
            }
            break;
    }
}

void PressureTarget_Step(PressureTarget_t *pt, float pressure_mmhg, uint32_t dt_ms,
                         PressureTarget_Output_t *out)
{
    out->pump_power = 0.0f;
    out->vent_open = 0u;

    if ((pt->state != PRESSURE_TARGET_RISING) && (pt->state != PRESSURE_TARGET_SETTLING)) {
        return;
    }

    pt->pressure_mmhg = pressure_mmhg;
    pt->elapsed_ms += dt_ms;
    if (pressure_mmhg > (pt->target_mmhg + PT_OVER_MARGIN_MMHG)) {
        fail(pt, PRESSURE_TARGET_FAIL_OVER);
    } else if ((pt->state == PRESSURE_TARGET_RISING) && (pt->elapsed_ms >= PT_TIMEOUT_MS)) {
        fail(pt, PRESSURE_TARGET_FAIL_NO_RISE);
    } else if (pt->state == PRESSURE_TARGET_RISING) {
        step_rising(pt, pressure_mmhg, dt_ms, out);
    } else {
        step_settling(pt, pressure_mmhg, dt_ms, out);
    }
}
