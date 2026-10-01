#include "sim_plant.h"
#include <string.h>

static uint32_t xorshift32(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

/* Uniforme em [-1, 1], deterministico a partir da semente. */
static float rand_unit(uint32_t *s)
{
    uint32_t r = xorshift32(s) & 0x00FFFFFFu;
    return ((float)r / (float)0x00FFFFFFu) * 2.0f - 1.0f;
}

void SimPlant_Init(SimPlant_t *p, const SimPlant_Config_t *cfg, uint32_t rng_seed)
{
    memset(p, 0, sizeof(*p));
    p->cfg = *cfg;
    p->rng_state = (rng_seed != 0u) ? rng_seed : 1u;
}

void SimPlant_Step(SimPlant_t *p, float pump_power, uint8_t valves_open_count,
                   uint32_t dt_ms, float *measured_kpa)
{
    float dt_s = (float)dt_ms / 1000.0f;
    float alpha = (p->cfg.tau_lag_s > 0.0f) ? (dt_s / (p->cfg.tau_lag_s + dt_s)) : 1.0f;
    float dpdt;
    float c = (p->cfg.C_manguito > 0.0f) ? p->cfg.C_manguito : 1.0f;

    p->pump_power_filtered += (pump_power - p->pump_power_filtered) * alpha;

    dpdt = p->cfg.k_pump * p->pump_power_filtered;
    dpdt -= p->cfg.k_valve * (float)valves_open_count * p->true_pressure_kpa;
    if (p->cfg.leak_enabled != 0u) {
        dpdt -= p->cfg.k_leak * p->true_pressure_kpa;
    }
    dpdt /= c;

    p->true_pressure_kpa += dpdt * dt_s;
    if (p->true_pressure_kpa < 0.0f) {
        p->true_pressure_kpa = 0.0f;
    }

    *measured_kpa = p->true_pressure_kpa + p->cfg.sensor_offset_kpa +
                    (rand_unit(&p->rng_state) * p->cfg.sensor_noise_amplitude_kpa);
}
