# pc/ — Aplicativo de PC

Um único programa com dois níveis de acesso (decisão do projeto). O nível de usuário é a Fase 8;
o nível DEV (parâmetros, modo manual, calibração, osciloscópio) entra na Fase 9.

- `core/`: protocolo USB, cliente do equipamento, leitura dos arquivos do SD, verificação de
  integridade, relatórios e dispositivo virtual. Não depende de Qt.
- `app/`: interface em PySide6.
- `tests/`: `pytest`, com a janela real contra o dispositivo virtual.

## Rodar

O ambiente fica fora do OneDrive porque os caminhos do Qt passam do limite do Windows dentro da
pasta do projeto:

```bash
python -m venv %LOCALAPPDATA%\np-venv
%LOCALAPPDATA%\np-venv\Scripts\python -m pip install -r requirements.txt
cd pc
%LOCALAPPDATA%\np-venv\Scripts\python -m app             # equipamento pela USB
%LOCALAPPDATA%\np-venv\Scripts\python -m app --virtual   # sem hardware
%LOCALAPPDATA%\np-venv\Scripts\python -m pytest
```

A cópia local dos registros fica em `%APPDATA%\nPressure\<número de série>`.

## Integridade

- **ÍNTEGRO**: todas as linhas e ensaios conferem com a corrente de hash.
- **ADULTERADO**: hash errado ou linha removida.
- **NÃO VERIFICÁVEL**: registros de firmware antigo, com linha cortada ou corrente reiniciada. Não
  provam nem negam adulteração.

A corrente usa SHA-256 sem chave: detecta alteração acidental ou parcial, mas quem conhece o formato
pode recalcular um arquivo inteiro.
