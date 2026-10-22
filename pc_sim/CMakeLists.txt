# Simulador de PC da IHM myPRESSURE: LVGL v9 vendorizado (Core/ThirdParty/lvgl)
# + os MESMOS fontes de Core/App/HMI_UI do firmware + backend mock + SDL2.
#
# Projeto CMake separado do firmware (outro compilador: gcc do host). Ver
# pc_sim/README.md para compilar no Windows (MSYS2 ucrt64) ou Linux.
cmake_minimum_required(VERSION 3.16)
project(mypressure_sim C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(SDL2 REQUIRED)

set(PROJ_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(LVGL_DIR  ${PROJ_ROOT}/Core/ThirdParty/lvgl)
set(UI_DIR    ${PROJ_ROOT}/Core/App/HMI_UI)

option(SIM_MONITORS "Mostra heap/FPS do LVGL sobre a tela" OFF)

# --- LVGL (sem warnings estritos: codigo de terceiros) ---
file(GLOB_RECURSE LVGL_SRC ${LVGL_DIR}/src/*.c)
add_library(lvgl STATIC ${LVGL_SRC})
target_include_directories(lvgl SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}     # lv_conf.h do simulador
    ${LVGL_DIR}
)
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE SIM_MONITORS=$<BOOL:${SIM_MONITORS}>)
target_link_libraries(lvgl PUBLIC SDL2::SDL2)

# --- IHM (mesmos .c do firmware) + mock + main ---
file(GLOB UI_SRC ${UI_DIR}/Src/*.c)
add_executable(mypressure_sim main_pc.c ui_backend_mock.c ${UI_SRC})
target_include_directories(mypressure_sim PRIVATE ${UI_DIR}/Inc ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mypressure_sim PRIVATE lvgl m)

# Mesmo conjunto de warnings do firmware (N_PRESSURE_STRICT_WARNINGS), para
# o simulador acusar o que quebraria o build do firmware.
target_compile_options(mypressure_sim PRIVATE
    -Wall -Wextra -Wconversion -Wshadow -Wundef -Wdouble-promotion -Wformat=2 -Werror)
