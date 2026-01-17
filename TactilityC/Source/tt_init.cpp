#ifdef ESP_PLATFORM

#include "tt_app.h"
#include "tt_app_alertdialog.h"
#include "tt_app_selectiondialog.h"
#include "tt_bundle.h"
#include "tt_gps.h"
#include "tt_hal.h"
#include "tt_hal_device.h"
#include "tt_hal_display.h"
#include "tt_hal_gpio.h"
#include "tt_hal_i2c.h"
#include "tt_hal_touch.h"
#include "tt_hal_uart.h"
#include <tt_lock.h>
#include "tt_lvgl.h"
#include "tt_lvgl_keyboard.h"
#include "tt_lvgl_spinner.h"
#include "tt_lvgl_toolbar.h"
#include "tt_preferences.h"
#include "tt_time.h"
#include "tt_wifi.h"

#include "symbols/esp_event.h"
#include "symbols/esp_http_client.h"
#include "symbols/pthread.h"
#include "symbols/stl.h"
#include "symbols/string.h"
#include "symbols/cplusplus.h"
#include "symbols/freertos.h"
#include "symbols/gcc_soft_float.h"

#include <cstring>
#include <ctype.h>
#include <cassert>
#include <getopt.h>
#include <cmath>
#include <ctime>
#include <setjmp.h>
#include <sys/errno.h>
#include <sys/unistd.h>

#include <esp_log.h>
#include <esp_random.h>
#include <esp_sntp.h>
#include <esp_netif.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
#include <lwip/sockets.h>

#include <lvgl.h>
#include <vector>

extern "C" {

extern double __floatsidf(int x);

const esp_elfsym main_symbols[] {
    // stdlib.h
    ESP_ELFSYM_EXPORT(malloc),
    ESP_ELFSYM_EXPORT(calloc),
    ESP_ELFSYM_EXPORT(realloc),
    ESP_ELFSYM_EXPORT(free),
    ESP_ELFSYM_EXPORT(rand),
    ESP_ELFSYM_EXPORT(srand),
    ESP_ELFSYM_EXPORT(rand_r),
    ESP_ELFSYM_EXPORT(atoi),
    ESP_ELFSYM_EXPORT(atol),
    // esp random
    ESP_ELFSYM_EXPORT(esp_random),
    ESP_ELFSYM_EXPORT(esp_fill_random),
    // esp other
    ESP_ELFSYM_EXPORT(__floatsidf),
    // unistd.h
    ESP_ELFSYM_EXPORT(usleep),
    ESP_ELFSYM_EXPORT(sleep),
    ESP_ELFSYM_EXPORT(exit),
    ESP_ELFSYM_EXPORT(close),
    ESP_ELFSYM_EXPORT(rmdir),
    ESP_ELFSYM_EXPORT(unlink),
    // time.h
    ESP_ELFSYM_EXPORT(clock_gettime),
    ESP_ELFSYM_EXPORT(strftime),
    ESP_ELFSYM_EXPORT(time),
    ESP_ELFSYM_EXPORT(localtime_r),
    ESP_ELFSYM_EXPORT(localtime),
    // esp_sntp.h
    ESP_ELFSYM_EXPORT(sntp_get_sync_status),
    // math.h
    ESP_ELFSYM_EXPORT(cos),
    ESP_ELFSYM_EXPORT(sin),
    // sys/errno.h
    ESP_ELFSYM_EXPORT(__errno),
    // freertos_tasks_c_additions.h
    ESP_ELFSYM_EXPORT(__getreent),
#ifdef __HAVE_LOCALE_INFO__
    // ctype.h
    ESP_ELFSYM_EXPORT(__locale_ctype_ptr),
#else
    ESP_ELFSYM_EXPORT(_ctype_),
#endif
    // getopt.h
    ESP_ELFSYM_EXPORT(getopt_long),
    ESP_ELFSYM_EXPORT(optind),
    ESP_ELFSYM_EXPORT(opterr),
    ESP_ELFSYM_EXPORT(optarg),
    ESP_ELFSYM_EXPORT(optopt),
    // setjmp.h
    ESP_ELFSYM_EXPORT(longjmp),
    ESP_ELFSYM_EXPORT(setjmp),
    // cassert
    ESP_ELFSYM_EXPORT(__assert_func),
    // cstdio
    ESP_ELFSYM_EXPORT(fclose),
    ESP_ELFSYM_EXPORT(feof),
    ESP_ELFSYM_EXPORT(ferror),
    ESP_ELFSYM_EXPORT(fflush),
    ESP_ELFSYM_EXPORT(fgetc),
    ESP_ELFSYM_EXPORT(fgetpos),
    ESP_ELFSYM_EXPORT(fgets),
    ESP_ELFSYM_EXPORT(fopen),
    ESP_ELFSYM_EXPORT(fputc),
    ESP_ELFSYM_EXPORT(fputs),
    ESP_ELFSYM_EXPORT(fprintf),
    ESP_ELFSYM_EXPORT(fread),
    ESP_ELFSYM_EXPORT(fseek),
    ESP_ELFSYM_EXPORT(fsetpos),
    ESP_ELFSYM_EXPORT(fscanf),
    ESP_ELFSYM_EXPORT(ftell),
    ESP_ELFSYM_EXPORT(fwrite),
    ESP_ELFSYM_EXPORT(getc),
    ESP_ELFSYM_EXPORT(putc),
    ESP_ELFSYM_EXPORT(puts),
    ESP_ELFSYM_EXPORT(printf),
    ESP_ELFSYM_EXPORT(sscanf),
    ESP_ELFSYM_EXPORT(snprintf),
    ESP_ELFSYM_EXPORT(sprintf),
    ESP_ELFSYM_EXPORT(vsprintf),
    ESP_ELFSYM_EXPORT(vsnprintf),
    // cstring
    ESP_ELFSYM_EXPORT(strlen),
    ESP_ELFSYM_EXPORT(strcmp),
    ESP_ELFSYM_EXPORT(strncmp),
    ESP_ELFSYM_EXPORT(strncpy),
    ESP_ELFSYM_EXPORT(strcpy),
    ESP_ELFSYM_EXPORT(strcat),
    ESP_ELFSYM_EXPORT(strchr),
    ESP_ELFSYM_EXPORT(strstr),
    ESP_ELFSYM_EXPORT(strerror),
    ESP_ELFSYM_EXPORT(strtod),
    ESP_ELFSYM_EXPORT(strrchr),
    ESP_ELFSYM_EXPORT(strtol),
    ESP_ELFSYM_EXPORT(strcspn),
    ESP_ELFSYM_EXPORT(strncat),
    ESP_ELFSYM_EXPORT(memset),
    ESP_ELFSYM_EXPORT(memcpy),
    ESP_ELFSYM_EXPORT(memcmp),
    ESP_ELFSYM_EXPORT(memchr),
    ESP_ELFSYM_EXPORT(memmove),
    // ctype
    ESP_ELFSYM_EXPORT(isalnum),
    ESP_ELFSYM_EXPORT(isalpha),
    ESP_ELFSYM_EXPORT(iscntrl),
    ESP_ELFSYM_EXPORT(isdigit),
    ESP_ELFSYM_EXPORT(isgraph),
    ESP_ELFSYM_EXPORT(islower),
    ESP_ELFSYM_EXPORT(isprint),
    ESP_ELFSYM_EXPORT(ispunct),
    ESP_ELFSYM_EXPORT(isspace),
    ESP_ELFSYM_EXPORT(isupper),
    ESP_ELFSYM_EXPORT(isxdigit),
    ESP_ELFSYM_EXPORT(tolower),
    ESP_ELFSYM_EXPORT(toupper),
    // ESP-IDF
    ESP_ELFSYM_EXPORT(esp_log),
    ESP_ELFSYM_EXPORT(esp_log_write),
    ESP_ELFSYM_EXPORT(esp_log_timestamp),
    ESP_ELFSYM_EXPORT(esp_err_to_name),
    // Tactility
    ESP_ELFSYM_EXPORT(tt_app_start),
    ESP_ELFSYM_EXPORT(tt_app_start_with_bundle),
    ESP_ELFSYM_EXPORT(tt_app_stop),
    ESP_ELFSYM_EXPORT(tt_app_register),
    ESP_ELFSYM_EXPORT(tt_app_get_parameters),
    ESP_ELFSYM_EXPORT(tt_app_set_result),
    ESP_ELFSYM_EXPORT(tt_app_has_result),
    ESP_ELFSYM_EXPORT(tt_app_selectiondialog_start),
    ESP_ELFSYM_EXPORT(tt_app_selectiondialog_get_result_index),
    ESP_ELFSYM_EXPORT(tt_app_alertdialog_start),
    ESP_ELFSYM_EXPORT(tt_app_alertdialog_get_result_index),
    ESP_ELFSYM_EXPORT(tt_app_get_user_data_path),
    ESP_ELFSYM_EXPORT(tt_app_get_user_data_child_path),
    ESP_ELFSYM_EXPORT(tt_app_get_assets_path),
    ESP_ELFSYM_EXPORT(tt_app_get_assets_child_path),
    ESP_ELFSYM_EXPORT(tt_lock_alloc_for_path),
    ESP_ELFSYM_EXPORT(tt_lock_acquire),
    ESP_ELFSYM_EXPORT(tt_lock_release),
    ESP_ELFSYM_EXPORT(tt_lock_free),
    ESP_ELFSYM_EXPORT(tt_bundle_alloc),
    ESP_ELFSYM_EXPORT(tt_bundle_free),
    ESP_ELFSYM_EXPORT(tt_bundle_opt_bool),
    ESP_ELFSYM_EXPORT(tt_bundle_opt_int32),
    ESP_ELFSYM_EXPORT(tt_bundle_opt_string),
    ESP_ELFSYM_EXPORT(tt_bundle_put_bool),
    ESP_ELFSYM_EXPORT(tt_bundle_put_int32),
    ESP_ELFSYM_EXPORT(tt_bundle_put_string),
    ESP_ELFSYM_EXPORT(tt_gps_has_coordinates),
    ESP_ELFSYM_EXPORT(tt_gps_get_coordinates),
    ESP_ELFSYM_EXPORT(tt_hal_configuration_get_ui_scale),
    ESP_ELFSYM_EXPORT(tt_hal_device_find),
    ESP_ELFSYM_EXPORT(tt_hal_display_driver_alloc),
    ESP_ELFSYM_EXPORT(tt_hal_display_driver_draw_bitmap),
    ESP_ELFSYM_EXPORT(tt_hal_display_driver_free),
    ESP_ELFSYM_EXPORT(tt_hal_display_driver_get_colorformat),
    ESP_ELFSYM_EXPORT(tt_hal_display_driver_get_pixel_height),
    ESP_ELFSYM_EXPORT(tt_hal_display_driver_get_pixel_width),
    ESP_ELFSYM_EXPORT(tt_hal_display_driver_lock),
    ESP_ELFSYM_EXPORT(tt_hal_display_driver_unlock),
    ESP_ELFSYM_EXPORT(tt_hal_display_driver_supported),
    ESP_ELFSYM_EXPORT(tt_hal_gpio_configure),
    ESP_ELFSYM_EXPORT(tt_hal_gpio_configure_with_pin_bitmask),
    ESP_ELFSYM_EXPORT(tt_hal_gpio_set_mode),
    ESP_ELFSYM_EXPORT(tt_hal_gpio_get_level),
    ESP_ELFSYM_EXPORT(tt_hal_gpio_set_level),
    ESP_ELFSYM_EXPORT(tt_hal_gpio_get_pin_count),
    ESP_ELFSYM_EXPORT(tt_hal_i2c_start),
    ESP_ELFSYM_EXPORT(tt_hal_i2c_stop),
    ESP_ELFSYM_EXPORT(tt_hal_i2c_is_started),
    ESP_ELFSYM_EXPORT(tt_hal_i2c_master_read),
    ESP_ELFSYM_EXPORT(tt_hal_i2c_master_read_register),
    ESP_ELFSYM_EXPORT(tt_hal_i2c_master_write),
    ESP_ELFSYM_EXPORT(tt_hal_i2c_master_write_register),
    ESP_ELFSYM_EXPORT(tt_hal_i2c_master_write_read),
    ESP_ELFSYM_EXPORT(tt_hal_i2c_master_has_device_at_address),
    ESP_ELFSYM_EXPORT(tt_hal_i2c_lock),
    ESP_ELFSYM_EXPORT(tt_hal_i2c_unlock),
    ESP_ELFSYM_EXPORT(tt_hal_touch_driver_supported),
    ESP_ELFSYM_EXPORT(tt_hal_touch_driver_alloc),
    ESP_ELFSYM_EXPORT(tt_hal_touch_driver_free),
    ESP_ELFSYM_EXPORT(tt_hal_touch_driver_get_touched_points),
    ESP_ELFSYM_EXPORT(tt_hal_uart_get_count),
    ESP_ELFSYM_EXPORT(tt_hal_uart_get_name),
    ESP_ELFSYM_EXPORT(tt_hal_uart_alloc),
    ESP_ELFSYM_EXPORT(tt_hal_uart_free),
    ESP_ELFSYM_EXPORT(tt_hal_uart_start),
    ESP_ELFSYM_EXPORT(tt_hal_uart_is_started),
    ESP_ELFSYM_EXPORT(tt_hal_uart_stop),
    ESP_ELFSYM_EXPORT(tt_hal_uart_read_bytes),
    ESP_ELFSYM_EXPORT(tt_hal_uart_read_byte),
    ESP_ELFSYM_EXPORT(tt_hal_uart_write_bytes),
    ESP_ELFSYM_EXPORT(tt_hal_uart_available),
    ESP_ELFSYM_EXPORT(tt_hal_uart_set_baud_rate),
    ESP_ELFSYM_EXPORT(tt_hal_uart_get_baud_rate),
    ESP_ELFSYM_EXPORT(tt_hal_uart_flush_input),
    ESP_ELFSYM_EXPORT(tt_lvgl_is_started),
    ESP_ELFSYM_EXPORT(tt_lvgl_lock),
    ESP_ELFSYM_EXPORT(tt_lvgl_unlock),
    ESP_ELFSYM_EXPORT(tt_lvgl_start),
    ESP_ELFSYM_EXPORT(tt_lvgl_stop),
    ESP_ELFSYM_EXPORT(tt_lvgl_software_keyboard_show),
    ESP_ELFSYM_EXPORT(tt_lvgl_software_keyboard_hide),
    ESP_ELFSYM_EXPORT(tt_lvgl_software_keyboard_is_enabled),
    ESP_ELFSYM_EXPORT(tt_lvgl_software_keyboard_activate),
    ESP_ELFSYM_EXPORT(tt_lvgl_software_keyboard_deactivate),
    ESP_ELFSYM_EXPORT(tt_lvgl_hardware_keyboard_is_available),
    ESP_ELFSYM_EXPORT(tt_lvgl_hardware_keyboard_set_indev),
    ESP_ELFSYM_EXPORT(tt_lvgl_toolbar_create),
    ESP_ELFSYM_EXPORT(tt_lvgl_toolbar_create_for_app),
    ESP_ELFSYM_EXPORT(tt_lvgl_toolbar_set_title),
    ESP_ELFSYM_EXPORT(tt_lvgl_toolbar_set_nav_action),
    ESP_ELFSYM_EXPORT(tt_lvgl_toolbar_add_image_button_action),
    ESP_ELFSYM_EXPORT(tt_lvgl_toolbar_add_text_button_action),
    ESP_ELFSYM_EXPORT(tt_lvgl_toolbar_add_switch_action),
    ESP_ELFSYM_EXPORT(tt_lvgl_toolbar_add_spinner_action),
    ESP_ELFSYM_EXPORT(tt_lvgl_toolbar_clear_actions),
    ESP_ELFSYM_EXPORT(tt_preferences_alloc),
    ESP_ELFSYM_EXPORT(tt_preferences_free),
    ESP_ELFSYM_EXPORT(tt_preferences_opt_bool),
    ESP_ELFSYM_EXPORT(tt_preferences_opt_int32),
    ESP_ELFSYM_EXPORT(tt_preferences_opt_string),
    ESP_ELFSYM_EXPORT(tt_preferences_put_bool),
    ESP_ELFSYM_EXPORT(tt_preferences_put_int32),
    ESP_ELFSYM_EXPORT(tt_preferences_put_string),
    ESP_ELFSYM_EXPORT(tt_timezone_set),
    ESP_ELFSYM_EXPORT(tt_timezone_get_name),
    ESP_ELFSYM_EXPORT(tt_timezone_get_code),
    ESP_ELFSYM_EXPORT(tt_timezone_is_format_24_hour),
    ESP_ELFSYM_EXPORT(tt_timezone_set_format_24_hour),
    ESP_ELFSYM_EXPORT(tt_wifi_get_radio_state),
    ESP_ELFSYM_EXPORT(tt_wifi_radio_state_to_string),
    ESP_ELFSYM_EXPORT(tt_wifi_scan),
    ESP_ELFSYM_EXPORT(tt_wifi_is_scanning),
    ESP_ELFSYM_EXPORT(tt_wifi_get_connection_target),
    ESP_ELFSYM_EXPORT(tt_wifi_set_enabled),
    ESP_ELFSYM_EXPORT(tt_wifi_connect),
    ESP_ELFSYM_EXPORT(tt_wifi_disconnect),
    ESP_ELFSYM_EXPORT(tt_wifi_is_connnection_secure),
    ESP_ELFSYM_EXPORT(tt_wifi_get_rssi),
    // tt::lvgl
    ESP_ELFSYM_EXPORT(tt_lvgl_spinner_create),
    // lv_event
    ESP_ELFSYM_EXPORT(lv_event_get_code),
    ESP_ELFSYM_EXPORT(lv_event_get_indev),
    ESP_ELFSYM_EXPORT(lv_event_get_key),
    ESP_ELFSYM_EXPORT(lv_event_get_param),
    ESP_ELFSYM_EXPORT(lv_event_get_scroll_anim),
    ESP_ELFSYM_EXPORT(lv_event_get_user_data),
    ESP_ELFSYM_EXPORT(lv_event_get_target_obj),
    ESP_ELFSYM_EXPORT(lv_event_get_target),
    ESP_ELFSYM_EXPORT(lv_event_get_current_target_obj),
    ESP_ELFSYM_EXPORT(lv_event_get_draw_task),
    // lv_obj
    ESP_ELFSYM_EXPORT(lv_color_hex),
    ESP_ELFSYM_EXPORT(lv_color_make),
    ESP_ELFSYM_EXPORT(lv_obj_center),
    ESP_ELFSYM_EXPORT(lv_obj_clean),
    ESP_ELFSYM_EXPORT(lv_obj_clear_flag),
    ESP_ELFSYM_EXPORT(lv_obj_create),
    ESP_ELFSYM_EXPORT(lv_obj_delete),
    ESP_ELFSYM_EXPORT(lv_obj_add_event_cb),
    ESP_ELFSYM_EXPORT(lv_obj_add_flag),
    ESP_ELFSYM_EXPORT(lv_obj_add_state),
    ESP_ELFSYM_EXPORT(lv_obj_align),
    ESP_ELFSYM_EXPORT(lv_obj_align_to),
    ESP_ELFSYM_EXPORT(lv_obj_get_parent),
    ESP_ELFSYM_EXPORT(lv_obj_get_height),
    ESP_ELFSYM_EXPORT(lv_obj_get_width),
    ESP_ELFSYM_EXPORT(lv_obj_get_coords),
    ESP_ELFSYM_EXPORT(lv_obj_get_x),
    ESP_ELFSYM_EXPORT(lv_obj_get_display),
    ESP_ELFSYM_EXPORT(lv_obj_get_y),
    ESP_ELFSYM_EXPORT(lv_obj_get_content_width),
    ESP_ELFSYM_EXPORT(lv_obj_get_content_height),
    ESP_ELFSYM_EXPORT(lv_obj_get_group),
    ESP_ELFSYM_EXPORT(lv_obj_get_user_data),
    ESP_ELFSYM_EXPORT(lv_obj_get_state),
    ESP_ELFSYM_EXPORT(lv_obj_has_flag),
    ESP_ELFSYM_EXPORT(lv_obj_has_flag_any),
    ESP_ELFSYM_EXPORT(lv_obj_has_state),
    ESP_ELFSYM_EXPORT(lv_obj_invalidate),
    ESP_ELFSYM_EXPORT(lv_obj_is_valid),
    ESP_ELFSYM_EXPORT(lv_obj_remove_event_cb),
    ESP_ELFSYM_EXPORT(lv_obj_remove_flag),
    ESP_ELFSYM_EXPORT(lv_obj_remove_state),
    ESP_ELFSYM_EXPORT(lv_obj_set_pos),
    ESP_ELFSYM_EXPORT(lv_obj_set_flex_align),
    ESP_ELFSYM_EXPORT(lv_obj_set_flex_flow),
    ESP_ELFSYM_EXPORT(lv_obj_set_flex_grow),
    ESP_ELFSYM_EXPORT(lv_obj_set_layout),
    ESP_ELFSYM_EXPORT(lv_obj_is_layout_positioned),
    ESP_ELFSYM_EXPORT(lv_obj_mark_layout_as_dirty),
    ESP_ELFSYM_EXPORT(lv_obj_get_style_layout),
    ESP_ELFSYM_EXPORT(lv_obj_update_layout),
    ESP_ELFSYM_EXPORT(lv_obj_set_scroll_dir),
    ESP_ELFSYM_EXPORT(lv_obj_scroll_to_view),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_radius),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_border_width),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_border_color),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_border_opa),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_line_width),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_line_color),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_line_opa),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_line_rounded),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_opa),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_bg_color),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_bg_opa),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_bg_image_src),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_bg_image_opa),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_bg_image_recolor),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_bg_image_recolor_opa),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_margin_hor),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_margin_ver),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_margin_top),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_margin_bottom),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_margin_left),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_margin_right),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_margin_all),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_pad_all),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_pad_hor),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_pad_ver),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_pad_top),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_pad_bottom),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_pad_left),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_pad_right),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_pad_column),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_pad_row),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_border_post),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_border_side),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_text_opa),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_text_align),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_text_color),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_text_font),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_text_decor),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_text_letter_space),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_text_line_space),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_text_outline_stroke_color),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_text_outline_stroke_opa),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_text_outline_stroke_width),
    ESP_ELFSYM_EXPORT(lv_obj_set_user_data),
    ESP_ELFSYM_EXPORT(lv_obj_set_align),
    ESP_ELFSYM_EXPORT(lv_obj_set_x),
    ESP_ELFSYM_EXPORT(lv_obj_set_y),
    ESP_ELFSYM_EXPORT(lv_obj_set_size),
    ESP_ELFSYM_EXPORT(lv_obj_set_width),
    ESP_ELFSYM_EXPORT(lv_obj_set_height),
    ESP_ELFSYM_EXPORT(lv_obj_send_event),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_outline_color),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_outline_width),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_outline_pad),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_outline_opa),
    ESP_ELFSYM_EXPORT(lv_obj_scroll_to_y),
    ESP_ELFSYM_EXPORT(lv_obj_set_scrollbar_mode),
    ESP_ELFSYM_EXPORT(lv_obj_get_child_count),
    ESP_ELFSYM_EXPORT(lv_obj_get_child),
    // lv_font
    ESP_ELFSYM_EXPORT(lv_font_get_default),
    // lv_theme
    ESP_ELFSYM_EXPORT(lv_theme_get_color_primary),
    ESP_ELFSYM_EXPORT(lv_theme_get_color_secondary),
    ESP_ELFSYM_EXPORT(lv_theme_get_font_small),
    ESP_ELFSYM_EXPORT(lv_theme_get_font_normal),
    ESP_ELFSYM_EXPORT(lv_theme_get_font_large),
    // lv_button
    ESP_ELFSYM_EXPORT(lv_button_create),
    // lv_buttonmatrix
    ESP_ELFSYM_EXPORT(lv_buttonmatrix_create),
    ESP_ELFSYM_EXPORT(lv_buttonmatrix_get_button_text),
    ESP_ELFSYM_EXPORT(lv_buttonmatrix_get_map),
    ESP_ELFSYM_EXPORT(lv_buttonmatrix_get_one_checked),
    ESP_ELFSYM_EXPORT(lv_buttonmatrix_get_selected_button),
    ESP_ELFSYM_EXPORT(lv_buttonmatrix_set_button_ctrl),
    ESP_ELFSYM_EXPORT(lv_buttonmatrix_set_button_ctrl_all),
    ESP_ELFSYM_EXPORT(lv_buttonmatrix_set_ctrl_map),
    ESP_ELFSYM_EXPORT(lv_buttonmatrix_set_map),
    ESP_ELFSYM_EXPORT(lv_buttonmatrix_set_one_checked),
    ESP_ELFSYM_EXPORT(lv_buttonmatrix_set_button_width),
    ESP_ELFSYM_EXPORT(lv_buttonmatrix_set_selected_button),
    // lv_label
    ESP_ELFSYM_EXPORT(lv_label_create),
    ESP_ELFSYM_EXPORT(lv_label_cut_text),
    ESP_ELFSYM_EXPORT(lv_label_get_long_mode),
    ESP_ELFSYM_EXPORT(lv_label_set_long_mode),
    ESP_ELFSYM_EXPORT(lv_label_get_text),
    ESP_ELFSYM_EXPORT(lv_label_set_text),
    ESP_ELFSYM_EXPORT(lv_label_set_text_fmt),
    // lv_switch
    ESP_ELFSYM_EXPORT(lv_switch_create),
    // lv_checkbox
    ESP_ELFSYM_EXPORT(lv_checkbox_create),
    ESP_ELFSYM_EXPORT(lv_checkbox_set_text),
    ESP_ELFSYM_EXPORT(lv_checkbox_get_text),
    ESP_ELFSYM_EXPORT(lv_checkbox_set_text_static),
    // lv_bar
    ESP_ELFSYM_EXPORT(lv_bar_create),
    ESP_ELFSYM_EXPORT(lv_bar_get_max_value),
    ESP_ELFSYM_EXPORT(lv_bar_get_min_value),
    ESP_ELFSYM_EXPORT(lv_bar_get_mode),
    ESP_ELFSYM_EXPORT(lv_bar_get_start_value),
    ESP_ELFSYM_EXPORT(lv_bar_get_value),
    ESP_ELFSYM_EXPORT(lv_bar_set_mode),
    ESP_ELFSYM_EXPORT(lv_bar_set_range),
    ESP_ELFSYM_EXPORT(lv_bar_set_start_value),
    ESP_ELFSYM_EXPORT(lv_bar_set_value),
    ESP_ELFSYM_EXPORT(lv_bar_is_symmetrical),
    // lv_dropdown
    ESP_ELFSYM_EXPORT(lv_dropdown_create),
    ESP_ELFSYM_EXPORT(lv_dropdown_add_option),
    ESP_ELFSYM_EXPORT(lv_dropdown_clear_options),
    ESP_ELFSYM_EXPORT(lv_dropdown_close),
    ESP_ELFSYM_EXPORT(lv_dropdown_get_dir),
    ESP_ELFSYM_EXPORT(lv_dropdown_get_list),
    ESP_ELFSYM_EXPORT(lv_dropdown_get_option_count),
    ESP_ELFSYM_EXPORT(lv_dropdown_get_option_index),
    ESP_ELFSYM_EXPORT(lv_dropdown_get_options),
    ESP_ELFSYM_EXPORT(lv_dropdown_get_selected),
    ESP_ELFSYM_EXPORT(lv_dropdown_get_selected_str),
    ESP_ELFSYM_EXPORT(lv_dropdown_get_selected_highlight),
    ESP_ELFSYM_EXPORT(lv_dropdown_set_dir),
    ESP_ELFSYM_EXPORT(lv_dropdown_set_options),
    ESP_ELFSYM_EXPORT(lv_dropdown_set_options_static),
    ESP_ELFSYM_EXPORT(lv_dropdown_set_selected),
    ESP_ELFSYM_EXPORT(lv_dropdown_set_selected_highlight),
    ESP_ELFSYM_EXPORT(lv_dropdown_set_symbol),
    ESP_ELFSYM_EXPORT(lv_dropdown_set_text),
    ESP_ELFSYM_EXPORT(lv_dropdown_open),
    // lv_list
    ESP_ELFSYM_EXPORT(lv_list_create),
    ESP_ELFSYM_EXPORT(lv_list_add_text),
    ESP_ELFSYM_EXPORT(lv_list_add_button),
    ESP_ELFSYM_EXPORT(lv_list_get_button_text),
    ESP_ELFSYM_EXPORT(lv_list_set_button_text),
    // lv_textarea
    ESP_ELFSYM_EXPORT(lv_textarea_create),
    ESP_ELFSYM_EXPORT(lv_textarea_get_accepted_chars),
    ESP_ELFSYM_EXPORT(lv_textarea_get_label),
    ESP_ELFSYM_EXPORT(lv_textarea_get_max_length),
    ESP_ELFSYM_EXPORT(lv_textarea_get_one_line),
    ESP_ELFSYM_EXPORT(lv_textarea_get_text),
    ESP_ELFSYM_EXPORT(lv_textarea_get_text_selection),
    ESP_ELFSYM_EXPORT(lv_textarea_set_one_line),
    ESP_ELFSYM_EXPORT(lv_textarea_set_accepted_chars),
    ESP_ELFSYM_EXPORT(lv_textarea_set_align),
    ESP_ELFSYM_EXPORT(lv_textarea_set_password_bullet),
    ESP_ELFSYM_EXPORT(lv_textarea_set_password_mode),
    ESP_ELFSYM_EXPORT(lv_textarea_set_password_show_time),
    ESP_ELFSYM_EXPORT(lv_textarea_set_placeholder_text),
    ESP_ELFSYM_EXPORT(lv_textarea_set_text),
    ESP_ELFSYM_EXPORT(lv_textarea_set_text_selection),
    ESP_ELFSYM_EXPORT(lv_textarea_set_max_length),
    ESP_ELFSYM_EXPORT(lv_textarea_set_cursor_click_pos),
    ESP_ELFSYM_EXPORT(lv_textarea_add_text),
    // lv_palette
    ESP_ELFSYM_EXPORT(lv_palette_main),
    ESP_ELFSYM_EXPORT(lv_palette_darken),
    ESP_ELFSYM_EXPORT(lv_palette_lighten),
    // lv_display
    ESP_ELFSYM_EXPORT(lv_display_get_default),
    ESP_ELFSYM_EXPORT(lv_display_get_horizontal_resolution),
    ESP_ELFSYM_EXPORT(lv_display_get_vertical_resolution),
    ESP_ELFSYM_EXPORT(lv_display_get_physical_horizontal_resolution),
    ESP_ELFSYM_EXPORT(lv_display_get_physical_vertical_resolution),
    ESP_ELFSYM_EXPORT(lv_display_dpx),
    // lv_pct
    ESP_ELFSYM_EXPORT(lv_pct),
    ESP_ELFSYM_EXPORT(lv_pct_to_px),
    // lv_spinbox
    ESP_ELFSYM_EXPORT(lv_spinbox_create),
    ESP_ELFSYM_EXPORT(lv_spinbox_decrement),
    ESP_ELFSYM_EXPORT(lv_spinbox_get_rollover),
    ESP_ELFSYM_EXPORT(lv_spinbox_get_step),
    ESP_ELFSYM_EXPORT(lv_spinbox_get_value),
    ESP_ELFSYM_EXPORT(lv_spinbox_increment),
    ESP_ELFSYM_EXPORT(lv_spinbox_set_rollover),
    ESP_ELFSYM_EXPORT(lv_spinbox_set_step),
    ESP_ELFSYM_EXPORT(lv_spinbox_set_range),
    ESP_ELFSYM_EXPORT(lv_spinbox_set_digit_format),
    ESP_ELFSYM_EXPORT(lv_spinbox_set_digit_step_direction),
    ESP_ELFSYM_EXPORT(lv_spinbox_set_value),
    ESP_ELFSYM_EXPORT(lv_spinbox_set_cursor_pos),
    ESP_ELFSYM_EXPORT(lv_spinbox_step_next),
    ESP_ELFSYM_EXPORT(lv_spinbox_step_prev),
    // lv_indev
    ESP_ELFSYM_EXPORT(lv_indev_get_type),
    ESP_ELFSYM_EXPORT(lv_indev_get_point),
    ESP_ELFSYM_EXPORT(lv_indev_get_display),
    ESP_ELFSYM_EXPORT(lv_indev_get_key),
    ESP_ELFSYM_EXPORT(lv_indev_get_gesture_dir),
    ESP_ELFSYM_EXPORT(lv_indev_get_state),
    ESP_ELFSYM_EXPORT(lv_indev_active),
    // lv_timer
    ESP_ELFSYM_EXPORT(lv_timer_handler),
    ESP_ELFSYM_EXPORT(lv_timer_handler_run_in_period),
    ESP_ELFSYM_EXPORT(lv_timer_periodic_handler),
    ESP_ELFSYM_EXPORT(lv_timer_handler_set_resume_cb),
    ESP_ELFSYM_EXPORT(lv_timer_create_basic),
    ESP_ELFSYM_EXPORT(lv_timer_create),
    ESP_ELFSYM_EXPORT(lv_timer_delete),
    ESP_ELFSYM_EXPORT(lv_timer_pause),
    ESP_ELFSYM_EXPORT(lv_timer_resume),
    ESP_ELFSYM_EXPORT(lv_timer_set_cb),
    ESP_ELFSYM_EXPORT(lv_timer_set_period),
    ESP_ELFSYM_EXPORT(lv_timer_ready),
    ESP_ELFSYM_EXPORT(lv_timer_set_repeat_count),
    ESP_ELFSYM_EXPORT(lv_timer_set_auto_delete),
    ESP_ELFSYM_EXPORT(lv_timer_set_user_data),
    ESP_ELFSYM_EXPORT(lv_timer_reset),
    ESP_ELFSYM_EXPORT(lv_timer_enable),
    ESP_ELFSYM_EXPORT(lv_timer_get_idle),
    ESP_ELFSYM_EXPORT(lv_timer_get_time_until_next),
    ESP_ELFSYM_EXPORT(lv_timer_get_next),
    ESP_ELFSYM_EXPORT(lv_timer_get_user_data),
    ESP_ELFSYM_EXPORT(lv_timer_get_paused),
    // lvgl other
    ESP_ELFSYM_EXPORT(lv_refr_now),
    ESP_ELFSYM_EXPORT(lv_line_create),
    ESP_ELFSYM_EXPORT(lv_line_set_points),
    ESP_ELFSYM_EXPORT(lv_line_set_points_mutable),
    // lv_group
    ESP_ELFSYM_EXPORT(lv_group_remove_obj),
    ESP_ELFSYM_EXPORT(lv_group_focus_obj),
    ESP_ELFSYM_EXPORT(lv_group_get_default),
    ESP_ELFSYM_EXPORT(lv_group_add_obj),
    ESP_ELFSYM_EXPORT(lv_group_set_default),
    // lv_mem
    ESP_ELFSYM_EXPORT(lv_free),
    ESP_ELFSYM_EXPORT(lv_malloc),
    // lv_draw
    ESP_ELFSYM_EXPORT(lv_draw_task_get_draw_dsc),
    ESP_ELFSYM_EXPORT(lv_draw_task_get_label_dsc),
    ESP_ELFSYM_EXPORT(lv_draw_task_get_fill_dsc),
    // lv_image
    ESP_ELFSYM_EXPORT(lv_image_create),
    ESP_ELFSYM_EXPORT(lv_image_set_src),
    // stdio.h
    ESP_ELFSYM_EXPORT(rename),
    // dirent.h
    ESP_ELFSYM_EXPORT(opendir),
    ESP_ELFSYM_EXPORT(closedir),
    ESP_ELFSYM_EXPORT(readdir),
    // fcntl.h
    ESP_ELFSYM_EXPORT(fcntl),
    // lwip/sockets.h
    ESP_ELFSYM_EXPORT(lwip_setsockopt),
    ESP_ELFSYM_EXPORT(lwip_socket),
    ESP_ELFSYM_EXPORT(lwip_recv),
    ESP_ELFSYM_EXPORT(lwip_getpeername),
    ESP_ELFSYM_EXPORT(lwip_bind),
    ESP_ELFSYM_EXPORT(lwip_listen),
    ESP_ELFSYM_EXPORT(lwip_close),
    ESP_ELFSYM_EXPORT(lwip_accept),
    ESP_ELFSYM_EXPORT(lwip_getsockname),
    ESP_ELFSYM_EXPORT(lwip_send),
    // sys/stat.h
    ESP_ELFSYM_EXPORT(stat),
    ESP_ELFSYM_EXPORT(mkdir),
    // esp_netif.h
    ESP_ELFSYM_EXPORT(esp_netif_get_ip_info),
    ESP_ELFSYM_EXPORT(esp_netif_get_handle_from_ifkey),
    // delimiter
    ESP_ELFSYM_END
};

uintptr_t resolve_symbol(const esp_elfsym* source, const char* symbolName) {
    const esp_elfsym* symbol_iterator = source;
    while (symbol_iterator->name != nullptr) {
        if (strcmp(symbol_iterator->name, symbolName) == 0) {
            return reinterpret_cast<uintptr_t>(symbol_iterator->sym);
        }
        symbol_iterator++;
    }
    return 0;
}

uintptr_t tt_symbol_resolver(const char* symbolName) {
    static const std::vector all_symbols = {
        main_symbols,
        gcc_soft_float_symbols,
        stl_symbols,
        cplusplus_symbols,
        pthread_symbols,
        freertos_symbols,
        string_symbols,
        esp_event_symbols,
        esp_http_client_symbols,
    };

    for (const auto* symbols : all_symbols) {
        const uintptr_t address = resolve_symbol(symbols, symbolName);
        if (address != 0) {
            return address;
        }
    }

    return 0;
}

void tt_init_tactility_c() {
    elf_set_symbol_resolver(tt_symbol_resolver);
}

} // extern "C"

#else // Simulator

extern "C" {

void tt_init_tactility_c() {
}

}

#endif // ESP_PLATFORM
