# PROMPT DE PROJETO — Insuflador de Manguito com STM32H723 (firmware + IHM + 2 apps de PC)

> **Como usar este arquivo**
> 1. Copie a **PARTE A** para o arquivo `CLAUDE.md` na raiz do projeto (regras permanentes, o agente relê em toda sessão).
> 2. Cole a **PARTE B** (Prompt mestre) como primeira mensagem no Claude Code. Ela força começar pela **Fase 0 (auditoria)** e parar para sua aprovação.
> 3. Depois de aprovar cada fase, mande apenas: `Aprovado. Execute a Fase N conforme a Parte C.`
> 4. Os **Apêndices** (protocolo, formato do GIF/vídeo, layout do SD, códigos de erro, parâmetros) são referência. O agente deve copiá-los para `docs/` e mantê-los atualizados.
>
> Um prompt gigante executado de uma vez gera código que compila e não funciona. O fluxo em fases com critério de aceitação evita isso.

---

# PARTE A — `CLAUDE.md` (regras permanentes)

## Papel
Você é Engenheiro Sênior de Sistemas Embarcados, Hardware e IHM. Responda em português (pt-BR). Seja direto e estritamente técnico.

## Produto
Equipamento médico/ensaio que **insufla um manguito pneumático até um setpoint de pressão com rapidez e precisão, sem overshoot, e o desinsufla rapidamente** para realizar um exame. É um equipamento com **característica de auditoria**: cada ensaio, erro, calibração e alteração de configuração deve ser rastreável, com data/hora, operador e integridade verificável.

MCU: **STM32H723** (Cortex-M7, RAM segmentada DTCM/AXI/SRAM1-4, D-cache, MPU). Periféricos do hardware: 1 motor de bomba (PWM), 3 válvulas (V1, V2, V3), pedal de acionamento, botão `bt_user`, display **ST7789** (SPI), encoder rotativo com botão, USB-C, cartão SD (SDMMC), bateria de RTC, sensor de pressão, sinalização sonora. **Pinagem e detalhes de cada periférico devem ser extraídos do código/CubeMX existente e do esquema, nunca inventados.**

## Padrões de código
- Linguagem: **C11** para firmware (drivers, controle, máquina de estados). C++ só onde trouxer ganho claro (ex.: testes). Sem exceções e sem RTTI.
- Aplicar **MISRA C:2012 (regras obrigatórias/requeridas) e SEI CERT C**. Desvios são documentados em comentário `/* MISRA Dev x.y: motivo */`.
- Compilar com `-Wall -Wextra -Wconversion -Wshadow -Wundef -Wdouble-promotion -Wformat=2 -Werror`. Rodar `cppcheck --addon=misra` e `clang-tidy` no alvo `make check`.
- **Proibido `malloc/free/new/delete`** em qualquer parte do firmware. Toda memória é estática ou em pools/filas pré-alocados do RTOS (`xTaskCreateStatic`, `xQueueCreateStatic`, ...).
- Sem `float` em ISR sem necessidade. Loop de controle pode usar `float` (FPU do M7, precisão simples). Sempre `f` sufixo em literais (`0.5f`). Nada de `double` acidental.
- Tipos de largura fixa (`uint32_t`...). Sem `int` "solto" em aritmética de registradores. Sem recursão. Sem VLAs. Sem `printf` com `%f` em contexto crítico.
- **Comentar toda operação bit a bit**: máscara, shift, set/clear/toggle de registradores, e explicar o motivo do valor (referência ao RM0468 quando cabível). Toda máquina de estados tem tabela de transições comentada no topo do arquivo.
- ISRs: curtas e determinísticas (alvo < 5 µs), sem chamadas bloqueantes, só `...FromISR`. Variáveis compartilhadas com ISR: `volatile` + acesso atômico (`__LDREX/__STREX` ou seção crítica curta) + comentário de justificativa.
- Todo retorno de HAL/FatFS/USB é verificado. Erros vão para o módulo `fault_manager` com código único (Apêndice D).
- Watchdog: **IWDG** alimentado **somente** pela tarefa supervisora quando **todas** as tarefas críticas fizeram check-in na janela. WWDG opcional para o loop de controle.
- Cache do M7: buffers de DMA (SDMMC IDMA, SPI DMA, ADC DMA, USB) ficam em **AXI SRAM/SRAM1-3** alinhados a 32 bytes, **nunca em DTCM** (o IDMA do SDMMC não enxerga DTCM). Usar região MPU não-cacheável para esses buffers ou fazer `SCB_CleanDCache_by_Addr`/`SCB_InvalidateDCache_by_Addr` com comentário.
- Drivers modulares: cada driver expõe `xxx_init()`, API pequena, sem dependência de UI/aplicação, e uma camada `port` fina sobre a HAL para permitir teste no host com mocks.
- Camadas: `drivers/` → `services/` → `app/` → `ui/`. Camada superior nunca é chamada por inferior (usar callbacks/eventos).

## Segurança funcional (não negociável)
1. **Estado seguro** = motor OFF, PWM=0, **V1+V2+V3 abertas** (manguito despressurizado). Qualquer falha, reset, watchdog ou perda de comunicação em modo manual leva ao estado seguro. Verificar no esquema se as válvulas são NA ou NF e se, com MCU em reset/tri-state, o hardware cai no estado seguro; documentar em `docs/hardware.md`.
2. **Limite absoluto de pressão** (`P_MAX_ABS`, parâmetro protegido no firmware, não alterável pelo app de usuário): ao ser atingido → estado seguro imediato + erro E001, independente de setpoint e do estado da máquina.
3. Sensor de pressão é validado a cada amostra (faixa, taxa de variação máxima plausível, sensor travado/desconectado).
4. Timeouts em todas as fases (inflar, estabilizar, manter, desinflar).
5. Comando de **desinflar sempre tem prioridade** sobre qualquer outro comando.
6. O controle **não depende** de SD, USB nem display. Falha desses módulos nunca pode bloquear ou atrasar o loop de controle.

## Auditoria
- Registros de ensaio e logs são **append-only** e **encadeados por hash** (SHA-256 do registro anterior no registro atual) para detecção de adulteração.
- Nenhum comando de protocolo USB (nem no app de desenvolvedor) apaga ou sobrescreve arquivos de ensaio/log. Apenas leitura. Formatação do cartão só localmente e com registro do evento.
- ID de ensaio é monotônico e sobrevive à troca de cartão SD (contador em flash interna + registradores de backup TAMP, com CRC).
- Nunca rodar ensaio com RTC inválido (bateria descarregada/hora não ajustada) nem sem SD gravável (comportamento configurável apenas no app de desenvolvedor; padrão = bloquear).

## Processo de trabalho
- Trabalhe por **fases** (Parte C). Ao final de cada fase: compila sem warnings, `make check` passa, testes de host passam, `docs/` atualizado, **commit** com mensagem convencional (`feat(control): ...`). Depois **pare e resuma** o que foi feito, riscos e o que precisa ser testado na bancada.
- Nunca declare que algo "funciona no hardware" sem ter rodado no hardware. Diferencie: *compila*, *passa no simulador/teste de host*, *validado em bancada* (só o usuário valida).
- Antes de alterar código legado, leia-o. Não apague código existente funcional sem antes registrar em `docs/00-auditoria.md` o que ele faz e por que será substituído.
- Se faltar informação de hardware (pinagem, tipo de sensor, polaridade de válvula, resolução do display, buzzer/DAC), **pergunte ou marque como `TODO(HW)` em `docs/hardware.md`**. Não chute pinos.
- Todo parâmetro numérico de controle vive na tabela de parâmetros (Apêndice E), não em `#define` espalhado.

---

# PARTE B — PROMPT MESTRE (cole no Claude Code)

## 1. Contexto
Desenvolva do zero (refatorando o que existir) o firmware e as ferramentas de PC de um **insuflador/desinsuflador de manguito** baseado em **STM32H723**. Já existe no repositório uma tentativa parcialmente funcional; ela deve ser **auditada, aproveitada onde fizer sentido e refatorada/substituída** onde não atender aos requisitos abaixo. Existe uma pasta `Docs/` com um **exemplo de documento de logs** que define quais informações o equipamento deve registrar: **leia-a antes de projetar qualquer formato de log e siga-a como especificação**. Onde o exemplo conflitar com este prompt, aponte o conflito em `docs/00-auditoria.md` e pergunte.

## 2. Requisitos funcionais do equipamento

### 2.1 Fluxo de aplicação
1. **Boot**: reproduz animação (GIF/vídeo convertido, lida do SD, Apêndice B) + efeito sonoro. Durante a animação, em paralelo, roda o auto-teste (mount do SD, RTC, carga de calibração/parâmetros, auto-zero do sensor com válvulas abertas, verificação de válvulas/motor sem acionar a bomba). Se algo falhar, mostra a falha após a animação. Se o arquivo da animação faltar ou estiver corrompido, usa logo estático embutido e **não trava o boot**. Qualquer clique pula a animação.
2. **Tela principal**: dados do ensaio atual (pressão instantânea em número grande + barra/gauge, setpoint, estado do ensaio, tempo decorrido, pico, ID do ensaio), data e hora, operador ativo, ícones de SD/USB/erro. Estados exibidos: `STANDBY`, `INFLANDO`, `ESTABILIZANDO`, `AGUARDANDO DESINFLAR` (manutenção), `DESINFLANDO`, `CONCLUÍDO`, `ERRO`.
3. Ao fim de cada ensaio: **popup** de "Concluído" (com pressão de pico, tempo até setpoint, overshoot) ou de **erro** (código + descrição + ação sugerida). Fecha sozinho após tempo configurável ou por clique.
4. **Menu de configuração** (encoder: girar = navegar, clique = selecionar, clique longo = voltar): setpoint, data/hora, operador (seleção/cadastro simples), tempo de manutenção, brilho, volume, informações (versão de FW, data da última calibração, contador de ensaios, capacidade do SD), visualizar últimos eventos/erros.
5. Idioma pt-BR. Fonte legível a 1 m. Cores com contraste alto (verde=ok, amarelo=em andamento, vermelho=erro).

### 2.2 Comando inflar/desinflar
- Pedal e `bt_user` geram **interrupção externa (EXTI)**. A ISR apenas registra timestamp e envia evento (task notification/queue `FromISR`), com **debounce por software (~20-30 ms)**, sem lógica de negócio.
- **1 clique = INFLAR** (se em STANDBY). **Novo clique durante INFLAR/ESTABILIZAR/MANTER = DESINFLAR**. Clique durante DESINFLAR é ignorado. Deve existir também um flag atômico de "abort" lido pelo loop de controle no próximo tick de 1 ms, de modo que o comando de desinflar não espere pela tarefa de aplicação.
- Requisito de latência: do borda do pedal até a abertura das válvulas ≤ 2 ms (medir e documentar com GPIO de debug + analisador lógico; deixar o pino de debug no `docs/hardware.md`).

### 2.3 Controle de pressão (o coração do produto)
- Atuadores: **motor** (PWM, alta frequência, ex.: 20 kHz) e **válvulas V1/V2** (liga/desliga). **V1 sozinha, V2 sozinha e V1+V2 juntas dão 3 níveis de vazão de sangria** (mais o nível zero = fechadas), usados para controlar a pressão quando ela passa do setpoint. **V3** é a válvula de descarga/porta do manômetro de calibração: é usada para **desinflar** o manguito e para calibração.
- Objetivos: **atingir o setpoint no menor tempo possível, sem overshoot** (spec de tolerância nos parâmetros; ver Apêndice E, valores iniciais a confirmar com o usuário e medir).
- Estratégia obrigatória (implemente e valide em simulador antes do hardware):
  1. **Amostragem**: ADC (ou interface do sensor existente) disparado por timer a 1-2 kHz com DMA, oversampling/filtragem (mediana de 3 + IIR de 1ª ordem ou Kalman simples) e cálculo de `dP/dt` filtrado. Latência de filtro documentada.
  2. **Fase RAMPA (grossa)**: PWM em 100% (ou limite configurado) enquanto a pressão **prevista** `p_prev = p + (dP/dt) * T_lag` estiver abaixo de `SP - margem_grossa`. `T_lag` = atraso total (inércia do motor + pneumática + válvulas), **parâmetro calibrável e auto-estimável** (rotina de identificação no app de desenvolvedor).
  3. **Fase APROXIMAÇÃO (fina)**: reduz o PWM continuamente (rampa proporcional ao erro previsto, com limite de `dP/dt` máximo próximo ao alvo) para chegar ao setpoint com velocidade quase nula. Anti-windup se houver PI.
  4. **Fase ESTABILIZAÇÃO**: motor em PWM mínimo de compensação de vazamento (ou pulsado) e sangria por V1/V2 se `erro > +histerese`, escolhendo o nível de vazão pelo tamanho do erro (nível 1 → V1, nível 2 → V2, nível 3 → V1+V2). Histerese e tempo mínimo de válvula aberta para evitar chattering. Estável = `|erro| ≤ tolerância` por `t_estavel`.
  5. **Fase MANUTENÇÃO** ("aguardando para desinflar"): mantém o setpoint até o clique do pedal/`bt_user` ou `t_manutencao_max`. Detectar vazamento (queda > `dP_vazamento` em `t_janela`) → erro E013.
  6. **Fase DESINFLAR**: motor OFF, abre V1+V2+V3 (máxima vazão); ao cruzar `P_SEGURA` por `t_seguro`, fecha as válvulas conforme parâmetro e retorna a STANDBY/CONCLUÍDO. Timeout → E004.
- O controle roda em **período fixo de 1 ms**, disparado por timer de hardware, na tarefa de maior prioridade. Registrar jitter máximo (`DWT->CYCCNT`) e expor via diagnóstico.
- **Escreva primeiro um simulador de planta no host** (`tools/sim/`, Python ou C): modelo pneumático de 1ª/2ª ordem com volume do manguito variável (`C_manguito`), atraso de motor, vazão das válvulas (V1, V2, V1+V2, V3), vazamento e ruído do sensor. O **mesmo código C do controlador** (`app/control/`, sem dependência de HAL) é compilado no host e testado contra o simulador em cenários (manguito pequeno/grande, com/sem vazamento, ruído, sensor com offset). Critérios de aceitação medidos automaticamente: overshoot máximo, tempo ao setpoint, erro em regime, ausência de chattering. Gerar relatório em `docs/sim-report.md`.

### 2.4 Armazenamento (cartão SD, FAT32 via FatFS + SDMMC com DMA)
- **Tudo** de usuário/ensaio/log fica no SD, conforme Apêndice C. Nada de acesso ao SD no contexto do loop de controle: existe uma **`storage_task`** que recebe registros por fila e grava. Buffer de ensaio em RAM (RAM estática) para não perder amostras se o SD demorar; flush com `f_sync` ao fim do ensaio e periodicamente durante o ensaio.
- Registro de ensaio contém no mínimo (alinhar com `Docs/`): ID, data/hora início/fim (RTC), operador, setpoint, pico, overshoot, tempo até setpoint, tempo de manutenção, tempo de desinflar, resultado, códigos de erro, versão de FW, ID/data da calibração, temperatura se houver, curva de pressão amostrada (ex.: 100 Hz) em arquivo próprio, hash encadeado.
- Logs de evento e de erro (Apêndice D) com timestamp, ID de ensaio associado, estado, pressão no momento, contexto.
- Tolerar SD ausente/corrompido: erro E005, ícone, bloqueio de novos ensaios (padrão), tentativa periódica de remount, nunca travar.
- Mutex de FatFS (`FF_FS_REENTRANT=1`) porque USB e tarefa de storage compartilham o volume. Prioridade do storage abaixo do controle e acima da UI.

### 2.5 Dados críticos fora do SD
Calibração do sensor e parâmetros de controle **sobrevivem à troca de cartão**: guardar em **setor dedicado da flash interna** (com magic, versão, CRC32, cópia dupla A/B) e espelhar no SD. **Atenção**: o H723 tem flash de banco único; apagar setor **para a execução** por até ~2 s. Regravar só em `STANDBY`, motor off, com o código de escrita na ITCM/RAM se possível, e com o IWDG realimentado antes. Documente a decisão.

### 2.6 USB-C
O equipamento é **USB Device** (o PC é o host). Implementar **CDC (porta serial virtual)** com protocolo binário próprio (Apêndice A). **Não** expor o cartão SD como MSC ao mesmo tempo que o firmware o usa (corrompe o FAT); os arquivos são lidos via comandos `FS_*` do protocolo. Verificar no esquema qual PHY está roteado no conector USB-C (OTG_FS ou OTG_HS com PHY FS embutido), alimentação/VBUS sense e resistores CC1/CC2 (5,1 kΩ a GND para device), e documentar.
Dois níveis de acesso, **impostos no firmware** (não só na interface do PC):
- **USER**: leitura de status/telemetria, listar/ler/exportar ensaios e logs, ajustar **setpoint dentro dos limites**, data/hora, operador, iniciar/abortar ensaio. **Nenhuma** variável de controle, calibração ou ajuste de fábrica.
- **DEV**: tudo do USER + ler/escrever tabela de parâmetros de controle, modo manual (motor/válvulas), calibração, auto-sintonia/identificação de `T_lag`, telemetria de alta taxa, logs internos, auto-teste, reset, limites. Acesso por **autenticação challenge-response** (HMAC-SHA256 sobre nonce de 16 bytes gerado no equipamento, chave de desenvolvedor de 32 bytes fora do repositório) com **timeout de sessão** e **registro de toda sessão DEV e toda escrita de parâmetro no log de auditoria**. Modo manual exige *heartbeat* a 10 Hz; sem heartbeat por 300 ms → estado seguro.

### 2.7 Aplicações de PC (duas, no mesmo monorepo `pc/`)
Linguagem padrão sugerida: **Python 3.11+**, **PySide6** (Qt), `pyserial`, `pyqtgraph`, `reportlab`/`openpyxl` para relatórios, empacotamento com **PyInstaller**. (Se preferir outra stack, justifique antes de começar a Fase 8.) Estrutura:
- `pc/core/` — biblioteca compartilhada: transporte serial, COBS/CRC, codecs do protocolo, modelos de dados, parsing dos arquivos do SD, autenticação. Testes com `pytest` e um **dispositivo virtual** (fake device) que fala o protocolo, para desenvolver sem hardware.
- `pc/app_user/` — **Aplicação do Usuário**: conectar/detectar porta, painel de status ao vivo (pressão, estado, setpoint, data/hora, SD, erros), gráfico em tempo real, iniciar/abortar ensaio, ajustar setpoint (dentro do limite) e data/hora, **navegar ensaios gravados no SD, ver curva, exportar (CSV, PDF, XLSX) individual ou em lote, gerar relatório de uso por período e relatório de erros**, verificação de integridade (valida cadeia de hash e exibe "ÍNTEGRO / ADULTERADO"). **Não contém** nenhuma tela ou comando de parâmetros de controle/calibração (e o firmware recusaria de qualquer forma).
- `pc/app_dev/` — **Aplicação do Desenvolvedor**: tudo da anterior + login DEV, editor da tabela de parâmetros (com min/max/unidade, importar/exportar JSON, diff contra o equipamento), modo manual (sliders de PWM, botões de V1/V2/V3 com heartbeat), **osciloscópio** de pressão/PWM/válvulas/estado até 1 kHz, **assistente de calibração** (zero + 2 a 3 pontos com manômetro de referência na V3, cálculo de ganho/offset, validação de linearidade e histerese, gravação com registro de auditoria), identificação de `T_lag`, teste de vazamento, logs de erro completos, auto-teste, informações de versão/serial, exportação bruta.

## 3. Requisitos de arquitetura do firmware (FreeRTOS via CMSIS-RTOS2)
| Tarefa | Prioridade | Função |
|---|---|---|
| `control_task` | mais alta | Loop de 1 ms: lê pressão, executa controle, aciona PWM/válvulas, checa segurança |
| `safety_task` (supervisor) | alta | Check-in das tarefas, alimenta IWDG, monitora `P_MAX_ABS`, tensão, temperatura |
| `input_task` | média-alta | Debounce, eventos de pedal/`bt_user`/encoder |
| `app_task` | média | Máquina de estados de ensaio, orquestração, popups |
| `usb_proto_task` | média-baixa | Parser do protocolo, sessões USER/DEV, telemetria |
| `storage_task` | média-baixa | FatFS, logs, ensaios, arquivos |
| `ui_task` | baixa | LVGL (ou renderer próprio), DMA para ST7789 |
| `audio_task` | baixa | Efeitos sonoros |
- Comunicação por **filas e task notifications** com tipos de mensagem fixos (structs POD). Dados compartilhados de leitura (ex.: snapshot de pressão/estado) por buffer duplo com contador de sequência (seqlock) ou mutex curto, **nunca** mutex dentro da `control_task` para dado não crítico.
- Prioridades de interrupção: EXTI de pedal e timer de controle **acima** de USB/SDMMC/SPI-DMA, mas dentro da faixa `configMAX_SYSCALL_INTERRUPT_PRIORITY` se usarem API do RTOS. Documentar a tabela NVIC completa em `docs/irq.md`.
- Métricas em tempo de execução: uso de pilha (high-water-mark) por tarefa, CPU load, jitter do controle, falhas de fila cheia. Expor no protocolo (DEV).
- Configurar **MPU** (regiões não cacheáveis para DMA), **D-cache/I-cache** habilitados, código crítico em ITCM e dados de controle em DTCM se medido como vantajoso. Reportar mapa de memória (`arm-none-eabi-size`, `.map`) ao fim de cada fase.

## 4. IHM e display
- ST7789 via SPI + DMA, RGB565, buffer parcial (~1/10 do frame, duplo se couber) em RAM DMA-capable. Clock de SPI o mais alto que o display aceitar (documentar o real). Backlight por PWM. **Descobrir a resolução real (240×240 ou 240×320) no código existente.**
- **LVGL 9.x** para a UI (telas: principal, menu, popup, configurações). Se o consumo de RAM/CPU ficar inviável, propor renderer próprio com justificativa.
- A animação de boot **não passa pelo LVGL**: é *streaming* direto do SD para o driver do display (Apêndice B), com buffer duplo (ler frame N+1 do SD enquanto o DMA envia o frame N).
- Encoder: TIM em modo encoder (hardware) com filtro de entrada (`ICxF`), botão com debounce e detecção de clique curto/longo. Comente os campos de `SMCR`/`CCMR`/`CCER` usados.
- Áudio: **identificar no hardware** o método (buzzer por PWM, DAC ou I2S). Padrão: sequência de tons definida em código (tabela de notas/durações) tocada no boot, conclusão e erro; se houver DAC/I2S, suportar `.wav` PCM 8/16 bits opcional em `/boot/boot.wav`.

## 5. Qualidade e testes
- **Host**: Unity/Ceedling ou GoogleTest para `app/control`, máquina de estados, parser de protocolo, formatadores de log, hash chain, FatFS mock. Alvo `make test`.
- **Simulador de planta** (2.3) integrado ao `make test`, com critérios de aceitação automáticos.
- **Fuzz** simples do parser de protocolo (frames truncados, CRC ruim, comprimento absurdo, comando sem permissão).
- **Bancada**: para cada fase, gerar `docs/bancada/faseN-checklist.md` com procedimentos passo a passo (equipamento, medições, resultado esperado, campos para preencher).
- Análise estática e MISRA reportadas em `docs/qualidade/`.
- Cada requisito deste prompt vira um ID (`REQ-xxx`) em `docs/rastreabilidade.md`, com link para código e teste.

## 6. Entregáveis
Firmware compilável, drivers, simulador, dois apps de PC, ferramenta `tools/make_bootanim.py`, `docs/` (hardware, arquitetura, IRQ, protocolo, formato de arquivos, calibração, manual do operador, checklists de bancada, rastreabilidade), scripts de build, `CLAUDE.md`, `README.md`.

## 7. Comece agora — instruções para esta primeira resposta
Execute **somente a Fase 0** (Parte C). Não altere nem apague código legado. Ao terminar, apresente: (a) resumo do que existe e do que funciona, (b) lista de decisões que preciso tomar, (c) lista de informações de hardware que faltam, (d) proposta de estrutura de pastas, (e) riscos. **Pare e aguarde minha aprovação.**

---

# PARTE C — PLANO EM FASES (critérios de aceitação)

### Fase 0 — Auditoria e descoberta (sem alterar código)
- Mapear o repositório: build system (CubeIDE/CMake/Makefile), versão do HAL/CubeH7, RTOS (se houver), middlewares, `.ioc`, drivers existentes, o que está funcionando e o que não.
- Ler `Docs/` (exemplo de logs) e extrair a **especificação de campos** para `docs/formato-logs.md`.
- Gerar `docs/hardware.md`: tabela de pinos (função, pino, periférico, polaridade, nível lógico, resistores), sensor de pressão (tipo, faixa, interface, resolução), tipo de válvulas (NA/NF), driver do motor, frequência de PWM atual, display (resolução, clock máx.), áudio, USB (PHY, VBUS), SD (SDMMC1/2, 4-bit?), RTC (LSE, VBAT), alimentação. Tudo que faltar vira `TODO(HW)` + pergunta.
- Medir o estado atual: tamanho (flash/RAM), warnings, uso de heap/malloc, ISRs longas, blocos bloqueantes.
- Entregar `docs/00-auditoria.md`, proposta de arquitetura e lista de perguntas.
- **Aceitação**: nenhum arquivo de código alterado; documentos entregues; perguntas objetivas.

### Fase 1 — Fundação do projeto
- Reorganizar em `firmware/{app,drivers,services,ui,port,third_party,config}`, `pc/`, `tools/`, `docs/`, `test/`. Se o projeto é CubeIDE, **preferir migrar para CMake + arm-none-eabi-gcc** mantendo o `.ioc` como fonte dos `MX_*_Init` (documente o fluxo de regeneração para não perder código).
- Toolchain: flags de warning, linker script com seções para DMA/ITCM/DTCM, `make/cmake` com alvos `build`, `flash`, `check`, `test`, `size`.
- `port/`: relógio (550 MHz, PLLs), MPU, caches, NVIC (tabela), DWT cycle counter, `fault_manager`, hard fault handler com dump para backup SRAM/TAMP e registro no próximo boot.
- FreeRTOS estático, hooks (`vApplicationStackOverflowHook`, `MallocFailedHook` inexistente pois não há heap), supervisor + IWDG.
- **Aceitação**: compila sem warnings; `make check` roda; "hello" via SWO/UART; hard fault forçado é registrado e sobrevive ao reset.

### Fase 2 — Drivers básicos
- `pressure_sensor` (ADC+DMA+timer ou interface real; filtros; auto-zero; conversão para mmHg com coeficientes de calibração), `pwm_motor`, `valve` (V1/V2/V3 com abstração NA/NF), `input` (pedal, `bt_user`, encoder), `rtc` (LSE, VBAT, detecção de perda de hora via flag/TAMP), `buzzer/audio`.
- Cada driver com teste de host (mock da `port`) e programa de bancada em `firmware/app/bench/` (modo de diagnóstico compilável que exercita o periférico isoladamente).
- **Aceitação**: leitura de pressão estável com ruído medido e documentado; PWM medido no osciloscópio; latência do pedal medida.

### Fase 3 — Controle de pressão + simulador
- Implementar `app/control/` em C puro (sem HAL), a máquina de estados do ensaio e o simulador de planta no host.
- Loop de 1 ms no hardware com medição de jitter.
- Ajustar parâmetros no simulador até cumprir os critérios; gerar `docs/sim-report.md`.
- **Aceitação**: no simulador, overshoot ≤ valor da tabela do Apêndice E em todos os cenários; sem chattering; falhas injetadas (sensor travado, vazamento, sobrepressão) levam ao estado seguro e ao erro correto.

### Fase 4 — Controle no hardware + segurança
- Integrar ao hardware real, **primeiro com manguito de volume conhecido/reservatório** e pressão baixa; subir gradualmente. Checklist de bancada com critérios.
- Implementar `P_MAX_ABS`, validação do sensor, timeouts, estado seguro, watchdog, comportamento em reset/queda de energia.
- **Aceitação (a validar pelo usuário na bancada)**: N inflagens consecutivas ao setpoint dentro da tolerância; desinflar dentro do tempo-alvo; falhas injetadas em bancada levam ao estado seguro.

### Fase 5 — Armazenamento e auditoria
- FatFS + SDMMC (IDMA, buffers alinhados, cache), `storage_task`, formatos do Apêndice C, hash encadeado, contador de ensaios redundante, logs de evento/erro, política de cartão ausente/cheio, rotação por data.
- **Aceitação**: 1000 ensaios simulados gravados e verificados; corte de energia durante gravação não corrompe registros anteriores (verificado pela cadeia de hash); SD removido durante ensaio → E005 sem afetar controle.

### Fase 6 — IHM
- Driver ST7789 com DMA, LVGL, telas, popups, menu, encoder, data/hora, operador, mensagens de erro em pt-BR.
- Streaming da animação de boot (`.anim`) + áudio; `tools/make_bootanim.py` (Apêndice B).
- **Aceitação**: ≥ 30 FPS de UI na tela principal durante inflar (medido), sem impacto no jitter do controle (comparar antes/depois), animação de boot sem tearing visível e sem travar caso o arquivo falte.

### Fase 7 — USB e protocolo
- USBD CDC, framing COBS+CRC, parser robusto, sessões USER/DEV com autenticação, telemetria, `FS_*` somente leitura, tabela de parâmetros, modo manual com heartbeat.
- Implementar o **dispositivo virtual** no PC para testes.
- **Aceitação**: fuzz sem falhas; comando fora de permissão recusado com erro registrado; throughput de leitura de arquivo ≥ 500 kB/s; desconexão do cabo em modo manual → estado seguro em ≤ 300 ms; USB ativo não altera jitter do controle além do limite.

### Fase 8 — Aplicativo do usuário (PC)
- `pc/core` + `pc/app_user`, incluindo relatórios (PDF/CSV/XLSX), verificação de hash chain, telas de status e histórico.
- **Aceitação**: testes com dispositivo virtual e com hardware real; exportação de 1000 ensaios; relatório de uso e de erros; nenhuma função de controle acessível (revisão de código + tentativa de envio manual de comando DEV recusada).

### Fase 9 — Aplicativo do desenvolvedor (PC)
- `pc/app_dev`: login DEV, parâmetros, modo manual, osciloscópio, calibração assistida, identificação de `T_lag`, logs, auto-teste.
- **Aceitação**: calibração de 3 pontos gera coeficientes, valida linearidade/histerese e registra no log de auditoria com valores antes/depois; toda escrita de parâmetro aparece no log.

### Fase 10 — Fechamento
- Rastreabilidade completa, manual do operador, guia de manutenção/calibração, guia de build/flash, pacote de release (`.hex/.bin`, apps em `.exe`), tabela de riscos residuais, relatório MISRA/CERT com desvios justificados, lista do que ainda depende de validação na bancada.

---

# APÊNDICES (o agente deve copiá-los para `docs/` e mantê-los atualizados)

## Apêndice A — Protocolo USB (CDC)

**Framing**: cada frame é codificado com **COBS** e terminado por byte `0x00`. Frame decodificado (little-endian):

```
+------+------+------+--------+--------+---------+-----------+--------+
| VER  | TYPE | SEQ  | CMD    | LEN    | PAYLOAD | (opc.)    | CRC32  |
| 1 B  | 1 B  | 2 B  | 2 B    | 2 B    | 0..1024 |           | 4 B    |
+------+------+------+--------+--------+---------+-----------+--------+
TYPE: 0x01=REQ  0x02=RSP  0x03=EVT (assíncrono)  0x04=STREAM
CRC32: polinômio 0x04C11DB7 (usar o CRC do hardware), calculado sobre VER..PAYLOAD
```
RSP carrega `status` (1 B) no início do payload: `0=OK, 1=CMD_DESCONHECIDO, 2=SEM_PERMISSAO, 3=PARAM_INVALIDO, 4=OCUPADO, 5=ESTADO_INVALIDO, 6=ERRO_SD, 7=FORA_DE_FAIXA, 8=NAO_AUTENTICADO`.

**Comandos** (máscara de papel: U=USER, D=DEV):

| CMD | Nome | Papel | Descrição |
|---|---|---|---|
| 0x0001 | HELLO | U,D | Versão do protocolo, FW, serial, papel atual |
| 0x0002 | GET_STATUS | U,D | Estado, pressão, setpoint, RTC, SD, erros ativos, ID do ensaio |
| 0x0003 | SET_SETPOINT | U,D | Só dentro de `[SP_MIN, SP_MAX]` (parâmetros) |
| 0x0004 | SET_DATETIME | U,D | Ajusta RTC e registra evento |
| 0x0005 | START_TEST | U,D | Equivale ao clique de inflar |
| 0x0006 | ABORT_TEST | U,D | Equivale a desinflar |
| 0x0007 | SET_OPERATOR | U,D | Seleciona operador ativo |
| 0x0010 | STREAM_START/STOP | U (≤50 Hz), D (≤1 kHz) | Telemetria contínua |
| 0x0020 | FS_LIST | U,D | Lista diretório (somente leitura) |
| 0x0021 | FS_STAT | U,D | Tamanho, data, CRC32 do arquivo |
| 0x0022 | FS_READ | U,D | `offset,len` → chunk (≤1024 B) |
| 0x0023 | FS_CRC | U,D | CRC32/SHA-256 do arquivo, para validar exportação |
| 0x0030 | GET_LOG_TAIL | U,D | Últimos N eventos/erros |
| 0x0040 | AUTH_CHALLENGE | U | Pede nonce de 16 B |
| 0x0041 | AUTH_RESPONSE | U | HMAC-SHA256(chave_dev, nonce ‖ serial) → eleva a sessão para DEV |
| 0x0042 | AUTH_LOGOUT | D | Volta a USER |
| 0x0100 | PARAM_LIST | D | Tabela de parâmetros com min/max/unidade/valor |
| 0x0101 | PARAM_GET / PARAM_SET | D | Escrita valida faixa, gera log de auditoria (antes/depois) |
| 0x0102 | PARAM_COMMIT / PARAM_REVERT | D | Grava na flash (só em STANDBY) ou descarta |
| 0x0110 | MANUAL_ENTER/EXIT | D | Modo manual (só em STANDBY) |
| 0x0111 | MANUAL_SET | D | PWM, V1, V2, V3; exige heartbeat |
| 0x0112 | MANUAL_HEARTBEAT | D | 10 Hz; sem heartbeat por 300 ms → estado seguro |
| 0x0120 | CAL_START/POINT/APPLY/ABORT | D | Fluxo de calibração |
| 0x0130 | IDENT_TLAG | D | Rotina de identificação de `T_lag` |
| 0x0140 | SELFTEST | D | Auto-teste completo |
| 0x0150 | GET_DIAG | D | Pilhas, CPU, jitter, filas, resets, hard fault dump |
| 0x0160 | REBOOT | D | Reinicia (registra evento) |

Eventos assíncronos (EVT): `ESTADO_MUDOU`, `ENSAIO_CONCLUIDO`, `ERRO`, `SD_INSERIDO/REMOVIDO`, `AUTH_EXPIROU`.
Regra: qualquer comando com papel insuficiente retorna `SEM_PERMISSAO`, **e o evento é gravado no log** (tentativa de acesso negado).

## Apêndice B — Como configurar o GIF/vídeo de entrada (fluxo do usuário)

**Por que não ler o GIF direto**: decodificar GIF/MP4 em tempo real num Cortex-M7 consome muita CPU e RAM. A solução robusta é **converter no PC** para um formato de *frames brutos RGB565* que o firmware apenas lê do SD e envia por DMA ao display.

**Especificação do formato `.anim`** (little-endian), arquivo em `/boot/boot.anim` no SD:

```
Offset  Tam  Campo
0x00    4    magic = "CBAN"
0x04    2    versão = 1
0x06    2    largura  (deve ser igual à do display, ex.: 240)
0x08    2    altura   (ex.: 240 ou 320)
0x0A    2    fps      (recomendado 15..24)
0x0C    4    nº de frames
0x10    2    formato de pixel: 0 = RGB565 big-endian (ordem que o ST7789 espera no SPI)
0x12    2    compressão: 0 = nenhuma (v1)
0x14    4    CRC32 do payload (todos os frames)
0x18    8    reservado (zeros)
0x20    ...  frames: cada um largura*altura*2 bytes, consecutivos, sem padding
```
Frame de 240×240 = 115.200 B. 15 fps × 3 s = 45 frames ≈ 5,2 MB. Vazão exigida ≈ 1,7 MB/s, muito abaixo do que SDMMC 4-bit entrega. Recomendação: **≤ 4 s, 15 fps, resolução nativa do display**. O arquivo deve ser gravado **contíguo** no cartão (copiar para SD recém-formatado) para leitura mais rápida.

**Passo a passo do usuário**:
1. Tenha o GIF/vídeo de origem (ex.: `logo.gif` ou `logo.mp4`).
2. Rode a ferramenta do projeto (será criada na Fase 6):
   ```
   python tools/make_bootanim.py logo.gif --width 240 --height 240 --fps 15 --out boot.anim
   ```
   A ferramenta usa `ffmpeg` internamente, conforme abaixo, redimensiona com barras pretas para manter proporção, converte para RGB565 big-endian, monta o cabeçalho, calcula o CRC32 e imprime tamanho/duração/aviso se passar de 4 s ou 8 MB.
   Equivalente manual do passo de conversão:
   ```
   ffmpeg -i logo.gif -vf "fps=15,scale=240:240:force_original_aspect_ratio=decrease,pad=240:240:(ow-iw)/2:(oh-ih)/2:black" -pix_fmt rgb565be -f rawvideo frames.raw
   ```
3. Copie `boot.anim` para `SD:/boot/boot.anim` (e, opcionalmente, `boot.wav` para o som se houver DAC/I2S).
4. Insira o cartão e ligue. Se o arquivo faltar, tiver magic/CRC/resolução inválidos, o firmware mostra o logo estático embutido, registra o evento `W_BOOTANIM_INVALID` e continua o boot normalmente.

**Requisitos do firmware**: validar cabeçalho e resolução; ler frame a frame com **buffer duplo** (2 × frame em AXI SRAM alinhada a 32 B, ou 2 × meia-tela se faltar RAM), enviar por SPI-DMA em janela de endereço do ST7789 (`CASET/RASET/RAMWR`), temporizar por fps com timer/`vTaskDelayUntil`, permitir pular com qualquer clique, e conferir o CRC32 **em segundo plano** apenas no primeiro uso (ou por comando de diagnóstico), sem atrasar a reprodução. Ferramenta também deve gerar um `boot.anim` de exemplo (padrão de teste) para desenvolvimento.

## Apêndice C — Layout do cartão SD

```
/boot/boot.anim            animação de entrada
/boot/boot.wav             (opcional) som de entrada
/config/settings.json      configurações de usuário (setpoint, brilho, volume, tempos)
/config/control_backup.bin espelho dos parâmetros de controle (a cópia oficial está na flash)
/config/cal_backup.bin     espelho da calibração
/users/operators.csv       id;nome;ativo;criado_em
/tests/AAAA/MM/T00000123.rec    registro resumo do ensaio (binário/JSON conforme Docs/)
/tests/AAAA/MM/T00000123.csv    curva de pressão (t_ms;pressao;pwm;v1;v2;v3;estado)
/tests/index.csv           índice (id;data;operador;setpoint;pico;resultado;hash)
/logs/events/AAAA-MM.log   eventos (linha por evento, hash encadeado)
/logs/errors/AAAA-MM.log   erros (código, contexto, pressão, estado)
/logs/audit/AAAA-MM.log    sessões DEV, alterações de parâmetro, calibrações, acessos negados
/reports/                  relatórios gerados pelo firmware (opcional)
```
Formato de linha de log (exemplo, ajustar conforme `Docs/`):
`2026-09-19T14:32:10-03:00;seq=000123;lvl=ERR;code=E003;test=T00000045;state=INFLANDO;p=187.4;msg=Timeout de inflar;prev=ab12..;hash=cd34..`
Cada linha contém `hash = SHA-256(prev_hash ‖ conteúdo_da_linha)`. O primeiro registro do mês referencia o último hash do mês anterior.

## Apêndice D — Códigos de erro (base; alinhar com `Docs/` e completar)

| Código | Nome | Ação do firmware |
|---|---|---|
| E001 | Sobrepressão (`P_MAX_ABS` ou `SP + margem`) | Estado seguro imediato, ensaio inválido |
| E002 | Falha do sensor (fora de faixa, travado, salto impossível) | Estado seguro, bloqueia novos ensaios |
| E003 | Timeout de inflar / não atinge setpoint (vazamento/sem manguito) | Aborta e desinfla |
| E004 | Timeout de desinflar | Mantém válvulas abertas, alarme |
| E005 | Falha de SD (ausente, RO, cheio, corrompido) | Bloqueia ensaios (padrão), ícone, remount periódico |
| E006 | RTC inválido / bateria baixa | Bloqueia ensaios até ajuste de hora |
| E007 | Calibração inválida/ausente (CRC) | Bloqueia ensaios |
| E008 | Reset por watchdog | Registrado no boot seguinte |
| E009 | Hard fault / stack overflow | Dump em TAMP, registrado no boot seguinte |
| E010 | Erro de comunicação USB / heartbeat perdido em manual | Estado seguro |
| E011 | Parâmetros com CRC inválido | Usa defaults seguros, bloqueia ensaios |
| E012 | Pedal/`bt_user` travado (pressionado > X s) | Ignora entrada, alerta |
| E013 | Vazamento em manutenção (queda > `dP_vazamento`) | Encerra ensaio com aviso |
| E014 | Tensão de alimentação fora da faixa | Estado seguro |
| W_xxx | Avisos (animação inválida, SD quase cheio, calibração vencida) | Só log/ícone |

Cada erro tem: código, texto pt-BR curto para popup, texto longo no log, causa provável, ação sugerida.

## Apêndice E — Tabela de parâmetros de controle (valores iniciais **a confirmar/medir**; todos com min, max, unidade, protegidos por papel DEV)

| Parâmetro | Valor inicial | Observação |
|---|---|---|
| `sp_min` / `sp_max` | **a definir com o usuário** (mmHg) | Limites do setpoint do app de usuário |
| `P_MAX_ABS` | **a definir** | Limite absoluto, só DEV |
| `unidade` | mmHg | Configurável (kPa) apenas na apresentação; interno em mmHg×10 (inteiro) ou `float` mmHg |
| `tol_setpoint` | ±? mmHg | Faixa de "estável" |
| `overshoot_max` (critério de aceitação) | **a definir** (ex.: ≤ 2 mmHg) | Medido no simulador e na bancada |
| `t_alvo_setpoint` | **a definir** | Tempo alvo para chegar ao setpoint |
| `T_lag` | medir | Atraso total motor+pneumático (ms) |
| `margem_grossa` | medir | Distância ao SP para sair de 100% PWM |
| `pwm_max`, `pwm_min_comp` | 100% / medir | Limites do PWM |
| `dPdt_max_perto_alvo` | medir | Limite de taxa próximo ao SP |
| `Kp`, `Ki` | sintonizar no simulador | Se usado PI na aproximação |
| `histerese_valvula`, `t_min_valvula` | medir | Evita chattering |
| `niveis_sangria` (limites de erro p/ V1, V2, V1+V2) | medir | Escolha da vazão |
| `t_estavel` | ~200 ms | Tempo dentro da tolerância |
| `t_manutencao_max` | **a definir** | Tempo máximo mantendo pressão |
| `t_inflar_max`, `t_desinflar_max` | **a definir** | Timeouts |
| `P_SEGURA`, `t_seguro` | ~5 mmHg / ~200 ms | Critério de "despressurizado" |
| `dP_vazamento`, `t_janela` | medir | Detecção de vazamento |
| `taxa_ensaio_curva` | 100 Hz | Amostragem gravada no SD |

## Apêndice F — Estrutura de pastas alvo

```
/CLAUDE.md
/README.md
/Docs/                      (já existente — exemplo de logs; NÃO alterar)
/docs/                      (documentação do projeto: hardware, arquitetura, irq, protocolo, formatos, bancada, qualidade)
/firmware/
  app/{control,state_machine,bench,params,cal}
  drivers/{pressure,motor_pwm,valve,input,encoder,rtc,st7789,audio,sd_port}
  services/{storage,logger,audit_hash,usb_proto,fault_manager,supervisor}
  ui/{screens,popups,menu,boot_anim}
  port/                     (clock, MPU, cache, NVIC, DWT, HAL glue)
  third_party/{freertos,lvgl,fatfs,usb_device,sha256}
  config/                   (FreeRTOSConfig.h, lv_conf.h, ffconf.h, defaults de parâmetros)
/pc/{core,app_user,app_dev,tests}
/tools/{sim,make_bootanim.py,gen_dev_key.py,fake_device}
/test/                      (testes de host do firmware)
```

## Apêndice G — Decisões que o agente deve confirmar comigo na Fase 0
1. Faixa de pressão de trabalho, `P_MAX_ABS`, tolerância, overshoot aceitável e tempo alvo ao setpoint.
2. Unidade primária (mmHg ou kPa).
3. Sensor de pressão: modelo/interface e resolução (define o tratamento do sinal).
4. Válvulas NA ou NF e estado do hardware em reset.
5. Buzzer (PWM), DAC ou I2S.
6. Bloquear ensaios sem SD/RTC válidos (padrão: sim).
7. Stack dos apps de PC (padrão: Python + PySide6).
8. Política de retenção/rotação de logs e tamanho mínimo do SD.
9. Necessidade de bootloader/atualização de firmware por USB (DFU) — fora do escopo inicial, mas reservar setor de flash e documentar.
10. Requisitos regulatórios aplicáveis (ex.: norma de equipamento eletromédico) — se existirem, alteram rastreabilidade e validação.
