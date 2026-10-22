/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-17
 */

/**
 * @file ui_backend_stm32.h
 * @brief Implementacao de ui_backend.h para o firmware (Control_Loop,
 *        ADC_Acq, RTC, StorageTask, EEPROM, Buzzer).
 *
 * Fica fora de Core/App/HMI_UI de proposito: e' o unico ponto da IHM que
 * conhece HAL e servicos. O simulador de PC troca este arquivo por
 * pc_sim/ui_backend_mock.c.
 */

#ifndef UI_BACKEND_STM32_H
#define UI_BACKEND_STM32_H

#ifdef __cplusplus
extern "C" {
#endif

#include "system_types.h"

/* Chamar antes de HMI_Init(). Os ponteiros precisam viver o programa todo
 * (sao os mesmos passados ao Control_Loop). */
void UiBackendStm32_Bind(SystemRuntime_t * runtime, SystemConfig_t * config);

/* Chamada por OUTRA tarefa (usbProtoTask, SET_OPERATOR): deixa o pedido
 * para a uiTask aplicar via UiBackend_PollRemoteSession(). Nao bloqueia. */
void UiBackendStm32_PostRemoteSession(uint16_t operator_id, uint16_t participant_id);

/* Chamada pela usbProtoTask apos START_TEST aceito: a uiTask toca o aviso
 * sonoro (UiBackend_PollRemoteStart). Nao bloqueia o USB. */
void UiBackendStm32_PostRemoteStart(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_BACKEND_STM32_H */
