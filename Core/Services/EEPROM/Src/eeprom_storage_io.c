/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-07-24
 * Modified: 2026-08-03
 */

#include "eeprom_storage_io.h"

#include <string.h>

#include "eeprom.h"
#include "eeprom_port_stm32_hal.h"

static EEPROM_Handle_t s_eeprom;
static uint8_t s_ready = 0u;

#define EEPROM_TRY_OK(expr) do { if ((expr) != EEPROM_OK) return 0u; } while (0)

static uint32_t crc32_seed(void)
{
    return 0xFFFFFFFFu;
}

static uint32_t crc32_update(uint32_t crc, const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t i;
    uint8_t bit;

    for (i = 0u; i < len; i++) {
        crc ^= p[i];
        for (bit = 0u; bit < 8u; bit++) {
            if ((crc & 1u) != 0u) {
                crc = (crc >> 1u) ^ 0xEDB88320u;
            } else {
                crc >>= 1u;
            }
        }
    }

    return crc;
}

static uint32_t crc32_finish(uint32_t crc)
{
    return ~crc;
}

static uint32_t eeprom_cfg_crc32(const SystemConfig_t *cfg)
{
    uint32_t crc;
    uint8_t unit;

    if (cfg == NULL) {
        return 0u;
    }

    crc = crc32_seed();

    crc = crc32_update(crc, &cfg->pid.kp, (uint32_t)sizeof(cfg->pid.kp));
    crc = crc32_update(crc, &cfg->pid.ki, (uint32_t)sizeof(cfg->pid.ki));
    crc = crc32_update(crc, &cfg->pid.kd, (uint32_t)sizeof(cfg->pid.kd));
    crc = crc32_update(crc, &cfg->pid.ff, (uint32_t)sizeof(cfg->pid.ff));
    crc = crc32_update(crc, &cfg->pid.out_min, (uint32_t)sizeof(cfg->pid.out_min));
    crc = crc32_update(crc, &cfg->pid.out_max, (uint32_t)sizeof(cfg->pid.out_max));
    crc = crc32_update(crc, &cfg->pid.i_min, (uint32_t)sizeof(cfg->pid.i_min));
    crc = crc32_update(crc, &cfg->pid.i_max, (uint32_t)sizeof(cfg->pid.i_max));
    crc = crc32_update(crc, &cfg->pid.sample_time_ms, (uint32_t)sizeof(cfg->pid.sample_time_ms));

    crc = crc32_update(crc, &cfg->sensor_cal.offset_kpa, (uint32_t)sizeof(cfg->sensor_cal.offset_kpa));
    crc = crc32_update(crc, &cfg->sensor_cal.scale, (uint32_t)sizeof(cfg->sensor_cal.scale));

    unit = (uint8_t)cfg->unit;
    crc = crc32_update(crc, &unit, (uint32_t)sizeof(unit));

    crc = crc32_update(crc, &cfg->max_setpoint_mmhg, (uint32_t)sizeof(cfg->max_setpoint_mmhg));
    crc = crc32_update(crc, &cfg->current_setpoint_mmhg, (uint32_t)sizeof(cfg->current_setpoint_mmhg));
    crc = crc32_update(crc, &cfg->stabilization.band_mmhg, (uint32_t)sizeof(cfg->stabilization.band_mmhg));
    crc = crc32_update(crc, &cfg->stabilization.time_ms, (uint32_t)sizeof(cfg->stabilization.time_ms));

    crc = crc32_update(crc, &cfg->leak_test.target_mmhg, (uint32_t)sizeof(cfg->leak_test.target_mmhg));
    crc = crc32_update(crc, &cfg->leak_test.hold_time_ms, (uint32_t)sizeof(cfg->leak_test.hold_time_ms));
    crc = crc32_update(crc, &cfg->leak_test.max_drop_mmhg, (uint32_t)sizeof(cfg->leak_test.max_drop_mmhg));
    crc = crc32_update(crc, &cfg->leak_test.reach_timeout_ms, (uint32_t)sizeof(cfg->leak_test.reach_timeout_ms));

    return crc32_finish(crc);
}

uint8_t EEPROM_Storage_Init(I2C_HandleTypeDef *hi2c, uint8_t addr_pins)
{
    if (EEPROM_HAL_Init(&s_eeprom, hi2c, addr_pins) != EEPROM_OK) {
        s_ready = 0u;
        return 0u;
    }

    s_ready = 1u;
    return 1u;
}

void EEPROM_Storage_LoadDefaults(SystemConfig_t *cfg)
{
    if (cfg == NULL) {
        return;
    }

    cfg->magic = EEPROM_STORAGE_MAGIC_VALUE;
    cfg->layout_version = EEPROM_STORAGE_LAYOUT_VERSION;
    cfg->crc32 = 0u;

    cfg->pid.kp = 2.5f;
    cfg->pid.ki = 0.25f;
    cfg->pid.kd = 0.02f;
    cfg->pid.ff = 1.5f;
    cfg->pid.out_min = 0.0f;
    cfg->pid.out_max = 1000.0f;
    cfg->pid.i_min = -300.0f;
    cfg->pid.i_max = 300.0f;
    cfg->pid.sample_time_ms = 1u;

    cfg->sensor_cal.offset_kpa = 0.0f;
    cfg->sensor_cal.scale = 1.0f;
    cfg->unit = SYS_PRESSURE_UNIT_MMHG;

    cfg->max_setpoint_mmhg = 300.0f;
    cfg->current_setpoint_mmhg = 120.0f;
    cfg->stabilization.band_mmhg = 2.0f;
    cfg->stabilization.time_ms = 1000u;

    cfg->leak_test.target_mmhg = 180.0f;
    cfg->leak_test.hold_time_ms = 30000u;
    cfg->leak_test.max_drop_mmhg = 5.0f;
    cfg->leak_test.reach_timeout_ms = 20000u;

    cfg->crc32 = eeprom_cfg_crc32(cfg);
}

uint8_t EEPROM_Storage_Load(SystemConfig_t *cfg)
{
    uint32_t magic;
    uint16_t version;
    uint8_t unit;

    if (!s_ready || (cfg == NULL)) {
        return 0u;
    }

    EEPROM_TRY_OK(EEPROM_ReadValue(&s_eeprom, EEPROM_ADDR_CFG_MAGIC, magic));
    EEPROM_TRY_OK(EEPROM_ReadValue(&s_eeprom, EEPROM_ADDR_CFG_VERSION, version));

    if ((magic != EEPROM_STORAGE_MAGIC_VALUE) || (version != EEPROM_STORAGE_LAYOUT_VERSION)) {
        return 0u;
    }

    cfg->magic = magic;
    cfg->layout_version = version;

    EEPROM_TRY_OK(EEPROM_ReadValue(&s_eeprom, EEPROM_ADDR_CFG_CRC32, cfg->crc32));

    EEPROM_TRY_OK(EEPROM_ReadValue(&s_eeprom, EEPROM_ADDR_PID_KP, cfg->pid.kp));
    EEPROM_TRY_OK(EEPROM_ReadValue(&s_eeprom, EEPROM_ADDR_PID_KI, cfg->pid.ki));
    EEPROM_TRY_OK(EEPROM_ReadValue(&s_eeprom, EEPROM_ADDR_PID_KD, cfg->pid.kd));
    EEPROM_TRY_OK(EEPROM_ReadValue(&s_eeprom, EEPROM_ADDR_PID_KFF, cfg->pid.ff));
    EEPROM_TRY_OK(EEPROM_ReadValue(&s_eeprom, EEPROM_ADDR_PID_OUT_MIN, cfg->pid.out_min));
    EEPROM_TRY_OK(EEPROM_ReadValue(&s_eeprom, EEPROM_ADDR_PID_OUT_MAX, cfg->pid.out_max));
    EEPROM_TRY_OK(EEPROM_ReadValue(&s_eeprom, EEPROM_ADDR_PID_I_MIN, cfg->pid.i_min));
    EEPROM_TRY_OK(EEPROM_ReadValue(&s_eeprom, EEPROM_ADDR_PID_I_MAX, cfg->pid.i_max));
    EEPROM_TRY_OK(EEPROM_ReadValue(&s_eeprom, EEPROM_ADDR_PID_TS_MS, cfg->pid.sample_time_ms));

    EEPROM_TRY_OK(EEPROM_ReadValue(&s_eeprom, EEPROM_ADDR_SENSOR_OFFSET_KPA, cfg->sensor_cal.offset_kpa));
    EEPROM_TRY_OK(EEPROM_ReadValue(&s_eeprom, EEPROM_ADDR_SENSOR_SCALE, cfg->sensor_cal.scale));

    EEPROM_TRY_OK(EEPROM_ReadValue(&s_eeprom, EEPROM_ADDR_PRESSURE_UNIT, unit));
    if (unit >= (uint8_t)SYS_PRESSURE_UNIT_COUNT) {
        unit = (uint8_t)SYS_PRESSURE_UNIT_MMHG;
    }
    cfg->unit = (PressureUnit_t)unit;

    EEPROM_TRY_OK(EEPROM_ReadValue(&s_eeprom, EEPROM_ADDR_MAX_SETPOINT_MMHG, cfg->max_setpoint_mmhg));
    EEPROM_TRY_OK(EEPROM_ReadValue(&s_eeprom, EEPROM_ADDR_CURRENT_SETPOINT_MMHG, cfg->current_setpoint_mmhg));
    EEPROM_TRY_OK(EEPROM_ReadValue(&s_eeprom, EEPROM_ADDR_STABLE_BAND_MMHG, cfg->stabilization.band_mmhg));
    EEPROM_TRY_OK(EEPROM_ReadValue(&s_eeprom, EEPROM_ADDR_STABLE_TIME_MS, cfg->stabilization.time_ms));

    if (cfg->current_setpoint_mmhg < 0.0f) {
        cfg->current_setpoint_mmhg = 0.0f;
    }
    if (cfg->current_setpoint_mmhg > cfg->max_setpoint_mmhg) {
        cfg->current_setpoint_mmhg = cfg->max_setpoint_mmhg;
    }

    EEPROM_TRY_OK(EEPROM_ReadValue(&s_eeprom, EEPROM_ADDR_LEAK_TARGET_MMHG, cfg->leak_test.target_mmhg));
    EEPROM_TRY_OK(EEPROM_ReadValue(&s_eeprom, EEPROM_ADDR_LEAK_HOLD_TIME_MS, cfg->leak_test.hold_time_ms));
    EEPROM_TRY_OK(EEPROM_ReadValue(&s_eeprom, EEPROM_ADDR_LEAK_MAX_DROP_MMHG, cfg->leak_test.max_drop_mmhg));
    EEPROM_TRY_OK(EEPROM_ReadValue(&s_eeprom, EEPROM_ADDR_LEAK_REACH_TIMEOUT_MS, cfg->leak_test.reach_timeout_ms));

    if (cfg->crc32 != eeprom_cfg_crc32(cfg)) {
        return 0u;
    }

    return 1u;
}

uint8_t EEPROM_Storage_Save(const SystemConfig_t *cfg)
{
    SystemConfig_t cfg_copy;
    uint8_t unit;

    if (!s_ready || (cfg == NULL)) {
        return 0u;
    }

    memcpy(&cfg_copy, cfg, sizeof(cfg_copy));
    cfg_copy.magic = EEPROM_STORAGE_MAGIC_VALUE;
    cfg_copy.layout_version = EEPROM_STORAGE_LAYOUT_VERSION;
    cfg_copy.crc32 = eeprom_cfg_crc32(&cfg_copy);

    unit = (uint8_t)cfg_copy.unit;

    EEPROM_TRY_OK(EEPROM_WriteValue(&s_eeprom, EEPROM_ADDR_CFG_MAGIC, cfg_copy.magic));
    EEPROM_TRY_OK(EEPROM_WriteValue(&s_eeprom, EEPROM_ADDR_CFG_VERSION, cfg_copy.layout_version));
    EEPROM_TRY_OK(EEPROM_WriteValue(&s_eeprom, EEPROM_ADDR_CFG_CRC32, cfg_copy.crc32));

    EEPROM_TRY_OK(EEPROM_WriteValue(&s_eeprom, EEPROM_ADDR_PID_KP, cfg_copy.pid.kp));
    EEPROM_TRY_OK(EEPROM_WriteValue(&s_eeprom, EEPROM_ADDR_PID_KI, cfg_copy.pid.ki));
    EEPROM_TRY_OK(EEPROM_WriteValue(&s_eeprom, EEPROM_ADDR_PID_KD, cfg_copy.pid.kd));
    EEPROM_TRY_OK(EEPROM_WriteValue(&s_eeprom, EEPROM_ADDR_PID_KFF, cfg_copy.pid.ff));
    EEPROM_TRY_OK(EEPROM_WriteValue(&s_eeprom, EEPROM_ADDR_PID_OUT_MIN, cfg_copy.pid.out_min));
    EEPROM_TRY_OK(EEPROM_WriteValue(&s_eeprom, EEPROM_ADDR_PID_OUT_MAX, cfg_copy.pid.out_max));
    EEPROM_TRY_OK(EEPROM_WriteValue(&s_eeprom, EEPROM_ADDR_PID_I_MIN, cfg_copy.pid.i_min));
    EEPROM_TRY_OK(EEPROM_WriteValue(&s_eeprom, EEPROM_ADDR_PID_I_MAX, cfg_copy.pid.i_max));
    EEPROM_TRY_OK(EEPROM_WriteValue(&s_eeprom, EEPROM_ADDR_PID_TS_MS, cfg_copy.pid.sample_time_ms));

    EEPROM_TRY_OK(EEPROM_WriteValue(&s_eeprom, EEPROM_ADDR_SENSOR_OFFSET_KPA, cfg_copy.sensor_cal.offset_kpa));
    EEPROM_TRY_OK(EEPROM_WriteValue(&s_eeprom, EEPROM_ADDR_SENSOR_SCALE, cfg_copy.sensor_cal.scale));
    EEPROM_TRY_OK(EEPROM_WriteValue(&s_eeprom, EEPROM_ADDR_PRESSURE_UNIT, unit));

    EEPROM_TRY_OK(EEPROM_WriteValue(&s_eeprom, EEPROM_ADDR_MAX_SETPOINT_MMHG, cfg_copy.max_setpoint_mmhg));
    EEPROM_TRY_OK(EEPROM_WriteValue(&s_eeprom, EEPROM_ADDR_CURRENT_SETPOINT_MMHG, cfg_copy.current_setpoint_mmhg));
    EEPROM_TRY_OK(EEPROM_WriteValue(&s_eeprom, EEPROM_ADDR_STABLE_BAND_MMHG, cfg_copy.stabilization.band_mmhg));
    EEPROM_TRY_OK(EEPROM_WriteValue(&s_eeprom, EEPROM_ADDR_STABLE_TIME_MS, cfg_copy.stabilization.time_ms));

    EEPROM_TRY_OK(EEPROM_WriteValue(&s_eeprom, EEPROM_ADDR_LEAK_TARGET_MMHG, cfg_copy.leak_test.target_mmhg));
    EEPROM_TRY_OK(EEPROM_WriteValue(&s_eeprom, EEPROM_ADDR_LEAK_HOLD_TIME_MS, cfg_copy.leak_test.hold_time_ms));
    EEPROM_TRY_OK(EEPROM_WriteValue(&s_eeprom, EEPROM_ADDR_LEAK_MAX_DROP_MMHG, cfg_copy.leak_test.max_drop_mmhg));
    EEPROM_TRY_OK(EEPROM_WriteValue(&s_eeprom, EEPROM_ADDR_LEAK_REACH_TIMEOUT_MS, cfg_copy.leak_test.reach_timeout_ms));

    return 1u;
}
