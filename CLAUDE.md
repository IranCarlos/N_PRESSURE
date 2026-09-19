# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

Firmware for a medical pressure/insufflation device (cuff pump + 3 solenoid valves + pressure
sensor), built on an STM32H723VGT6 (Cortex-M7). STM32CubeMX generates the peripheral init code;
everything else is hand-written C, built with CMake/Ninja and the GNU ARM Embedded toolchain.
Display is an ST7789 SPI TFT driven by LVGL v9; input is a rotary encoder.

## Build

Toolchain: `C:\ST\STM32CubeCLT_1.22.0` (CMake, Ninja, arm-none-eabi-gcc). The CMake presets select
the ARM toolchain file (`cmake/gcc-arm-none-eabi.cmake`) — **always configure via the preset**, never
a bare `cmake -S . -B ...`, or CMake will fall back to a host compiler found on `PATH` (e.g. a
Windows/MSYS gcc) and silently build for the wrong target.

```bash
export PATH="/c/ST/STM32CubeCLT_1.22.0/CMake/bin:/c/ST/STM32CubeCLT_1.22.0/Ninja/bin:/c/ST/STM32CubeCLT_1.22.0/GNU-tools-for-STM32/bin:$PATH"
cmake --preset Debug          # or Release; configures build/<preset>/
cmake --build --preset Debug
```

Output: `build/Debug/N_PRESSURE.elf` (and `.map`). Re-run the configure step whenever a `.c` file is
added/removed/moved — sources are picked up via `file(GLOB_RECURSE ...)` in the root
`CMakeLists.txt`, which is only re-evaluated at configure time.

Flashing/debugging is done through the STM32Cube VS Code extension (`.vscode/launch.json`, ST-Link
GDB server, `runEntry: main`) — there is no CLI flash command in this repo.

## No automated tests

There is no unit test suite (this is bare-metal firmware with no host-testable abstraction layer).
Verification is: (1) clean build, (2) flash to real hardware, (3) hardware bring-up — see below.
`.clang-format`/`.editorconfig` exist for style but are not enforced by a CI/lint command; they only
apply to hand-written code, not `Core/ThirdParty/lvgl` or CubeMX-generated files.

## Architecture

Layered structure under `Core/`, dependency direction `App -> Services -> Bsp -> Drivers (HAL)`,
with `Common` as a dependency-free leaf any layer may include:

- **`Core/Inc`, `Core/Src`** — CubeMX-managed (`main.c`, `stm32h7xx_it.c`, `stm32h7xx_hal_msp.c`,
  `main.h`, `stm32h7xx_hal_conf.h`, `stm32h7xx_it.h`, plus `syscalls.c`/`sysmem.c`/`system_stm32h7xx.c`).
  Edit only inside `USER CODE BEGIN/END` markers — CubeMX regeneration (from `N_PRESSURE.ioc`)
  rewrites everything else in these files, but does **not** touch any of the folders below (confirmed
  via `.mxproject`'s tracked-file list).
- **`Core/Common/Inc/system_types.h`** — shared domain types (`SystemConfig_t`, `SystemRuntime_t`,
  `PID_Config_t`, etc.) and the `DMA_BUFFER` macro. No dependencies of its own.
- **`Core/Bsp/<Module>/{Inc,Src}`** — one hardware peripheral per module, thin wrappers over HAL
  (`ST7789`, `Encoder` — TIM2 quadrature, `Buzzer` — GPIO bit-bang square wave, `Pedal` — TIM4 input
  capture, `LvglPort` — the LVGL↔ST7789/Encoder glue: `lv_port_disp.c/h`, `lv_port_indev.c/h`,
  `lv_conf.h`). Bsp modules are allowed to include `main.h` for this board's pin/handle macros.
- **`Core/Services/<Module>/{Inc,Src}`** — business logic: `ADC_Acq` (pressure read + calibration/
  filtering), `Control_Loop` (PID + actuator state machine), `EEPROM` (layered: `eeprom.c` generic
  hardware-agnostic driver → `eeprom_port_stm32_hal.c` HAL I2C binding → `eeprom_storage_io.c`
  app-level config load/save with CRC32). Services must **not** include `main.h`; board-specific
  handles/pins are injected by the caller (e.g. `ControlLoop_Init()` takes a
  `ControlLoop_ValvePins_t`).
- **`Core/App/HMI_UI/{Inc,Src}`** — UI orchestration: `hmi_ui.c` (facade), `hmi_encoder_group.c`
  (LVGL group/focus management for encoder navigation), `ui_main_screen.c`, `ui_config_screen.c`.
- **`Core/ThirdParty/lvgl`** — vendored LVGL v9. Has one intentional local patch:
  `src/misc/lv_palette.c` reads its packed-3-byte `lv_color_t` palette tables byte-by-byte instead of
  via the compiler's fused 16-bit load, which otherwise HardFaults on Cortex-M7 (unaligned access) —
  see the comment on `palette_color_read()` before touching that file.
- Root `Drivers/` (HAL+CMSIS) and `Middlewares/Third_Party/FatFs` + `FATFS/` are vendor/CubeMX-owned;
  FatFs/SD support is generated but not yet wired into the app.

**Include convention**: cross-module includes are bare filenames (`#include "control_loop.h"`), never
relative paths — every module's `Inc/` is on the CMake include path (see `target_include_directories`
in the root `CMakeLists.txt`). Keep new modules consistent with this.

**DMA buffers must use `RAM_D1`, not the default region**: the linker script
(`STM32H723xG_flash.ld`) puts `.bss`/`.data` in `DTCMRAM` by default, which the STM32H7's DMA1/DMA2
controllers **cannot reach** (DTCM is core-local). Any buffer passed to `HAL_*_Start_DMA()` /
`HAL_*_Transmit_DMA()` must be tagged with the `DMA_BUFFER` macro (from `system_types.h`), which
places it in the custom `.dma_buffer` section mapped to `RAM_D1` (AXI SRAM). Forgetting this makes
the DMA transfer silently read/write garbage instead of failing loudly.

## Hardware bring-up harness

`Core/Src/main.c` has `APP_MODE_BRINGUP` (currently `1`) gating two entirely different `main()`
bodies: a bring-up harness vs. the full production path (LVGL + EEPROM + ADC_Acq + Control_Loop +
HMI_UI, currently the dead `#else` branch — it compiles but doesn't run). Inside the bring-up
harness, `BRINGUP_ENABLE_DISPLAY` / `BRINGUP_ENABLE_ENCODER` / `BRINGUP_ENABLE_BUZZER` /
`BRINGUP_ENABLE_LVGL` independently toggle each peripheral so it can be hardware-validated in
isolation before being wired into the rest of the app — follow this same one-piece-at-a-time pattern
for any new peripheral rather than enabling everything at once.

## Safety-relevant note

`Control_Loop` drives the pump and 3 solenoid valves. Any change to its PID tuning, state machine,
or actuator timing must be bench-verified on real hardware — it cannot be validated by compiling
alone. `SystemRuntime_t.bt_user_pressed` (in `system_types.h`) is a deliberately shared "physical
button pressed" event flag, consumed by whichever module currently owns `SystemRuntime_t.state`
(documented at its declaration) — this is intentional, not a bug to "fix" by giving it a single owner.
