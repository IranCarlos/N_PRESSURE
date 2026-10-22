# IHM myPRESSURE — decisões

Especificação de origem: `Docs/PROMPT_UI_LVGL.md`. Reescrita em 2026-10-17.

## Alvo

| Item | Valor |
|---|---|
| Display | ST7789 SPI, 320×240 paisagem, RGB565, flush por DMA (`Core/Bsp/LvglPort`) |
| Entrada | só encoder (`LV_INDEV_TYPE_ENCODER`, um `lv_group_t`) + pedal/botão do usuário fora do LVGL |
| LVGL | v9.0.0 vendorizado, heap fixo `LV_MEM_SIZE` 64 kB, sem `malloc` do sistema |
| Fontes | Montserrat 10/14/16/22/34 embutidas, **sem acentos** (sem Latin-1); textos em PT sem acento |
| Tarefa | tudo roda na `uiTask` (FreeRTOS), a mesma que chama `lv_timer_handler()`; `HMI_Update()` a cada ciclo |

## Camadas

```
Core/App/HMI_UI/     portável: só lvgl.h + ui_backend.h  (compila no firmware E no pc_sim)
  hmi_ui.c             facade: navegação, popups
  ui_*_screen.c        uma tela por arquivo: Init / Show / Update
  ui_*_popup.c         resultado e erro sobre lv_layer_top()
  ui_theme.c           estilos static (paleta oficial), criados uma vez
  ui_widgets.c         construtores de botão/spinbox/roller/caixa + troca de tela
  ui_session.c         operador, participante, ciclo, buzzer on/off (só RAM)
  hmi_encoder_group.c  grupo único do encoder
  ui_backend.h         contrato com o resto do sistema
Core/App/HMI_Port/   firmware: ui_backend_stm32.c (Control_Loop, ADC_Acq, RTC, StorageTask, EEPROM, Buzzer)
pc_sim/              simulador: ui_backend_mock.c + main_pc.c + lv_conf.h
```

Nenhum arquivo de `HMI_UI` inclui HAL, `main.h`, `system_types.h` ou serviços.

## Regras

- **Uma tela alocada por vez.** `UiW_LoadScreen()` carrega a nova e apaga a anterior com
  `lv_obj_delete_async()`. Manter todas as telas vivas estourou os 64 kB ao abrir os Ajustes
  (visto no simulador).
- `Show()` sempre recria a tela e recarrega o grupo do encoder; `Update()` não faz nada se a tela não
  estiver ativa e só reescreve rótulo cujo valor mudou (o display é SPI).
- Telas não se conhecem: só chamam callbacks registrados em `hmi_ui.c`.
- Arrays de focáveis com tamanho por `HMI_ARRAY_COUNT()`; nos Ajustes, `LV_ASSERT` confere a contagem.
- Caixa com título: título em `LV_ALIGN_TOP_LEFT`, nunca `LEFT_MID`.
- Caixas com `pad_all = 0`: as coordenadas dos layouts são px a partir da borda da caixa.
- Números com `%d` e inteiros: o `sprintf` embutido do LVGL não formata `float` (`"%.1f"` sairia
  como `"f"`); a queda do teste de vazamento é impressa em décimos inteiros.
- O alarme de erro na IHM é só notificação. A sinalização crítica não pode depender da GUI viva.

## Comportamento de navegação

```
BOOT --(1,5 s ou clique)--> HOME <--> MENU --> NOVO ENSAIO --(Confirmar)--> HOME (ensaio comeca pelo pedal)
                                         \--> AJUSTES --(Voltar: salva config)--> MENU
Popups (lv_layer_top): RESULTADO DO ENSAIO, ERRO
```

- O erro tem prioridade: se chegar com o resultado aberto, o resultado é fechado. Um resultado que
  chegar com o erro aberto espera o erro ser fechado.
- O fim de ciclo é ignorado nos Ajustes (o teste de vazamento usa a mesma máquina de estados e não
  pode gerar popup clínico nem registro no SD).
- O contador de ciclo avança na transição IDLE → ativo (não conta CONFIG nem LEAK_TEST), observada
  em qualquer tela, para não perder um ensaio iniciado pelo pedal fora da Home.
- Sair dos Ajustes ou abrir/fechar um popup sobre eles desliga a bomba de calibração e devolve as
  válvulas ao controle (`UiBackend_CalAbort()`).

## Layouts

Cada `ui_*.c` tem no topo o layout em ASCII com as coordenadas em px. Para conferir visualmente:
`pc_sim/README.md` → "Capturas automáticas".

## Pendências

- Bancada: a IHM nova ainda não rodou no hardware. Conferir em especial a calibração (V3 + bomba) e o
  teste de vazamento, que comandam atuadores.
- `g_lv_mem` (Live Watch) com Ajustes aberto e um popup por cima: confirmar margem ≥ 20 % do heap.
- Fontes com acento (Latin-1) exigiriam gerar fontes com `lv_font_conv` (custo de Flash).
