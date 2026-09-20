/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-09-20
 */

#include "supervisor.h"

/* Acumulada pelas tarefas criticas, consumida pela safetyTask. Acesso sempre
 * por __atomic_* (LDREX/STREX no Cortex-M7) — sem mutex e sem secao critica,
 * entao nenhuma tarefa de prioridade baixa segura o check-in de uma de
 * prioridade alta. */
static volatile uint32_t s_checkin_mask;

void Supervisor_CheckIn(Supervisor_Task_t task) {
    if ((uint32_t)task < (uint32_t)SUPERVISOR_TASK_COUNT) {
        (void)__atomic_fetch_or(&s_checkin_mask, (1u << (uint32_t)task), __ATOMIC_RELAXED);
    }
}

uint32_t Supervisor_CollectCheckIns(void) {
    return __atomic_exchange_n(&s_checkin_mask, 0u, __ATOMIC_RELAXED);
}

uint8_t Supervisor_AllCheckedIn(uint32_t collected_mask) {
    return ((collected_mask & SUPERVISOR_ALL_TASKS_MASK) == SUPERVISOR_ALL_TASKS_MASK) ? 1u : 0u;
}
