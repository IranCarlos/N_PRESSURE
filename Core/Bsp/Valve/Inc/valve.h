/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-09-20
 *
 * Abstracao das 3 valvulas solenoides (V1/V2 = inflacao/alivio, V3 = porta do
 * manometro externo de calibracao). Logica pura: o acesso ao pino e' injetado
 * (Valve_Hw_t), entao compila no host. Binding STM32 em valve_gpio.c.
 *
 * Hoje: valvulas NA (normalmente abertas), pino em LOW = aberta. `open_level`
 * concentra essa polaridade — trocar para NF e' mudar UM campo.
 */
#ifndef VALVE_H
#define VALVE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { VALVE_1 = 0, VALVE_2 = 1, VALVE_3 = 2, VALVE_COUNT = 3 } Valve_Id_t;
typedef enum { VALVE_CLOSED = 0, VALVE_OPEN = 1 } Valve_State_t;

typedef void (*Valve_WriteFn)(void *ctx, Valve_Id_t id, uint8_t pin_level);

typedef struct {
    Valve_WriteFn write;
    void         *ctx;
    uint8_t       open_level; /* nivel do pino (0/1) que ABRE a valvula */
} Valve_Hw_t;

/* Registra o hardware e leva TODAS as valvulas ao estado aberto (seguro). */
void Valve_Init(const Valve_Hw_t *hw);

/* 0 = ok, -1 = id invalido ou modulo sem Init. */
int Valve_Set(Valve_Id_t id, Valve_State_t state);
Valve_State_t Valve_Get(Valve_Id_t id);

/* v_open != 0 abre. Ordem: V1, V2, V3. */
void Valve_SetAll(uint8_t v1_open, uint8_t v2_open, uint8_t v3_open);

/* Estado seguro: V1+V2+V3 abertas. */
void Valve_OpenAll(void);

#ifdef __cplusplus
}
#endif

#endif /* VALVE_H */
