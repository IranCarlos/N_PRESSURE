/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-07-24
 * Modified: 2026-08-03
 */

#ifndef EEPROM_STORAGE_H
#define EEPROM_STORAGE_H

#include <stdint.h>
#include "system_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file eeprom_storage.h
 * @brief Persistent storage address map.
 *
 * This file defines a maintenance-friendly EEPROM address map using one
 * #define per variable. Addresses are intentionally spaced to allow future
 * expansion without forcing remaps of existing fields.
 *
 * Os tipos de dominio persistidos aqui (SystemConfig_t etc.) vivem em
 * system_types.h — este modulo so' define ONDE cada campo fica gravado.
 */

/** @brief Validation constant for stored configuration block. */
#define EEPROM_STORAGE_MAGIC_VALUE                 (0xA5A55A5AU)

/** @brief Current EEPROM layout version. Increment on incompatible changes. */
#define EEPROM_STORAGE_LAYOUT_VERSION              (0x0002U)

/* ========================================================================== */
/* EEPROM map (bytes) - metadata                                               */
/* ========================================================================== */

/** @brief uint32_t: configuration magic value. */
#define EEPROM_ADDR_CFG_MAGIC                      (0x0000U)

/** @brief uint16_t: configuration layout version. */
#define EEPROM_ADDR_CFG_VERSION                    (0x0010U)

/** @brief uint32_t: CRC32 of configuration payload (implementation-defined). */
#define EEPROM_ADDR_CFG_CRC32                      (0x0020U)

/* ========================================================================== */
/* EEPROM map (bytes) - PID + feedforward                                      */
/* ========================================================================== */

/** @brief float: proportional gain. */
#define EEPROM_ADDR_PID_KP                         (0x0100U)

/** @brief float: integral gain. */
#define EEPROM_ADDR_PID_KI                         (0x0110U)

/** @brief float: derivative gain. */
#define EEPROM_ADDR_PID_KD                         (0x0120U)

/** @brief float: feedforward gain (pressure-to-command). */
#define EEPROM_ADDR_PID_KFF                        (0x0130U)

/** @brief float: control output minimum limit (DAC units or normalized). */
#define EEPROM_ADDR_PID_OUT_MIN                    (0x0140U)

/** @brief float: control output maximum limit (DAC units or normalized). */
#define EEPROM_ADDR_PID_OUT_MAX                    (0x0150U)

/** @brief float: integral term minimum clamp. */
#define EEPROM_ADDR_PID_I_MIN                      (0x0160U)

/** @brief float: integral term maximum clamp. */
#define EEPROM_ADDR_PID_I_MAX                      (0x0170U)

/** @brief uint16_t: control period in milliseconds. */
#define EEPROM_ADDR_PID_TS_MS                      (0x0180U)

/* ========================================================================== */
/* EEPROM map (bytes) - sensor + unit                                          */
/* ========================================================================== */

/** @brief float: pressure sensor offset in kPa. */
#define EEPROM_ADDR_SENSOR_OFFSET_KPA              (0x0200U)

/** @brief float: pressure sensor gain/scale correction. */
#define EEPROM_ADDR_SENSOR_SCALE                   (0x0210U)

/** @brief uint8_t: selected pressure unit (see PressureUnit_t). */
#define EEPROM_ADDR_PRESSURE_UNIT                  (0x0220U)

/* ========================================================================== */
/* EEPROM map (bytes) - behavior and safety limits                             */
/* ========================================================================== */

/** @brief float: user setpoint upper limit in mmHg (hard cap = 300 mmHg). */
#define EEPROM_ADDR_MAX_SETPOINT_MMHG              (0x0300U)

/** @brief float: stabilization band around setpoint in mmHg. */
#define EEPROM_ADDR_STABLE_BAND_MMHG               (0x0310U)

/** @brief uint32_t: time required inside band to declare stabilized. */
#define EEPROM_ADDR_STABLE_TIME_MS                 (0x0320U)

/** @brief float: current user-selected setpoint in mmHg. */
#define EEPROM_ADDR_CURRENT_SETPOINT_MMHG          (0x0330U)

/* ========================================================================== */
/* EEPROM map (bytes) - leak test                                              */
/* ========================================================================== */

/** @brief float: leak-test target pressure in mmHg. */
#define EEPROM_ADDR_LEAK_TARGET_MMHG               (0x0400U)

/** @brief uint32_t: leak-test hold duration in milliseconds. */
#define EEPROM_ADDR_LEAK_HOLD_TIME_MS              (0x0410U)

/** @brief float: max allowed pressure drop during leak test (mmHg). */
#define EEPROM_ADDR_LEAK_MAX_DROP_MMHG             (0x0420U)

/** @brief uint32_t: timeout to reach leak-test target pressure (ms). */
#define EEPROM_ADDR_LEAK_REACH_TIMEOUT_MS          (0x0430U)

#ifdef __cplusplus
}
#endif

#endif /* EEPROM_STORAGE_H */
