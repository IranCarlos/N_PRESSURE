/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-09-18
 */

#ifndef SYSTEM_TYPES_H
#define SYSTEM_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file system_types.h
 * @brief Tipos de dominio compartilhados entre camadas (Services/App).
 *
 * Header-folha sem dependencias de outras camadas: qualquer modulo pode
 * incluir isto sem inverter a direcao de dependencia (App -> Services ->
 * Bsp -> HAL). Antes vivia dentro de eeprom_storage.h — mas estes sao
 * tipos de dominio do sistema, nao detalhes do modulo de persistencia.
 */

/** @brief Marca um buffer como destino/origem de transferencia DMA.
 *
 * DTCMRAM (onde .bss/.data caem por padrao no linker script deste projeto)
 * e' ligada apenas ao core Cortex-M7 e NAO e' alcancavel pelos
 * controladores DMA do STM32H7. Qualquer buffer usado com
 * HAL_*_Start_DMA()/HAL_*_Transmit_DMA() precisa desta macro para cair em
 * RAM_D1 (AXI SRAM), que esta' na matriz de barramento.
 */
#define DMA_BUFFER __attribute__((section(".dma_buffer")))

/**
 * @brief Pressure unit selection used by UI and sensor conversion layers.
 */
typedef enum
{
    SYS_PRESSURE_UNIT_KPA = 0,
    SYS_PRESSURE_UNIT_HPA,
    SYS_PRESSURE_UNIT_MMHG,
    SYS_PRESSURE_UNIT_PSI,
    SYS_PRESSURE_UNIT_CMH2O,
    SYS_PRESSURE_UNIT_BAR,
    SYS_PRESSURE_UNIT_COUNT
} PressureUnit_t;

/**
 * @brief High-level machine states for the insufflation workflow.
 */
typedef enum
{
    SYSTEM_STATE_BOOT = 0,
    SYSTEM_STATE_IDLE,
    SYSTEM_STATE_INFLATING,
    SYSTEM_STATE_STABILIZING,
    SYSTEM_STATE_STABILIZED,
    SYSTEM_STATE_DEFLATING_FAST,
    SYSTEM_STATE_LEAK_TEST,
    SYSTEM_STATE_CONFIG,
    SYSTEM_STATE_FAULT
} SystemState_t;

/**
 * @brief PID + feedforward configuration.
 */
typedef struct
{
    float kp;
    float ki;
    float kd;
    float ff;
    float out_min;
    float out_max;
    float i_min;
    float i_max;
    uint16_t sample_time_ms;
} PID_Config_t;

/**
 * @brief Pressure sensor calibration parameters.
 */
typedef struct
{
    float offset_kpa;
    float scale;
} SensorConfig_t;

/**
 * @brief Stabilization decision parameters.
 */
typedef struct
{
    float band_mmhg;
    uint32_t time_ms;
} StabilizationConfig_t;

/**
 * @brief Leak-test configuration parameters.
 */
typedef struct
{
    float target_mmhg;
    uint32_t hold_time_ms;
    float max_drop_mmhg;
    uint32_t reach_timeout_ms;
} LeakTestConfig_t;

/**
 * @brief Persistent setup edited in configuration screens.
 */
typedef struct
{
    uint32_t magic;
    uint16_t layout_version;
    uint32_t crc32;

    PID_Config_t pid;
    SensorConfig_t sensor_cal;
    PressureUnit_t unit;

    float max_setpoint_mmhg;
    float current_setpoint_mmhg;
    StabilizationConfig_t stabilization;
    LeakTestConfig_t leak_test;
} SystemConfig_t;

/**
 * @brief Runtime snapshot used by control/UI tasks.
 *
 * @var bt_user_pressed Evento global de "botao fisico pressionado" (botao
 *      do encoder ou pedal). NAO e' propriedade de um unico modulo: e'
 *      consumido por quem quer que seja o dono do `state` atual no momento
 *      em que o evento chega —Control_Loop durante INFLATING/STABILIZING/
 *      STABILIZED (aciona desinflar rapido), ui_config_screen durante
 *      CONFIG (aciona "voltar"). Isso e' seguro por construcao: como
 *      `state` e' uma maquina de estados exclusiva, só um desses
 *      consumidores esta "no comando" a cada instante. Ao consumir o
 *      evento, sempre zere o flag.
 */
typedef struct
{
    SystemState_t state;
    float pressure_kpa;
    float pressure_display;
    float setpoint_mmhg;
    uint8_t bt_user_pressed;
} SystemRuntime_t;

#ifdef __cplusplus
}
#endif

#endif /* SYSTEM_TYPES_H */
