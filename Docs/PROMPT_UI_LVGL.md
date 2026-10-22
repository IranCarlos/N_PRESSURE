# Prompt — Interface myPRESSURE em LVGL v9 (projeto separado / simulador)

> Copie tudo abaixo da linha e cole no novo projeto/assistente.

---

## Contexto

Você vai construir a interface gráfica (IHM) do **myPRESSURE** (marca: **Nobrega Technology**), um
equipamento médico que infla um manguito até uma pressão-alvo (setpoint), mantém estabilizado e
desinfla. Em produção a UI roda em um **STM32H723 (Cortex-M7, bare-metal + FreeRTOS)** com display
**ST7789 SPI 320×240** e um **encoder rotativo com botão** como ÚNICA entrada (mais um pedal que só
equivale ao clique "iniciar/parar"). **Não há touch nem teclado.**

O objetivo deste projeto é desenvolver/iterar a UI **fora do hardware** (simulador de PC com LVGL +
SDL, ex.: `lv_port_pc_vscode`, ou outro ambiente LVGL), com dados simulados, e depois trazer o código
de volta para o firmware **com o mínimo de adaptação**.

## Restrições técnicas (obrigatórias)

- **LVGL v9** (API v9: `lv_button_create`, `lv_screen_load`, `lv_obj_remove_style_all`,
  `lv_display_create`, etc. — nada da API v8).
- Resolução **320×240, paisagem, `LV_COLOR_DEPTH 16` (RGB565)**.
- **C99 puro**, sem C++, sem `malloc` fora do alocador do LVGL, sem dependências extras.
- Fontes disponíveis (só estas estão habilitadas no firmware): **Montserrat 10, 14, 16, 22, 34**.
  Se precisar de outra, diga explicitamente — custa flash.
- Símbolos: apenas os `LV_SYMBOL_*` embutidos.
- Textos **em português sem acentos** (as fontes embutidas não têm glifos acentuados): "Calibracao",
  "Pressao", "Nao conforme", etc.
- Sem animações pesadas: `anim_duration = 0` em spinbox/roller; telas trocadas com `lv_screen_load`
  simples. O display é SPI — redesenho de tela inteira é caro.
- Entrada: **`LV_INDEV_TYPE_ENCODER`** ligado a um único `lv_group_t`. No simulador, mapeie roda do
  mouse/setas = girar, Enter/clique = pressionar, segurar = long-press. **Todo elemento interativo
  precisa ser alcançável e operável só pelo encoder.**

## Identidade visual

Estilo "minimalismo, tecnologia, sofisticação" (referência Apple: clareza, formas limpas), alto
contraste, legível a ~1 m.

| Token | Hex | Uso |
|---|---|---|
| BG_SCREEN | `#16092C` | fundo de tela (roxo profundo) |
| BG_BOX | `#291B54` | caixas/painéis |
| BORDER | `#4A3A7A` | borda 1px das caixas, raio 4 |
| TITLE | `#FFFFFF` | títulos (Montserrat 22) |
| BODY | `#D9D4F5` | texto corrido (Montserrat 14) |
| HINT | `#9C90C9` | texto secundário (Montserrat 14) |
| BIG_VALUE | `#FFFFFF` | valor de pressão (Montserrat 34) |
| BTN_BG / BTN_PRESSED | `#332368` / `#4A3A8C` | botão, raio 6 |
| ACCENT | `#8B8BF0` | contorno de foco 2px (`LV_STATE_FOCUSED`), destaques |
| INPUT_BG / INPUT_BORDER | `#241A47` / `#5B4A96` | spinbox/roller |
| SELECTED | `#4A3A8C` | `LV_PART_SELECTED` do roller |
| OK / WARN / DANGER | `#9EE7B3` / `#F5D76E` / `#FF6B6B` | semântica (não mudar) |
| NEUTRAL | `#A79BDB` | estado neutro |

Cor por estado do sistema (banner + barra de pressão):

| Estado | Texto | Cor |
|---|---|---|
| IDLE | "Aguardando" | NEUTRAL |
| INFLATING | "Inflando..." | `#FFB347` |
| STABILIZING | "Estabilizando" | `#66E2FF` |
| STABILIZED | "Estabilizado - Pronto p/ desinflar" | OK |
| DEFLATING_FAST | "Desinflando" | `#F36C6C` |
| LEAK_TEST | "Teste de vazamento" | WARN |
| CONFIG | "Modo configuracao" | NEUTRAL |
| FAULT | "Falha" | DANGER |
| BOOT | "Inicializando" | NEUTRAL |

Centralize tudo isso num módulo **`ui_theme.c/h`** com helpers (`UiTheme_StyleScreen`,
`UiTheme_StyleBox`, `UiTheme_StyleButton`, `UiTheme_StyleFocusOutline`, `UiTheme_StyleInputField`,
`UiTheme_LabelTitle/Body/Hint/Big`, `UiTheme_ColorForState`, `UiTheme_ColorOk/Warn/Danger/Accent/Track`).
Nenhuma tela usa cor hex solta.

## Telas e fluxo

```
BOOT --(1,5 s ou clique)--> HOME <--> MENU --> NOVO ENSAIO --(Iniciar)--> HOME
                                         \--> AJUSTES --(Voltar: salva config)--> MENU
Popups sobrepostos (lv_layer_top): RESULTADO DO ENSAIO, ERRO
```

### 1. Boot
Centro: "myPRESSURE" (Montserrat 34, branco), abaixo "Nobrega Technology" (14, ACCENT), abaixo
"Inicializando sistema" (hint). Rodapé: duas linhas de status atualizadas ao vivo —
"Cartao SD: verificando... / OK / ausente" e "Relogio: verificando... / OK / ajustar data/hora".
Sai sozinha após **1500 ms mínimo** ou ao clique. Sem foco/grupo.

### 2. Home (tela principal, atualizada a cada ~20 ms)
De cima para baixo (margens laterais 6–8 px):
- **Banner de estado** (caixa 26 px de altura): texto do estado com a cor do estado.
- **Caixa de pressão** (86 px): valor grande `"%03d mmHg"` (Montserrat 34) + `lv_bar` horizontal 12 px
  de 0 até `max_setpoint` (trilho INPUT_BG, indicador com a cor do estado).
- Linha: "Setpoint: 160 mmHg" (esquerda) | durante ensaio "42s | pico 163 mmHg" (direita, vazio em IDLE).
- Linha hint: "Operador: OP03 | Participante: P037".
- Linha hint: "17/10/2026 14:32" (esquerda) | `LV_SYMBOL_SD_CARD " OK"` em OK ou `" --"` em WARN (direita).
- Rodapé esquerdo (hint): "Pressione o encoder/pedal para iniciar/parar".
- Rodapé direito: botão pequeno 34×28 com `LV_SYMBOL_LIST` → abre o Menu. É o único item focável.
- O contador de ciclo da sessão avança ao detectar transição IDLE → qualquer estado ativo; o pico é
  o máximo de pressão desde essa transição.

### 3. Menu
Título "Menu". Três botões largos (largura total −24, altura 36, empilhados):
"Novo Ensaio", "Ajustes", `LV_SYMBOL_LEFT " Voltar"`. Grupo com wrap.

### 4. Novo Ensaio
Título "Novo Ensaio". Três linhas (caixa 32 px, rótulo à esquerda, spinbox 96×26 à direita):
- "Operador (OP)" 0–99
- "Participante (P)" 0–999
- "Setpoint (mmHg)" 0–`max_setpoint`
Rodapé: botões 150×32 `LV_SYMBOL_PLAY " Iniciar"` (esq.) e `LV_SYMBOL_LEFT " Cancelar"` (dir.).
"Iniciar" grava operador/participante na sessão, aplica o setpoint, dispara a inflação e volta à Home.

### 5. Ajustes (tela rolável)
Cabeçalho fixo (24 px): "Ajustes" à esquerda, "Role para ver mais" (hint) à direita.
Área central rolável (vertical, scrollbar oculta; o foco do encoder rola até o item). Seções, cada uma
numa caixa com título hint no **canto superior esquerdo** (`LV_ALIGN_TOP_LEFT`, nunca `LEFT_MID` —
quebra quando a caixa muda de altura):
1. **Setpoint** — spinbox 0–max, aplica ao mudar.
2. **Unidade** — roller 1 linha: kPa, hPa, mmHg, psi, cmH2O, bar.
3. **Buzzer** — botão "Testar" + botão alternável "Ligado/Desligado" (padrão Ligado a cada boot).
4. **Calibracao (manometro na V3)** — fluxo guiado, cada item em linha própria:
   "Leitura bruta: N mmHg" (ao vivo) · botão "Zerar (ar)" · "Pressao de teste (%)" spinbox 0–100
   passo 10 · "P1" spinbox 0–300 + "Capturar" + rótulo do ponto capturado · "P2" idem ·
   botão "Calcular e salvar" · linha de resultado (ex.: "Pontos invalidos (P1=P2?).").
5. **Teste de vazamento** — "Tempo: N s" (contagem regressiva), "Queda: X.X mmHg",
   "Resultado: aguardando / APROVADO (OK) / REPROVADO (DANGER)".
6. **Data/Hora (DD MM AAAA HH MM)** — 5 spinboxes pequenos + botão "Aplicar".
7. **Operador ativo (OP)** — spinbox 0–99.
8. **Informacoes** (só leitura) — "N_PRESSURE-001 | FW x.y.z", "Cartao SD: OK/ausente",
   "Ensaios nesta sessao: N".
Rodapé fixo (fora da rolagem): botões 152×28 "Teste de Vazamento" (esq.) e `LV_SYMBOL_LEFT " Voltar"` (dir.).

**Não** coloque nesta tela ganhos de PID, PWM manual livre, toggles soltos de válvula nem
offset/escala crus de sensor — isso é proibido no nível de usuário (vai para um app de PC separado).

### 6. Popup de resultado (ao finalizar ensaio)
Fundo `lv_layer_top()` preto 50 % sobre a tela inteira; card centralizado (largura −40, altura 170)
com estilo de caixa: título "Ensaio concluido"; linha Montserrat 16 "Conforme (NC00)" em OK ou
"Nao conforme (NCxx)" em DANGER; três linhas body: "Pico: N mmHg", "Tempo ate setpoint: N s",
"Overshoot: N mmHg"; botão "Fechar" 100×28. **Fecha sozinho após 8 s.** Enquanto aberto, o grupo do
encoder contém só o "Fechar"; ao fechar, a tela de baixo recebe o grupo de volta.

### 7. Popup de erro
Mesmo esquema, card altura 150 com borda DANGER 2 px; título `LV_SYMBOL_WARNING " E001"`
(Montserrat 22, DANGER); descrição curta (body, quebra de linha) e ação recomendada (hint, quebra de
linha); botão "Fechar". Só fecha pelo botão. Tabela:

| Código | Descrição | Ação |
|---|---|---|
| E001 | Sobrepressao detectada | Ensaio invalidado. Verifique o manguito e reinicie. |
| E002 | Falha no sensor de pressao | Novos ensaios bloqueados. Verifique a conexao do sensor. |
| E003 | Nao atingiu o setpoint a tempo | Ensaio abortado e desinflado. Verifique vazamentos/manguito. |
| E004 | Tempo de desinflar excedido | Valvulas mantidas abertas. Verifique o circuito pneumatico. |
| E013 | Vazamento detectado | Ensaio encerrado com aviso. Verifique conexoes/manguito. |
| outro | Erro nao catalogado | Consulte o registro de eventos. |

## Interação com o encoder (padrão a seguir em todas as telas)

- Girar = mover foco; clique em botão = ação.
- Clique num spinbox/roller = entra em modo edição (`lv_group_set_editing(true)`); girando altera o
  valor; novo clique no spinbox avança o dígito (`lv_spinbox_step_next`); **long-press ou perder o
  foco** sai da edição.
- Cada tela monta seu grupo a partir de um array de focáveis na ordem visual, com wrap. Crie um
  helper tipo `HMI_EncoderGroup_Load(const lv_obj_t **objs, count, initial_idx, wrap)` que limpa o
  grupo e o recarrega. **Calcule o tamanho do array a partir do número real de itens** (no firmware
  já houve estouro de pilha por um `#define` de contagem desatualizado).

## Arquitetura de código (para portar de volta sem retrabalho)

- Um par `.c/.h` por tela: `ui_boot_screen`, `ui_home_screen`, `ui_menu_screen`,
  `ui_new_test_screen`, `ui_settings_screen`, `ui_result_popup`, `ui_error_popup`, mais `ui_theme`,
  `ui_session` (operador, participante, contador de ciclo, buzzer on/off — só RAM),
  `hmi_encoder_group` e um facade `hmi_ui.c` com `HMI_Init(...)` e `HMI_Update()` (chamado a cada 20 ms).
- Cada tela expõe `UI_Xxx_Init(dependências, callbacks)`, `UI_Xxx_Show()` e, se tiver dado ao vivo,
  `UI_Xxx_Update()`. **`Show()` destrói e recria a tela do zero** e recarrega o grupo do encoder.
  `Update()` não faz nada se `lv_screen_active()` não for a sua tela.
- Navegação só por callbacks registrados no facade — telas não se conhecem entre si.
- **Nenhuma tela acessa hardware diretamente.** Toda leitura/ação passa por uma camada fina
  `ui_backend.h` que no simulador é um mock e no firmware será ligada aos serviços reais:

```c
/* Estado vindo do controle */
typedef enum { ST_BOOT, ST_IDLE, ST_INFLATING, ST_STABILIZING, ST_STABILIZED,
               ST_DEFLATING_FAST, ST_LEAK_TEST, ST_CONFIG, ST_FAULT } UiState_t;

UiState_t UiBackend_GetState(void);
float     UiBackend_GetPressureMmHg(void);
float     UiBackend_GetSetpointMmHg(void);
uint16_t  UiBackend_GetMaxSetpointMmHg(void);     /* default 300 */
uint32_t  UiBackend_GetTickMs(void);
bool      UiBackend_GetDateTime(UiDateTime_t *out);
bool      UiBackend_SetDateTime(const UiDateTime_t *dt);
bool      UiBackend_IsSdMounted(void);
bool      UiBackend_IsRtcValid(void);
uint16_t  UiBackend_GetLastError(void);            /* 0 = nenhum */
bool      UiBackend_GetLastCycleResult(UiCycleResult_t *out); /* pico, tempo, overshoot, conforme, nc_code */
float     UiBackend_GetRawPressureMmHg(void);      /* calibracao */
uint32_t  UiBackend_GetLeakHoldTimeMs(void);
float     UiBackend_GetLeakMaxDropMmHg(void);

/* Ações */
void UiBackend_SetSetpoint(float mmhg);
void UiBackend_StartInflation(void);
void UiBackend_EnterConfigMode(void);
void UiBackend_ExitConfigMode(void);               /* também salva a config */
void UiBackend_StartLeakTest(void);
void UiBackend_SetUnit(uint8_t unit_idx);
void UiBackend_BuzzerBeep(uint16_t freq_hz, uint16_t ms);
void UiBackend_CalZero(void);
void UiBackend_CalSetPumpPct(uint8_t pct);
void UiBackend_CalCapture(uint8_t point, float ref_mmhg);
bool UiBackend_CalComputeAndSave(char *msg, size_t msg_len);
```

- No simulador, implemente o mock com uma máquina de estados fake: clique em IDLE inicia
  INFLATING (pressão sobe ~20 mmHg/s até o setpoint) → STABILIZING (2 s) → STABILIZED (oscila ±2) →
  clique → DEFLATING_FAST (desce até 0) → IDLE e publica um resultado. Adicione teclas de atalho
  para injetar E001–E013 e alternar SD/RTC ok/ausente, para testar os popups e a Home.
- Tons do buzzer (via backend, respeitando "buzzer ligado"): conclusão conforme = 1800 Hz/100 ms +
  2400 Hz/150 ms; não conforme = 900 Hz/250 ms; erro = 3× 600 Hz/120 ms.

## Entregáveis

1. Projeto de simulador compilável (CMake) com LVGL v9 + SDL, 320×240, encoder simulado.
2. Os módulos acima, com o mock do backend em arquivo separado (`ui_backend_mock.c`).
3. `lv_conf.h` compatível com as restrições (RGB565, fontes listadas, `LV_USE_FLEX`/`LV_USE_GRID` ok).
4. Um README curto: como compilar/rodar, mapa de teclas e lista de funções do `ui_backend.h` que o
   firmware precisa implementar.

Antes de codificar, confirme que entendeu as restrições e proponha o layout de cada tela em ASCII
com coordenadas aproximadas em px; depois implemente tela por tela, começando pela Home.
