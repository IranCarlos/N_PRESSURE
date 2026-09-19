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

# --- LVGL Sources (vendored, third-party) ---
file(GLOB_RECURSE LVGL_CORE_SOURCES
    "Core/ThirdParty/lvgl/src/*.c"
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${LVGL_CORE_SOURCES}
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Common/Inc
    Core/Bsp/ST7789/Inc
    Core/Bsp/Encoder/Inc
    Core/Bsp/Buzzer/Inc
    Core/Bsp/Pedal/Inc
    Core/Bsp/LvglPort/Inc
    Core/Services/ADC_Acq/Inc
    Core/Services/Control_Loop/Inc
    Core/Services/EEPROM/Inc
    Core/App/HMI_UI/Inc
    # LVGL includes
    Core/ThirdParty/lvgl
)

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
