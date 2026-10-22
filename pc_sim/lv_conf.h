/**
 * @file lv_conf.h
 * @brief LVGL do SIMULADOR de PC — espelha Core/Bsp/LvglPort/Inc/lv_conf.h
 *        (RGB565, 64 kB de heap, mesmas fontes/widgets) e so' acrescenta o
 *        driver SDL, log e asserts. Se mudar algo no lv_conf do firmware
 *        (fontes, LV_MEM_SIZE, widgets), replique aqui.
 */
#if 1
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* Cores — igual ao firmware */
#define LV_COLOR_DEPTH 16

/* Memoria — mesmo heap do firmware, para o simulador acusar falta de RAM */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (64 * 1024U)

#define LV_DEF_REFR_PERIOD  33
#define LV_DPI_DEF          130

#define LV_USE_OS   LV_OS_NONE

/* Desenho */
#define LV_DRAW_SW_SUPPORT_RGB565       1
#define LV_DRAW_SW_SUPPORT_RGB888       0
#define LV_DRAW_SW_SUPPORT_ARGB8888     0
#define LV_DRAW_SW_SUPPORT_L8           0
#define LV_DRAW_SW_SUPPORT_AL88         0
#define LV_DRAW_SW_SUPPORT_A8           0
#define LV_DRAW_SW_SUPPORT_I1           0
#define LV_DRAW_SW_DRAW_UNIT_CNT        1
#define LV_USE_DRAW_SW_ASM              LV_DRAW_SW_ASM_NONE

/* Log e asserts (so' no simulador) */
#define LV_USE_LOG              1
#define LV_LOG_LEVEL            LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF           1
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         1
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           1

/* Fontes — SO' as habilitadas no firmware */
#define LV_FONT_MONTSERRAT_10  1
#define LV_FONT_MONTSERRAT_12  0
#define LV_FONT_MONTSERRAT_14  1
#define LV_FONT_MONTSERRAT_16  1
#define LV_FONT_MONTSERRAT_20  0
#define LV_FONT_MONTSERRAT_22  1
#define LV_FONT_MONTSERRAT_24  0
#define LV_FONT_MONTSERRAT_34  1
#define LV_FONT_DEFAULT        &lv_font_montserrat_14

/* Widgets — igual ao firmware */
#define LV_USE_ANIMIMG      1
#define LV_USE_ARC          1
#define LV_USE_BAR          1
#define LV_USE_BUTTON       1
#define LV_USE_BUTTONMATRIX 1
#define LV_USE_CALENDAR     1
#define LV_USE_CANVAS       1
#define LV_USE_CHART        1
#define LV_USE_CHECKBOX     1
#define LV_USE_DROPDOWN     1
#define LV_USE_IMAGE        1
#define LV_USE_IMAGEBUTTON  1
#define LV_USE_KEYBOARD     1
#define LV_USE_LABEL        1
#define LV_USE_LED          1
#define LV_USE_LINE         1
#define LV_USE_LIST         1
#define LV_USE_MENU         1
#define LV_USE_MSGBOX       1
#define LV_USE_ROLLER       1
#define LV_USE_SCALE        1
#define LV_USE_SLIDER       1
#define LV_USE_SPAN         1
#define LV_USE_SPINBOX      1
#define LV_USE_SPINNER      1
#define LV_USE_SWITCH       1
#define LV_USE_TABLE        1
#define LV_USE_TABVIEW      1
#define LV_USE_TEXTAREA     1
#define LV_USE_TILEVIEW     1
#define LV_USE_WIN          1

#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1
#define LV_THEME_DEFAULT_GROW 1

#define LV_USE_FLEX 1
#define LV_USE_GRID 1

/* Monitor de heap/FPS sobre a tela: -DSIM_MONITORS=1 no CMake. Desligado
 * por padrao porque cobre parte dos 320x240. */
#ifndef SIM_MONITORS
#define SIM_MONITORS 0
#endif
#define LV_USE_SYSMON       SIM_MONITORS
#if LV_USE_SYSMON
    #define LV_SYSMON_GET_IDLE lv_timer_get_idle
    #define LV_USE_PERF_MONITOR 1
    #define LV_USE_PERF_MONITOR_POS LV_ALIGN_BOTTOM_RIGHT
    #define LV_USE_PERF_MONITOR_LOG_MODE 0
    #define LV_USE_MEM_MONITOR 1
    #define LV_USE_MEM_MONITOR_POS LV_ALIGN_BOTTOM_LEFT
#endif

#define LV_USE_SNAPSHOT     0
#define LV_USE_PROFILER     0
#define LV_USE_OBSERVER     0
#define LV_BUILD_EXAMPLES   0

/* Driver SDL (so' no simulador) */
#define LV_USE_SDL              1
#define LV_SDL_INCLUDE_PATH     <SDL2/SDL.h>
#define LV_SDL_RENDER_MODE      LV_DISPLAY_RENDER_MODE_DIRECT
#define LV_SDL_BUF_COUNT        1
#define LV_SDL_FULLSCREEN       0
#define LV_SDL_DIRECT_EXIT      1

#endif /*LV_CONF_H*/
#endif
