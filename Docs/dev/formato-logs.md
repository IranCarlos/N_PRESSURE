# Formato de logs / trilha de auditoria — especificação extraída

> Fonte primária (conforme instrução da Parte B do prompt-mestre): 
> `Docs/Trilha eletrônica de auditoria e rastreabilidade dos ciclos pneumáticos.md`.
> Este documento apenas extrai e organiza os campos como especificação técnica; não altera nem
> substitui o arquivo original. Conflitos com o Apêndice C/D do prompt-mestre estão marcados e
> também listados em `Docs/dev/00-auditoria.md`, seção 4.

## 1. Campos do registro de ciclo (trilha de auditoria)

| Campo | Nomenclatura técnica | Tipo/formato | Limite operacional (fase piloto) |
|---|---|---|---|
| Identificador do acionamento | ID único | `ID000001`, `ID000002`, ... (sequencial, 6 dígitos) | — |
| Identificação do participante | Código pseudonimizado | `P001`, `P002`, ... | — |
| Identificação da manobra | Identificador do ciclo pneumático | `C1`, `C2`, `C3`, ... | — |
| Data/hora | Data e horário do ciclo | `DD/MM/AAAA – hh:mm:ss` | — |
| Pressão programada | Pressão-alvo programada | mmHg | — |
| Maior pressão atingida | Pressão máxima atingida | mmHg | ≤ 105% da pressão-alvo |
| Pressão mantida | Pressão média durante o platô | mmHg | — |
| Estabilidade da pressão | Variação pressórica no platô | mín.–máx. | −2% a +2% da pressão-alvo |
| Tempo para atingir a pressão | Tempo de insuflação até o alvo | s | até 10 s |
| Tempo sob pressão | Duração do platô | s | até 10 s |
| Tempo de esvaziamento | Tempo de descompressão | s | 1 a 2 s |
| Pressão após esvaziamento | Pressão residual pós-descompressão | mmHg | < 5 mmHg |
| Resultado operacional | Status de conformidade | `Conforme` / `Não conforme` | — |
| Código de não conformidade | Código técnico | `NC00`–`NC08` | — |
| Identificação do aparelho | Nº de série do dispositivo | string | — |
| Software | Versão do firmware | string (ex.: `FW 1.0.3`) | — |
| Calibração | Data da última verificação metrológica | `DD/MM/AAAA – hh:mm:ss` | — |

Estes limites operacionais são **explicitamente preliminares** (nota 2 das "ORIENTAÇÕES" do
documento-fonte): valem para a fase piloto e serão revistos/congelados antes da coleta definitiva.
Isso substitui grande parte dos valores "a definir" do Apêndice E do prompt-mestre — ver
mapeamento na seção 4 abaixo.

## 2. Códigos de não conformidade (NC)

| Código | Significado |
|---|---|
| NC00 | Conforme |
| NC01 | Pressão-alvo não atingida |
| NC02 | Instabilidade pressórica durante o platô |
| NC03 | Tempo de insuflação fora da faixa especificada |
| NC04 | Duração do platô fora da faixa especificada |
| NC05 | Tempo de descompressão fora da faixa especificada |
| NC06 | Pressão residual pós-descompressão acima do limite |
| NC07 | Ciclo interrompido pelo operador |
| NC08 | Falha técnica do dispositivo |

**Distinção importante em relação ao Apêndice D do prompt-mestre**: os códigos `NC0x` avaliam
**conformidade clínica/metrológica do ciclo** (o ciclo rodou tecnicamente bem, mas o resultado
ficou fora de tolerância). Os códigos `E0xx` do Apêndice D avaliam **falhas técnicas do
equipamento** (sensor travado, timeout, SD ausente, watchdog, etc.). `NC08` ("falha técnica do
dispositivo") é o ponto de junção entre os dois sistemas: um erro técnico `E0xx` durante um ciclo
deve ser registrado tanto no log de erros (formato `E0xx`) quanto refletido como `NC08` no
registro de ciclo. Proposta a confirmar com o usuário (pergunta P1 em `00-auditoria.md`).

## 3. Regras de integridade e formato

1. A trilha é gerada automaticamente e **formalmente não editável pelo operador** — equivalente
   ao requisito "append-only" da Parte A, seção Auditoria.
2. Cada linha é encadeada por hash: `hash = SHA-256(prev_hash ‖ conteúdo_da_linha)`. O primeiro
   registro do mês referencia o último hash do mês anterior — já compatível com o esquema de
   encadeamento mensal descrito no Apêndice C do prompt-mestre (`/logs/events/AAAA-MM.log`).
3. Reajustes de data/hora do RTC devem ser registrados automaticamente na trilha, preservando
   valor anterior, novo valor e momento da alteração (requisito adicional do `Docs/`, não
   coberto explicitamente pelo Apêndice A/D do prompt-mestre — adicionar como evento de auditoria
   dedicado, ex. `EVT_RTC_ADJUSTED`).
4. Alteração de versão de firmware/parâmetros operacionais deve ser documentada com versão,
   data/hora e descrição da mudança — coerente com o log de auditoria (`/logs/audit/AAAA-MM.log`)
   do Apêndice C.
5. **Reexecução de um ciclo gera um novo ID**, nunca reaproveita ou sobrescreve o anterior —
   unicidade absoluta mesmo quando o mesmo participante repete a mesma etapa/ciclo (exemplo do
   `Docs/`: `ID000126` não-conforme seguido de `ID000127` conforme, ambos referenciando `P038`/`C2`).

## 4. Exemplos de registro (do documento-fonte)

**Ciclo conforme:**
```
ID000123 | P037 | 11/08/2026 09:42:16 | Ciclo 2 | Alvo 100 mmHg | Máx. 100,4 |
Platô médio 99,8 | Faixa 99,2–100,4 | Insuflação 3,2 s | Platô 8,0 s |
Descompressão 0,94 s | Pressão residual 1,2 mmHg | Conforme | NC00 |
nPressure NP001 | FW 1.0.3 | última calibração 02/08/2026 18:23:47
```

**Ciclo não conforme (tempo de descompressão fora da faixa):**
```
ID000126 | P038 | 11/08/2026 09:42:16 | Ciclo 2 | Alvo 100 mmHg | Máx. 100,4 |
Platô médio 99,8 | Faixa 99,2–100,4 | Insuflação 3,2 s | Platô 7,0 s |
Descompressão 2,1 s | Pressão residual 1,2 mmHg | Não conforme | NC05 |
nPressure NP001 | FW 1.0.3 | última calibração 02/08/2026 18:23:47
```

## 5. Exportação CSV

**Trilha de auditoria** (uma linha por `ID000XXX`):
```
ID,Participante,DataHora,Ciclo,PressaoAlvo,PressaoMaxima,PlatoMedio,Descompressao,Status,CodigoNC,Dispositivo,Firmware,UltimaCalibracao
ID000123,P037,11/08/2026 09:42:16,C2,100,100.4,99.8,0.94,Conforme,NC00,nPressure NP001,FW 1.0.3,02/08/2026 18:23:47
```
*(o exemplo original do `Docs/` tem uma inconsistência de colunas — o cabeçalho tem 10 campos mas
a linha de dados tem 13 valores; a tabela acima reflete a correção implícita: dispositivo, firmware
e calibração fazem parte do cabeçalho.)*

**Curva pressão × tempo** (arquivo separado, vinculado ao mesmo `ID000XXX`):
```
Tempo_s,Pressao_mmHg
0.00,0.0
0.10,3.2
0.20,7.8
...
3.20,100.0
...
12.14,1.2
```

Isso mapeia diretamente para os dois arquivos já previstos no Apêndice C do prompt-mestre:
`/tests/AAAA/MM/T00000123.rec` (resumo/metadados do ensaio) e
`/tests/AAAA/MM/T00000123.csv` (curva de pressão amostrada).

## 6. Referenciais normativos citados no `Docs/`

IEC 62304 (ciclo de vida de software médico), FDA 21 CFR Part 11 (registros/assinaturas
eletrônicas), FDA General Principles of Software Validation, ISO 13485 (sistema de gestão da
qualidade). Nenhum desses é referenciado no prompt-mestre — vira insumo para a rastreabilidade
(`REQ-xxx`) e para a lista "requisitos regulatórios aplicáveis" do Apêndice G, item 10.

## 7. Mapeamento para o layout de arquivos do Apêndice C (prompt-mestre)

| Conceito do `Docs/` | Campo/arquivo equivalente no Apêndice C |
|---|---|
| `ID000XXX` | Nome de arquivo `T000XXXXX.rec`/`.csv` (ajustar largura do contador — `Docs/` usa 6 dígitos, Apêndice C usa 8) |
| `P0XX` (participante) | Campo novo a adicionar ao `.rec` — não existe hoje em "operador" (ver conflito C3 em `00-auditoria.md`) |
| `C1/C2/C3` (ciclo) | Campo novo — hoje o Apêndice C não distingue múltiplos ciclos por participante explicitamente |
| `NC00`–`NC08` | Campo novo no `.rec`, em paralelo ao(s) código(s) `E0xx` de erro técnico já previstos |
| Curva pressão × tempo | Já coberto por `T00000123.csv` no Apêndice C |
| Hash encadeado por linha | Já coberto pelo esquema de `/logs/*/AAAA-MM.log` do Apêndice C |
