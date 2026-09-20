cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME N_PRESSURE)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Marca os includes do alvo `stm32cubemx` (HAL/CMSIS/FatFs — adicionados por
# cmake/stm32cubemx/CMakeLists.txt, um arquivo regenerado pelo CubeMX e por
# isso nao editavel aqui) como "system" para quem os consome. Sem isso, os
# warnings estritos aplicados aos NOSSOS .c mais abaixo tambem disparariam
# para o conteudo desses headers vendorizados quando incluidos
# transitivamente (ex.: stm32h7xx_hal.h -> stm32h7xx_hal_flash_ex.h, ou
# CMSIS core_cm7.h -> cachel1_armv7.h), quebrando o -Werror por modulo em
# codigo que nao e' nosso. Propriedade SYSTEM de alvo (CMake >= 3.24) —
# funciona mesmo sem tocar no arquivo gerado, porque age sobre o alvo ja
# definido por ele, nao sobre a lista de diretorios em si.
set_property(TARGET stm32cubemx PROPERTY SYSTEM TRUE)

# Add sources to executable — arquitetura em camadas (portados de
# STM32L476/Core/Controller): Bsp (drivers de hardware desta placa),
# Services (logica de negocio), App (orquestracao/UI). Dependencia:
# App -> Services -> Bsp -> Drivers(HAL). Common (Core/Common) nao tem
# .c, so' headers de tipos compartilhados.
file(GLOB_RECURSE CORE_SRC
    "Core/Bsp/*/Src/*.c"
    "Core/Services/*/Src/*.c"
    "Core/App/*/Src/*.c"
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${CORE_SRC}
)

# --- Warnings estritos da Parte A do prompt de projeto (Docs/PROMPT_PROJETO_
# STM32H723_MANGUITO.md), ligados progressivamente por modulo (decisao D5 em
# Docs/dev/00-auditoria.md) — nunca em Drivers/ ou Core/ThirdParty/lvgl
# (vendorizados). -Wall ja e' global (cmake/gcc-arm-none-eabi.cmake); aqui so'
# adicionamos o resto do conjunto exigido. -Werror entra por arquivo somente
# depois de confirmado limpo sob essas flags — ver comentario abaixo.
# Confirmado limpo sob todo o conjunto acima em 2026-09-19 (Core/Bsp,
# Core/Services, Core/App inteiros) — por isso -Werror ja entra junto para
# este conjunto de arquivos. Se um novo arquivo/modulo nao estiver limpo
# ainda, tire-o de CORE_SRC aqui (ou aplique so' os warnings, sem -Werror)
# ate ser corrigido, em vez de desligar a flag para todo mundo.
set(N_PRESSURE_STRICT_WARNINGS
    -Wextra
    -Wconversion
    -Wshadow
    -Wundef
    -Wdouble-promotion
    -Wformat=2
    -Werror
)
set_source_files_properties(${CORE_SRC} PROPERTIES COMPILE_OPTIONS "${N_PRESSURE_STRICT_WARNINGS}")

# Codigo gerado pelo CubeMX (nao editavel fora de USER CODE): no modo USB
# dual-role, stm32h7xx_hal_msp.c define HAL_RCC_USB_OTG_HS_CLK_ENABLED como
# contador compartilhado entre os MspInit de device e host, e cada um so'
# usa se aquele papel for inicializado — gera -Wunused-variable. Silenciado
# so' neste arquivo, aqui (este CMakeLists nao e' regenerado).
set_source_files_properties(Core/Src/stm32h7xx_hal_msp.c PROPERTIES COMPILE_OPTIONS "-Wno-unused-variable")

# --- LVGL Sources (vendored, third-party) ---
file(GLOB_RECURSE LVGL_CORE_SOURCES
    "Core/ThirdParty/lvgl/src/*.c"
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${LVGL_CORE_SOURCES}
)

# Add include paths (lista compartilhada com o alvo `check`, mais abaixo)
set(N_PRESSURE_INC_DIRS
    Core/Common/Inc
    Core/Bsp/ST7789/Inc
    Core/Bsp/Encoder/Inc
    Core/Bsp/Buzzer/Inc
    Core/Bsp/Pedal/Inc
    Core/Bsp/Port/Inc
    Core/Bsp/Valve/Inc
    Core/Bsp/PwmMotor/Inc
    Core/Bsp/LvglPort/Inc
    Core/Services/ADC_Acq/Inc
    Core/Services/Control_Loop/Inc
    Core/Services/EEPROM/Inc
    Core/Services/Fault_Manager/Inc
    Core/Services/Supervisor/Inc
    Core/Services/Pressure_Core/Inc
    Core/App/HMI_UI/Inc
    Core/App/Bench/Inc
)
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${N_PRESSURE_INC_DIRS})

# LVGL includes — SYSTEM porque e' vendorizado (Core/ThirdParty/lvgl): tem
# varias funcoes static inline em cabecalhos que disparariam os warnings
# estritos de N_PRESSURE_STRICT_WARNINGS quando instanciadas dentro dos
# NOSSOS .c (ex.: Core/Bsp/LvglPort, Core/App/HMI_UI) que incluem "lvgl.h".
target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PRIVATE
    Core/ThirdParty/lvgl
)

# --- Alvos auxiliares (Fase 1 do prompt de projeto) --------------------------
# size:  cmake --build --preset Debug --target size
#        Uso de Flash/RAM por secao (arm-none-eabi-size). O resumo por regiao
#        (DTCM/AXI/ITCM/Flash) ja sai a cada link via --print-memory-usage.
add_custom_target(size
    COMMAND ${CMAKE_SIZE} --format=berkeley $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
    DEPENDS ${CMAKE_PROJECT_NAME}
    COMMENT "Uso de memoria de ${CMAKE_PROJECT_NAME}"
)

# check: cmake --build --preset Debug --target check
#        Analise estatica com o -fanalyzer do proprio GCC (nao exige cppcheck/
#        clang-tidy). Compila SO' o codigo nosso (Core/App, Core/Bsp,
#        Core/Services) — com os mesmos warnings estritos + -Werror do build
#        normal (set_source_files_properties acima vale para os dois alvos) —
#        sem linkar. Analise MISRA (cppcheck --addon=misra) fica para quando a
#        ferramenta estiver instalada; ver Docs/dev/00-auditoria.md.
add_library(n_pressure_analyze OBJECT EXCLUDE_FROM_ALL ${CORE_SRC})
target_include_directories(n_pressure_analyze PRIVATE ${N_PRESSURE_INC_DIRS})
target_include_directories(n_pressure_analyze SYSTEM PRIVATE Core/ThirdParty/lvgl)
target_link_libraries(n_pressure_analyze PRIVATE stm32cubemx)
target_compile_options(n_pressure_analyze PRIVATE -fanalyzer)
add_custom_target(check DEPENDS n_pressure_analyze)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
