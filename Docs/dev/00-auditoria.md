# 00 — Auditoria do repositório (Fase 0)

> Gerado conforme Parte C / Fase 0 do `Docs/PROMPT_PROJETO_STM32H723_MANGUITO.md`.
> Nenhum arquivo de código foi alterado para produzir este documento.

## Nota sobre nomes de pasta

O prompt-mestre pede uma pasta `docs/` (novos documentos do projeto) distinta de `Docs/`
(já existente, não alterar). No filesystem deste repositório (Windows/NTFS, *case-insensitive*
mas *case-preserving*), `docs/` e `Docs/` são **a mesma pasta** — não é possível ter as duas.
Decisão adotada: os documentos gerados pelo agente ficam em **`Docs/dev/`** (esta subpasta é
nova; nada em `Docs/` fora dela foi tocado). Ajustar as referências do Apêndice F/prompt
mentalmente: `docs/x.md` → `Docs/dev/x.md`.

## 1. Build system e toolchain

- CMake + Ninja + `arm-none-eabi-gcc` (GNU 14.3.1), toolchain file `cmake/gcc-arm-none-eabi.cmake`,
  presets `Debug`/`Release` em `CMakePresets.json`. **Não é CubeIDE** — já migrado para CMake
  (o item "preferir migrar para CMake" da Fase 1 do prompt **já está feito**).
- `.ioc` (`N_PRESSURE.ioc`) presente e é a fonte dos `MX_*_Init()`; código de usuário vive nos
  marcadores `USER CODE BEGIN/END` de `Core/Src/main.c`, `stm32h7xx_it.c`, `stm32h7xx_hal_msp.c` —
  fluxo de regeneração já documentado em `CLAUDE.md`.
- **Build atual (Debug) compila sem erros.** Warnings: apenas 4, todos
  `-Wunused-variable` em `main.c` (variáveis globais do caminho de produção — `g_system_cfg`,
  `g_runtime_data`, `g_last_control_ms`, `g_last_hmi_ms` — não usadas porque
  `APP_MODE_BRINGUP=1` ainda está ativo e o `#else` de produção é código morto).
- **Não há `-Wall -Wextra -Wconversion -Wshadow -Wundef -Wdouble-promotion -Wformat=2 -Werror`**
  configurado em nenhum CMakeLists — são flags exigidas pela Parte A e precisam ser adicionadas
  na Fase 1. Ligar tudo de uma vez no código atual provavelmente vai gerar uma quantidade grande
  de avisos (LVGL/HAL vendorizados inclusive) — decidir se os warnings de `Core/ThirdParty/lvgl`
  e `Drivers/` ficam isentos (`-Wno-error` local) ou se as flags só valem para `Core/App`,
  `Core/Bsp`, `Core/Services`, `Core/Common`.
- `cppcheck --addon=misra`, `clang-tidy` e alvo `make check` **não existem ainda**.
- Uso de memória (Debug, build atual):

  | Região | Usado | Capacidade | % |
  |---|---|---|---|
  | DTCMRAM | 76.760 B | 128 KB | 58,6% |
  | RAM_D1 (AXI SRAM) | 15 KB | 320 KB | 4,7% |
  | RAM_D2 | 0 | 32 KB | 0% |
  | RAM_D3 | 0 | 16 KB | 0% |
  | ITCMRAM | 0 | 64 KB | 0% |
  | FLASH | 389.940 B | 1 MB | 37,2% |

  DTCMRAM já em 58,6% majoritariamente por `.bss`/`.data` (padrão do linker) — folga real para
  pilhas/filas de um RTOS deve ser medida de novo depois de sair do modo bring-up.
- `malloc/free`: nenhuma ocorrência encontrada em código hand-written (LVGL vendorizado também
  não usa heap dinâmico neste projeto — não verificado exaustivamente linha a linha, mas
  `lv_conf.h` deste port não habilita `LV_MEM_CUSTOM`; revisitar na Fase 1/2).

## 2. RTOS

**Não há FreeRTOS nem qualquer RTOS no repositório.** A arquitetura atual é um *super loop*
cooperativo, não-bloqueante, com agendamento por comparação de `HAL_GetTick()` (ver `main.c`,
seção `while(1)`). Isso é uma **divergência grande** em relação à Parte B, seção 3, que assume
FreeRTOS via CMSIS-RTOS2 com 8 tarefas priorizadas. Ver decisão **D1** no Apêndice G adaptado.

## 3. Estado do que existe e funciona

Estrutura de camadas (`App → Services → Bsp → Drivers`, `Common` como folha) já implantada e
documentada em `CLAUDE.md`. Resumo por módulo:

| Módulo | Local | Estado |
|---|---|---|
| Display ST7789 | `Core/Bsp/ST7789` | Driver SPI **por polling** (não DMA ainda), 320×240 landscape, `RAMWR`/`CASET`/`RASET` implementados. Contador de erro de SPI exposto (`ST7789_GetSpiErrorCount`). |
| LVGL v9 | `Core/ThirdParty/lvgl` + `Core/Bsp/LvglPort` | Vendorizado, com 1 patch local documentado (`lv_palette.c`, leitura desalinhada). `lv_port_disp.c` já usa `HAL_SPI_Transmit_DMA` no flush (apesar do driver base do painel ainda ser por polling no bring-up). Só uma tela de teste (arco) existe; nenhuma tela de produto. |
| Encoder | `Core/Bsp/Encoder` | TIM2 em modo encoder de hardware (`TIM_ENCODERMODE_TI1`, only-TI1 — **não é quadratura completa TI12**, ver H1 abaixo), sem filtro de entrada (`IC1Filter=0`, `IC2Filter=0`). Clique curto/longo não confirmados no header rapidamente — a verificar na Fase 2. |
| Pedal | `Core/Bsp/Pedal` | TIM4 input capture, `ICFilter=10` (filtro de hardware ativo), órfão do bring-up atual (só usado no `#else` de produção). Ver H2 (polaridade/borda). |
| Buzzer | `Core/Bsp/Buzzer` | **GPIO bit-bang** (confirma dúvida do Apêndice G item 5: não é PWM de timer nem DAC/I2S) — `Buzzer_Beep(freq_hz, duration_ms)`. Sem suporte a `.wav`; Apêndice B (áudio de boot via PCM) não é viável sem trocar de hardware/abordagem. |
| Sensor de pressão | `Core/Services/ADC_Acq` | ADC2 canal 5 (`PB1`), **entrada analógica de tensão**, resolução 16 bits, oversampling ×4. Não é sensor digital (I2C/SPI) — é um transdutor analógico de saída em tensão de fabricante desconhecido (ver H3). Conversão para kPa/mmHg feita em `ADC_Acq_ConvertPressure` com `offset_kpa`/`scale` (1 ponto, não os 2-3 pontos do Apêndice G/2.9). |
| Malha de controle | `Core/Services/Control_Loop` | PID+feedforward único (sem separação explícita rampa-grossa/aproximação-fina da Parte B seção 2.3) — ver H4. Estados: `IDLE, INFLATING, STABILIZING, STABILIZED, DEFLATING_FAST, LEAK_TEST, CONFIG, FAULT` (FAULT existe no enum mas **nenhum handler o usa** — sem transição automática para lá). V1/V2 usadas para alívio de sobrepressão com pulsos fixos de 1 ms; **V3 nunca é aberta automaticamente** (só via `ControlLoop_SetManualActuators`) — diverge do requisito de segurança "V1+V2+V3 abertas" no estado seguro (ver conflito C1). |
| EEPROM | `Core/Services/EEPROM` (3 camadas: `eeprom.c` genérico → `eeprom_port_stm32_hal.c` → `eeprom_storage_io.c`) | Mapa de endereços com 1 `#define` por campo, `magic`/`layout_version`/`crc32` reservados em `SystemConfig_t`. Endereço I2C do chip é parametrizado (`addr_pins`), não fixo no código — modelo do chip físico não identificado (ver H5). Cópia dupla A/B do Apêndice de segurança da Parte A **não implementada** (só um bloco). |
| HMI/UI | `Core/App/HMI_UI` | Facade (`hmi_ui.c`), grupo de foco do encoder (`hmi_encoder_group.c`), esqueleto de tela principal e de configuração — sem popups, sem menu completo, sem pt-BR ainda (a validar na Fase 6). |
| USB | Só a inicialização CubeMX (`MX_USB_OTG_HS_PCD_Init` + `MX_USB_OTG_HS_HCD_Init` gerados) | Nenhuma classe USB (CDC) implementada. Ver H6/H7 sobre papel real do hardware (device puro vs. dual-role). |
| SD/FatFS | `MX_SDMMC1_SD_Init`, `MX_FATFS_Init`, `Middlewares/Third_Party/FatFs` vendorizado | Inicialização gerada, **nenhum uso de aplicação** (sem `storage_task`, sem gravação de ensaios/logs). Barramento já configurado 4-bit. |
| RTC | — | **Não configurado no `.ioc`.** Nenhum `RTC.*`, `LSE`, `VBAT` encontrado. Bloco de segurança "nunca rodar ensaio com RTC inválido" não tem RTC para validar ainda — ver H8/D2. |
| Fault manager / auditoria / hash chain / rastreabilidade | — | Não existem. |
| Simulador de planta / testes de host / apps de PC / protocolo USB | — | Não existem (`pc/`, `tools/`, `test/` não existem no repo). |

## 4. `Docs/` (especificação de logs) — extração de campos

Arquivo-fonte: `Docs/Trilha eletrônica de auditoria e rastreabilidade dos ciclos pneumáticos.md`
(ver `Docs/dev/formato-logs.md` para a especificação de campos extraída e o mapeamento para o
Apêndice C/D do prompt-mestre).

**Conflitos entre `Docs/` (fonte primária, conforme instrução da Parte B) e o prompt-mestre:**

- **C1 — Nomenclatura de campos diverge**: `Docs/` usa `ID000001` (6 dígitos, prefixo `ID`),
  `P001` (participante/paciente), `C1/C2/C3` (ciclo), `NC00..NC08` (não-conformidade). O prompt-mestre
  (Apêndice C/D) usa `T00000123` para arquivos de ensaio e `E00x` para erros de firmware —
  **são coisas diferentes**: `NC0x` é resultado clínico do ciclo (conformidade), `E0xx` é erro
  técnico do equipamento. Proposta: manter ambos, sem fundir — um ensaio pode ser tecnicamente
  bem-sucedido (`E000`/sem erro) e ainda assim `Não conforme` (`NC01..NC08`) por critério clínico
  de tolerância. **Precisa confirmação (pergunta P1 no Apêndice G).**
- **C2 — Limites operacionais**: `Docs/` já define limites concretos (overshoot ≤105% do alvo,
  banda ±2% no platô, insuflação ≤10 s, platô ≤10 s, esvaziamento 1–2 s, residual <5 mmHg) que
  substituem boa parte dos "a definir" do Apêndice E do prompt-mestre. Adotados como valores
  iniciais da fase piloto (a tabela do `Docs/` já avisa que são preliminares).
- **C3 — Identificador do participante**: `Docs/` pede `P001, P002...` (pseudonimizado) — mais
  simples que o "operador" do prompt-mestre (cadastro de operador do menu de configuração).
  São conceitos diferentes: **operador** (quem opera o equipamento) vs. **participante/paciente**
  (sujeito do ensaio) — o prompt-mestre não distingue os dois claramente. Precisa confirmação (P2).
- **C4 — Estado seguro do firmware não abre V3** (ver seção 3, linha Control_Loop) enquanto a
  Parte A exige "V1+V2+V3 abertas" no estado seguro. Isso é uma contradição **dentro do próprio
  hardware/uso**: V3 é compartilhada com a porta do manômetro de calibração (uso manual/DEV) — abri-la
  automaticamente em qualquer falha poderia interferir num ensaio de calibração em andamento.
  Precisa decisão (D3).

## 5. Decisões (resolvidas em 2026-09-19)

- **D1 — RTOS: decidido FreeRTOS (CMSIS-RTOS2), estático, sem heap.** Motivo do bare-metal
  anterior (RAM escassa no MCU antigo) não se aplica mais: o H723 tem DTCMRAM (128 KB) + AXI SRAM
  (320 KB) + SRAM D2 (32 KB) + SRAM D3 (16 KB) ≈ 496 KB de RAM utilizável, muito acima do que um
  punhado de tarefas com pilhas estáticas de algumas centenas de bytes a poucos KB cada vai
  consumir. Recomendação mantida e adotada: um RTOS com escalonamento por prioridade **facilita**,
  não dificulta, a garantia do loop de controle de 1 ms determinístico (ele fica sempre a maior
  prioridade e preempta UI/USB/SD automaticamente), o que é mais difícil de garantir à mão num
  super loop cooperativo conforme mais tarefas (USB, SD, áudio, IHM) forem entrando. Migração deve
  ser feita como fase isolada, com o mesmo padrão incremental de bring-up já usado neste
  repositório (`BRINGUP_ENABLE_*`) — subir tarefa por tarefa, medindo jitter do controle
  (`DWT->CYCCNT`) antes/depois de cada uma entrar.
- **D2 — RTC**: confirmado pelo usuário — cristal de 32,768 kHz e bateria/backup existem no
  hardware físico, só falta habilitar no `.ioc`. Instruções exatas de configuração no STM32CubeMX
  entregues ao usuário fora deste documento (ver conversa) — a fazer manualmente na GUI do CubeMX
  e regenerar o projeto (fluxo já documentado em `CLAUDE.md`).
- **D3 — Papel de V3 no estado seguro**: **implementado.** `Core/Services/Control_Loop` ganhou um
  handler real para `SYSTEM_STATE_FAULT` (antes era um caso vazio no switch, nunca atingível) e a
  função pública `ControlLoop_EnterFaultState()`, que abre V1+V2+V3 e zera a bomba imediatamente.
  Critério de "mais claro/fácil manutenção": um único ponto de entrada para o estado seguro,
  chamado por qualquer detector de falha futuro (Fases 3/4: sobrepressão, sensor inválido,
  timeout, watchdog, heartbeat perdido), em vez de cada detector mexer em válvulas/bomba
  diretamente. **Ainda não há nada que chame `ControlLoop_EnterFaultState()`** — a lógica de
  detecção de falhas (P_MAX_ABS, validação de sensor, timeouts) é trabalho da Fase 3/4. Mudança
  não verificada em bancada (regra de segurança do `CLAUDE.md`) — só compilada.
- **D4 — Estrutura de pastas**: confirmado reaproveitar a estrutura atual
  (`Core/App`, `Core/Bsp`, `Core/Services`, `Core/Common`, `Core/ThirdParty`) em vez do layout
  literal do Apêndice F. Ver mapeamento na seção 6.
- **D5 — MISRA/flags de warning**: **implementado em 2026-09-19.** `-Wextra -Wconversion -Wshadow
  -Wundef -Wdouble-promotion -Wformat=2 -Werror` agora se aplicam a **todo** `Core/App`,
  `Core/Bsp` e `Core/Services` (confirmados limpos de uma vez, não precisou ir módulo a módulo —
  só 16 avisos no total, todos truncamentos triviais já corrigidos com casts explícitos em
  `st7789.c`, `lv_port_disp.c`, `lv_port_indev.c` e `ui_config_screen.c`). `Drivers/` e
  `Core/ThirdParty/lvgl` ficam de fora via `set_property(TARGET stm32cubemx PROPERTY SYSTEM TRUE)`
  e `target_include_directories(... SYSTEM ...)` para o include do LVGL — ambos vivem só no
  `CMakeLists.txt` raiz (não regenerado pelo CubeMX), então sobrevivem a `cmake --preset` de novo
  a qualquer momento. cppcheck/clang-tidy (`make check`) ainda não configurados — próximo passo
  de qualidade estática, não bloqueante.

## 5.1 Política de uso das RAMs do H723 (decidida com o usuário)

O H723 tem 5 regiões de RAM utilizáveis, com características diferentes de latência e
acessibilidade por DMA. Política adotada para as próximas fases (nenhum código foi alterado para
isto agora — vale para o que for criado a partir da Fase 1 em diante):

| Região | Tamanho | Latência p/ CPU | Acessível por DMA | Uso definido |
|---|---|---|---|---|
| **DTCMRAM** | 128 KB | mais rápida (0 wait-state, local ao core) | **Não** (DMA1/DMA2/MDMA não alcançam) | Default do linker (`.data`/`.bss`) — variáveis globais, `SystemConfig_t`/`SystemRuntime_t`, estado do PID/máquina de estados, pilhas de tarefas do RTOS (quando D1 for implementado). Tudo que é só lido/escrito pela CPU no hot-path de 1 ms. |
| **RAM_D1 (AXI SRAM)** | 320 KB | rápida, cacheável | **Sim** (DMA1/DMA2/MDMA) | Já é o destino do macro `DMA_BUFFER` existente (`system_types.h`). Buffers grandes/streaming: linhas/frames do LVGL, buffer duplo da animação de boot, buffers de transmissão SPI do display. |
| **RAM_D2 (SRAM1-3)** | 32 KB | rápida | **Sim** (DMA1/DMA2, mesma malha que D1) | Segunda região de DMA — usar quando D1 estiver sob pressão de outro periférico simultâneo (ex.: separar buffer do ADC/curva de pressão do ensaio, que é escrita continuamente, do buffer do display, que é rajada). Também um candidato natural para buffers do SDMMC/USB quando esses forem implementados (Fases 5/7), para não competir com o tráfego de vídeo. |
| **RAM_D3 (SRAM4)** | 16 KB | rápida | Só por BDMA (periféricos do domínio D3: LPUART1, SPI6, I2C4, ADC3 — nenhum usado neste projeto hoje) | Não é utilizável por DMA1/DMA2/SDMMC/SPI1/ADC2 deste projeto. Reservar para dados só-CPU de baixa prioridade que aliviam a DTCM (ex.: buffers de montagem de linha de log antes de ir para a fila da `storage_task`), não para nada que precise de DMA. |
| **ITCMRAM** | 64 KB | mais rápida para **código** (fetch de instrução, 0% usado hoje) | N/A (é para código, não dado) | Reservar para as funções mais quentes/críticas de latência: a ISR/função de 1 ms do loop de controle, `run_pid_iteration()`, `set_pump_power()`/`set_valves()`, e o handler de hard fault — via atributo de seção do linker, quando a Fase 3/4 (medição de jitter) mostrar que compensa. Não fazer isso agora sem medir primeiro (o ART cache da flash já entrega desempenho parecido para código pequeno e quente). |

Os macros de seção (`RAM_D2_BUFFER`, `RAM_D3_BUFFER`, atributo de função para ITCM) ainda não
existem em `system_types.h`/no linker script — serão adicionados quando o primeiro buffer real que
precisa de cada região for criado (Fases 5-7), seguindo o mesmo padrão do `DMA_BUFFER` já
existente, para não introduzir infraestrutura sem uso imediato.

## 5.2 Progresso da Fase 1 (2026-09-19)

- **`pc/`, `tools/`, `test/`**: criados com `README.md` explicando o que cada um vai conter e em
  qual fase, sem código ainda (dependem de fases posteriores — protocolo USB para `pc/`,
  simulador/desacoplamento da HAL para `test/`).
- **`Core/Bsp/Port` (novo módulo)**: `port_dwt.h/.c` — contador de ciclos de CPU
  (`DWT->CYCCNT`), para medir jitter do loop de controle e latência pedal→válvula nas Fases 3/4.
  Inicializado em `main()` (`PortDWT_Init()`), sem uso ainda (só disponibilidade).
- **`Core/Services/Fault_Manager` (novo módulo)**: captura de `HardFault` via
  `stm32h7xx_it.c` (trampolim em assembly que extrai o quadro de exceção empilhado) +
  persistência em registradores de backup do RTC (`RTC->BKPxR`, equivalente do H7 à "backup
  SRAM/TAMP" citada na Fase 1 do prompt de projeto — sobrevivem a reset enquanto o VBAT estiver
  presente). No próximo boot, `FaultManager_Init()` consome o registro pendente e a causa de reset
  (`IWDG`/etc. via `RCC->RSR`). `FaultManager_ForceHardFault()` existe para o teste de bancada do
  critério de aceitação ("hard fault forçado é registrado e sobrevive ao reset") — **não é chamado
  em lugar nenhum por padrão**, só deve ser invocado manualmente para esse teste. Ninguém ainda
  consome `FaultManager_GetLastFault()` (sem storage/IHM de erro implementados — Fases 5/6).
- **Bug corrigido no `MX_RTC_Init()` gerado pelo CubeMX**: o código gerado chama
  `HAL_RTC_SetTime`/`SetDate` incondicionalmente a cada boot, com hora fixa (`00:00:00, 01/01`) —
  sem correção, o calendário seria resetado a cada ligamento mesmo com VBAT presente. Corrigido
  usando os marcadores `USER CODE BEGIN/END Check_RTC_BKUP` e `RTC_Init 2` (sobrevive a
  regeneração): grava um magic em `RTC_BKP_DR13` na primeira vez e pula o `SetTime`/`SetDate` nos
  boots seguintes se o magic já estiver lá. Isto é um problema conhecido de todo projeto CubeMX
  com RTC — não específico deste projeto.
- **D3 implementado** (ver seção 5): `ControlLoop_EnterFaultState()` em `Core/Services/Control_Loop`.
- **Flags de warning por módulo** (D5, ver seção 5): implementado.
- Build confirmado limpo (`cmake --build --preset Debug`) depois de cada mudança acima.

**Migração para FreeRTOS (D1) — implementada em 2026-09-19.** CMSIS-RTOS2, alocação estática
(`SUPPORT_STATIC_ALLOCATION=1`; `SUPPORT_DYNAMIC_ALLOCATION` ficou `1` também — ver ressalva
abaixo), 8 tarefas conforme a tabela da Parte B seção 3, timebase do HAL movido de SysTick para
TIM6 (SysTick pertence ao kernel agora), prioridades de NVIC redistribuídas (nada mais em
prioridade 0 exceto TIM6, que não chama API do RTOS). O clock do sistema também mudou nesta
regeneração — **de HSI para HSE** (cristal em `PH0`/`PH1`, confirmado existir fisicamente pelo
usuário), agora rodando a 550 MHz (o máximo nominal do H723 em VOS0) em vez dos 544 MHz
anteriores; `TIM3.Prescaler`/`Period` recalculados para manter exatos 20 kHz de PWM do motor com o
novo clock. A lógica do super loop cooperativo foi migrada para as tarefas correspondentes
(`StartInputTask`, `StartUiTask`, `StartSafetyTask` com conteúdo real; `StartControlTask`/
`StartAppTask` com o código de produção atrás de `#if !APP_MODE_BRINGUP` mas ainda inertes, já que
`ControlLoop_Init`/`HMI_Init` não rodam em bring-up; `StartUsbProtoTask`/`StartStorageTask`/
`StartAudioTask` vazias, aguardando suas fases). `vApplicationStackOverflowHook` chama
`NVIC_SystemReset()` em vez de ficar vazio. Build limpo confirmado do zero.

**Ressalvas conhecidas, não bloqueantes:**
- `configSUPPORT_DYNAMIC_ALLOCATION` ficou `1` (não `0` como o ideal para a regra "sem malloc" da
  Parte A) — o CubeMX linkou `heap_4.c` e reservou 15 KB de heap. Nenhum código nosso usa alocação
  dinâmica (todas as 8 tarefas são `Static`), mas nada impede alguém de introduzir
  `xTaskCreate`/`pvPortMalloc` sem perceber. Revisitar quando o resto dos objetos do RTOS
  (mutexes, filas — nenhum criado ainda) também estiver todo estático.
- **DTCMRAM subiu para 83,4%** (109 KB de 128 KB) com as 8 pilhas de tarefa (~11,7 KB) + heap do
  FreeRTOS (15 KB) + variáveis de bring-up. Ainda cabe, mas a folga ficou pequena — medir de novo
  a cada fase que adicionar buffers grandes (ver política de RAM na seção 5.1); se apertar,
  candidatos a cortar primeiro são o heap do FreeRTOS (se `SUPPORT_DYNAMIC_ALLOCATION` virar `0`)
  e o tamanho da pilha da `uiTask` (1024 *words* = 4 KB, a maior de todas).
- Os ramos de bring-up "desenho cru" (`BRINGUP_ENABLE_LVGL=0`: barra acompanhando o encoder,
  ciclo de cores sólidas) não foram portados para as tarefas — só o caminho atualmente ativo
  (LVGL=1) foi migrado. Reativar aqueles exigiria portar esse desenho para `StartUiTask`/
  `StartInputTask` também (é pouco código, só não foi prioridade agora).
- `safetyTask` alimenta o IWDG incondicionalmente a cada 1 ms (mesmo comportamento de antes) —
  ainda não faz o check-in das tarefas críticas antes de alimentar, como a Parte B pede. Fica para
  quando as tarefas tiverem algo real para checar.

**Bugs encontrados e corrigidos no primeiro flash real (2026-09-19):**
- **Flash latency**: `FLASH_LATENCY_3` (herdado de antes, nunca testado em hardware de verdade)
  causava `IBUSERR` (bus fault de instrução) a ~275 MHz de HCLK. Corrigido via CubeMX (Clock
  Configuration recalculou o valor correto) — não é algo que se corrige no código, já que essa
  chamada é gerada fora de marcadores `USER CODE`.
- **SDMMC1 clock**: também precisou de ajuste de velocidade (mencionado pelo usuário como a causa
  raiz do boot-loop; corrigido junto com o item acima no CubeMX).
- **Buzzer mudo**: `Buzzer_Beep()` continuava escrevendo direto no GPIO de `PC7` via
  `HAL_GPIO_WritePin`, mas `PC7` virou `TIM8_CH2` (função alternada) quando configuramos o PWM do
  buzzer — escrita em GPIO não tem efeito nenhum com o pino em modo AF. Reescrito
  `Core/Bsp/Buzzer` para gerar o tom por PWM de hardware de verdade (recalcula `PSC`/`ARR` do
  TIM8 por chamada, já que a frequência varia; usa `osDelay` em vez de busy-wait bloqueante).
  `Buzzer_Init()` agora recebe o handle do timer (`&htim8`), mesmo padrão de `Encoder_Init`.
- **LVGL travado (gauge não atualiza)**: a troca do timebase do HAL de `SysTick` para `TIM6`
  (necessária pro FreeRTOS) fez o CubeMX **remover a função `SysTick_Handler()` inteira** de
  `stm32h7xx_it.c` — o `lv_tick_inc(1)` que vivia dentro dela (num marcador `USER CODE`) foi
  embora junto, órfão, porque a função-mãe deixou de existir (isso não é preservado em lugar
  nenhum quando uma função CubeMX inteira some do template). Sem tick, o LVGL nunca via o tempo
  passar — desenhava uma vez e travava (nem input nem redraw). Corrigido chamando
  `lv_tick_inc(TASK_LVGL_HANDLER_PERIOD_MS)` direto na `StartUiTask` antes de `lv_timer_handler()`,
  já que essa tarefa sabe exatamente o intervalo (`osDelay`) entre chamadas — não depende mais de
  qual IRQ o CubeMX usar para o timebase.

**Placa "morta" / debug sem conexão (2026-09-20) — fonte de alimentação do núcleo trocada para
bypass:** uma regeneração do CubeMX mudou `RCC.SupplySource` de `PWR_LDO_SUPPLY` (baseline) para
`PWR_EXTERNAL_SOURCE_SUPPLY`, e `SystemClock_Config()` passou a chamar
`HAL_PWREx_ConfigSupply(PWR_EXTERNAL_SOURCE_SUPPLY)`. Isso desliga o LDO interno; como esta placa
(H723VGT6, LQFP100, sem SMPS) alimenta o núcleo pelo LDO, o chip perde a tensão de núcleo assim que
essa linha executa — o GDB server "perdia o alvo" no `cont`. O bit de supply só é limpo por
power-on-reset (não por NRST), então a placa continua morta até cortar **toda** a alimentação.
Corrigido nos três lugares (`.ioc`, `main.c`, `cmake/stm32cubemx/CMakeLists.txt`) e adicionada uma
trava `#error` em `main.c` (marcador `USER CODE BEGIN PD`, sobrevive à regeneração) que derruba o
build se `USE_PWR_EXTERNAL_SOURCE_SUPPLY`/`USE_PWR_DIRECT_SMPS_SUPPLY` voltar a ser definido.
**Atualização (2026-09-20):** com o LDO restaurado, o usuário reporta 550 MHz estável, sem os
`IBUSERR` (que a 400 MHz só aparecia ao mexer no encoder, isto é, sob carga) — o padrão
"erros de barramento erráticos, sensíveis a clock e carga" é coerente com tensão de núcleo instável
por bypass do LDO. Hipótese de latência de flash descartada (o CubeMX calcula sozinho: 3 WS a
275 MHz, 2 WS a 200 MHz). **Buzzer uma oitava acima**: `Buzzer_Beep` assumia clock do TIM8 =
`SystemCoreClock/2` (válido só com AHB÷2 e APB2÷2); corrigido para ler o clock real do timer no
RCC (`get_tim8_kernel_clock_hz()`), independente dos divisores. Observação: o `main.c` visto em
disco nesta data tinha PLL `M=32/N=129/P=2`, VOS2, divisores ÷1 e `FLASH_LATENCY_1` — inconsistente
com 550 MHz (VCO de entrada ≈0,78 MHz, fora da faixa) — conferir a aba Clock Configuration antes
de gerar/gravar. `TIM3.Period` só dá 20 kHz exatos com clock de timer de 275 MHz (Period 13750);
em outro clock, Period = clock_do_timer / 20000.

**PROBLEMA ABERTO — HardFault `IMPRECISERR` no desenho do arco LVGL (2026-09-20):** ao mexer no
encoder, `uiTask` caía em `circ_calc_aa4` (`lv_draw_sw_mask.c`, LVGL 9.0.0) com bus fault imprecise.
Descartado com medições: estouro de pilha (`uiTask` ≥ 896 B livres de 4096) e esgotamento do heap
do LVGL (`max_used` 7 KB de 63 KB, 1 bloco livre). Como o erro é de **escrita bufferizada**, o M7
não dá o endereço (BFAR inválido) e a instrução apontada é irrelevante. Contramedida de diagnóstico
em `main()` (só builds Debug): `SCnSCB->ACTLR |= (1<<1)` (DISDEFWBUF, desliga o write buffer no mapa
de memória padrão) — com ela o erro **deixou de ocorrer**, o que indica sensibilidade a
ordenação/temporização de escritas, **mas não prova correção** (pode só estar mascarado, e Release
não tem a contramedida). Suspeitas ainda não confirmadas: escrita em RAM sem ECC inicializado
(AXI SRAM `.dma_buffer`, RAMECC), coerência D-Cache/DMA no `disp_flush`, bug do LVGL 9.0.0. Se o
erro voltar: remover a contramedida, reproduzir e ler `RAMECC1/2/3_MxSR/MxFAR` (registradores de
endereço de falha ECC). Instrumentação permanente disponível no Live Watch: `g_task_stack_free_bytes[]`
e `g_lv_mem`.

**Lição para próximas regenerações do CubeMX**: sempre que uma mudança no `.ioc` **remove uma
função de IRQ inteira** de `stm32h7xx_it.c` (não só edita, mas apaga a função), qualquer código
dentro dos marcadores `USER CODE` daquela função é perdido silenciosamente — não aparece como
erro de compilação nem warning. Vale conferir manualmente o diff de `stm32h7xx_it.c` depois de
qualquer regeneração que mexa em clock/timebase/modo de periférico.

**Fechamento da Fase 1 (2026-09-20):**
- **Supervisor de watchdog** (`Core/Services/Supervisor`, novo): o IWDG só é alimentado, a cada
  janela de 100 ms, se as tarefas críticas (`control`, `input`, `app`) fizeram check-in
  (`Supervisor_CheckIn()`, atômico, sem mutex). UI/USB/storage/audio ficam de fora de propósito
  (falha delas não pode derrubar o controle). Estado no Live Watch: `g_supervisor_last_mask`,
  `g_supervisor_missed_windows`. Substitui a alimentação incondicional a cada 1 ms.
- **Teste de bancada do Fault_Manager**: `FAULT_TEST_FORCE_HARDFAULT_MS` em `main.c` (0 = desligado).
  Com N > 0 força um HardFault N ms após o boot; no boot seguinte `g_boot_fault_pending`/
  `g_boot_fault_record` trazem o registro (r0–r3, r12, lr, pc, psr, CFSR, HFSR, MMFAR, BFAR) e o
  `appTask` toca 3 bipes graves. **Ainda não executado em hardware** — é o critério de aceitação
  "hard fault forçado é registrado e sobrevive ao reset".
- **Alvos CMake**: `size` (uso por seção) e `check` (análise estática com `-fanalyzer` do GCC 14
  sobre `Core/App`, `Core/Bsp`, `Core/Services`, com os mesmos warnings estritos + `-Werror`).
  `cppcheck`/`clang-tidy` **não estão instalados** neste ambiente; a análise MISRA
  (`cppcheck --addon=misra`) fica pendente até instalar. `check` achou 1 problema real
  (`FaultManager_ForceHardFault` terminava num laço infinito, CWE-835) — corrigido.
  **Não** há alvo `flash`: `CLAUDE.md` documenta que o flashing é pela extensão STM32Cube do VS
  Code, sem CLI no repositório.
- **Zero warnings** no build completo (Debug e Release). Os 2 avisos de `main.c` (variáveis só do
  caminho de produção) foram resolvidos com `#if !APP_MODE_BRINGUP`; o de `stm32h7xx_hal_msp.c`
  (código do CubeMX, contador USB dual-role) é silenciado só nesse arquivo pelo `CMakeLists.txt`.
- **Tabela de interrupções**: `Docs/dev/irq.md` (nova).
- **Memória (Debug, 550→400 MHz irrelevante para tamanho)**: Flash 423.664 B (40,4%), DTCMRAM
  110.032 B (83,95%), RAM_D1 15 KB (4,7%), ITCM/D2/D3 0. Release (`-Os`): Flash 219.216 B (20,9%),
  DTCMRAM 109.944 B. DTCM segue apertada — folga possível: heap do FreeRTOS (15 KB, sem uso), buffers
  do LVGL.

**Pendências da Fase 1 que dependem de você / do hardware:**
1. Rodar o teste do Fault_Manager (`FAULT_TEST_FORCE_HARDFAULT_MS` > 0) e confirmar o registro após o reset.
2. Decidir sobre o problema aberto do `IMPRECISERR` (seção acima) — `DISDEFWBUF` está só em Debug.
3. "Hello" por SWO/UART (critério de aceitação) — só se o SWO (PB3) estiver ligado no seu conector de debug; não implementado.
4. Instalar `cppcheck` (com addon MISRA) se quiser o relatório MISRA em `Docs/dev/qualidade/`.

## 6. Proposta de estrutura de pastas (reaproveitando o que já existe)

Não renomear o que já funciona; mapear o vocabulário do prompt-mestre para os nomes atuais e só
criar o que falta:

```
/CLAUDE.md                          (já existe)
/README.md                          (não existe — criar na Fase 1/10)
/Docs/                              (já existe — NÃO alterar arquivos-raiz)
/Docs/dev/                          (novo — auditoria, hardware, protocolo, formatos, bancada)
/Core/
  App/HMI_UI/                       (= "ui/" do Apêndice F)
  App/{control_app, bench}/         (novo — orquestração de ensaio / modo bancada; = "app/" restante)
  Services/{Control_Loop,ADC_Acq,EEPROM,...}/   (= "services/")
  Services/{storage,logger,audit_hash,usb_proto,fault_manager,supervisor}/  (novo, Fases 5/7)
  Bsp/{ST7789,Encoder,Buzzer,Pedal,LvglPort,...}/  (= "drivers/" + parte de "port/")
  Common/                           (tipos de domínio compartilhados — sem equivalente direto no Apêndice F, mantém como está)
  ThirdParty/{lvgl,fatfs,...}/      (= "third_party/"; FreeRTOS entraria aqui se D1 for "sim")
/pc/{core,app_user,app_dev,tests}   (novo, Fases 8/9)
/tools/{sim,make_bootanim.py,...}   (novo, Fases 3/6)
/test/                              (novo — testes de host, Fase 1+)
```

`port/` (clock, MPU, cache, NVIC, DWT, fault_manager) do Apêndice F ainda não existe como módulo
próprio — hoje esse código vive espalhado em `main.c`/`stm32h7xx_it.c`. Extrair para
`Core/Bsp/Port` (ou `Core/App/Port`, a definir) é trabalho da Fase 1.

## 7. Informações de hardware que faltam

Ver lista completa e marcações `TODO(HW)` em `Docs/dev/hardware.md`. Resumo dos pontos que
bloqueiam decisões de firmware:

- H1: Encoder configurado só com `TI1` (`TIM_ENCODERMODE_TI1`) — typo/decisão consciente de usar
  só 1 canal (metade da resolução de um encoder de quadratura completo)? Confirmar circuito do
  encoder rotativo (tem 2 canais A/B conectados de fato?).
- H2: Polaridade/borda do pedal (`PEDAL_Pin`, TIM4 IC1, `TIM_INPUTCHANNELPOLARITY_RISING`) — pedal
  é normalmente aberto ou fechado no repouso?
- H3: Modelo/fabricante do sensor de pressão (`PRESSURE_SENSOR_Pin`/`PB1`, ADC2_INP5) — faixa de
  pressão, tensão de saída, linearidade, se é ratiométrico à alimentação.
- H4: Válvulas V1/V2/V3 — NA ou NF? Comportamento do driver de potência (transistor/relé) quando o
  GPIO está em reset/tri-state (antes do `MX_GPIO_Init` rodar) — o código atual escreve nível LOW
  como "aberto" (`set_valves` inverte o sinal); preciso confirmar isso bate com o esquema.
- H5: Modelo do chip EEPROM I2C (endereço passado como parâmetro, não fixo no código) e se há
  segundo chip para cópia A/B redundante.
- H6/H7: PHY USB (embutido FS, confirmado pelo `.ioc`) e propósito real dos pinos `EN_VBUS`/`OC_USB`
  (sugerem uma chave de VBUS com sensor de sobrecorrente — típica de porta **host**, não de
  dispositivo). O prompt-mestre assume "equipamento é USB Device" — confirmar se o hardware físico
  tem essa capacidade de host por outro motivo (ex.: leitor USB auxiliar) ou se é resquício de
  protótipo e pode ser ignorado.
- H8: Existência física de cristal LSE (32,768 kHz) e bateria/supercap de RTC (`VBAT`) na placa —
  não aparecem no `.ioc` atual.
- H9: Resistores CC1/CC2 do conector USB-C (5,1 kΩ para GND, obrigatórios para o host reconhecer
  o dispositivo) — não verificável pelo `.ioc` (é analógico/passivo), preciso do esquemático.

## 8. Riscos

- **Reescrever a malha de controle do zero** (rampa grossa + aproximação fina do Apêndice
  Parte B 2.3) joga fora um PID que já funciona em bring-up parcial — precisa ser feito no
  simulador (Fase 3) antes de tocar no hardware, para não regredir o que já está validado.
- **Migração para FreeRTOS** (se D1 = sim) é a mudança de maior risco do projeto: toca em todos
  os módulos, todas as ISRs, temporização de PWM/válvulas e o watchdog. Deve ser isolada em sua
  própria fase com bring-up incremental (mesmo padrão de `APP_MODE_BRINGUP`/`BRINGUP_ENABLE_*`
  já usado neste repositório).
- **RTC ausente**: qualquer requisito de auditoria que dependa de timestamp confiável fica bloqueado
  até H8 ser respondido e o hardware (se existir o cristal/bateria) ser configurado no `.ioc`.
- **DTCMRAM já em 58,6%** no bring-up atual — pouca folga visível antes de somar pilhas de RTOS,
  buffers de ensaio em RAM e buffers de streaming da animação de boot. Vai precisar de medição
  cuidadosa por fase (`arm-none-eabi-size`, já disponível).
- **Buzzer é GPIO bit-bang**, não PWM/DAC — o plano de áudio de boot via `.wav` (Apêndice B) não é
  viável sem trocar de hardware ou aceitar apenas tons sintéticos (que já é o padrão descrito como
  fallback no próprio Apêndice B).
- **Confusão de nomes `docs/` vs `Docs/`** no filesystem Windows deste projeto — resolvida nesta
  auditoria (seção "Nota sobre nomes de pasta"), mas qualquer instrução futura que cite `docs/`
  literalmente deve ser lida como `Docs/dev/`.

## 9. Critério de aceitação desta fase

- [x] Nenhum arquivo de código alterado (só arquivos novos em `Docs/dev/`).
- [x] `docs/hardware.md` → `Docs/dev/hardware.md` entregue.
- [x] `docs/formato-logs.md` → `Docs/dev/formato-logs.md` entregue.
- [x] Proposta de arquitetura (seção 6).
- [x] Lista de perguntas/decisões objetivas (seção 5, D1–D5) e conflitos (seção 4, C1–C4).

**Aguardando sua aprovação antes de iniciar a Fase 1.**

## Fase 2 — progresso

### 2.1 Sensor de pressao (2026-09-20) — codigo pronto, AGUARDA validacao em bancada
- **Bug encontrado**: ADC2 estava em software-start + DMA one-shot e `ADC_Acq_Init` disparava uma unica
  conversao; `ADC_Acq_Update` filtrava sempre o mesmo valor. Corrigido: TIM15 (PSC 274, ARR 999 = 1 kHz)
  -> TRGO -> ADC2 (borda de subida), DMA circular de 16 amostras (`ADC_ACQ_DMA_SAMPLES`, 32 B em RAM_D1).
- `Core/Services/Pressure_Core` (C puro, sem HAL): contagens -> kPa, baseline atmosferico, calibracao
  (offset/escala + `PressureCore_CalibrateTwoPoint`), flags `OUT_OF_RANGE` (<0.10 V ou >4.90 V no sensor)
  e `STUCK` (500 blocos com pico-a-pico 0). `ADC_Acq` so' cuida de ADC/DMA/cache e alimenta o core.
- Teste de host: `bash test/host/run.sh` (gcc MSYS2 em `C:\msys64\ucrt64\bin`; sem sanitizers no MinGW).
- Bring-up: `BRINGUP_ENABLE_PRESSURE` em `main.c`; Live Watch em `g_pressure_diag`
  (`pressure_kpa`, `last_p2p_counts` = ruido, `flags`).
- Pendente (bancada): medir e documentar o ruido em repouso; calibrar 2 pontos com o manometro em V3.
- Encoder H1: decisao do usuario = TI1 (mantido).
- **Achado de hardware (2026-09-20):** ADC lia 65535 fixo. Causa: pino **VREF+ flutuando (0,32 V)**, sem ligacao
  ao VDDA (3,26 V); VREFBUF fica desligado no reset. Correcao: VREF+ ligado ao VDDA na placa — validado pelo
  usuario (leitura normal apos o reparo). **Corrigir no esquema/PCB da proxima revisao.** Sondas mantidas:
  `g_adc_dbg` (half/full/err), `g_boot_rsr`.
- Reinicios observados antes do reparo cessaram apos zerar/`Clean` do buffer DMA (RAM_D1, ECC) em `ADC_Acq_Init`;
  causa provavel, nao provada (sem repro isolada).
- Pendente: ruido em repouso (`last_p2p_counts`) e calibracao de 2 pontos com o manometro em V3.

### 2.2 Valvulas e bomba (2026-09-20) — codigo pronto, AGUARDA bancada
- `Core/Bsp/Valve` (`valve.c` logica pura + `valve_gpio.c` binding): abstracao de polaridade em UM campo
  (`open_level`; hoje NA, pino LOW = aberta). `Valve_Init` leva tudo a ABERTA. Valvula/id invalido reporta OPEN.
- `Core/Bsp/PwmMotor` (`pwm_motor.c` + `pwm_motor_tim.c`): `CCR = potencia*(ARR+1)`; 100 % agora e' 100 % real
  (antes `potencia*ARR` dava 99,99 %). NaN/negativo = 0; `PwmMotor_SetLimit` para teto de bancada.
- `Control_Loop` passou a usar os dois drivers (mesmo comportamento; a malha continua fora do caminho ativo).
  Ainda nao verificado em bancada — como toda mudanca no Control_Loop, exige verificacao com hardware.
- `Core/App/Bench/actuator_bench.c` (logica pura, testada no host): bancada comandada pelo Live Watch
  (`g_act_arm`, `g_act_pump`, `g_act_valves`). Desarmado = bomba 0 + V1..V3 abertas; potencia <= 30 %;
  bomba continua > 5 s = trip; pressao > 15 kPa ou sensor invalido = trip (latch ate desarmar). Habilita com
  `BRINGUP_ENABLE_ACTUATORS=1` (default 0).
- Testes de host: `bash test/host/run.sh` (pressure_core + valve + pwm_motor + actuator_bench).
- Aberto: como a bomba/driver se comporta com o pino em alta impedancia no reset (as valvulas abrem por serem NA;
  confirmar tambem que a bomba fica desligada por pull-down no hardware).

### 2.2 — resultado da bancada de atuadores (2026-09-30)
- **Sensor de pressao**: reparo do VREF+ confirmado pelo usuario — leitura correta.
- **Achado de hardware (aberto)**: no reset/power-up, valvulas e bomba energizam brevemente mesmo com
  pull-down no hardware. Causa provavel: entre o reset e `MX_GPIO_Init()` (primeira coisa em `main()`
  apos os clocks — ja na ordem mais cedo possivel), os pinos ficam em alta impedancia (estado de reset
  do GPIO) por uma janela de alguns ms (startup + HAL_Init + PLL lock), dependendo so' do pull-down
  externo. Se o pull-down nao segura o pino nessa janela, e' revisao de hardware (valor do resistor,
  ou um estagio de enable controlado por RC/poder-bom), nao corrigivel em firmware alem do que ja'
  esta' feito. **Decisao do usuario: adiar teste/mitigacao mais profunda para quando o software de
  controle (Fase 3/4) estiver pronto.**
- **Bomba**: potencia minima estavel medida em bancada ≈ **30%**. Isso e' MAIOR que
  `PUMP_MIN_COMPENSATION_POWER` (0.2) em `control_loop.c` — com esse valor a malha poderia comandar uma
  potencia que nao gira a bomba. **Revisar na Fase 3/4** (ajuste de PID/feedforward com a bomba real).
  Tambem e' igual ao teto da bancada (`ACTBENCH_PUMP_MAX = 0.30`) — sem faixa de sobra para testar
  controle incremental no bench; aceitavel pois o bench foi despriorizado pelo usuario.

### 2.3 Relogio de tempo real — RTC clocado por LSI, nao LSE (2026-10-01) — BUG encontrado, aguarda fix no CubeMX
- **Achado**: apesar do hardware ter cristal LSE + bateria (confirmado pelo usuario na Fase 0), o
  `PeriphCommonClock_Config()` gerado nunca configura `RCC_PERIPHCLK_RTC` — e o `HAL_RTC_MspInit()`
  (`stm32h7xx_hal_msp.c`) usa `RTCClockSelection = RCC_RTCCLKSOURCE_LSI`. O `.ioc` nem habilita o
  oscilador LSE (`RCC_OscInitStruct.OscillatorType` so' tem HSI48/HSI/LSI/HSE).
- **Por que importa**: LSI vive no dominio VDD, nao no dominio de backup (VBAT). Sem alimentacao
  principal, o LSI para e o calendario **congela** mesmo com a bateria presente — a bateria so' mantem
  os registradores, nao o avanco do tempo. Isso anula o proposito de ter bateria+cristal: os
  timestamps de log/ensaio (Fase 5/Apendice C) ficariam errados apos qualquer queda de energia. LSI
  tambem tem baixa precisao (~±5%), inadequado mesmo com VDD presente.
- **Correcao (CubeMX, a fazer pelo usuario)**:
  1. Pinout & Configuration → System Core → **RCC**: em "Low Speed Clock (LSE)", selecionar
     **Crystal/Ceramic Resonator** (mantem LSI ligado — necessario para o IWDG, que usa LSI fixo
     no H7 independente desta escolha).
  2. Aba **Clock Configuration**: no seletor "RTC Clock Mux", trocar de **LSI** para **LSE**.
  3. Gerar o codigo. Conferir no diff: `RCC_OscInitStruct.OscillatorType` deve ganhar
     `RCC_OSCILLATORTYPE_LSE` com `LSEState = RCC_LSE_ON` (ou `_BYPASS` se for oscilador externo, nao
     cristal — confirmar com o usuario qual e' o caso); em `stm32h7xx_hal_msp.c`,
     `RTCClockSelection` deve virar `RCC_RTCCLKSOURCE_LSE`.
  4. **Efeito colateral esperado, unico, no 1o boot apos a troca**: o HAL reseta o dominio de backup
     quando a fonte do RTC muda (`RCC_BDCR_RTCSEL` so' pode mudar via `BDRST`) — isso apaga os
     registradores de backup (calendario, magic de `RTC_CALENDAR_INITIALIZED_BKP_REG`, e o registro
     de fault do `Fault_Manager`). Esperado, nao e' bug; calendario volta ao default e e' reajustado
     normalmente depois.
- **Firmware ja pronto para quando o CubeMX for ajustado** (`Core/Services/Rtc_Service`):
  - `rtc_validity.c` (C puro, testado no host): avalia hora/data lidas do RTC e devolve flags —
    `LSE_NOT_READY`, `BACKUP_DOMAIN_RESET`, `NEVER_SET` (calendario no default exato do
    `MX_RTC_Init()`) e `DATE_IMPLAUSIBLE` (campos invalidos ou ano < 2026).
  - `rtc_service.c` (binding HAL): le `HAL_RTC_GetTime/GetDate` + `RCC_FLAG_LSERDY`, chama
    `RtcValidity_Evaluate`. Chamado uma vez no boot (`RtcService_CheckAtBoot`, logo apos
    `FaultManager_Init`); resultado em `g_rtc_status` (Live Watch).
  - `g_rtc_backup_domain_was_reset` setado dentro do `USER CODE BEGIN RTC_Init 2` (sobrevive a'
    regeneracao) — detecta que o dominio de backup acabou de ser resetado neste boot.
  - Teste de host: `bash test/host/run.sh` (pressure_core + actuators + rtc_validity).

### 2.3 — correção aplicada e verificada pelo usuário (2026-10-01)
- CubeMX ajustado: RCC → Low Speed Clock (LSE) = Crystal/Ceramic Resonator; RTC Clock Mux = LSE.
  `RTCClockSelection` agora `RCC_RTCCLKSOURCE_LSE`, `RTCFreq_Value=32768`. Build Debug/Release limpo.
- CubeMX ligou sozinho o Clock Security System (HSE+LSE) como efeito colateral de usar cristal nos
  dois osciladores — ver `hardware.md` ("RTC / relógio de tempo real") para o comportamento do
  `NMI_Handler` resultante e o item pendente de revisão na Fase 4.
- `I2C1.Timing` recalculado pelo wizard (nao e' causado pela mudanca do RTC) — reverificar EEPROM
  na proxima bancada.
- Fase 2 dada como **concluida**: pressure_sensor, pwm_motor, valve, rtc feitos e testados
  (no que depende de codigo); input (pedal/bt_user/encoder) ja funcional desde a Fase 0/1, falta so'
  a medicao manual de latencia do pedal em bancada (nao bloqueia o avanco de fase).

## Fase 3 — Controle de pressao + simulador (2026-10-01)

- `Core/Services/Control_Core` (C puro, sem HAL): mesma logica de `Control_Loop` (D4), parametrizada
  por `dt_ms` em vez de `HAL_GetTick()`, com protecoes novas (P_MAX_ABS->E001, sensor invalido->E002,
  vazamento em manutencao->E013, timeouts de inflar/desinflar->E003/E004). `Control_Loop.c`
  (hardware) **nao foi alterado** — integracao e verificacao em bancada ficam para a Fase 4.
- `test/host/sim_plant.c`: simulador de planta pneumatica de 1a ordem (nao validado fisicamente),
  so' para exercitar o Control_Core no host.
- `test/host/test_control_core.c`: cenarios de manguito pequeno/grande, vazamento, sensor travado,
  sobrepressao e abort. Todos passam — `bash test/host/run.sh`.
- Relatorio completo (parametros, achados, bugs encontrados nos ganhos de PID nunca testados do
  `Control_Loop.c` atual, decisoes de projeto tomadas no Control_Core): `Docs/dev/sim-report.md`.
- Build Debug/Release do firmware continua limpo (Control_Core entra no include path mas ainda nao
  e' chamado por `main.c` — mesmo padrao de codigo "existe mas nao esta' ligado" que o resto do
  projeto ja usa, ex. o proprio Control_Loop sob APP_MODE_BRINGUP).
- Pendente antes da Fase 4: decidir com o usuario o `overshoot_max` real (hoje so' o exemplo do
  Apendice E, "<= 2 mmHg"); medir T_lag/vazao das valvulas/capacitancia do manguito na bancada e
  re-tunar o mesmo simulador antes de tocar no hardware de verdade.

## Fase 4 — Controle no hardware (inicio, 2026-10-01)

- **Decisao do usuario**: manter `overshoot_max` ja citado (Apendice E, <= 2 mmHg); ajustes finos de
  PID/bandas ficam para quando o ciclo completo ja estiver rodando no hardware, nao antes.
- `Core/Services/Control_Loop` agora delega o ensaio automatico (IDLE/INFLATING/STABILIZING/
  STABILIZED/DEFLATING_FAST/FAULT) ao `Control_Core` (Fase 3). CONFIG (modo manual da tela de
  config) e LEAK_TEST (calibracao) continuam com a logica antiga, intocados.
- `EEPROM_Storage_LoadDefaults()` atualizado com os ganhos de PID e a banda de estabilizacao
  validados SO' no simulador (`kp=0.8 ki=0.015 kd=0.15 i_min=-8 i_max=8`, `stabilization.band_mmhg=3.0`)
  — os valores antigos (nunca testados) causavam ~60 mmHg de overshoot. Ainda pendente de ajuste na
  bancada de verdade.
- Jitter do loop de controle instrumentado via DWT (`ControlLoop_GetMaxJitterCycles()`), exposto no
  bring-up como `g_control_jitter_us`. Erro do ensaio automatico em `ControlLoop_GetLastError()`
  (`g_control_last_error` no bring-up).
- **Bring-up novo: `BRINGUP_ENABLE_CONTROL`** (default **0**, nunca testado em hardware ainda).
  Mutuamente exclusivo com `BRINGUP_ENABLE_ACTUATORS` (os dois comandam os mesmos atuadores) —
  por isso `BRINGUP_ENABLE_ACTUATORS` voltou a 0 neste commit. Liga o ensaio automatico de verdade
  pela primeira vez no hardware: pedal/botao do encoder insufla/desinfla, setpoint inicial forcado a
  **40 mmHg** (bem abaixo do default de 120 mmHg) para o primeiro teste. Variaveis de diagnostico:
  `g_control_runtime` (state/pressure_display/setpoint), `g_control_last_error`, `g_control_jitter_us`.
- **Checklist de bancada (Parte C/Fase 4 do prompt, antes de habilitar)**:
  1. Manguito/reservatorio de **volume conhecido**, NUNCA direto num paciente ou membro nesta fase.
  2. Confirmar visualmente que as 3 valvulas e a bomba respondem (reusar o teste da Fase 2 se preciso).
  3. Habilitar `BRINGUP_ENABLE_CONTROL=1`, gravar, acompanhar `g_control_runtime->state` no Live Watch.
  4. Testar 1 ciclo completo em 40 mmHg: inflar -> estabilizar -> pedal -> desinflar -> IDLE.
  5. So' depois subir `g_bringup_sys_cfg.current_setpoint_mmhg` aos poucos (Live Watch), repetindo o
     ciclo a cada degrau.
  6. Registrar overshoot, tempo ao setpoint e `g_control_jitter_us` em cada degrau — isso vira o
     proximo ajuste do simulador e, depois, da EEPROM.
- Build Debug/Release limpo com a flag em 0 (default) e em 1 (testado so' por compilacao).
