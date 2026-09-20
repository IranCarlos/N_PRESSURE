# test/ — Testes de host (Unity/Ceedling ou GoogleTest)

Ainda não há testes de host porque nenhum módulo em `Core/Services`/`Core/App` é compilável fora
do alvo ARM hoje: `control_loop.h` inclui `stm32h7xx_hal.h` diretamente (usa `GPIO_TypeDef*` e
`TIM_HandleTypeDef*` nas assinaturas), e o mesmo vale para os outros módulos de `Services`. Isso é
intencional por ora — ver `CLAUDE.md`, seção "Camadas": `Services` não deve incluir `main.h`, mas
ainda depende dos *tipos* da HAL.

Pré-requisito para este diretório ganhar conteúdo real (Fase 3 do prompt de projeto,
`Docs/PROMPT_PROJETO_STM32H723_MANGUITO.md`): extrair a lógica pura de `Control_Loop` (PID,
máquina de estados, sem chamadas de HAL) para que o mesmo `.c` compile tanto para ARM quanto para
o host, contra o simulador de planta em `tools/sim/`. A partir daí:

- `unit/` — testes unitários por módulo (máquina de estados, parser de protocolo quando existir,
  formatadores de log, hash chain).
- `sim/` — critérios de aceitação automáticos do simulador de planta (overshoot, tempo ao
  setpoint, erro em regime, ausência de *chattering*), alimentando `Docs/dev/sim-report.md`.

Ver `Docs/dev/00-auditoria.md` para o estado atual do projeto.
