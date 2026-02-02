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

#include "symbols/cplusplus.h"
#include "symbols/esp_event.h"
#include "symbols/esp_http_client.h"
#include "symbols/freertos.h"
#include "symbols/gcc_soft_float.h"
#include "symbols/pthread.h"
#include "symbols/stl.h"
#include "symbols/string.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <ctime>
#include <ctype.h>
#include <getopt.h>
#include <dirent.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_sntp.h>
#include <esp_netif.h>
#include <fcntl.h>
#include <lwip/sockets.h>
#include <locale.h>
#include <setjmp.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <vector>

#include <Tactility/Tactility.h>

bool module_parent_resolve_symbol(ModuleParent* pParent, const char* name, uintptr_t* pInt);
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
    ESP_ELFSYM_EXPORT(atan),
    ESP_ELFSYM_EXPORT(cos),
    ESP_ELFSYM_EXPORT(sin),
    ESP_ELFSYM_EXPORT(tan),
    ESP_ELFSYM_EXPORT(tanh),
    ESP_ELFSYM_EXPORT(frexp),
    ESP_ELFSYM_EXPORT(modf),
    ESP_ELFSYM_EXPORT(ceil),
    ESP_ELFSYM_EXPORT(fabs),
    ESP_ELFSYM_EXPORT(floor),
#ifndef _REENT_ONLY
    ESP_ELFSYM_EXPORT(acos),
    ESP_ELFSYM_EXPORT(asin),
    ESP_ELFSYM_EXPORT(atan2),
    ESP_ELFSYM_EXPORT(cos),
    ESP_ELFSYM_EXPORT(sinh),
    ESP_ELFSYM_EXPORT(exp),
    ESP_ELFSYM_EXPORT(ldexp),
    ESP_ELFSYM_EXPORT(log),
    ESP_ELFSYM_EXPORT(log10),
    ESP_ELFSYM_EXPORT(pow),
    ESP_ELFSYM_EXPORT(sqrt),
    ESP_ELFSYM_EXPORT(fmod),
#endif
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
    ESP_ELFSYM_EXPORT(abort),
    ESP_ELFSYM_EXPORT(fclose),
    ESP_ELFSYM_EXPORT(feof),
    ESP_ELFSYM_EXPORT(ferror),
    ESP_ELFSYM_EXPORT(fflush),
    ESP_ELFSYM_EXPORT(fgetc),
    ESP_ELFSYM_EXPORT(fgetpos),
    ESP_ELFSYM_EXPORT(fgets),
    ESP_ELFSYM_EXPORT(fopen),
    ESP_ELFSYM_EXPORT(freopen),
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
    ESP_ELFSYM_EXPORT(strpbrk),
    ESP_ELFSYM_EXPORT(strspn),
    ESP_ELFSYM_EXPORT(strcoll),
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
    ESP_ELFSYM_EXPORT(tt_hal_gpio_get_level),
    ESP_ELFSYM_EXPORT(tt_hal_gpio_get_pin_count),
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
    // Locale
    ESP_ELFSYM_EXPORT(localeconv),
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

    auto& module_parent = tt::getModuleParent();
    uintptr_t symbol_address;
    if (module_parent_resolve_symbol(&module_parent, symbolName, &symbol_address)) {
        return symbol_address;
    }

    return 0;
}

void tt_init_tactility_c() {
    elf_set_symbol_resolver(tt_symbol_resolver);
}

}

// extern "C"

#else // Simulator

extern "C" {

void tt_init_tactility_c() {
}

}

#endif // ESP_PLATFORM
