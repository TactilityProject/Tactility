#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#include <assert.h>
#include <driver/gpio.h>
#include <driver/i2s_common.h>
#include <driver/i2s_std.h>
#include <driver/ledc.h>
#include <esp_err.h>
#include <esp_event.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_log_level.h>
#include <esp_log_timestamp.h>
#include <esp_log_write.h>
#include <esp_random.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_vfs.h>
#include <miniz.h>
#include <sys/errno.h>

#ifdef CONFIG_IDF_TARGET_ESP32P4
#include <esp_cache.h>
#include <driver/ppa.h>
#endif

#include <tactility/module.h>

#include <soc/soc_caps.h>

// GCC soft-float / compiler-rt helpers an app may need but can't reach through a header -
// reference: https://gcc.gnu.org/onlinedocs/gccint/Soft-float-library-routines.html
// ESP32P4 (RISC-V, native single-precision FPU) only needs the double-precision + 64-bit-div
// subset; every other (Xtensa) target needs the full single- and double-precision set.
extern "C" {
#ifndef CONFIG_IDF_TARGET_ESP32P4
    extern float __addsf3(float a, float b);
    extern double __adddf3(double a, double b);
    extern float __subsf3(float a, float b);
    extern double __subdf3(double a, double b);
    extern float __mulsf3(float a, float b);
    extern double __muldf3(double a, double b);
    extern float __divsf3(float a, float b);
    extern double __divdf3(double a, double b);
    extern float __negsf2(float a);
    extern double __negdf2(double a);
    extern double __extendsfdf2(float a);
    extern float __truncdfsf2(double a);
    extern int __fixsfsi(float a);
    extern int __fixdfsi(double a);
    extern long __fixsfdi(float a);
    extern long __fixdfdi(double a);
    extern unsigned int __fixunssfsi(float a);
    extern unsigned int __fixunsdfsi(double a);
    extern unsigned long __fixunssfdi(float a);
    extern unsigned long __fixunsdfdi(double a);
    extern float __floatsisf(int i);
    extern double __floatsidf(int i);
    extern float __floatdisf(long i);
    extern double __floatdidf(long i);
    extern float __floatunsisf(unsigned int i);
    extern double __floatunsidf(unsigned int i);
    extern float __floatundisf(unsigned long i);
    extern double __floatundidf(unsigned long i);
    float __powisf2(float a, int b);
    double __powidf2(double a, int b);
    int __cmpdf2(double a, double b);
    int __unordsf2(float a, float b);
    int __unorddf2(double a, double b);
    int __eqsf2(float a, float b);
    int __eqdf2(double a, double b);
    int __nesf2(float a, float b);
    int __nedf2(double a, double b);
    int __gesf2(float a, float b);
    int __gedf2(double a, double b);
    int __ltsf2(float a, float b);
    int __ltdf2(double a, double b);
    int __lesf2(float a, float b);
    int __ledf2(double a, double b);
    int __gtsf2(float a, float b);
    int __gtdf2(double a, double b);
    // GCC integer arithmetic helpers (needed on 32-bit targets for 64-bit ops)
    long long __divdi3(long long a, long long b);
    long long __moddi3(long long a, long long b);
    unsigned long long __udivdi3(unsigned long long a, unsigned long long b);
    unsigned long long __umoddi3(unsigned long long a, unsigned long long b);
#else
    extern double __adddf3(double a, double b);
    extern double __subdf3(double a, double b);
    extern double __muldf3(double a, double b);
    extern double __divdf3(double a, double b);
    extern double __negdf2(double a);
    extern double __extendsfdf2(float a);
    extern float __truncdfsf2(double a);
    extern int __fixdfsi(double a);
    extern long __fixdfdi(double a);
    extern unsigned int __fixunsdfsi(double a);
    extern unsigned long __fixunssfdi(float a);
    extern unsigned long __fixunsdfdi(double a);
    extern double __floatsidf(int i);
    extern float __floatdisf(long i);
    extern double __floatdidf(long i);
    extern double __floatunsidf(unsigned int i);
    extern float __floatundisf(unsigned long i);
    extern double __floatundidf(unsigned long i);
    float __powisf2(float a, int b);
    double __powidf2(double a, int b);
    int __cmpdf2(double a, double b);
    int __unorddf2(double a, double b);
    int __eqdf2(double a, double b);
    int __nedf2(double a, double b);
    int __gedf2(double a, double b);
    int __ltdf2(double a, double b);
    int __ledf2(double a, double b);
    int __gtdf2(double a, double b);
    // GCC integer/bitwise helpers (compiler-rt)
    int __clzsi2(unsigned int x);
    // GCC 64-bit integer arithmetic helpers (needed for 64-bit div on 32-bit RISC-V)
    long long __divdi3(long long a, long long b);
    unsigned long long __udivdi3(unsigned long long a, unsigned long long b);
#endif
}

extern "C" {

static const ModuleSymbol platform_esp32_symbols[] = {
    // cassert
    DEFINE_MODULE_SYMBOL(__assert_func),
    // esp_log
    DEFINE_MODULE_SYMBOL(esp_log),
    DEFINE_MODULE_SYMBOL(esp_log_level_set),
    DEFINE_MODULE_SYMBOL(esp_log_set_vprintf),
    DEFINE_MODULE_SYMBOL(esp_log_write),
    DEFINE_MODULE_SYMBOL(esp_log_timestamp),
    // esp_err
    DEFINE_MODULE_SYMBOL(esp_err_to_name),
    DEFINE_MODULE_SYMBOL(_esp_error_check_failed),
    // esp random
    DEFINE_MODULE_SYMBOL(esp_random),
    DEFINE_MODULE_SYMBOL(esp_fill_random),
    // esp_event_loop
    DEFINE_MODULE_SYMBOL(esp_event_loop_create),
    DEFINE_MODULE_SYMBOL(esp_event_loop_delete),
    DEFINE_MODULE_SYMBOL(esp_event_loop_create_default),
    DEFINE_MODULE_SYMBOL(esp_event_loop_delete_default),
    DEFINE_MODULE_SYMBOL(esp_event_loop_run),
    DEFINE_MODULE_SYMBOL(esp_event_handler_register),
    DEFINE_MODULE_SYMBOL(esp_event_handler_register_with),
    DEFINE_MODULE_SYMBOL(esp_event_handler_instance_register_with),
    DEFINE_MODULE_SYMBOL(esp_event_handler_instance_register),
    DEFINE_MODULE_SYMBOL(esp_event_handler_unregister),
    DEFINE_MODULE_SYMBOL(esp_event_handler_unregister_with),
    DEFINE_MODULE_SYMBOL(esp_event_handler_instance_unregister_with),
    DEFINE_MODULE_SYMBOL(esp_event_handler_instance_unregister),
    DEFINE_MODULE_SYMBOL(esp_event_post),
    DEFINE_MODULE_SYMBOL(esp_event_post_to),
    DEFINE_MODULE_SYMBOL(esp_event_isr_post),
    DEFINE_MODULE_SYMBOL(esp_event_isr_post_to),
    // sys/errno.h
    DEFINE_MODULE_SYMBOL(__errno),
    // esp_vfs.h
    DEFINE_MODULE_SYMBOL(esp_vfs_register),
    DEFINE_MODULE_SYMBOL(esp_vfs_unregister),
    // driver/gpio.h
    DEFINE_MODULE_SYMBOL(gpio_config),
    DEFINE_MODULE_SYMBOL(gpio_get_level),
    DEFINE_MODULE_SYMBOL(gpio_set_level),
    DEFINE_MODULE_SYMBOL(gpio_reset_pin),
    // driver/i2s_common.h
    DEFINE_MODULE_SYMBOL(i2s_new_channel),
    DEFINE_MODULE_SYMBOL(i2s_del_channel),
    DEFINE_MODULE_SYMBOL(i2s_channel_enable),
    DEFINE_MODULE_SYMBOL(i2s_channel_disable),
    DEFINE_MODULE_SYMBOL(i2s_channel_write),
    DEFINE_MODULE_SYMBOL(i2s_channel_get_info),
    DEFINE_MODULE_SYMBOL(i2s_channel_read),
    DEFINE_MODULE_SYMBOL(i2s_channel_register_event_callback),
    DEFINE_MODULE_SYMBOL(i2s_channel_preload_data),
    DEFINE_MODULE_SYMBOL(i2s_channel_tune_rate),
    // driver/i2s_std.h
    DEFINE_MODULE_SYMBOL(i2s_channel_init_std_mode),
    DEFINE_MODULE_SYMBOL(i2s_channel_reconfig_std_clock),
    DEFINE_MODULE_SYMBOL(i2s_channel_reconfig_std_slot),
    DEFINE_MODULE_SYMBOL(i2s_channel_reconfig_std_gpio),
    // miniz.h
    DEFINE_MODULE_SYMBOL(tinfl_decompress),
    DEFINE_MODULE_SYMBOL(tinfl_decompress_mem_to_callback),
    DEFINE_MODULE_SYMBOL(tinfl_decompress_mem_to_mem),
    DEFINE_MODULE_SYMBOL(tdefl_init),
    DEFINE_MODULE_SYMBOL(tdefl_compress),
    DEFINE_MODULE_SYMBOL(tdefl_compress_buffer),
    DEFINE_MODULE_SYMBOL(tdefl_compress_mem_to_mem),
    DEFINE_MODULE_SYMBOL(tdefl_compress_mem_to_output),
    DEFINE_MODULE_SYMBOL(tdefl_get_adler32),
    DEFINE_MODULE_SYMBOL(tdefl_get_prev_return_status),
    // ledc
    DEFINE_MODULE_SYMBOL(ledc_update_duty),
    DEFINE_MODULE_SYMBOL(ledc_set_freq),
    DEFINE_MODULE_SYMBOL(ledc_channel_config),
    DEFINE_MODULE_SYMBOL(ledc_set_duty),
    DEFINE_MODULE_SYMBOL(ledc_set_fade),
    DEFINE_MODULE_SYMBOL(ledc_set_fade_with_step),
    DEFINE_MODULE_SYMBOL(ledc_set_fade_with_time),
    DEFINE_MODULE_SYMBOL(ledc_set_fade_step_and_start),
    DEFINE_MODULE_SYMBOL(ledc_set_fade_time_and_start),
    DEFINE_MODULE_SYMBOL(ledc_set_pin),
    DEFINE_MODULE_SYMBOL(ledc_timer_config),
    DEFINE_MODULE_SYMBOL(ledc_timer_pause),
    DEFINE_MODULE_SYMBOL(ledc_timer_resume),
    DEFINE_MODULE_SYMBOL(ledc_timer_rst),
    // esp_heap_caps.h
    DEFINE_MODULE_SYMBOL(heap_caps_get_total_size),
    DEFINE_MODULE_SYMBOL(heap_caps_get_allocated_size),
    DEFINE_MODULE_SYMBOL(heap_caps_get_free_size),
    DEFINE_MODULE_SYMBOL(heap_caps_get_largest_free_block),
    DEFINE_MODULE_SYMBOL(heap_caps_aligned_alloc),
    DEFINE_MODULE_SYMBOL(heap_caps_malloc),
    DEFINE_MODULE_SYMBOL(heap_caps_calloc),
    DEFINE_MODULE_SYMBOL(heap_caps_free),
    // esp_timer.h
    DEFINE_MODULE_SYMBOL(esp_timer_create),
    DEFINE_MODULE_SYMBOL(esp_timer_stop),
    DEFINE_MODULE_SYMBOL(esp_timer_delete),
    DEFINE_MODULE_SYMBOL(esp_timer_start_periodic),
    DEFINE_MODULE_SYMBOL(esp_timer_start_once),
    DEFINE_MODULE_SYMBOL(esp_timer_get_time),
#ifdef CONFIG_IDF_TARGET_ESP32P4
    // driver/ppa.h
    DEFINE_MODULE_SYMBOL(ppa_register_client),
    DEFINE_MODULE_SYMBOL(ppa_unregister_client),
    DEFINE_MODULE_SYMBOL(ppa_do_scale_rotate_mirror),
    // esp_cache.h
    DEFINE_MODULE_SYMBOL(esp_cache_msync),
#endif
    // esp_system.h
    DEFINE_MODULE_SYMBOL(esp_restart),
    // soft float
#ifndef CONFIG_IDF_TARGET_ESP32P4
    DEFINE_MODULE_SYMBOL(__addsf3),
    DEFINE_MODULE_SYMBOL(__adddf3),
    DEFINE_MODULE_SYMBOL(__subsf3),
    DEFINE_MODULE_SYMBOL(__subdf3),
    DEFINE_MODULE_SYMBOL(__mulsf3),
    DEFINE_MODULE_SYMBOL(__muldf3),
    DEFINE_MODULE_SYMBOL(__divsf3),
    DEFINE_MODULE_SYMBOL(__divdf3),
    DEFINE_MODULE_SYMBOL(__negsf2),
    DEFINE_MODULE_SYMBOL(__negdf2),
    DEFINE_MODULE_SYMBOL(__extendsfdf2),
    DEFINE_MODULE_SYMBOL(__truncdfsf2),
    DEFINE_MODULE_SYMBOL(__fixsfsi),
    DEFINE_MODULE_SYMBOL(__fixdfsi),
    DEFINE_MODULE_SYMBOL(__fixsfdi),
    DEFINE_MODULE_SYMBOL(__fixdfdi),
    DEFINE_MODULE_SYMBOL(__fixunssfsi),
    DEFINE_MODULE_SYMBOL(__fixunsdfsi),
    DEFINE_MODULE_SYMBOL(__fixunssfdi),
    DEFINE_MODULE_SYMBOL(__fixunsdfdi),
    DEFINE_MODULE_SYMBOL(__floatsisf),
    DEFINE_MODULE_SYMBOL(__floatsidf),
    DEFINE_MODULE_SYMBOL(__floatdisf),
    DEFINE_MODULE_SYMBOL(__floatdidf),
    DEFINE_MODULE_SYMBOL(__floatunsisf),
    DEFINE_MODULE_SYMBOL(__floatunsidf),
    DEFINE_MODULE_SYMBOL(__floatundisf),
    DEFINE_MODULE_SYMBOL(__floatundidf),
    DEFINE_MODULE_SYMBOL(__powisf2),
    DEFINE_MODULE_SYMBOL(__powidf2),
    DEFINE_MODULE_SYMBOL(__unordsf2),
    DEFINE_MODULE_SYMBOL(__unorddf2),
    DEFINE_MODULE_SYMBOL(__eqsf2),
    DEFINE_MODULE_SYMBOL(__eqdf2),
    DEFINE_MODULE_SYMBOL(__nesf2),
    DEFINE_MODULE_SYMBOL(__nedf2),
    DEFINE_MODULE_SYMBOL(__gesf2),
    DEFINE_MODULE_SYMBOL(__gedf2),
    DEFINE_MODULE_SYMBOL(__ltsf2),
    DEFINE_MODULE_SYMBOL(__ltdf2),
    DEFINE_MODULE_SYMBOL(__lesf2),
    DEFINE_MODULE_SYMBOL(__ledf2),
    DEFINE_MODULE_SYMBOL(__gtsf2),
    DEFINE_MODULE_SYMBOL(__gtdf2),
    DEFINE_MODULE_SYMBOL(__divdi3),
    DEFINE_MODULE_SYMBOL(__moddi3),
    DEFINE_MODULE_SYMBOL(__udivdi3),
    DEFINE_MODULE_SYMBOL(__umoddi3),
#else
    DEFINE_MODULE_SYMBOL(__adddf3),
    DEFINE_MODULE_SYMBOL(__subdf3),
    DEFINE_MODULE_SYMBOL(__muldf3),
    DEFINE_MODULE_SYMBOL(__divdf3),
    DEFINE_MODULE_SYMBOL(__negdf2),
    DEFINE_MODULE_SYMBOL(__extendsfdf2),
    DEFINE_MODULE_SYMBOL(__truncdfsf2),
    DEFINE_MODULE_SYMBOL(__fixdfsi),
    DEFINE_MODULE_SYMBOL(__fixdfdi),
    DEFINE_MODULE_SYMBOL(__fixunsdfsi),
    DEFINE_MODULE_SYMBOL(__fixunssfdi),
    DEFINE_MODULE_SYMBOL(__fixunsdfdi),
    DEFINE_MODULE_SYMBOL(__floatsidf),
    DEFINE_MODULE_SYMBOL(__floatdisf),
    DEFINE_MODULE_SYMBOL(__floatdidf),
    DEFINE_MODULE_SYMBOL(__floatunsidf),
    DEFINE_MODULE_SYMBOL(__floatundisf),
    DEFINE_MODULE_SYMBOL(__floatundidf),
    DEFINE_MODULE_SYMBOL(__powisf2),
    DEFINE_MODULE_SYMBOL(__powidf2),
    DEFINE_MODULE_SYMBOL(__unorddf2),
    DEFINE_MODULE_SYMBOL(__eqdf2),
    DEFINE_MODULE_SYMBOL(__nedf2),
    DEFINE_MODULE_SYMBOL(__gedf2),
    DEFINE_MODULE_SYMBOL(__ltdf2),
    DEFINE_MODULE_SYMBOL(__ledf2),
    DEFINE_MODULE_SYMBOL(__gtdf2),
    DEFINE_MODULE_SYMBOL(__clzsi2),
    DEFINE_MODULE_SYMBOL(__divdi3),
    DEFINE_MODULE_SYMBOL(__udivdi3),
#endif
    MODULE_SYMBOL_TERMINATOR,
};

extern Driver esp32_adc_oneshot_driver;
extern Driver esp32_gpio_driver;
extern Driver esp32_i2c_driver;
extern Driver esp32_i2c_master_driver;
extern Driver esp32_i2s_driver;
#if SOC_LCD_I80_SUPPORTED
extern Driver esp32_i8080_driver;
#endif
extern Driver esp32_pwm_ledc_driver;
#if SOC_SDMMC_HOST_SUPPORTED
extern Driver esp32_sdmmc_driver;
#endif
extern Driver esp32_sdspi_driver;
extern Driver esp32_spi_driver;
extern Driver esp32_uart_driver;
extern Driver esp32_grove_driver;
#if defined(CONFIG_SOC_WIFI_SUPPORTED) || defined(CONFIG_ESP_HOSTED_ENABLED)
extern Driver esp32_wifi_driver;
extern Driver esp32_wifi_pinned_driver;
#endif
#if defined(CONFIG_BT_NIMBLE_ENABLED)
extern Driver esp32_bluetooth_driver;
extern Driver esp32_ble_serial_driver;
extern Driver esp32_ble_midi_driver;
extern Driver esp32_ble_hid_device_driver;
#endif
#if SOC_USB_OTG_SUPPORTED
extern Driver esp32_usbhost_driver;
extern Driver esp32_usbhost_hid_driver;
extern Driver esp32_usbhost_hid_keyboard_driver;
extern Driver esp32_usbhost_midi_driver;
extern Driver esp32_usbhost_msc_driver;
#endif
#if SOC_USB_OTG_SUPPORTED && (CONFIG_TINYUSB_HID_COUNT || CONFIG_TINYUSB_MSC_ENABLED || CONFIG_TINYUSB_MIDI_COUNT || CONFIG_TINYUSB_CDC_ENABLED)
extern Driver esp32_usb_device_controller_driver;
#endif
#if SOC_USB_OTG_SUPPORTED && CONFIG_TINYUSB_HID_COUNT
extern Driver esp32_usb_hid_device_driver;
#endif
#if SOC_USB_OTG_SUPPORTED && CONFIG_TINYUSB_MSC_ENABLED
extern Driver esp32_usb_msc_device_driver;
#endif
#if SOC_USB_OTG_SUPPORTED && CONFIG_TINYUSB_MIDI_COUNT
extern Driver esp32_usb_midi_device_driver;
#endif
#if SOC_USB_OTG_SUPPORTED && CONFIG_TINYUSB_CDC_ENABLED
extern Driver esp32_usb_cdc_device_driver;
#endif

static Driver* const platform_esp32_drivers[] = {
    &esp32_adc_oneshot_driver,
    &esp32_gpio_driver,
    &esp32_i2c_driver,
    &esp32_i2c_master_driver,
    &esp32_i2s_driver,
#if SOC_LCD_I80_SUPPORTED
    &esp32_i8080_driver,
#endif
    &esp32_pwm_ledc_driver,
#if SOC_SDMMC_HOST_SUPPORTED
    &esp32_sdmmc_driver,
#endif
    &esp32_sdspi_driver,
    &esp32_spi_driver,
    &esp32_uart_driver,
    &esp32_grove_driver,
#if defined(CONFIG_SOC_WIFI_SUPPORTED) || defined(CONFIG_ESP_HOSTED_ENABLED)
    &esp32_wifi_driver,
    &esp32_wifi_pinned_driver,
#endif
#ifdef CONFIG_BT_NIMBLE_ENABLED
    &esp32_bluetooth_driver,
    &esp32_ble_serial_driver,
    &esp32_ble_midi_driver,
    &esp32_ble_hid_device_driver,
#endif
#if SOC_USB_OTG_SUPPORTED
    &esp32_usbhost_driver,
    &esp32_usbhost_hid_driver,
    &esp32_usbhost_hid_keyboard_driver,
    &esp32_usbhost_midi_driver,
    &esp32_usbhost_msc_driver,
#endif
#if SOC_USB_OTG_SUPPORTED && (CONFIG_TINYUSB_HID_COUNT || CONFIG_TINYUSB_MSC_ENABLED || CONFIG_TINYUSB_MIDI_COUNT || CONFIG_TINYUSB_CDC_ENABLED)
    &esp32_usb_device_controller_driver,
#endif
#if SOC_USB_OTG_SUPPORTED && CONFIG_TINYUSB_HID_COUNT
    &esp32_usb_hid_device_driver,
#endif
#if SOC_USB_OTG_SUPPORTED && CONFIG_TINYUSB_MSC_ENABLED
    &esp32_usb_msc_device_driver,
#endif
#if SOC_USB_OTG_SUPPORTED && CONFIG_TINYUSB_MIDI_COUNT
    &esp32_usb_midi_device_driver,
#endif
#if SOC_USB_OTG_SUPPORTED && CONFIG_TINYUSB_CDC_ENABLED
    &esp32_usb_cdc_device_driver,
#endif
    nullptr,
};

Module platform_esp32_module = {
    .name = "platform-esp32",
    .start = nullptr,
    .stop = nullptr,
    .drivers = platform_esp32_drivers,
    .symbols = platform_esp32_symbols,
    .internal = nullptr,
};

}
