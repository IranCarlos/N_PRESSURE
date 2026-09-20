# Tabela de interrupções (NVIC)

> Fonte: `N_PRESSURE.ioc` e `HAL_NVIC_SetPriority()` gerados em `Core/Src/main.c` /
> `stm32h7xx_hal_msp.c`, conferidos em 2026-09-20. Prescrito pela Parte B, seção 3 do prompt de
> projeto ("documentar a tabela NVIC completa"). Atualizar sempre que uma interrupção nova entrar.

## Regras

- `NVIC_PRIORITYGROUP_4`: 4 bits de preempção (16 níveis, **0 = mais urgente, 15 = menos urgente**),
  0 bits de sub-prioridade. A coluna "sub" é sempre 0.
- FreeRTOS (`FreeRTOSConfig.h`): `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5`,
  `configLIBRARY_LOWEST_INTERRUPT_PRIORITY = 15`.
- **Uma ISR só pode chamar API do RTOS (inclusive as `...FromISR`) se sua prioridade numérica for
  ≥ 5.** ISR com prioridade 0–4 fica acima do que o kernel consegue mascarar; chamar o RTOS de lá
  corrompe o kernel sem aviso. Nenhuma ISR de periférico do projeto está nessa faixa (correto — todas
  ≥ 5).
- `SysTick` e `PendSV` ficam em 15 (o kernel exige isso). O timebase do HAL é o `TIM6` (também 15) —
  o `SysTick` pertence ao FreeRTOS.
- Toda ISR nova: escolher ≥ 5, curta e determinística (alvo < 5 µs), só `...FromISR`, sem lógica de
  negócio (Parte A).

## Exceções do núcleo

| Exceção | Prioridade | Observação |
|---|---|---|
| NMI, HardFault, MemManage, BusFault, UsageFault, DebugMon | 0 (fixas/máximas) | `HardFault_Handler` desvia para `FaultManager_CaptureHardFault()` (grava o quadro de exceção nos registradores de backup do RTC e reseta) |
| SVCall | 0 | usado pelo FreeRTOS só para iniciar o escalonador |
| PendSV | 15 | troca de contexto do FreeRTOS |
| SysTick | 15 | tick do FreeRTOS (1 kHz) |

## Periféricos

| IRQ | Prio | Pode chamar RTOS? | Dono / uso | Observações |
|---|---|---|---|---|
| `TIM6_DAC_IRQn` | 15 | sim | Timebase do HAL (`HAL_IncTick`, 1 ms) | só incrementa `uwTick`, não chama RTOS |
| `TIM3_IRQn` | 5 | sim | PWM da bomba (TIM3 CH1) | hoje só o PWM de hardware, sem lógica em ISR |
| `TIM4_IRQn` | 5 | sim | Captura de entrada do pedal (TIM4 CH1) | pedal ativo-alto, borda de subida (H2, `hardware.md`) |
| `ADC_IRQn` | 5 | sim | ADC2 — pressão (PB1) | DMA circular; sem uso no modo bring-up |
| `DMA1_Stream0_IRQn` | 5 | sim | DMA do ADC2 | |
| `DMA1_Stream1_IRQn` | 10 | sim | DMA TX do SPI1 (display) | callback `HAL_SPI_TxCpltCallback` chama `lv_display_flush_ready` |
| `SPI1_IRQn` | 10 | sim | SPI1 — display ST7789 | |
| `SDMMC1_IRQn` | 10 | sim | Cartão SD | sem uso pela aplicação ainda (Fase 5) |
| `I2C1_EV_IRQn`, `I2C1_ER_IRQn` | 10 | sim | EEPROM CAT24C512 | |
| `OTG_HS_IRQn` | 5 (ver nota) | sim | USB OTG HS (FS embutido), device + host | |
| `OTG_HS_EP1_IN_IRQn`, `OTG_HS_EP1_OUT_IRQn` | 5 | sim | USB OTG — endpoint 1 dedicado | |
| `OTG_HS1_IRQn` | 10 | sim | USB OTG (instância virtual do CubeMX) | |

**Nota:** o código gerado chama `HAL_NVIC_SetPriority(OTG_HS_IRQn, ...)` duas vezes em
`stm32h7xx_hal_msp.c` (uma com 10, outra com 5, nos `MspInit` de device e de host do modo
dual-role); vale a última chamada executada. Ambos os valores são seguros (≥ 5), então não há
risco para o RTOS — só reavaliar quando o USB (Fase 7) escolher o papel em tempo de execução.

## Latência do pedal (requisito ≤ 2 ms, Parte B 2.2)

O pedal entra por captura de entrada do `TIM4` (prioridade 5, no limiar do RTOS), não por EXTI como
o prompt supunha. Isso serve ao requisito porque a captura registra o instante exato da borda em
hardware; a decisão de abrir as válvulas depende do que a ISR/tarefa fizer depois. **A latência real
ainda não foi medida** — medir com o `PortDWT` e um GPIO de debug na Fase 4 (bancada).
