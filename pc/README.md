# pc/ — Aplicações de PC (Fases 8-9)

Monorepo das duas aplicações de PC descritas na Parte B, seção 2.7 do prompt de projeto
(`Docs/PROMPT_PROJETO_STM32H723_MANGUITO.md`):

- `core/` — biblioteca compartilhada: transporte serial, COBS/CRC, codecs do protocolo (Apêndice
  A), modelos de dados, parsing dos arquivos do SD, autenticação. Testes com `pytest` e um
  dispositivo virtual que fala o protocolo (para desenvolver sem hardware).
- `app_user/` — aplicação do usuário: status ao vivo, gráfico em tempo real, histórico de
  ensaios, exportação (CSV/PDF/XLSX), verificação de integridade da cadeia de hash.
- `app_dev/` — aplicação do desenvolvedor: tudo do usuário + login DEV, editor de parâmetros,
  modo manual, osciloscópio, assistente de calibração, identificação de `T_lag`.

Ainda não implementado — depende do protocolo USB (Fase 7) existir primeiro. Ver
`Docs/dev/00-auditoria.md` para o estado atual do projeto.
