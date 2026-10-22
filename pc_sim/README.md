# Simulador de PC da IHM myPRESSURE

Roda **os mesmos arquivos** de `Core/App/HMI_UI` que vão no firmware, com LVGL v9 vendorizado
(`Core/ThirdParty/lvgl`) e SDL2, numa janela 320×240 (zoom 2×). O único arquivo trocado é o
backend: `pc_sim/ui_backend_mock.c` (máquina de estados fake) no lugar de
`Core/App/HMI_Port/Src/ui_backend_stm32.c`.

## Compilar e rodar (Windows, MSYS2 ucrt64)

Pré-requisito, uma vez só: `pacman -S mingw-w64-ucrt-x86_64-SDL2` (no shell do MSYS2).

```bash
export PATH="/c/msys64/ucrt64/bin:/c/ST/STM32CubeCLT_1.21.0/CMake/bin:/c/ST/STM32CubeCLT_1.21.0/Ninja/bin:$PATH"
cmake -S pc_sim -B build/sim -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_PREFIX_PATH=C:/msys64/ucrt64
cmake --build build/sim
./build/sim/mypressure_sim.exe
```

É um projeto CMake separado (compilador do host). **Não** use isso para o firmware: o firmware é
sempre configurado por `cmake --preset Debug` (ver `CLAUDE.md`).

No Linux: `sudo apt install libsdl2-dev`, depois os mesmos dois comandos `cmake` (sem o
`CMAKE_PREFIX_PATH`).

Opções: `-DSIM_MONITORS=ON` mostra o uso do heap do LVGL e o FPS sobre a tela.

## Teclas

| Tecla | Equivale a |
|---|---|
| ← / → (ou ↑ / ↓) | girar o encoder (roda do mouse também) |
| Enter | botão do encoder; segurar = long-press (botão do meio do mouse também) |
| Espaço | pedal / botão do usuário: inicia em IDLE, senão aborta/desinfla |
| F1 F2 F3 F4 F5 | injeta E001 / E002 / E003 / E004 / E013 |
| F8 | limpa o erro |
| S / R | alterna cartão SD montado / RTC válido |
| Esc | sair |

O buzzer e as gravações no SD aparecem como linhas `[buzzer]` / `[mock]` no terminal.

## Capturas automáticas

```bash
./build/sim/mypressure_sim.exe --shots <pasta>
```

Executa um roteiro (boot → ensaio completo → popup de resultado → menu → novo ensaio → ajustes →
teste de vazamento → erro E001), salva um BMP 320×240 de cada etapa em `<pasta>` e fecha sozinho.
Serve para revisar o layout depois de qualquer mudança sem precisar navegar à mão.

## O que o firmware precisa implementar

Tudo que está em `Core/App/HMI_UI/Inc/ui_backend.h`. A implementação real fica em
`Core/App/HMI_Port/Src/ui_backend_stm32.c`:

| Grupo | Funções |
|---|---|
| Estado | `GetState`, `GetPressureMmHg`, `GetSetpointMmHg`, `GetMaxSetpointMmHg`, `GetTickMs`, `GetUnit`, `GetLastError`, `ConsumeUserButton`, `PollCycleResult` |
| Periféricos | `GetDateTime` / `SetDateTime` (RTC), `IsSdMounted`, `IsRtcValid`, `BuzzerBeep` |
| Comandos | `SetSetpoint`, `StartInflation`, `EnterConfigMode`, `ExitConfigMode` (salva na EEPROM), `StartLeakTest`, `SetUnit`, `SubmitCycleRecord` |
| Vazamento | `GetLeakHoldTimeMs`, `GetLeakMaxDropMmHg` |
| Calibração | `GetRawPressureMmHg`, `CalZero`, `CalSetPumpPct`, `CalAbort`, `CalCapture`, `CalComputeAndSave` |
| Info | `GetDeviceId`, `GetFirmwareVersion` |

## Limites do simulador

- O mock **não** modela a planta pneumática: as rampas são lineares, só para a UI ter o que mostrar.
  Nada daqui serve para sintonia de PID ou validação do controle.
- O PC é 64 bits: os objetos LVGL ocupam mais heap que no Cortex-M7 (32 bits). Se cabe nos 64 kB
  do simulador, cabe com folga no firmware; o contrário não vale. A medida que conta é
  `g_lv_mem` no firmware (Live Watch).
- Bipes não bloqueiam no simulador; no firmware `Buzzer_Beep()` é síncrono (~100–360 ms).
