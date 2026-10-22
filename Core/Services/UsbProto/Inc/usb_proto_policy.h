/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-22
 *
 * Fase 7.3: regras de "qual comando de controle via USB e' aceito em qual
 * estado da maquina". C puro (so' system_types.h, folha sem dependencias),
 * testado no host — quem executa a acao de verdade (Control_Loop, RTC) e' o
 * glue em Core/App/Usb_Port, que consulta estas funcoes antes.
 *
 * Tabela (estado -> resultado):
 *
 *   comando             | IDLE | INFLATING/STABILIZING/STABILIZED | DEFLATING_FAST | FAULT | BOOT/CONFIG/LEAK_TEST
 *   --------------------+------+----------------------------------+----------------+-------+----------------------
 *   START_TEST          | OK   | ESTADO_INVALIDO                  | ESTADO_INV.    | E.INV | ESTADO_INVALIDO
 *   ABORT_TEST          | OK*  | OK + pede abort                  | OK*            | OK*   | ESTADO_INVALIDO
 *   SET_SETPOINT        | OK   | ESTADO_INVALIDO                  | ESTADO_INV.    | E.INV | ESTADO_INVALIDO
 *   SET_DATETIME        | OK   | ESTADO_INVALIDO                  | ESTADO_INV.    | E.INV | ESTADO_INVALIDO
 *   SET_OPERATOR        | OK   | ESTADO_INVALIDO                  | ESTADO_INV.    | E.INV | ESTADO_INVALIDO
 *
 *   OK* = nada a fazer (ja' despressurizado ou desinflando): responde OK
 *   para o PC poder mandar ABORT a qualquer momento sem tratar erro —
 *   "desinflar sempre tem prioridade" (Parte A, seguranca 5).
 *   BOOT/CONFIG/LEAK_TEST: o equipamento esta' sob controle local (IHM nos
 *   Ajustes / teste de vazamento, que tem o proprio fluxo de saida) — o PC
 *   nao interfere.
 *
 * Ajustes (setpoint, hora, operador) so' em IDLE: mudar o setpoint com o
 * manguito inflado ou a hora no meio de um ensaio quebraria o registro do
 * ciclo em andamento.
 */
#ifndef USB_PROTO_POLICY_H
#define USB_PROTO_POLICY_H

#include <stdint.h>
#include "system_types.h"
#include "usb_proto_dispatch.h"

#ifdef __cplusplus
extern "C" {
#endif

UsbProtoRspStatus_t UsbProtoPolicy_StartTest(SystemState_t state);

/* *raise_abort = 1 quando o chamador deve sinalizar o abort ao controle
 * (mesmo flag do pedal/bt_user, consumido no proximo tick de 1 ms). */
UsbProtoRspStatus_t UsbProtoPolicy_AbortTest(SystemState_t state, uint8_t *raise_abort);

/* SET_SETPOINT, SET_DATETIME e SET_OPERATOR. */
UsbProtoRspStatus_t UsbProtoPolicy_IdleOnly(SystemState_t state);

/* Faixa do setpoint: [0, max_setpoint_mmhg]. Fora → FORA_DE_FAIXA (o PC
 * recebe a recusa, nao um valor "ajustado" em silencio como no encoder). */
UsbProtoRspStatus_t UsbProtoPolicy_Setpoint(float setpoint_mmhg, float max_setpoint_mmhg);

#ifdef __cplusplus
}
#endif

#endif /* USB_PROTO_POLICY_H */
