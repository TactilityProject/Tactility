#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/concurrent/dispatcher.h>
#include <tactility/concurrent/event_group.h>
#include <tactility/concurrent/thread.h>
#include <tactility/concurrent/timer.h>
#include <tactility/error.h>
#include <tactility/log.h>
#include <tactility/module.h>

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
    DEFINE_MODULE_SYMBOL(device_lock),
    DEFINE_MODULE_SYMBOL(device_try_lock),
    DEFINE_MODULE_SYMBOL(device_unlock),
    DEFINE_MODULE_SYMBOL(device_get_type),
    DEFINE_MODULE_SYMBOL(device_for_each),
    DEFINE_MODULE_SYMBOL(device_for_each_child),
    DEFINE_MODULE_SYMBOL(device_for_each_of_type),
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
    // drivers/gpio_controller
    DEFINE_MODULE_SYMBOL(gpio_controller_set_level),
    DEFINE_MODULE_SYMBOL(gpio_controller_get_level),
    DEFINE_MODULE_SYMBOL(gpio_controller_set_options),
    DEFINE_MODULE_SYMBOL(gpio_controller_get_options),
    DEFINE_MODULE_SYMBOL(gpio_controller_get_pin_count),
    // drivers/i2c_controller
    DEFINE_MODULE_SYMBOL(i2c_controller_read),
    DEFINE_MODULE_SYMBOL(i2c_controller_write),
    DEFINE_MODULE_SYMBOL(i2c_controller_write_read),
    DEFINE_MODULE_SYMBOL(i2c_controller_read_register),
    DEFINE_MODULE_SYMBOL(i2c_controller_write_register),
    DEFINE_MODULE_SYMBOL(i2c_controller_write_register_array),
    DEFINE_MODULE_SYMBOL(i2c_controller_has_device_at_address),
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
    // terminator
    MODULE_SYMBOL_TERMINATOR
};
