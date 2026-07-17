#include <tactility/concurrent/dispatcher.h>
#include <tactility/concurrent/event_group.h>
#include <tactility/concurrent/thread.h>
#include <tactility/concurrent/timer.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/adc_controller.h>
#include <tactility/drivers/audio_codec.h>
#include <tactility/drivers/audio_stream.h>
#include <tactility/drivers/backlight.h>
#include <tactility/drivers/bluetooth.h>
#include <tactility/drivers/bluetooth_serial.h>
#include <tactility/drivers/bluetooth_midi.h>
#include <tactility/drivers/bluetooth_hid_device.h>
#include <tactility/drivers/camera.h>
#include <tactility/drivers/usb_host_hid.h>
#include <tactility/drivers/usb_host_midi.h>
#include <tactility/drivers/usb_host_msc.h>
#include <tactility/drivers/display.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/grove.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/i2s_controller.h>
#include <tactility/drivers/i8080_controller.h>
#include <tactility/drivers/keyboard.h>
#include <tactility/drivers/lora.h>
#include <tactility/drivers/pointer.h>
#include <tactility/drivers/power_supply.h>
#include <tactility/drivers/root.h>
#include <tactility/drivers/rtc.h>
#include <tactility/drivers/sdcard.h>
#include <tactility/drivers/spi_controller.h>
#include <tactility/drivers/uart_controller.h>
#include <tactility/drivers/wifi.h>
#include <tactility/error.h>
#include <tactility/filesystem/file_system.h>
#include <tactility/module.h>
#include <tactility/service/service_instance.h>
#include <tactility/service/service_manager.h>
#include <tactility/service/service_paths.h>

#ifndef ESP_PLATFORM
#include <tactility/log.h>
#endif

/**
 * This file is a C file instead of C++, so we can import all headers as C code.
 * The intent is to catch errors that only show up when compiling as C and not as C++.
 * For example: wrong header includes.
 */
const struct ModuleSymbol KERNEL_SYMBOLS[] = {
    // device
    DEFINE_MODULE_SYMBOL(device_construct),
    DEFINE_MODULE_SYMBOL(device_destruct),
    DEFINE_MODULE_SYMBOL(device_add),
    DEFINE_MODULE_SYMBOL(device_remove),
    DEFINE_MODULE_SYMBOL(device_start),
    DEFINE_MODULE_SYMBOL(device_stop),
    DEFINE_MODULE_SYMBOL(device_construct_add),
    DEFINE_MODULE_SYMBOL(device_construct_add_start),
    DEFINE_MODULE_SYMBOL(device_set_parent),
    DEFINE_MODULE_SYMBOL(device_get_parent),
    DEFINE_MODULE_SYMBOL(device_set_driver),
    DEFINE_MODULE_SYMBOL(device_get_driver),
    DEFINE_MODULE_SYMBOL(device_set_driver_data),
    DEFINE_MODULE_SYMBOL(device_get_driver_data),
    DEFINE_MODULE_SYMBOL(device_is_added),
    DEFINE_MODULE_SYMBOL(device_is_ready),
    DEFINE_MODULE_SYMBOL(device_is_compatible),
    DEFINE_MODULE_SYMBOL(device_lock),
    DEFINE_MODULE_SYMBOL(device_try_lock),
    DEFINE_MODULE_SYMBOL(device_unlock),
    DEFINE_MODULE_SYMBOL(device_get_type),
    DEFINE_MODULE_SYMBOL(device_for_each),
    DEFINE_MODULE_SYMBOL(device_for_each_child),
    DEFINE_MODULE_SYMBOL(device_for_each_of_type),
    DEFINE_MODULE_SYMBOL(device_exists_of_type),
    DEFINE_MODULE_SYMBOL(device_find_by_name),
    DEFINE_MODULE_SYMBOL(device_find_first_active_by_type),
    DEFINE_MODULE_SYMBOL(device_find_first_by_type),
    DEFINE_MODULE_SYMBOL(device_find_first_by_compatible),
    // driver
    DEFINE_MODULE_SYMBOL(driver_construct),
    DEFINE_MODULE_SYMBOL(driver_destruct),
    DEFINE_MODULE_SYMBOL(driver_add),
    DEFINE_MODULE_SYMBOL(driver_remove),
    DEFINE_MODULE_SYMBOL(driver_construct_add),
    DEFINE_MODULE_SYMBOL(driver_remove_destruct),
    DEFINE_MODULE_SYMBOL(driver_bind),
    DEFINE_MODULE_SYMBOL(driver_unbind),
    DEFINE_MODULE_SYMBOL(driver_is_compatible),
    DEFINE_MODULE_SYMBOL(driver_find_compatible),
    DEFINE_MODULE_SYMBOL(driver_get_device_type),
    // drivers/audio_codec
    DEFINE_MODULE_SYMBOL(audio_codec_open),
    DEFINE_MODULE_SYMBOL(audio_codec_close),
    DEFINE_MODULE_SYMBOL(audio_codec_read),
    DEFINE_MODULE_SYMBOL(audio_codec_write),
    DEFINE_MODULE_SYMBOL(audio_codec_set_volume),
    DEFINE_MODULE_SYMBOL(audio_codec_get_volume),
    DEFINE_MODULE_SYMBOL(audio_codec_set_mute),
    DEFINE_MODULE_SYMBOL(audio_codec_get_mute),
    DEFINE_MODULE_SYMBOL(audio_codec_get_native_sample_rate),
    DEFINE_MODULE_SYMBOL(audio_codec_get_native_channels),
    DEFINE_MODULE_SYMBOL(audio_codec_get_capabilities),
    DEFINE_MODULE_SYMBOL(audio_codec_get_input_gain_multiplier),
    DEFINE_MODULE_SYMBOL(AUDIO_CODEC_TYPE),
    // drivers/audio_stream
    DEFINE_MODULE_SYMBOL(audio_stream_open_input),
    DEFINE_MODULE_SYMBOL(audio_stream_open_output),
    DEFINE_MODULE_SYMBOL(audio_stream_read),
    DEFINE_MODULE_SYMBOL(audio_stream_write),
    DEFINE_MODULE_SYMBOL(audio_stream_close),
    DEFINE_MODULE_SYMBOL(audio_stream_set_volume),
    DEFINE_MODULE_SYMBOL(audio_stream_get_volume),
    DEFINE_MODULE_SYMBOL(audio_stream_set_mute),
    DEFINE_MODULE_SYMBOL(audio_stream_get_mute),
    DEFINE_MODULE_SYMBOL(audio_stream_set_enabled),
    DEFINE_MODULE_SYMBOL(audio_stream_get_enabled),
    DEFINE_MODULE_SYMBOL(audio_stream_is_supported),
    DEFINE_MODULE_SYMBOL(audio_stream_set_change_callback),
    DEFINE_MODULE_SYMBOL(AUDIO_STREAM_TYPE),
    // drivers/adc_controller
    DEFINE_MODULE_SYMBOL(adc_controller_read_raw),
    DEFINE_MODULE_SYMBOL(adc_channel_read_raw),
    DEFINE_MODULE_SYMBOL(ADC_CONTROLLER_TYPE),
    // drivers/backlight
    DEFINE_MODULE_SYMBOL(backlight_set_brightness),
    DEFINE_MODULE_SYMBOL(backlight_set_brightness_default),
    DEFINE_MODULE_SYMBOL(backlight_get_brightness),
    DEFINE_MODULE_SYMBOL(backlight_get_min_brightness),
    DEFINE_MODULE_SYMBOL(backlight_get_max_brightness),
    DEFINE_MODULE_SYMBOL(BACKLIGHT_TYPE),
    // drivers/display
    DEFINE_MODULE_SYMBOL(display_reset),
    DEFINE_MODULE_SYMBOL(display_init),
    DEFINE_MODULE_SYMBOL(display_draw_bitmap),
    DEFINE_MODULE_SYMBOL(display_mirror),
    DEFINE_MODULE_SYMBOL(display_swap_xy),
    DEFINE_MODULE_SYMBOL(display_get_swap_xy),
    DEFINE_MODULE_SYMBOL(display_get_mirror_x),
    DEFINE_MODULE_SYMBOL(display_get_mirror_y),
    DEFINE_MODULE_SYMBOL(display_set_gap),
    DEFINE_MODULE_SYMBOL(display_invert_color),
    DEFINE_MODULE_SYMBOL(display_disp_on_off),
    DEFINE_MODULE_SYMBOL(display_disp_sleep),
    DEFINE_MODULE_SYMBOL(display_get_color_format),
    DEFINE_MODULE_SYMBOL(display_get_resolution_x),
    DEFINE_MODULE_SYMBOL(display_get_resolution_y),
    DEFINE_MODULE_SYMBOL(display_get_frame_buffer),
    DEFINE_MODULE_SYMBOL(display_get_frame_buffer_count),
    DEFINE_MODULE_SYMBOL(display_get_backlight),
    DEFINE_MODULE_SYMBOL(DISPLAY_TYPE),
    // drivers/gpio_controller
    DEFINE_MODULE_SYMBOL(gpio_descriptor_acquire),
    DEFINE_MODULE_SYMBOL(gpio_descriptor_release),
    DEFINE_MODULE_SYMBOL(gpio_descriptor_set_level),
    DEFINE_MODULE_SYMBOL(gpio_descriptor_get_level),
    DEFINE_MODULE_SYMBOL(gpio_descriptor_set_flags),
    DEFINE_MODULE_SYMBOL(gpio_descriptor_get_flags),
    DEFINE_MODULE_SYMBOL(gpio_descriptor_get_native_pin_number),
    DEFINE_MODULE_SYMBOL(gpio_descriptor_get_pin_number),
    DEFINE_MODULE_SYMBOL(gpio_descriptor_get_owner_type),
    DEFINE_MODULE_SYMBOL(gpio_descriptor_add_callback),
    DEFINE_MODULE_SYMBOL(gpio_descriptor_remove_callback),
    DEFINE_MODULE_SYMBOL(gpio_descriptor_enable_interrupt),
    DEFINE_MODULE_SYMBOL(gpio_descriptor_disable_interrupt),
    DEFINE_MODULE_SYMBOL(gpio_controller_get_pin_count),
    DEFINE_MODULE_SYMBOL(gpio_controller_init_descriptors),
    DEFINE_MODULE_SYMBOL(gpio_controller_deinit_descriptors),
    DEFINE_MODULE_SYMBOL(gpio_controller_get_controller_context),
    DEFINE_MODULE_SYMBOL(GPIO_CONTROLLER_TYPE),
    // drivers/grove
    DEFINE_MODULE_SYMBOL(grove_set_mode),
    DEFINE_MODULE_SYMBOL(grove_get_mode),
    DEFINE_MODULE_SYMBOL(GROVE_TYPE),
    // drivers/i2c_controller
    DEFINE_MODULE_SYMBOL(i2c_controller_read),
    DEFINE_MODULE_SYMBOL(i2c_controller_write),
    DEFINE_MODULE_SYMBOL(i2c_controller_write_read),
    DEFINE_MODULE_SYMBOL(i2c_controller_read_register),
    DEFINE_MODULE_SYMBOL(i2c_controller_write_register),
    DEFINE_MODULE_SYMBOL(i2c_controller_write_register_array),
    DEFINE_MODULE_SYMBOL(i2c_controller_has_device_at_address),
    DEFINE_MODULE_SYMBOL(i2c_controller_register8_set),
    DEFINE_MODULE_SYMBOL(i2c_controller_register8_get),
    DEFINE_MODULE_SYMBOL(i2c_controller_register8_set_bits),
    DEFINE_MODULE_SYMBOL(i2c_controller_register8_reset_bits),
    DEFINE_MODULE_SYMBOL(i2c_controller_register16le_get),
    DEFINE_MODULE_SYMBOL(i2c_controller_register16le_set),
    DEFINE_MODULE_SYMBOL(i2c_controller_register16be_get),
    DEFINE_MODULE_SYMBOL(i2c_controller_register16be_set),
    DEFINE_MODULE_SYMBOL(I2C_CONTROLLER_TYPE),
    // drivers/i2s_controller
    DEFINE_MODULE_SYMBOL(i2s_controller_read),
    DEFINE_MODULE_SYMBOL(i2s_controller_write),
    DEFINE_MODULE_SYMBOL(i2s_controller_set_config),
    DEFINE_MODULE_SYMBOL(i2s_controller_get_config),
    DEFINE_MODULE_SYMBOL(i2s_controller_reset),
    DEFINE_MODULE_SYMBOL(i2s_controller_set_rx_tdm_config),
    DEFINE_MODULE_SYMBOL(i2s_controller_set_rx_pdm_config),
    DEFINE_MODULE_SYMBOL(i2s_controller_disable_direction),
    DEFINE_MODULE_SYMBOL(I2S_CONTROLLER_TYPE),
    // drivers/i8080_controller
    DEFINE_MODULE_SYMBOL(I8080_CONTROLLER_TYPE),
    // drivers/keyboard
    DEFINE_MODULE_SYMBOL(keyboard_read_key),
    DEFINE_MODULE_SYMBOL(KEYBOARD_TYPE),
    // drivers/pointer
    DEFINE_MODULE_SYMBOL(pointer_enter_sleep),
    DEFINE_MODULE_SYMBOL(pointer_exit_sleep),
    DEFINE_MODULE_SYMBOL(pointer_read_data),
    DEFINE_MODULE_SYMBOL(pointer_get_touched_points),
    DEFINE_MODULE_SYMBOL(pointer_set_swap_xy),
    DEFINE_MODULE_SYMBOL(pointer_get_swap_xy),
    DEFINE_MODULE_SYMBOL(pointer_set_mirror_x),
    DEFINE_MODULE_SYMBOL(pointer_get_mirror_x),
    DEFINE_MODULE_SYMBOL(pointer_set_mirror_y),
    DEFINE_MODULE_SYMBOL(pointer_get_mirror_y),
    DEFINE_MODULE_SYMBOL(POINTER_TYPE),
    // drivers/power_supply
    DEFINE_MODULE_SYMBOL(power_supply_supports_property),
    DEFINE_MODULE_SYMBOL(power_supply_get_property),
    DEFINE_MODULE_SYMBOL(power_supply_supports_charge_control),
    DEFINE_MODULE_SYMBOL(power_supply_is_allowed_to_charge),
    DEFINE_MODULE_SYMBOL(power_supply_set_allowed_to_charge),
    DEFINE_MODULE_SYMBOL(power_supply_supports_quick_charge),
    DEFINE_MODULE_SYMBOL(power_supply_is_quick_charge_enabled),
    DEFINE_MODULE_SYMBOL(power_supply_set_quick_charge_enabled),
    DEFINE_MODULE_SYMBOL(power_supply_supports_power_off),
    DEFINE_MODULE_SYMBOL(power_supply_power_off),
    DEFINE_MODULE_SYMBOL(POWER_SUPPLY_TYPE),
    // drivers/root
    DEFINE_MODULE_SYMBOL(root_is_model),
    // drivers/rtc
    DEFINE_MODULE_SYMBOL(rtc_get_time),
    DEFINE_MODULE_SYMBOL(rtc_set_time),
    DEFINE_MODULE_SYMBOL(RTC_TYPE),
    // drivers/sdcard
    DEFINE_MODULE_SYMBOL(SDCARD_TYPE),
    // drivers/spi_controller
    DEFINE_MODULE_SYMBOL(spi_controller_lock),
    DEFINE_MODULE_SYMBOL(spi_controller_try_lock),
    DEFINE_MODULE_SYMBOL(spi_controller_unlock),
    DEFINE_MODULE_SYMBOL(SPI_CONTROLLER_TYPE),
    // drivers/uart_controller
    DEFINE_MODULE_SYMBOL(uart_controller_open),
    DEFINE_MODULE_SYMBOL(uart_controller_close),
    DEFINE_MODULE_SYMBOL(uart_controller_is_open),
    DEFINE_MODULE_SYMBOL(uart_controller_read_byte),
    DEFINE_MODULE_SYMBOL(uart_controller_read_bytes),
    DEFINE_MODULE_SYMBOL(uart_controller_read_until),
    DEFINE_MODULE_SYMBOL(uart_controller_write_byte),
    DEFINE_MODULE_SYMBOL(uart_controller_write_bytes),
    DEFINE_MODULE_SYMBOL(uart_controller_set_config),
    DEFINE_MODULE_SYMBOL(uart_controller_get_config),
    DEFINE_MODULE_SYMBOL(uart_controller_get_available),
    DEFINE_MODULE_SYMBOL(uart_controller_flush_input),
    DEFINE_MODULE_SYMBOL(UART_CONTROLLER_TYPE),
    // drivers/bluetooth
    DEFINE_MODULE_SYMBOL(bluetooth_find_first_ready_device),
    DEFINE_MODULE_SYMBOL(bluetooth_get_radio_state),
    DEFINE_MODULE_SYMBOL(bluetooth_set_radio_enabled),
    DEFINE_MODULE_SYMBOL(bluetooth_scan_start),
    DEFINE_MODULE_SYMBOL(bluetooth_scan_stop),
    DEFINE_MODULE_SYMBOL(bluetooth_is_scanning),
    DEFINE_MODULE_SYMBOL(bluetooth_pair),
    DEFINE_MODULE_SYMBOL(bluetooth_unpair),
    DEFINE_MODULE_SYMBOL(bluetooth_get_paired_peers),
    DEFINE_MODULE_SYMBOL(bluetooth_connect),
    DEFINE_MODULE_SYMBOL(bluetooth_disconnect),
    DEFINE_MODULE_SYMBOL(bluetooth_add_event_callback),
    DEFINE_MODULE_SYMBOL(bluetooth_remove_event_callback),
    DEFINE_MODULE_SYMBOL(bluetooth_set_device_name),
    DEFINE_MODULE_SYMBOL(bluetooth_get_device_name),
    DEFINE_MODULE_SYMBOL(bluetooth_set_hid_host_active),
    DEFINE_MODULE_SYMBOL(bluetooth_fire_event),
    DEFINE_MODULE_SYMBOL(BLUETOOTH_TYPE),
    // drivers/bluetooth_serial
    DEFINE_MODULE_SYMBOL(bluetooth_serial_get_device),
    DEFINE_MODULE_SYMBOL(bluetooth_serial_start),
    DEFINE_MODULE_SYMBOL(bluetooth_serial_stop),
    DEFINE_MODULE_SYMBOL(bluetooth_serial_write),
    DEFINE_MODULE_SYMBOL(bluetooth_serial_read),
    DEFINE_MODULE_SYMBOL(bluetooth_serial_is_connected),
    DEFINE_MODULE_SYMBOL(BLUETOOTH_SERIAL_TYPE),
    // drivers/bluetooth_midi
    DEFINE_MODULE_SYMBOL(bluetooth_midi_get_device),
    DEFINE_MODULE_SYMBOL(bluetooth_midi_start),
    DEFINE_MODULE_SYMBOL(bluetooth_midi_stop),
    DEFINE_MODULE_SYMBOL(bluetooth_midi_send),
    DEFINE_MODULE_SYMBOL(bluetooth_midi_is_connected),
    DEFINE_MODULE_SYMBOL(BLUETOOTH_MIDI_TYPE),
    // drivers/bluetooth_hid_device
    DEFINE_MODULE_SYMBOL(bluetooth_hid_device_get_device),
    DEFINE_MODULE_SYMBOL(bluetooth_hid_device_start),
    DEFINE_MODULE_SYMBOL(bluetooth_hid_device_stop),
    DEFINE_MODULE_SYMBOL(bluetooth_hid_device_send_key),
    DEFINE_MODULE_SYMBOL(bluetooth_hid_device_send_keyboard),
    DEFINE_MODULE_SYMBOL(bluetooth_hid_device_send_consumer),
    DEFINE_MODULE_SYMBOL(bluetooth_hid_device_send_mouse),
    DEFINE_MODULE_SYMBOL(bluetooth_hid_device_send_gamepad),
    DEFINE_MODULE_SYMBOL(bluetooth_hid_device_is_connected),
    DEFINE_MODULE_SYMBOL(BLUETOOTH_HID_DEVICE_TYPE),
    // drivers/camera
    DEFINE_MODULE_SYMBOL(camera_open),
    DEFINE_MODULE_SYMBOL(camera_close),
    DEFINE_MODULE_SYMBOL(camera_get_frame),
    DEFINE_MODULE_SYMBOL(camera_release_frame),
    DEFINE_MODULE_SYMBOL(camera_get_width),
    DEFINE_MODULE_SYMBOL(camera_get_height),
    DEFINE_MODULE_SYMBOL(camera_set_rotation),
    DEFINE_MODULE_SYMBOL(camera_capture_jpeg),
    DEFINE_MODULE_SYMBOL(CAMERA_TYPE),
    // drivers/wifi
    DEFINE_MODULE_SYMBOL(wifi_find_first_registered_device),
    DEFINE_MODULE_SYMBOL(wifi_get_radio_state),
    DEFINE_MODULE_SYMBOL(wifi_get_station_state),
    DEFINE_MODULE_SYMBOL(wifi_get_access_point_state),
    DEFINE_MODULE_SYMBOL(wifi_is_scanning),
    DEFINE_MODULE_SYMBOL(wifi_scan),
    DEFINE_MODULE_SYMBOL(wifi_get_scan_results),
    DEFINE_MODULE_SYMBOL(wifi_station_get_ipv4_address),
    DEFINE_MODULE_SYMBOL(wifi_station_get_target_ssid),
    DEFINE_MODULE_SYMBOL(wifi_station_connect),
    DEFINE_MODULE_SYMBOL(wifi_station_disconnect),
    DEFINE_MODULE_SYMBOL(wifi_station_get_rssi),
    DEFINE_MODULE_SYMBOL(wifi_add_event_callback),
    DEFINE_MODULE_SYMBOL(wifi_remove_event_callback),
    DEFINE_MODULE_SYMBOL(WIFI_TYPE),
    // drivers/lora
    DEFINE_MODULE_SYMBOL(lora_find_first_registered_device),
    DEFINE_MODULE_SYMBOL(lora_get_radio_state),
    DEFINE_MODULE_SYMBOL(lora_set_enabled),
    DEFINE_MODULE_SYMBOL(lora_set_modulation),
    DEFINE_MODULE_SYMBOL(lora_get_modulation),
    DEFINE_MODULE_SYMBOL(lora_can_transmit),
    DEFINE_MODULE_SYMBOL(lora_can_receive),
    DEFINE_MODULE_SYMBOL(lora_set_parameter),
    DEFINE_MODULE_SYMBOL(lora_get_parameter),
    DEFINE_MODULE_SYMBOL(lora_transmit),
    DEFINE_MODULE_SYMBOL(lora_add_rx_callback),
    DEFINE_MODULE_SYMBOL(lora_remove_rx_callback),
    DEFINE_MODULE_SYMBOL(lora_add_state_callback),
    DEFINE_MODULE_SYMBOL(lora_remove_state_callback),
    DEFINE_MODULE_SYMBOL(lora_add_tx_callback),
    DEFINE_MODULE_SYMBOL(lora_remove_tx_callback),
    DEFINE_MODULE_SYMBOL(LORA_TYPE),
    // drivers/usb_host_hid
    DEFINE_MODULE_SYMBOL(usb_host_hid_is_connected),
    DEFINE_MODULE_SYMBOL(usb_host_hid_subscribe),
    DEFINE_MODULE_SYMBOL(usb_host_hid_unsubscribe),
    DEFINE_MODULE_SYMBOL(USB_HOST_HID_TYPE),
    // drivers/usb_host_midi
    DEFINE_MODULE_SYMBOL(usb_midi_set_callback),
    DEFINE_MODULE_SYMBOL(usb_midi_is_connected),
    DEFINE_MODULE_SYMBOL(USB_HOST_MIDI_TYPE),
    // drivers/usb_host_msc
    DEFINE_MODULE_SYMBOL(usb_msc_eject),
    DEFINE_MODULE_SYMBOL(USB_HOST_MSC_TYPE),
    // concurrent/dispatcher
    DEFINE_MODULE_SYMBOL(dispatcher_alloc),
    DEFINE_MODULE_SYMBOL(dispatcher_free),
    DEFINE_MODULE_SYMBOL(dispatcher_dispatch_timed),
    DEFINE_MODULE_SYMBOL(dispatcher_consume_timed),
    // concurrent/event_group
    DEFINE_MODULE_SYMBOL(event_group_set),
    DEFINE_MODULE_SYMBOL(event_group_clear),
    DEFINE_MODULE_SYMBOL(event_group_get),
    DEFINE_MODULE_SYMBOL(event_group_wait),
    // concurrent/thread
    DEFINE_MODULE_SYMBOL(thread_alloc),
    DEFINE_MODULE_SYMBOL(thread_alloc_full),
    DEFINE_MODULE_SYMBOL(thread_free),
    DEFINE_MODULE_SYMBOL(thread_set_name),
    DEFINE_MODULE_SYMBOL(thread_set_stack_size),
    DEFINE_MODULE_SYMBOL(thread_set_affinity),
    DEFINE_MODULE_SYMBOL(thread_set_main_function),
    DEFINE_MODULE_SYMBOL(thread_set_priority),
    DEFINE_MODULE_SYMBOL(thread_set_state_callback),
    DEFINE_MODULE_SYMBOL(thread_get_state),
    DEFINE_MODULE_SYMBOL(thread_start),
    DEFINE_MODULE_SYMBOL(thread_join),
    DEFINE_MODULE_SYMBOL(thread_get_task_handle),
    DEFINE_MODULE_SYMBOL(thread_get_return_code),
    DEFINE_MODULE_SYMBOL(thread_get_stack_space),
    DEFINE_MODULE_SYMBOL(thread_get_current),
    // concurrent/timer
    DEFINE_MODULE_SYMBOL(timer_alloc),
    DEFINE_MODULE_SYMBOL(timer_free),
    DEFINE_MODULE_SYMBOL(timer_start),
    DEFINE_MODULE_SYMBOL(timer_stop),
    DEFINE_MODULE_SYMBOL(timer_reset_with_interval),
    DEFINE_MODULE_SYMBOL(timer_reset),
    DEFINE_MODULE_SYMBOL(timer_is_running),
    DEFINE_MODULE_SYMBOL(timer_get_expiry_time),
    DEFINE_MODULE_SYMBOL(timer_set_pending_callback),
    DEFINE_MODULE_SYMBOL(timer_set_callback_priority),
    // error
    DEFINE_MODULE_SYMBOL(error_to_string),
    // file system
    DEFINE_MODULE_SYMBOL(file_system_mount),
    DEFINE_MODULE_SYMBOL(file_system_unmount),
    DEFINE_MODULE_SYMBOL(file_system_is_mounted),
    DEFINE_MODULE_SYMBOL(file_system_get_path),
    // log
#ifndef ESP_PLATFORM
    DEFINE_MODULE_SYMBOL(log_generic),
#endif
    // module
    DEFINE_MODULE_SYMBOL(module_construct),
    DEFINE_MODULE_SYMBOL(module_destruct),
    DEFINE_MODULE_SYMBOL(module_add),
    DEFINE_MODULE_SYMBOL(module_remove),
    DEFINE_MODULE_SYMBOL(module_construct_add_start),
    DEFINE_MODULE_SYMBOL(module_start),
    DEFINE_MODULE_SYMBOL(module_stop),
    DEFINE_MODULE_SYMBOL(module_is_started),
    DEFINE_MODULE_SYMBOL(module_resolve_symbol),
    DEFINE_MODULE_SYMBOL(module_resolve_symbol_global),
    // service/service_instance
    DEFINE_MODULE_SYMBOL(service_instance_construct),
    DEFINE_MODULE_SYMBOL(service_instance_destruct),
    DEFINE_MODULE_SYMBOL(service_instance_get_manifest),
    DEFINE_MODULE_SYMBOL(service_instance_get_data),
    DEFINE_MODULE_SYMBOL(service_instance_get_state),
    // service/service_manager
    DEFINE_MODULE_SYMBOL(service_manager_add),
    DEFINE_MODULE_SYMBOL(service_manager_remove),
    DEFINE_MODULE_SYMBOL(service_manager_find_manifest),
    DEFINE_MODULE_SYMBOL(service_manager_start),
    DEFINE_MODULE_SYMBOL(service_manager_stop),
    DEFINE_MODULE_SYMBOL(service_manager_get_state),
    DEFINE_MODULE_SYMBOL(service_manager_find_instance),
    // service/service_paths
    DEFINE_MODULE_SYMBOL(service_paths_get_user_data_directory),
    DEFINE_MODULE_SYMBOL(service_paths_get_user_data_path),
    DEFINE_MODULE_SYMBOL(service_paths_get_assets_directory),
    DEFINE_MODULE_SYMBOL(service_paths_get_assets_path),
    // terminator
    MODULE_SYMBOL_TERMINATOR
};
