# Hardware — N_PRESSURE (STM32H723VGT6)

> Extraído de `N_PRESSURE.ioc`, `Core/Inc/main.h`, `Core/Src/main.c` e dos módulos em
> `Core/Bsp`/`Core/Services` existentes em 2026-09-19. Onde a informação não está no
> código/`.ioc`, está marcada `TODO(HW)` — não foi inventada.

## Pinagem

| Sinal (label) | Pino MCU | Periférico/modo | Observações |
|---|---|---|---|
| ENCODER_A | PA0 | TIM2_CH1_ETR (encoder de hardware) | `TIM_ENCODERMODE_TI1` — **só o canal TI1 está em modo de contagem**; ver TODO(HW) H1 |
| ENCODER_B | PA1 | TIM2_CH2 | Configurado como `IC2` no modo encoder, mas `EncoderMode=TIM_ENCODERMODE_TI1` conta só por TI1 |
| ENCODER_BT | PA2 | GPIO input, sem pull | Botão do encoder |
| BT_USER | PA3 | GPIO input, sem pull | Botão físico de usuário |
| TFT_CLOCK | PA5 | SPI1_SCK | |
| TFT_BACKLIGHT | PA6 | GPIO output | Ligado em nível alto logo após `MX_GPIO_Init` (`HAL_GPIO_WritePin(..., SET)` em `main.c`) |
| TFT_DATA | PA7 | SPI1_MOSI | SPI1 configurado `SPI_DIRECTION_2LINES_TXONLY` (sem MISO) |
| USB_HS_INT | PA8 | EXTI8 (rising) | Nome sugere IRQ de um PHY/controlador USB externo — TODO(HW) H7 |
| USB_OTG_HS_VBUS | PA9 | USB_OTG_HS (VBUS sense interno) | |
| USB_OTG_HS_ID | PA10 | USB_OTG_HS | |
| USB_OTG_HS_DM | PA11 | USB_OTG_HS | PHY embutido (Full-Speed), não ULPI |
| USB_OTG_HS_DP | PA12 | USB_OTG_HS | |
| JTMS/SWDIO, JTCK/SWCLK, JTDO/SWO | PA13, PA14, PB3 | Debug | Padrão ST-Link |
| TFT_RES | PB0 | GPIO output | Reset do painel |
| PRESSURE_SENSOR | PB1 | ADC2_INP5 | Entrada analógica única — sensor de tensão, não digital |
| PEDAL | PB6 | TIM4_CH1 (input capture) | `ICFilter=10` (filtro de hardware), borda de subida |
| I2C1_SCL | PB8 | I2C1 | EEPROM externa |
| I2C1_SDA | PB9 | I2C1 | |
| TFT_CS | PC4 | GPIO output | |
| TFT_DC | PC5 | GPIO output | Data/Command do painel |
| PUMP_CTRL | PC6 | TIM3_CH1 (PWM) | Ver cálculo de frequência abaixo |
| BUZZER | PC7 | GPIO output (bit-bang) | Não é canal de timer nem DAC |
| SDMMC1_D0..D3 | PC8, PC9, PC10, PC11 | SDMMC1 (4-bit) | |
| SDMMC1_CK | PC12 | SDMMC1 | |
| SDMMC1_CMD | PD2 | SDMMC1 | |
| SDMMC1_CD | PD0 | GPIO input, sem pull | Card-detect (mecânico, sem pull configurado — TODO(HW) confirmar se precisa pull-up externo) |
| EN_VBUS | PD4 | GPIO output | Nome sugere fonte de VBUS (papel de *host*) — TODO(HW) H7 |
| OC_USB | PD5 | EXTI5 (rising) | Sinal de sobrecorrente — típico de chave de VBUS de host |
| LED_2 | PD11 | GPIO output | |
| LED_1 | PD12 | EXTI12 (rising) | Configurado como entrada com interrupção, não como saída — confirmar se é LED de status ou outra entrada mal rotulada (TODO(HW)) |
| VALVE_3 | PD13 | GPIO output | Válvula de descarga/porta do manômetro de calibração (uso manual/DEV) |
| VALVE_2 | PD14 | GPIO output | |
| VALVE_1 | PD15 | GPIO output | |

Todos os pinos de GPIO de válvula/backlight/TFT são reiniciados para `GPIO_PIN_RESET` em
`MX_GPIO_Init()` antes de qualquer lógica de aplicação rodar.

## Clocks (RCC, do `.ioc`)

- SYSCLK = 544 MHz (PLL1, fonte HSI, `PLLM=4, PLLN=34, PLLP=1`), voltage scale 0.
- HCLK (AHB) = 272 MHz (`AHBCLKDivider = DIV2`).
- APB1/APB2/APB3/APB4 = 136 MHz (todos `/2`); clock de timers em barramentos `/2` dobra para
  272 MHz (regra padrão do H7: se o prescaler APB ≠ 1, TIMxCLK = 2× APBxCLK).
- USB: `RCC_USBCLKSOURCE_HSI48` (48 MHz) — clock dedicado HSI48, independente do PLL principal.
- ADC: `RCC_ADCCLKSOURCE_CLKP` a 64 MHz (via `CKPER`, fonte HSI).
- **Sem HSE/LSE configurado** — todo o clock tree roda a partir de osciladores internos
  (HSI, HSI48, LSI). Isso é consistente com H8 (RTC ausente): não há cristal de 32,768 kHz
  ativo no projeto atual.

## Motor da bomba (PWM, TIM3_CH1)

- `Prescaler = 199` (÷200), `Period (ARR) = 999` (÷1000), `TIM_OCMODE_PWM1`, polaridade alta.
- Clock do timer (APB1, ver acima) = 272 MHz → contador a 272 MHz / 200 = **1,36 MHz**.
- Frequência de PWM resultante = 1,36 MHz / 1000 = **≈1,36 kHz** (audível).
- **Corrigido (2026-09-19)**: `TIM3.Prescaler = 0`, `TIM3.Period = 13750-1`. Contador a 272 MHz
  (ou ~275 MHz, conforme medido pelo usuário na tela de clock do CubeMX — a pequena diferença não
  é significativa aqui), PWM ≈ 272 MHz/13750 ≈ **19,8 kHz** (ou ~20,0 kHz com 275 MHz) — dentro do
  alvo de ~20 kHz acordado. Build confirmado limpo com essa configuração.

## Válvulas (V1/V2/V3)

- GPIO digital on/off (sem PWM), acionamento hoje feito em `Core/Services/Control_Loop/Src/control_loop.c`
  via `set_valves()`, que escreve o **nível invertido** do estado lógico desejado
  (`HAL_GPIO_WritePin(pin, !v_open)`): pino em `LOW` = válvula aberta, pino em `HIGH` = válvula
  fechada, nessa convenção de software.
- `MX_GPIO_Init()` inicializa V1/V2/V3 em `GPIO_PIN_RESET` (`LOW`) antes de qualquer código de
  aplicação — na convenção acima, isso corresponde a "abertas" já na inicialização do MCU.
- **H4 resolvido (confirmado pelo usuário)**: as válvulas são **NA (normalmente aberta)** e este
  deve ser o estado de reset — bate exatamente com a convenção de software já existente
  (`LOW` = aberta, `MX_GPIO_Init` zera os 3 pinos antes de qualquer código de aplicação rodar).
  Ainda assim, vale confirmar fisicamente que o estágio de potência (transistor/relé) entre o
  GPIO e o solenoide não inverte essa lógica em brownout/tri-state real de bancada — a análise de
  esquema deu "compatível", a validação final é de bancada (regra de segurança do `CLAUDE.md`).
- V3 seguia tratada no código como exclusiva de modo manual/calibração, mas o estado
  `SYSTEM_STATE_FAULT` agora tem handler próprio (`ControlLoop_EnterFaultState()`, implementado em
  2026-09-19) que abre V1+V2+V3 — decisão D3 em `Docs/dev/00-auditoria.md`.

## Display (ST7789)

- SPI1, `SPI_DIRECTION_2LINES_TXONLY` (sem leitura), `SPI_DATASIZE_8BIT`, modo 0
  (`CPOL=LOW, CPHA=1EDGE`), `BaudRatePrescaler=2` → SCK ≈ f(SPI1 kernel clock)/2. Clock do
  kernel SPI1/2/3 = 64 MHz (`RCC.SPI123Freq_Value`) → **SCK ≈ 32 MHz**. TODO(HW): confirmar no
  datasheet do painel se 32 MHz está dentro do limite do controlador ST7789 real montado (o
  datasheet genérico ST7789V permite até ~62,5 MHz em alguns modos, mas depende do módulo/fita
  flex específico usado).
- Resolução: **320×240, orientação landscape** (`ST7789_Init()` envia `MADCTL=0x60`; buffer de
  linha de `st7789.c` é `320*2` bytes).
- Driver atual transmite por **polling** (`spi_transmit_checked`, sem DMA) — `lv_port_disp.c`
  já usa `HAL_SPI_Transmit_DMA` no flush do LVGL, ou seja, há dois caminhos coexistindo (bring-up
  cru por polling vs. estágio LVGL por DMA); precisa unificar na Fase 6.
- Backlight: GPIO on/off simples (PA6) — **não é PWM**, então não há controle de brilho por
  software hoje (a Parte B pede "brilho" configurável no menu). TODO(HW)/decisão: adicionar PWM
  de backlight (mudança de hardware) ou aceitar só liga/desliga.

## Encoder rotativo

- TIM2 em modo encoder de hardware, mas com `EncoderMode = TIM_ENCODERMODE_TI1` — modo que conta
  apenas transições do canal TI1 (equivalente a metade da resolução de um encoder de quadratura
  completo, que usaria `TIM_ENCODERMODE_TI12`). TODO(HW) H1: confirmar se isso é intencional
  (ex.: encoder de detentes que só emite pulso limpo em um canal) ou se deveria ser TI12.
- `IC1Filter = IC2Filter = 0` — **sem filtro de entrada por hardware**; qualquer *debounce* de
  ruído mecânico do encoder depende inteiramente do firmware (`Encoder_Update()`).
- Botão do encoder: GPIO simples (PA2), sem pull configurado no `.ioc` — TODO(HW): confirmar se
  há pull-up/pull-down externo na placa (senão o pino pode flutuar).

## Pedal / botão de usuário

- Pedal: TIM4 input capture, canal 1, borda de subida, `ICFilter=10` (filtro de hardware ativo,
  ~10 amostras do clock do timer). Prescaler do TIM4 = 199 (mesma base de 272 MHz → 1,36 MHz),
  período máximo contável (`ARR=65535`) ≈ 48 ms antes de estourar — suficiente para capturar
  bordas de um acionamento manual, mas **não implementa medição de tempo de pressionamento longo
  diretamente no periférico** (isso teria que ser feito em software).
- `BT_USER`: GPIO simples (PA3), sem pull.
- **H2 resolvido (confirmado pelo usuário)**: pedal vai a nível lógico alto quando pressionado
  (repouso = baixo) — bate com a borda de captura já configurada
  (`TIM_INPUTCHANNELPOLARITY_RISING` captura o instante do pressionamento). Nenhuma mudança de
  `.ioc` necessária.

## Sensor de pressão

- Canal único ADC2_INP5 (PB1), resolução 16 bits, oversampling ×4 com `RightBitShift=2`
  (mantém a saída em 16 bits efetivos), tempo de amostragem 64,5 ciclos, disparo por software
  (`ADC_SOFTWARE_START`) — **não é disparado por timer com DMA contínuo** como a Parte B pede
  (ADC disparado por timer a 1-2 kHz); hoje é leitura sob demanda em `ADC_Acq_Update()`, chamada
  a cada ciclo do loop de controle.
- Conversão para pressão feita por `offset_kpa` + `scale` lineares (`SensorConfig_t`), calibração
  de 1 ponto apenas — o assistente de calibração de 2-3 pontos do app DEV (Parte B 2.7) exigiria
  estender esse modelo (ou pelo menos validar que 1 ponto + ganho é suficiente para a linearidade
  real do sensor).
- **H3 resolvido (confirmado pelo usuário): sensor NXP/Freescale MPXV7025DP**, alimentado em
  **5,0 V** conforme datasheet (faixa 0–25 kPa gauge, ≈0–187,5 mmHg em fundo de escala, saída
  nativa ~0,2–4,7 V). **Confirmado que existe um divisor resistivo bufferizado na placa** entre a
  saída do sensor e `PB1`/`ADC2_INP5`, condicionando o sinal para 0–3,3 V — seguro para o ADC do
  H723. Sem risco de sobretensão no pino.
  A razão exata do divisor **não é necessária no firmware**: como a conversão pressão↔ADC já é
  feita por calibração linear (`offset_kpa`/`scale` em `SensorConfig_t`, carregados da EEPROM),
  o ganho efetivo do divisor+buffer é absorvido nessa calibração — basta medir 2-3 pontos de
  pressão conhecida (manômetro de referência na V3) contra a leitura crua do ADC durante o
  bring-up/calibração (Fase 2/9) e ajustar `scale`/`offset_kpa` empiricamente, sem precisar do
  valor dos resistores. Nenhum bloqueio remanescente aqui.

## EEPROM (armazenamento não-volátil de calibração/parâmetros)

- I2C1 (PB8/PB9), `Timing = 0x00C0216C` (perfil pré-calculado pelo CubeMX para o clock atual —
  não decodificado aqui; ver `I2C123Freq_Value=64000000` no `.ioc`).
- Endereço do dispositivo é passado como parâmetro (`addr_pins`) em vez de fixo — chamada atual
  em `main.c` usa `EEPROM_Storage_Init(&hi2c1, 0u)`.
- **H5 resolvido (confirmado pelo usuário): CAT24C512WI-GT3** (ON Semi, I2C, 512 Kbit = 64 KB,
  endereçamento de memória em 2 bytes). Isso confirma que a chamada de inicialização deve usar
  endereçamento de 16 bits (`addr_size = 2` no driver genérico `eeprom.c`/`eeprom_port_stm32_hal.c`,
  que já suporta os dois tamanhos via parâmetro — checar/ajustar na Fase 2 se `main.c`/
  `eeprom_storage_io.c` já passam isso corretamente). Ainda não confirmado: existência de um
  segundo chip físico para a cópia redundante A/B exigida pela Parte A, seção 5 — a esclarecer
  quando a Fase 5 chegar nesse ponto (nada bloqueante agora).

## USB-C

- `USB_OTG_HS` configurado no `.ioc` em **`Device_Role_Device_FS`** (PHY embutido Full-Speed,
  sem ULPI) — compatível com a exigência da Parte B de "equipamento é USB Device".
- Porém `main.h`/`main.c` também geram `MX_USB_OTG_HS_HCD_Init()` (inicialização como **Host**),
  e existem pinos `EN_VBUS` (PD4, saída) e `OC_USB` (PD5, EXTI de sobrecorrente) — esse par é a
  assinatura típica de uma chave de alimentação de VBUS com proteção (ex.: TPS2051/AP2151),
  normalmente usada quando o **MCU é quem fornece os 5 V** para um dispositivo externo (papel de
  host), não quando o MCU é o dispositivo alimentado pelo PC.
- **H6/H7 resolvido e confirmado (2026-09-19)**: "Internal FS Phy" = **OTG/Dual_Role_Device** no
  CubeMX (confirmado por captura de tela do usuário), com as seções **Device** (Full Speed
  12 Mbit/s) e **Host** (Full Speed 12 Mbit/s, 16 canais, DMA interno do host habilitado)
  configuradas lado a lado. `USB_OTG_HS.IPParameters` ganhou os parâmetros específicos de host
  (`dma_enable_host=ENABLE`, `use_dedicated_ep1`, `lpm_enable`) que não existiam antes — confirma
  que o modo dual-role está de fato ativo (o valor de texto interno do CubeMX para esse parâmetro,
  `Device_Role_Device_FS`, é só o nome fixo/legado que o ST usa para essa opção de PHY embutido —
  não significa "somente device").
  **Comportamento esperado, não é bug**: `main()` continua chamando só `MX_USB_OTG_HS_PCD_Init()`
  no boot — `MX_USB_OTG_HS_HCD_Init()` é gerado mas fica disponível para a aplicação chamar
  condicionalmente (lendo `USB_OTG_HS_ID`/PA10 e/ou `USB_OTG_HS_VBUS`/PA9 para decidir o papel em
  tempo de execução). O CubeMX não gera essa lógica de decisão sozinho — é trabalho de aplicação
  da Fase 7+, quando (e se) o papel Host entrar no escopo do firmware.
  **Atenção de escopo**: a Parte B do prompt-mestre (seção 2.6) assume "o equipamento é USB
  Device" e todo o Apêndice A (protocolo CDC) é desenhado só para esse papel. Meia dúzia de itens
  do prompt-mestre teriam que ser adaptados se o modo host também entrar no escopo do firmware
  (ex.: para que serviria o equipamento hospedar um dispositivo USB — leitor de cartão externo?
  impressora de etiqueta?). Recomendo **implementar só o papel Device** (CDC) nas Fases 7-9 como
  planejado, deixando o papel Host configurado no `.ioc`/hardware mas sem código de aplicação
  ainda, a menos que você tenha um caso de uso específico para ele — nesse caso, me diga qual para
  eu incluir no escopo.

## Cartão SD (SDMMC1 + FatFS)

- Barramento de **4 bits** já configurado (`SDMMC_BUS_WIDE_4B`), clock com *power save* habilitado,
  sem *hardware flow control*.
- Pino de *card-detect* dedicado (PD0), configurado como entrada simples sem pull — TODO(HW):
  confirmar se o soquete físico tem pull-up externo no CD ou se é preciso habilitar
  `GPIO_PULLUP` no firmware.
- FatFS vendorizado (`Middlewares/Third_Party/FatFs`), inicializado (`MX_FATFS_Init()`), mas sem
  nenhum uso de aplicação ainda.

## RTC / relógio de tempo real

- **H8 resolvido e configurado (2026-09-19)**: LSE ativado em `PC14-OSC32_IN`/`PC15-OSC32_OUT`,
  RTC ativado com calendário (`VP_RTC_VS_RTC_Activate`, `VP_RTC_VS_RTC_Calendar`), `MX_RTC_Init()`
  gerado e chamado em `main()`. Build confirmado limpo após a regeneração. Falta só a aplicação
  (Fase 2+) usar `HAL_RTC_GetTime`/`HAL_RTC_GetDate` de fato e implementar a detecção de "hora não
  ajustada"/RTC inválido exigida pela Parte A.

## Áudio (buzzer)

- **H10 resolvido e configurado (2026-09-19)**: `PC7` reconfigurado para `TIM8_CH2` (PWM Generation
  CH2), independente do `TIM3` do motor — `MX_TIM8_Init()` gerado e chamado em `main()`, com
  `HAL_TIM_MspPostInit(&htim8)`. Build confirmado limpo.
- **Pendente (firmware, não CubeMX)**: `Core/Bsp/Buzzer` ainda implementa `Buzzer_Beep()` por
  *bit-bang* de GPIO puro — não foi adaptado para usar o canal de PWM do `TIM8` ainda. Isso é
  trabalho da Fase 2 (reescrever o driver para escrever `ARR`/`PSC`/`CCR` do `TIM8` a cada chamada,
  já que a frequência do tom varia por chamada). Reprodução de `.wav` PCM do Apêndice B continua
  não viável sem DAC/I2S dedicado — fora de escopo por ora.

## Alimentação

- `HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY)`, `PWR_REGULATOR_VOLTAGE_SCALE0` (desempenho máximo,
  544 MHz de SYSCLK). TODO(HW): topologia de alimentação da placa (fonte externa, regulador de
  qual tensão de entrada, monitoramento de tensão de barramento) não está no `.ioc`/código —
  necessário para o requisito de segurança "tensão de alimentação fora da faixa" (erro E014 do
  Apêndice D do prompt-mestre).

## Resumo dos `TODO(HW)` (atualizado em 2026-09-19)

| ID | Item | Status |
|---|---|---|
| H1 | Encoder: TI1-only vs. TI12 (quadratura completa) | **Em aberto** — não perguntado ainda |
| H2 | Polaridade de repouso do pedal | **Resolvido**: pressionado = nível alto; compatível com a captura de borda de subida já configurada |
| H3 | Modelo/faixa/tensão do sensor de pressão | **Resolvido**: MPXV7025DP, 5V, divisor resistivo bufferizado confirmado (saída 0–3,3V para o ADC). Calibração será empírica (2-3 pontos), sem bloqueio |
| H4 | Válvulas NA/NF + comportamento em reset/tri-state | **Resolvido**: NA, estado de reset = aberta, compatível com o software atual |
| H5 | Modelo do chip EEPROM + 2º chip para redundância A/B | **Resolvido**: CAT24C512WI-GT3. Redundância A/B ainda em aberto (não bloqueante agora) |
| H6/H7 | Papel real do USB (device puro vs. dual-role) + resistores CC1/CC2 | **Resolvido e configurado**: OTG/Dual_Role_Device confirmado no CubeMX (Device+Host lado a lado). Lógica de decisão de papel em runtime é trabalho de firmware da Fase 7+ |
| H8 | Cristal LSE + bateria/VBAT do RTC | **Resolvido e configurado**: LSE+RTC ativos no `.ioc`, `MX_RTC_Init()` gerado, build limpo |
| H9 | Driver de potência do motor (tolerância de frequência de PWM) | **Resolvido e configurado**: `Prescaler=0`, `Period=13750`, ≈19,8-20 kHz, build limpo |
| H10 (novo) | Buzzer: `PC7` → PWM de timer | **Resolvido e configurado**: `TIM8_CH2` ativo, `MX_TIM8_Init()` gerado, build limpo. Driver do buzzer ainda não usa o PWM (pendente Fase 2) |
| — | Topologia de alimentação / monitoramento de tensão | **Em aberto** — não perguntado ainda; necessário para erro E014 |

Itens ainda em aberto (H1, alimentação/monitoramento de tensão) não bloqueiam a Fase 1 — retomar
quando as fases correspondentes chegarem neles. **Todo o restante do bloco de decisões de
hardware da Fase 0 está resolvido.**
