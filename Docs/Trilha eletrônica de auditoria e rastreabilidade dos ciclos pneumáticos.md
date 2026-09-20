**Trilha eletrônica de auditoria e rastreabilidade dos ciclos pneumáticos**

| **Parâmetro** | **Nomenclatura técnica recomendada** | **Unidade/registro** | **Limites Operacionais \*** |
| --- | --- | --- | --- |
| Identificação do acionamento | **Identificador único do acionamento** | ID000001, ID000002, ... |  |
| Identificação do participante | **Código de identificação do participante** | P001, P002... |  |
| Identificação da manobra | **Identificador do ciclo pneumático** | C1, C2, C3... |  |
| Data/hora | **Data e horário do ciclo pneumático** | DD/MM/AAAA – hh:mm:ss |  |
| Pressão programada | **Pressão-alvo programada** | mmHg |  |
| Maior pressão atingida | **Pressão máxima atingida** | mmHg | ≤105% da pressão-alvo programada |
| Pressão mantida | **Pressão média durante o platô pressórico** | mmHg |  |
| Estabilidade da pressão | **Variação pressórica durante o platô** | mín.–máx. | - 2% a + 2% da pressão-alvo programada |
| Tempo para atingir a pressão | **Tempo de insuflação até a pressão-alvo** | s | Até 10 s |
| Tempo sob pressão | **Duração do platô pressórico** | s | Até 10 s |
| Tempo de esvaziamento | **Tempo de descompressão pneumática** | s | 1 a 2 segundos |
| Pressão após esvaziamento | **Pressão residual pós-descompressão** | mmHg | Menor que 5 mmHg |
| Resultado operacional | **Status de conformidade do ciclo pneumático** | Conforme / Não conforme |  |
| Código de não conformidade técnica | **Código de não conformidade técnica** | NC00, NC01, NC02, ... |  |
| Identificação do aparelho | **Identificador único do dispositivo** | nº de série |  |
| Software | **Versão do firmware/software de controle** | versão |  |
| Calibração | **Data da última verificação metrológica/funcional** | DD/MM/AAAA – hh:mm:ss |  |

**1. Status de conformidade técnica do ciclo pneumático:**\
Conforme / Não conforme

**2. Código de não conformidade técnica:**\
O código de conformidade/não conformidade será atribuído automaticamente a todos os acionamentos, sendo NC00 utilizado para ciclos conformes e NC01–NC08 para as respectivas não conformidades técnicas. Assim, poderíamos ter códigos como:

- **NC00 — conforme**
- **NC01 — pressão-alvo não atingida**
- **NC02 — instabilidade pressórica durante o platô**
- **NC03 — tempo de insuflação fora da faixa especificada**
- **NC04 — duração do platô fora da faixa especificada**
- **NC05 — tempo de descompressão fora da faixa especificada**
- **NC06 — pressão residual pós-descompressão acima do limite**
- **NC07 — ciclo interrompido pelo operador**
- **NC08 — falha técnica do dispositivo**

**Modelo de Registro Ideal da Trilha de Rastreabilidade:**

1. Modelo em que ocorreu tudo dentro do previsto.

ID000123 | P037 | 11/08/2026 09:42:16 | Ciclo 2 | Alvo 100 mmHg | Máx. 100,4 | Platô médio 99,8 | Faixa 99,2–100,4 | Insuflação 3,2 s | Platô 8,0 s | Descompressão 0,94 s | Pressão residual 1,2 mmHg | Conforme |NC00 | nPressure NP001 | FW 1.0.3 | última calibração 02/08/2026 18:23:47.

2. Modelo no qual o tempo de descompressão ficou acima do esperado.

ID000126 | P038 | 11/08/2026 09:42:16 | Ciclo 2 | Alvo 100 mmHg | Máx. 100,4 | Platô médio 99,8 | Faixa 99,2–100,4 | Insuflação 3,2 s | Platô 7,0 s | **Descompressão 2,1 s** | Pressão residual 1,2 mmHg | Não conforme | NC05 | nPressure NP001 | FW 1.0.3 | última calibração 02/08/2026 18:23:47.

**ORIENTAÇÕES**

1\. A trilha deve ser gerada automaticamente e ser formalmente não editável pelo operador.

2\. * Os limites operacionais, apresentados na tabela acima, inicialmente programados no dispositivo serão utilizados exclusivamente durante a fase piloto do projeto como parâmetros técnicos preliminares, destinados à avaliação da estabilidade pressórica, dos tempos de insuflação, platô e descompressão e da reprodutibilidade dos ciclos pneumáticos. Esses valores poderão ser ajustados com base nos achados da fase piloto. Após essa etapa, os parâmetros operacionais e os respectivos limites de conformidade serão formalmente definidos e congelados antes do início da coleta da amostra definitiva.

**3. Controle de configuração e alterações**

Qualquer alteração na versão do firmware/software de controle ou nos parâmetros operacionais do dispositivo deverá ser documentada, contendo identificação da versão, data e horário de implementação e descrição da alteração. Durante a fase definitiva da pesquisa, modificações que possam interferir nos parâmetros de aquisição somente poderão ser realizadas mediante justificativa técnica documentada.

**4. Data e horário**

A data e o horário deverão ser obtidos do relógio interno do sistema. Eventuais ajustes de data ou horário deverão ser registrados automaticamente na trilha de auditoria, preservando-se o valor anterior, o novo valor e o momento da alteração.

**5. Qual a importância de cada ciclo ser registrado como o ID000126 se já temos os outros dados?**

Porque ele garante **unicidade absoluta**. Imagine que P038 faça três ciclos e, por alguma razão, C02 precise ser repetido (no exemplo abaixo: o tempo de descompressão ficou superior a 2,0 segundos). Preciso ter dois registros relacionados ao mesmo participante e à mesma etapa, mas com IDs diferentes:

- 5.1 Modelo no qual o tempo de descompressão ficou acima do esperado.

ID000126 | P038 | 11/08/2026 09:42:16 | Ciclo 2 | Alvo 100 mmHg | Máx. 100,4 | Platô médio 99,8 | Faixa 99,2–100,4 | Insuflação 3,2 s | Platô 7,0 s | **Descompressão 2,1 s** | Pressão residual 1,2 mmHg | Não conforme | NC05 | nPressure NP001 | FW 1.0.3 | última calibração 02/08/2026 18:23:47.

- 5.2 Ciclo seguinte, repetido, apresentando o tempo de descompressão aceitável.

ID000127 | P038 | 11/08/2026 09:43:26 | Ciclo 2 | Alvo 100 mmHg | Máx. 100,6 | Platô médio 99,9 | Faixa 99,2–100,6 | Insuflação 6,2 s | Platô 7,1 s | Descompressão 1,4 s | Pressão residual 1,6 mmHg | Conforme | NC00 | nPressure NP001 | FW 1.0.3 | última calibração 02/08/2026 18:23:47.

**6. Curva curva pressão (mmHg) x tempo (s)**

Se possível o equipamento poderia apresentar uma curva pressão (mmHg) x tempo (s), ou pelo menos guardar internamente os números dessa curva, vinculada ao respectivo identificador único do acionamento. Pois seria extremamente valioso em caso de uma eventual auditoria.

**7. Modelo do texto e dos dados do gráfico para exportação.**

Seria possível a exportação desses dados em um texto CSV, Comma-Separated Values – “valores separados por vírgulas”. O arquivo propriamente dito seria algo parecido com:

ID,Participante,DataHora,Ciclo,PressaoAlvo,PressaoMaxima,PlatôMedio,Descompressao,Status,CodigoNC

ID000123,P037,11/08/2026 09:42:16,C2,100,100.4,99.8,0.94,Conforme,NC00,nPressure P001,FW 1.0.3,última calibração02/08/202618:23:47

**7.1. Exportação da trilha de auditoria em CSV** — uma linha para cada ID000XXX.

**7.2. Exportação dos dados brutos da curva pressão × tempo** — também em CSV, vinculados ao mesmo ID000XXX.

Por exemplo, para ID000123, o segundo arquivo poderia conter:

Tempo_s,Pressao_mmHg

0.00,0.0

0.10,3.2

0.20,7.8

...

3.20,100.0

3.30,100.1

...

11.20,99.8

11.30,82.4

...

12.14,1.2

**8. Incluir data e relógio no dispositivo (como seriam os seus ajustes?).**

**CONCLUSÃO**

A trilha eletrônica de auditoria e rastreabilidade foi estruturada em consonância com princípios internacionalmente reconhecidos de integridade de registros eletrônicos, rastreabilidade, controle de configuração, validação de software e gestão da qualidade aplicáveis a dispositivos médicos, tomando como referenciais a IEC 62304, o FDA 21 CFR Part 11 e respectivas orientações sobre registros eletrônicos e validação de software, e a ISO 13485.

| **Parâmetro** | **IEC 62304** | **FDA Part 11** | **FDA Software Validation** | **ISO 13485** | **Avaliação** |
| --- | --- | --- | --- | --- | --- |
| ID único por acionamento | ✓ | ✓ | ✓ | ✓ | Muito adequado |
| Data/hora com segundos | ✓ | **✓✓** | ✓ | ✓ | Muito adequado |
| Participante pseudonimizado | — | ✓ | — | ✓ | Adequado |
| Identificação C1/C2/C3 | ✓ | ✓ | ✓ | ✓ | Adequado |
| Registro dos parâmetros efetivamente medidos | ✓ | ✓ | **✓✓** | ✓ | Muito adequado |
| Conforme/Não conforme | ✓ | ✓ | ✓✓ | ✓✓ | Muito adequado |
| Códigos NC00–NC08 | ✓ | ✓ | ✓✓ | ✓✓ | Muito adequado |
| Número de série do dispositivo | ✓ | ✓ | ✓ | ✓✓ | Muito adequado |
| Versão do firmware | **✓✓** | ✓ | **✓✓** | ✓ | Excelente |
| Data/hora da última verificação metrológica | ✓ | ✓ | ✓ | **✓✓** | Excelente |
| Registro automático | ✓ | **✓✓** | ✓ | ✓ | Excelente |
| Registro não editável pelo operador | ✓ | **✓✓** | ✓ | ✓ | Excelente |
| Curva pressão × tempo vinculada ao ID | ✓ | ✓ | ✓✓ | ✓ | Excelente |
| Exportação CSV | ✓ | ✓ | ✓ | ✓ | Adequado |

Referências Bibliográficas

1. International Electrotechnical Commission. **IEC 62304:2006: Medical device software—software life cycle processes. Amendment 1:2015**. Geneva: IEC; 2006.
2. U.S. Food and Drug Administration. **Part 11, electronic records; electronic signatures—scope and application: guidance for industry** \[Internet]. Silver Spring (MD): U.S. Food and Drug Administration; 2003 \[cited 2026 Aug 11]. Available from: [FDA — Part 11, Electronic Records; Electronic Signatures](https://www.fda.gov/regulatory-information/search-fda-guidance-documents/part-11-electronic-records-electronic-signatures-scope-and-application?lng=en-US&ss=page_is-an-electronic-signature-legal_undefined&utm_source=chatgpt.com)
3. U.S. Food and Drug Administration. **General principles of software validation: final guidance for industry and FDA staff** \[Internet]. Rockville (MD): U.S. Food and Drug Administration; 2002 \[cited 2026 Aug 11].
4. International Organization for Standardization. **ISO 13485:2016: Medical devices—quality management systems—requirements for regulatory purposes**. 3rd ed. Geneva: ISO; 2016.
