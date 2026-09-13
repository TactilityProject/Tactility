/**
 * @file lv_conf.h
 * @brief LVGL configuration for ESP32-S3-PhotoPainter (800x480 6-color)
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

/* Larger memory for 800x480 display */
#define LV_MEM_SIZE (64 * 1024U)
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

/* Larger fonts for bigger display */
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_DEFAULT &lv_font_montserrat_16

/*====================
   WIDGET CONFIGURATION
 *====================*/

#define LV_USE_ARC        1
#define LV_USE_BAR        1
#define LV_USE_BUTTON     1
#define LV_USE_BUTTONMATRIX 1
#define LV_USE_CANVAS     0
#define LV_USE_CHECKBOX   1
#define LV_USE_DROPDOWN   1
#define LV_USE_IMAGE      1
#define LV_USE_LABEL      1
#define LV_USE_LINE       1
#define LV_USE_ROLLER     1
#define LV_USE_SLIDER     1
#define LV_USE_SWITCH     1
#define LV_USE_TABLE      1
#define LV_USE_TEXTAREA   1

/*====================
   LAYOUTS
 *====================*/

#define LV_USE_FLEX 1
#define LV_USE_GRID 1

#endif /* LV_CONF_H */
