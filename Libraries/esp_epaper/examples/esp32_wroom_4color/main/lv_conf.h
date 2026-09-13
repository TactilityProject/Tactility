/**
 * @file lv_conf.h
 * @brief LVGL configuration for ESP32-WROOM-32D + 4-Color E-Paper (No PSRAM)
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/

#define LV_COLOR_DEPTH 16

/*====================
   MEMORY SETTINGS
 *====================*/

/* Smaller memory for no-PSRAM ESP32 */
#define LV_MEM_SIZE (32 * 1024U)
#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC   malloc
#define LV_MEM_CUSTOM_FREE    free
#define LV_MEM_CUSTOM_REALLOC realloc

/*====================
   HAL SETTINGS
 *====================*/

#define LV_DEF_REFR_PERIOD 100
#define LV_DPI_DEF 130

/*====================
   FEATURE CONFIGURATION
 *====================*/

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

/*====================
   FONT CONFIGURATION
 *====================*/

#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/*====================
   WIDGET CONFIGURATION
 *====================*/

#define LV_USE_ARC        1
#define LV_USE_BAR        1
#define LV_USE_BUTTON     1
#define LV_USE_BUTTONMATRIX 0
#define LV_USE_CANVAS     0
#define LV_USE_CHECKBOX   1
#define LV_USE_DROPDOWN   0
#define LV_USE_IMAGE      1
#define LV_USE_LABEL      1
#define LV_USE_LINE       1
#define LV_USE_ROLLER     0
#define LV_USE_SLIDER     1
#define LV_USE_SWITCH     1
#define LV_USE_TABLE      0
#define LV_USE_TEXTAREA   0

/*====================
   LAYOUTS
 *====================*/

#define LV_USE_FLEX 1
#define LV_USE_GRID 0

#endif /* LV_CONF_H */
