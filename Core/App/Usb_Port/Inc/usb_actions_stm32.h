/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-22
 */

/**
 * @file usb_actions_stm32.h
 * @brief Fase 7.3: implementacao no firmware de UsbProtoActions_t
 *        (usb_proto_dispatch.h) - os comandos de controle via USB.
 *
 * Mesmo papel do HMI_Port para a IHM: e' o unico ponto do protocolo USB que
 * conhece Control_Loop, RTC, StorageTask e a sessao da IHM. A regra de
 * "pode neste estado?" vem de usb_proto_policy.h (testada no host); aqui so'
 * se consulta e executa.
 *
 * Os comandos seguem o MESMO caminho do pedal/bt_user e da IHM:
 *   START_TEST   -> ControlLoop_StartInflation() (so' em IDLE)
 *   ABORT_TEST   -> runtime->bt_user_pressed = 1 (consumido pelo controle no
 *                   proximo tick de 1 ms - nao espera nenhuma outra tarefa)
 *   SET_SETPOINT -> ControlLoop_SetSetpointMmHg()
 *   SET_DATETIME -> RtcService_SetDateTime() + registro no log de auditoria
 *   SET_OPERATOR -> sessao da IHM (UiBackendStm32_PostRemoteSession)
 * Todos rodam na usbProtoTask. Cada comando aceito gera uma linha de log
 * (evento ou auditoria) dizendo que veio do USB.
 */

#ifndef USB_ACTIONS_STM32_H
#define USB_ACTIONS_STM32_H

#ifdef __cplusplus
extern "C" {
#endif

#include "system_types.h"
#include "usb_proto_dispatch.h"

/* Chamar antes da usbProtoTask atender comandos. Os ponteiros sao os
 * mesmos passados ao Control_Loop e a UiBackendStm32_Bind(). */
void UsbActionsStm32_Bind(SystemRuntime_t * runtime, const SystemConfig_t * config);

/* Tabela para UsbProtoDispatchCtx_t.actions. */
const UsbProtoActions_t * UsbActionsStm32_Get(void);

#ifdef __cplusplus
}
#endif

#endif /* USB_ACTIONS_STM32_H */
