#pragma once

#include <tactility/error.h>

struct Device;

/**
 * Runs a function on the USB host worker task and blocks until it returns.
 * Use it for device_start() and device_stop() of dynamic devices, so device listeners never run on a USB task.
 * @param[in] host the USB host device
 * @param[in] function the function to run
 * @param[in] context passed to the function
 * @retval ERROR_NONE when the function ran
 * @retval ERROR_INVALID_STATE when the worker is not running
 */
error_t esp32_usbhost_run_on_worker(struct Device* host, void (*function)(void* context), void* context);
