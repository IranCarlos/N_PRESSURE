/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-09-20
 */

#ifndef SUPERVISOR_H
#define SUPERVISOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file supervisor.h
 * @brief Check-in de tarefas criticas para o watchdog (Parte A/B do prompt de projeto).
 *
 * O IWDG so' pode ser alimentado (pela safetyTask) quando TODAS as tarefas
 * criticas fizeram check-in dentro da janela de avaliacao. Uma tarefa
 * critica travada (deadlock, loop infinito, starvation) deixa de fazer
 * check-in, o watchdog nao e' alimentado, e a MCU reseta — caindo no estado
 * seguro por hardware (valvulas NA, ver Docs/dev/hardware.md, H4).
 *
 * Tarefas criticas: as que sustentam o controle e a maquina de estados.
 * UI, USB, storage e audio ficam DE FORA de proposito (Parte A, item 6:
 * falha desses modulos nao pode afetar o controle nem, por consequencia,
 * causar reset do equipamento).
 *
 * Nao depende de HAL nem de RTOS: so' operacoes atomicas do GCC, entao e'
 * testavel no host quando o diretorio test/ ganhar conteudo (Fase 3).
 */

/** @brief Tarefas monitoradas. Cada uma ocupa um bit da mascara de check-in. */
typedef enum {
    SUPERVISOR_TASK_CONTROL = 0,
    SUPERVISOR_TASK_INPUT,
    SUPERVISOR_TASK_APP,
    SUPERVISOR_TASK_COUNT
} Supervisor_Task_t;

/** @brief Mascara com todas as tarefas criticas (todos os bits de check-in). */
#define SUPERVISOR_ALL_TASKS_MASK ((1u << (uint32_t)SUPERVISOR_TASK_COUNT) - 1u)

/**
 * @brief Registra que a tarefa esta viva. Chamar uma vez por ciclo do loop
 *        da tarefa. Seguro entre tarefas/ISRs (operacao atomica).
 */
void Supervisor_CheckIn(Supervisor_Task_t task);

/**
 * @brief Le e zera, atomicamente, a mascara de check-ins acumulada desde a
 *        ultima chamada. Deve ser chamada so' pela safetyTask, uma vez por
 *        janela de avaliacao.
 *
 * @return Mascara de tarefas que fizeram check-in na janela.
 */
uint32_t Supervisor_CollectCheckIns(void);

/**
 * @brief Retorna 1 se a mascara coletada contem todas as tarefas criticas.
 */
uint8_t Supervisor_AllCheckedIn(uint32_t collected_mask);

#ifdef __cplusplus
}
#endif

#endif /* SUPERVISOR_H */
