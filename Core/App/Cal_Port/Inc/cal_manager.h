/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-22
 */

/**
 * @file cal_manager.h
 * @brief Fase 7.8: dono unico da calibracao em uso do sensor de pressao.
 *
 * A calibracao completa (app DEV, via USB) e a verificacao do operador (IHM)
 * passam por aqui: le a media de captura do Pressure_Core, aplica a reta no
 * SensorConfig_t que o ADC_Acq le a cada bloco, grava o registro (data,
 * origem, pontos, residuo) na EEPROM A/B e registra antes/depois na
 * auditoria. Rodar so' com o controle parado (STANDBY/MANUAL/AJUSTES): a
 * gravacao bloqueia ~5 ms.
 */

#ifndef CAL_MANAGER_H
#define CAL_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "system_types.h"
#include "calibration.h"

/* `target` = o SensorConfig_t passado ao ADC_Acq_Init(). `loaded` = o que
 * EEPROM_Storage_LoadCal() trouxe (ou o de fabrica). Aplica no alvo. */
void CalManager_Init(SensorConfig_t * target, const Cal_Record_t * loaded);

const Cal_Record_t * CalManager_Get(void);

/* Media do diferencial (antes da calibracao) e quanto ela ainda varia
 * (mmHg). Devolve 0 se o sensor esta' invalido. */
uint8_t CalManager_Capture(float * d_kpa, float * deviation_mmhg);

/* Aplica + grava + auditoria. `audit_prefix` identifica quem calibrou
 * ("CAL PC", "CAL IHM"). 1 = gravado; 0 = falha de EEPROM (nada mudou). */
uint8_t CalManager_Commit(const Cal_Coeffs_t * coeffs, Cal_Source_t source, uint8_t n_points,
                          float max_residual_mmhg, const char * audit_prefix);

/* Linha de auditoria/erro avulsa do processo de calibracao. */
void CalManager_Log(uint8_t is_error, const char * code, const char * message);

#ifdef __cplusplus
}
#endif

#endif /* CAL_MANAGER_H */
