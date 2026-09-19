/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-07-07
 * Modified: 2026-08-03
 */

#ifndef ADC_ACQ_H
#define ADC_ACQ_H

#include "stm32h7xx_hal.h"
#include "system_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicializa o módulo de aquisição do ADC.
 *
 * Configura o ADC para leitura contínua via DMA e inicializa o buffer do filtro.
 * Deve ser chamada uma vez durante a inicialização do sistema.
 *
 * @param hadc Ponteiro para o handle do ADC configurado no CubeMX.
 * @param p_sensor_cal Ponteiro para a estrutura de calibração do sensor (carregada da EEPROM).
 * @param p_runtime_data Ponteiro para dados de runtime usados para rastrear baseline atmosférico.
 */
void ADC_Acq_Init(ADC_HandleTypeDef* hadc,
				  const SensorConfig_t* p_sensor_cal,
				  const SystemRuntime_t* p_runtime_data);

/**
 * @brief Atualiza o processo de leitura e filtragem do sensor.
 *
 * Esta função deve ser chamada periodicamente no super loop (while(1)).
 * Ela consome o último valor do ADC (provido pelo DMA), o adiciona ao filtro de média móvel
 * e recalcula a pressão filtrada em kPa.
 */
void ADC_Acq_Update(void);

/**
 * @brief Retorna a última leitura de pressão filtrada no formato nativo (kPa).
 *
 * @return float A pressão em kiloPascals (kPa).
 */
float ADC_Acq_GetPressureKpa(void);

/**
 * @brief Converte um valor de pressão de kPa para a unidade especificada.
 *
 * @param pressure_kpa Pressão de entrada em kPa.
 * @param target_unit A unidade de destino para a conversão (ex: SYS_PRESSURE_UNIT_MMHG).
 * @return float O valor da pressão na unidade de destino.
 */
float ADC_Acq_ConvertPressure(float pressure_kpa, PressureUnit_t target_unit);

#ifdef __cplusplus
}
#endif

#endif /* ADC_ACQ_H */
