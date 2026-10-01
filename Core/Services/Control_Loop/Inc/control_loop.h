/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-07-24
 * Modified: 2026-08-03
 */

/**
 * @file control_loop.h
 * @Name: Iran Sousa
 * @Company: Dynaminds Electronics
 * @Contact: dynamindelectronics@gmail.com
 * @Created: 2026-07-24
 * @Modified: 2026-07-28
 *
 * @copyright Copyright (c) 2026
 *
 * @details Este módulo implementa a lógica de controle de pressão (PID + Feedforward),
 * gerenciamento dos atuadores (bomba, válvulas) e a máquina de estados principal
 * do equipamento (IDLE, INFLATING, STABILIZED, etc.).
 * A execução é não-bloqueante, projetada para rodar no super loop.
 */

#ifndef CONTROL_LOOP_H
#define CONTROL_LOOP_H

#include "stm32h7xx_hal.h"
#include "system_types.h"
#include "control_core.h" /* ControlCore_Error_t (ver ControlLoop_GetLastError) */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Par porta/pino de uma valvula, injetado pelo chamador.
 *
 * Mantem este modulo (camada Services) sem depender de main.h (camada
 * top-level, especifica desta placa) — quem conhece os pinos fisicos
 * (main.c) os passa para ca, em vez deste modulo hardcodar as macros do
 * CubeMX. Mesmos pinos fisicos de antes, so muda a direcao da dependencia.
 */
typedef struct {
    GPIO_TypeDef* port;
    uint16_t      pin;
} ControlLoop_GpioPin_t;

/** @brief Pinos dos 3 solenoides controlados pela malha. */
typedef struct {
    ControlLoop_GpioPin_t valve1; /**< Valvula 1 (inflacao/alivio). */
    ControlLoop_GpioPin_t valve2; /**< Valvula 2 (inflacao/alivio). */
    ControlLoop_GpioPin_t valve3; /**< Valvula 3 — uso exclusivo manual (manometro externo de calibracao). */
} ControlLoop_ValvePins_t;

/**
 * @brief Inicializa a malha de controle.
 *
 * Configura os ponteiros de configuração, inicializa o estado do PID e dos atuadores.
 * Inicia o canal de PWM para o controle da bomba.
 *
 * @param p_sys_cfg Ponteiro para a estrutura de configuração do sistema (carregada da EEPROM).
 * @param p_htim_pwm Ponteiro para o handle do Timer usado para o PWM da bomba.
 * @param p_runtime_data Ponteiro para a estrutura de dados de tempo de execução.
 * @param p_valve_pins Pinos das 3 valvulas (porta/pino), definidos pela placa em main.c.
 */
void ControlLoop_Init(SystemConfig_t* p_sys_cfg, TIM_HandleTypeDef* p_htim_pwm,
                       SystemRuntime_t* p_runtime_data, const ControlLoop_ValvePins_t* p_valve_pins);

/**
 * @brief Função de atualização da malha de controle (máquina de estados).
 *
 * Deve ser chamada continuamente no super loop (while(1)).
 * Gerencia os estados do sistema, executa o PID em intervalos fixos e
 * atualiza os atuadores.
 */
void ControlLoop_Update(void);

/**
 * @brief Maior jitter (em ciclos de CPU) ja observado entre chamadas
 *        consecutivas de ControlLoop_Update(), frente ao periodo nominal
 *        de 1 ms (Fase 3/4: "registrar jitter maximo... e expor via
 *        diagnostico"). Dividir por (SystemCoreClock/1000000) para us.
 */
uint32_t ControlLoop_GetMaxJitterCycles(void);

/**
 * @brief Ultimo erro latched pelo nucleo do ensaio automatico (Control_Core)
 *        — ver ControlCore_Error_t em control_core.h (subconjunto do
 *        Apendice D: E001/E002/E003/E004/E013). CONTROL_ERR_NONE se nenhum.
 */
ControlCore_Error_t ControlLoop_GetLastError(void);

/**
 * @brief Define um novo setpoint de pressão para o controlador.
 *
 * @param setpoint_mmhg O valor de pressão desejado, em mmHg.
 */
void ControlLoop_SetSetpointMmHg(float setpoint_mmhg);

/**
 * @brief Inicia o ciclo de insuflação.
 *
 * Muda o estado do sistema para SYSTEM_STATE_INFLATING.
 */
void ControlLoop_StartInflation(void);

/**
 * @brief Coloca o sistema em modo de configuração.
 *
 * Pausa a malha de controle e desliga os atuadores para permitir
 * a edição segura dos parâmetros.
 */
void ControlLoop_EnterConfigMode(void);

/**
 * @brief Retoma a operação normal após sair do modo de configuração.
 *
 * Retorna o sistema para o estado IDLE.
 */
void ControlLoop_ExitConfigMode(void);

/**
 * @brief Forca o sistema para o estado seguro (SYSTEM_STATE_FAULT).
 *
 * Estado seguro conforme definido em CLAUDE.md: motor OFF (PWM=0) e as 3
 * valvulas (V1+V2+V3) abertas, aplicado imediatamente (nao espera o proximo
 * ControlLoop_Update()). Chamar de qualquer ponto de deteccao de falha
 * (sobrepressao, sensor invalido, timeout de fase, watchdog, perda de
 * heartbeat em modo manual) em vez de manipular valvulas/bomba diretamente.
 * O sistema so sai de SYSTEM_STATE_FAULT por intervencao explicita (ainda
 * nao implementada — depende do fluxo de reconhecimento de erro da IHM/DEV).
 */
void ControlLoop_EnterFaultState(void);

/**
 * @brief Inicia a rotina de teste de vazamento.
 */
void ControlLoop_StartLeakTest(void);

/**
 * @brief Aplica comando manual de atuadores para testes na tela de configuracao.
 *
 * Este comando so deve ser usado durante o modo de configuracao.
 *
 * @param pump_power Potencia da bomba no intervalo [0.0, 1.0].
 * @param valve1_on 1 para ligar a valvula 1, 0 para desligar.
 * @param valve2_on 1 para ligar a valvula 2, 0 para desligar.
 * @param valve3_on 1 para ligar a valvula 3 (calibracao/manometro externo), 0 para desligar.
 */
void ControlLoop_SetManualActuators(float pump_power, uint8_t valve1_on, uint8_t valve2_on, uint8_t valve3_on);

/**
 * @brief Desativa o modo manual de teste e desliga bomba/valvulas.
 */
void ControlLoop_DisableManualActuators(void);

#ifdef __cplusplus
}
#endif

#endif /* CONTROL_LOOP_H */
