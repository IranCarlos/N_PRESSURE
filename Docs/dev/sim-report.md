# Relatório do simulador — Fase 3 (Control_Core)

Corresponde ao `docs/sim-report.md` pedido na Parte C do prompt do projeto (aqui em
`Docs/dev/` pela convenção de docs vivos já usada no projeto — ver `00-auditoria.md`).

## O que foi testado

`Core/Services/Control_Core` é a mesma lógica de `Core/Services/Control_Loop` (reaproveitada por
decisão do usuário, D4 em `00-auditoria.md`), reorganizada em **C puro, sem HAL**: recebe `dt_ms` em
vez de ler `HAL_GetTick()`, devolve os comandos de atuador numa struct em vez de chamar
`Valve_SetAll()`/`PwmMotor_SetPower()`. `Control_Loop.c` (ligado ao hardware) **não foi alterado**
nesta fase — a integração com o hardware real é Fase 4, com verificação em bancada obrigatória.

O simulador de planta (`test/host/sim_plant.c`) é um modelo de 1ª ordem simples (fluxo da bomba com
atraso de 1ª ordem representando `T_lag`, sangria por válvula aberta e vazamento opcional, ambos
lineares em P, dividido pela capacitância do manguito). **Não é um modelo validado fisicamente** —
serve para exercitar a lógica do Control_Core antes do hardware, como pede a Parte C/Fase 3. Os
ganhos reais do manguito/bomba só são conhecidos depois de medidos na bancada (Fase 4).

Testes em `test/host/test_control_core.c` (rodar com `bash test/host/run.sh`):

| Cenário | Resultado | Critério |
|---|---|---|
| Manguito pequeno/rápido (C=1), sem vazamento/ruído | overshoot 3,33 mmHg, estabiliza em 11,3 s e **mantém** | ver nota abaixo sobre overshoot_max |
| Manguito grande/lento (C=4), sem vazamento/ruído | overshoot 0,51 mmHg, estabiliza em 38,7 s e mantém | ≤ 2 mmHg (placeholder) — **passa** |
| Sem chattering após estabilizar de verdade | 1 transição de válvula (artefato da troca de amostra, não chattering real — ver comentário no teste) | — |
| Vazamento durante manutenção | detectado como E013, ensaio encerrado e desinflado | leva ao erro correto — **passa** |
| Sensor travado/inválido | E002, estado seguro (bomba off, V1+V2+V3 abertas) imediato | **passa** |
| Sobrepressão absoluta (salto de pressão) | E001, estado seguro no mesmo tick | **passa** |
| Pedido de abort (pedal/bt_user) | desinfla e retorna a IDLE | **passa** |

Todos os parâmetros de PID e de segurança usados nos testes são **placeholders**, exatamente como o
Apêndice E do prompt classifica a maioria deles ("a definir"/"medir"). Nenhum deve ser tratado como
valor final antes da bancada (Fase 4).

## Achados (defeitos/decisões encontrados só por existir o simulador)

Esta é a parte mais importante do relatório: o propósito da Fase 3 é achar isso **antes** do
hardware.

1. **Os ganhos de PID gravados em `EEPROM_Storage_LoadDefaults()` (`kp=2.5 ki=0.25 kd=0.02 ff=1.5
   out_max=1000 i_min=-300 i_max=300`) nunca foram testados em lugar nenhum** (`Control_Loop.c` é o
   ramo morto atrás de `APP_MODE_BRINGUP`, nunca executado — ver CLAUDE.md). No simulador, eles
   produzem overshoot de **~60 mmHg** e nunca estabilizam: o anti-windup (`i_max=300`, 30% da escala
   de saída) é grande demais e a banda de alívio (10 mmHg) deixa a pressão subir bastante antes de
   qualquer correção. Os valores usados nos testes acima (`kp=0.8 ki=0.015 kd=0.15 ff=1.5
   i_min=-8 i_max=8`) são o ponto de partida sugerido — ainda não validado em bancada.
2. **Bug: o piso de `PUMP_MIN_COMPENSATION_POWER` (20%) era aplicado também durante a aproximação**
   (dentro de `run_pid_iteration()`, sempre que `|erro| <= 5 mmHg`, mesmo com a pressão ainda
   *subindo* pela primeira vez rumo ao setpoint). Isso força potência mínima bem na hora de cruzar o
   alvo — um dos maiores causadores do overshoot de 60 mmHg do item 1. Removido no Control_Core (a
   fase de manutenção real, `handle_state_stabilized`, nem chama essa função — ela já desliga a
   bomba). **`Control_Loop.c` ainda tem esse comportamento sem correção** — revisar antes da Fase 4.
3. **Bug: o mesmo piso de 20% também era reaplicado dentro do ramo de sobrepressão *leve*** (depois
   de já ter decidido aliviar), fazendo a bomba voltar para ~12% (20% × 0,6) bem quando deveria estar
   sendo cortada para a válvula vencer. Em manguitos pequenos/rápidos isso causa uma "corrida" de
   sobrepressão: o fluxo residual da bomba supera o que a válvula consegue sangrar nos pulsos curtos,
   e a pressão nunca para de subir. Corrigido no Control_Core (zera a bomba na sobrepressão forte,
   reduz para 30% sem piso na leve). **Mesmo bug presente em `Control_Loop.c`.**
4. **`RELIEF_PULSE_FAST_MS`/`RELIEF_PULSE_STRONG_MS` = 1 ms são menores ou iguais ao próprio período
   do loop de controle (1 ms)** — na prática o pulso de alívio quase nunca fica visível por mais de
   uma chamada. É consistente com `t_min_valvula` do Apêndice E ainda estar como "medir". Usados 30
   ms/80 ms nos testes do Control_Core; `Control_Loop.c` continua com 1 ms.
5. **Achado de arquitetura (não é bug, é limitação do desenho atual):** o manguito pequeno/rápido
   ficou em oscilação prolongada (ciclo limite) ao entrar em `STABILIZED` antes da correção do item
   6 abaixo — a inércia da bomba (filtro de 1ª ordem, `T_lag`) continua empurrando a pressão por uma
   fração de segundo depois do comando de potência cair a zero, o suficiente para sair da banda de
   2–3 mmHg e voltar para `INFLATING`, repetindo o ciclo. Isso é exatamente o tipo de problema que a
   estratégia preditiva descrita na seção 2.3 do prompt (`p_prev = p + dP/dt·T_lag`) foi desenhada
   para evitar — o PID reativo simples (reaproveitado por D4) não antecipa a inércia da bomba.
6. **Correção aplicada — histerese na saída de `STABILIZED`:** o Control_Loop atual usa a MESMA
   banda (`stabilization.band_mmhg`) tanto para entrar quanto para sair da fase de manutenção. O
   Control_Core usa o dobro dessa banda para *sair*, dando margem à inércia do item 5 sem precisar
   voltar a inflar. Isso resolveu o ciclo limite no simulador (manguito pequeno: de nunca estabilizar
   de verdade em 60 s para estabilizar em 11,3 s e manter). **Não portado para `Control_Loop.c`
   ainda** — candidato óbvio para quando a Fase 4 reaproveitar esta lógica.
7. **Novo (não existia em `Control_Loop.c`): detecção de vazamento durante a manutenção (E013)**,
   por janela não sobreposta (`t_janela_ms`/`dp_vazamento_mmhg`); testado e funcionando no simulador.
8. **Novo: proteção de sobrepressão absoluta (`P_MAX_ABS` → E001)** e **sensor inválido com debounce
   (→ E002)**, ambas preemptivas a qualquer estado, incluindo `IDLE`/`CONFIG`. Nenhuma das duas
   existia em `Control_Loop.c`.
9. **Novo: timeouts de inflar (E003) e desinflar (E004)**, conforme Apêndice D/E.
10. `SYSTEM_STATE_LEAK_TEST` (calibração/teste manual de vazamento) e o modo manual
    (`ControlLoop_SetManualActuators`) **não foram portados** para o Control_Core nesta fase — fora
    do escopo da Fase 3 (não fazem parte do ciclo normal de ensaio). Ficam como stub seguro
    (saída fechada) se esse estado for alcançado.

## Sobre o `overshoot_max`

O Apêndice E deixa este valor como "a definir (ex.: ≤ 2 mmHg)". Com os ajustes acima, o manguito
grande passa nesse exemplo (0,51 mmHg); o pequeno fica em 3,33 mmHg. Dado que o próprio prompt trata
isso como exemplo e não como especificação fechada, e que a banda de alívio pode ser apertada ainda
mais às custas de mais chaveamento de válvula (trade-off), a recomendação é **decidir o valor real
de `overshoot_max` junto com o usuário** (critério clínico, não só de engenharia) e then re-tunar
este mesmo simulador — ele já está pronto para isso — antes de qualquer ajuste em hardware.

## O que NÃO este relatório garante

- Os números acima são de um modelo de planta **inventado para este teste**, não medido. T_lag,
  vazão de cada válvula, capacitância real do manguito — tudo "a definir/medir" conforme Apêndice E.
- Nenhum destes parâmetros deve ir para o hardware sem passar pela bancada (Fase 4), começando com
  reservatório de volume conhecido e pressão baixa, subindo gradualmente, exatamente como o prompt
  do projeto exige.
- `Control_Loop.c` (o módulo que realmente comanda a bomba/válvulas hoje) **não foi alterado** —
  continua com os bugs 1–4 acima até a Fase 4 decidir como portar as correções com verificação em
  bancada.
