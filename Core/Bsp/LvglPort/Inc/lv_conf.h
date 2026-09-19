/**
 * @file lv_conf.h
 * Configuração otimizada para STM32L476 + ST7789 320x240 + LVGL v9
 */

#if 1 /*Set it to "1" to enable content*/

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/
#define LV_COLOR_DEPTH 16

/*=========================
   STDLIB WRAPPER SETTINGS
 *=========================*/
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_BUILTIN

#define LV_MEM_SIZE (64 * 1024U)          
/*====================
   HAL SETTINGS
 *====================*/
#define LV_DEF_REFR_PERIOD  45
#define LV_DPI_DEF          130

/*=======================
 * FEATURE CONFIGURATION
 *=======================*/
#define LV_USE_OS   LV_OS_NONE          // mude para LV_OS_FREERTOS se usar FreeRTOS

/*========================
 *  DRAW CONFIGURATION
 *========================*/
#define LV_DRAW_SW_SUPPORT_RGB565       1
#define LV_DRAW_SW_SUPPORT_RGB888       0
#define LV_DRAW_SW_SUPPORT_ARGB8888     0
#define LV_DRAW_SW_SUPPORT_L8           0
#define LV_DRAW_SW_SUPPORT_AL88         0
#define LV_DRAW_SW_SUPPORT_A8           0
#define LV_DRAW_SW_SUPPORT_I1           0

#define LV_DRAW_SW_DRAW_UNIT_CNT        1
#define LV_USE_DRAW_SW_ASM              LV_DRAW_SW_ASM_NONE

/*=======================
 *  FONT USAGE
 *=======================*/
#define LV_FONT_MONTSERRAT_10  1 //
#define LV_FONT_MONTSERRAT_12  0
#define LV_FONT_MONTSERRAT_14  1
#define LV_FONT_MONTSERRAT_16  1
#define LV_FONT_MONTSERRAT_20  0
#define LV_FONT_MONTSERRAT_22  1
#define LV_FONT_MONTSERRAT_24  0
#define LV_FONT_MONTSERRAT_34  1

#define LV_FONT_DEFAULT        &lv_font_montserrat_14

/*=================
 *  WIDGET USAGE
 *=================*/
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

/*==================
 *  THEMES
 *==================*/
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1
#define LV_THEME_DEFAULT_GROW 1

/*==================
 *  LAYOUTS
 *==================*/
#define LV_USE_FLEX 1
#define LV_USE_GRID 1

/*==================
 *  3RD PARTY LIBRARIES
 *==================*/
#define LV_USE_FS_STDIO     0
#define LV_USE_FS_POSIX     0
#define LV_USE_FS_WIN32     0
#define LV_USE_FS_FATFS     0
#define LV_USE_FS_LITTLEFS  0

#define LV_USE_LODEPNG      0
#define LV_USE_LIBPNG       0
#define LV_USE_BMP          0
#define LV_USE_TJPGD        0
#define LV_USE_LIBJPEG_TURBO 0
#define LV_USE_GIF          0
#define LV_USE_QRCODE       0
#define LV_USE_BARCODE      0
#define LV_USE_FREETYPE     0
#define LV_USE_TINY_TTF     0
#define LV_USE_RLOTTIE      0
#define LV_USE_FFMPEG       0

/*==================
 *  OTHERS
 *==================*/
#define LV_USE_SNAPSHOT     0
#define LV_USE_SYSMON       0
#define LV_USE_PROFILER     0
#define LV_USE_OBSERVER     0
#define LV_USE_IME_PINYIN   0
#define LV_USE_FILE_EXPLORER 0

/*==================
 *  DRIVER
 *==================*/
#define LV_USE_ST7789       0          // ← habilita o driver oficial

/*==================
 *  EXAMPLES & DEMOS
 *==================*/
#define LV_BUILD_EXAMPLES   0
#define LV_USE_DEMO_WIDGETS 0
#define LV_USE_DEMO_BENCHMARK 0
#define LV_USE_DEMO_STRESS  0
#define LV_USE_DEMO_MUSIC   0

/*1: Enable system monitor component*/
#if LV_USE_SYSMON
    /*Get the idle percentage. E.g. uint32_t my_get_idle(void);*/
    #define LV_SYSMON_GET_IDLE lv_timer_get_idle

    /*1: Show CPU usage and FPS count
     * Requires `LV_USE_SYSMON = 1`*/
    #define LV_USE_PERF_MONITOR 1
    #if LV_USE_PERF_MONITOR
        #define LV_USE_PERF_MONITOR_POS LV_ALIGN_BOTTOM_RIGHT

        /*0: Displays performance data on the screen, 1: Prints performance data using log.*/
        #define LV_USE_PERF_MONITOR_LOG_MODE 0
    #endif

    /*1: Show the used memory and the memory fragmentation1
     * Requires `LV_USE_BUILTIN_MALLOC = 1`
     * Requires `LV_USE_SYSMON = 1`*/
    #define LV_USE_MEM_MONITOR 1
    #if LV_USE_MEM_MONITOR
        #define LV_USE_MEM_MONITOR_POS LV_ALIGN_BOTTOM_LEFT
    #endif

#endif /*LV_USE_SYSMON*/

/*--END OF LV_CONF_H--*/

#endif /*LV_CONF_H*/

#endif /*End of "Content enable"*/