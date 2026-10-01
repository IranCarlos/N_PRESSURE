/*
 * Simulador de planta pneumatica para testar Control_Core no host — Fase 3
 * do prompt do projeto (`tools/sim/` na nomenclatura do prompt; mantido em
 * test/host/ porque so' serve aos testes de host deste repositorio).
 *
 * Modelo de 1a ordem, deliberadamente simples: fluxo da bomba (com atraso de
 * 1a ordem representando T_lag) menos sangria por valvula aberta (linear em
 * P) menos vazamento opcional (linear em P), dividido pela capacitancia do
 * manguito. NAO e' um modelo validado fisicamente — serve para exercitar a
 * logica do Control_Core (alcanca o setpoint, nao oscila, reage a falhas)
 * antes do hardware. Os ganhos reais (T_lag, vazao das valvulas, etc.) so'
 * sao conhecidos depois de medidos na bancada (Fase 4).
 */
#ifndef SIM_PLANT_H
#define SIM_PLANT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float C_manguito;    /* capacitancia pneumatica (kPa por kPa/s de fluxo liquido, abstrato) */
    float k_pump;        /* ganho de fluxo da bomba, kPa/s a potencia=1 e C=1 */
    float tau_lag_s;     /* atraso de 1a ordem do motor+pneumatica (T_lag) */
    float k_valve;       /* coef. de sangria por valvula aberta (kPa/s por kPa de pressao) */
    float k_leak;        /* coef. de vazamento continuo (so' quando leak_enabled) */
    uint8_t leak_enabled;
    float sensor_noise_amplitude_kpa; /* ruido uniforme +-isto, so' na leitura */
    float sensor_offset_kpa;          /* erro de calibracao constante, so' na leitura */
} SimPlant_Config_t;

typedef struct {
    SimPlant_Config_t cfg;
    float    true_pressure_kpa;   /* pressao fisica real (nao vista pelo controlador) */
    float    pump_power_filtered; /* potencia apos o atraso de 1a ordem */
    uint32_t rng_state;
} SimPlant_t;

void SimPlant_Init(SimPlant_t *p, const SimPlant_Config_t *cfg, uint32_t rng_seed);

/* Avanca a fisica por `dt_ms` dado o comando de atuadores do passo anterior
 * do Control_Core; devolve em `measured_kpa` a leitura "do sensor" (com
 * ruido/offset aplicados). `p->true_pressure_kpa` e' a pressao real, usada
 * so' para medir overshoot/erro no teste (o controlador nunca a ve). */
void SimPlant_Step(SimPlant_t *p, float pump_power, uint8_t valves_open_count,
                   uint32_t dt_ms, float *measured_kpa);

#ifdef __cplusplus
}
#endif

#endif /* SIM_PLANT_H */
